/**
 * Copyright (c) 2022, Timothy Stack
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "md2attr_line.hh"

#include "base/attr_line.builder.hh"
#include "base/itertools.hh"
#include "base/lnav_log.hh"
#include "pcrepp/pcre2pp.hh"
#include "pugixml/pugixml.hpp"
#include "readline_highlighters.hh"
#include "view_curses.hh"

using namespace lnav::roles::literals;

void
md2attr_line::flush_footnotes()
{
    if (this->ml_footnotes.empty()) {
        return;
    }

    auto& block_text = this->ml_blocks.back();
    auto longest_foot = this->ml_footnotes
        | lnav::itertools::map(&attr_line_t::utf8_length_or_length)
        | lnav::itertools::max(0);
    size_t index = 1;

    block_text.append("\n");
    for (auto& foot : this->ml_footnotes) {
        block_text.append(lnav::string::attrs::preformatted(" "))
            .append("\u258c"_footnote_border)
            .append(lnav::roles::footnote_text(
                index < 10 && this->ml_footnotes.size() >= 10 ? " " : ""))
            .append(lnav::roles::footnote_text(
                fmt::format(FMT_STRING("[{}] - "), index)))
            .append(foot.pad_to(longest_foot))
            .append("\n");
        index += 1;
    }
    this->ml_footnotes.clear();
}

Result<void, std::string>
md2attr_line::enter_block(const md4cpp::event_handler::block& bl)
{
    if (this->ml_list_stack.empty()
        && (bl.is<MD_BLOCK_H_DETAIL*>() || bl.is<block_hr>()
            || bl.is<block_p>()))
    {
        this->flush_footnotes();
    }

    this->ml_blocks.resize(this->ml_blocks.size() + 1);
    if (bl.is<MD_BLOCK_OL_DETAIL*>()) {
        auto* ol_detail = bl.get<MD_BLOCK_OL_DETAIL*>();

        this->ml_list_stack.emplace_back(*ol_detail);
    } else if (bl.is<MD_BLOCK_UL_DETAIL*>()) {
        this->ml_list_stack.emplace_back(bl.get<MD_BLOCK_UL_DETAIL*>());
    } else if (bl.is<MD_BLOCK_TABLE_DETAIL*>()) {
        this->ml_tables.resize(this->ml_tables.size() + 1);
    } else if (bl.is<block_tr>()) {
        this->ml_tables.back().t_rows.resize(
            this->ml_tables.back().t_rows.size() + 1);
    } else if (bl.is<MD_BLOCK_CODE_DETAIL*>()) {
        this->ml_code_depth += 1;
    }

    return Ok();
}

Result<void, std::string>
md2attr_line::leave_block(const md4cpp::event_handler::block& bl)
{
    auto block_text = std::move(this->ml_blocks.back());
    this->ml_blocks.pop_back();

    auto& last_block = this->ml_blocks.back();
    if (!endswith(block_text.get_string(), "\n")) {
        block_text.append("\n");
    }
    if (bl.is<MD_BLOCK_H_DETAIL*>()) {
        auto* hbl = bl.get<MD_BLOCK_H_DETAIL*>();
        auto role = role_t::VCR_TEXT;

        switch (hbl->level) {
            case 1:
                role = role_t::VCR_H1;
                break;
            case 2:
                role = role_t::VCR_H2;
                break;
            case 3:
                role = role_t::VCR_H3;
                break;
            case 4:
                role = role_t::VCR_H4;
                break;
            case 5:
                role = role_t::VCR_H5;
                break;
            case 6:
                role = role_t::VCR_H6;
                break;
        }
        block_text.rtrim().with_attr_for_all(VC_ROLE.value(role));
        last_block.append("\n").append(block_text).append("\n");
    } else if (bl.is<block_hr>()) {
        block_text = attr_line_t()
                         .append(lnav::roles::hr(repeat("\u2501", 70)))
                         .with_attr_for_all(SA_PREFORMATTED.value());
        last_block.append("\n").append(block_text).append("\n");
    } else if (bl.is<MD_BLOCK_UL_DETAIL*>() || bl.is<MD_BLOCK_OL_DETAIL*>()) {
        this->ml_list_stack.pop_back();
        if (last_block.empty()) {
            last_block.append("\n");
        } else {
            if (!endswith(last_block.get_string(), "\n")) {
                last_block.append("\n");
            }
            if (this->ml_list_stack.empty()
                && !endswith(last_block.get_string(), "\n\n"))
            {
                last_block.append("\n");
            }
        }
        last_block.append(block_text);
    } else if (bl.is<MD_BLOCK_LI_DETAIL*>()) {
        auto last_list_block = this->ml_list_stack.back();
        text_wrap_settings tws = {0, 60};

        attr_line_builder alb(last_block);
        {
            auto prefix = alb.with_attr(SA_PREFORMATTED.value());

            alb.append(" ")
                .append(last_list_block.match(
                    [this, &tws](const MD_BLOCK_UL_DETAIL*) {
                        tws.tws_indent = 3;
                        return this->ml_list_stack.size() % 2 == 1
                            ? "\u2022"_list_glyph
                            : "\u2014"_list_glyph;
                    },
                    [this, &tws](MD_BLOCK_OL_DETAIL ol_detail) {
                        auto retval = lnav::roles::list_glyph(
                            fmt::format(FMT_STRING("{}{}"),
                                        ol_detail.start,
                                        ol_detail.mark_delimiter));
                        tws.tws_indent = retval.first.length() + 2;

                        this->ml_list_stack.pop_back();
                        ol_detail.start += 1;
                        this->ml_list_stack.emplace_back(ol_detail);
                        return retval;
                    }))
                .append(" ");
        }

        alb.append(block_text, &tws);
    } else if (bl.is<MD_BLOCK_CODE_DETAIL*>()) {
        auto* code_detail = bl.get<MD_BLOCK_CODE_DETAIL*>();

        this->ml_code_depth -= 1;

        auto lang_sf = string_fragment::from_bytes(code_detail->lang.text,
                                                   code_detail->lang.size);
        if (lang_sf == "lnav") {
            readline_lnav_highlighter(block_text, block_text.length());
        } else if (lang_sf == "sql" || lang_sf == "sqlite") {
            readline_sqlite_highlighter(block_text, block_text.length());
        } else if (lang_sf == "shell" || lang_sf == "bash") {
            readline_shlex_highlighter(block_text, block_text.length());
        } else if (lang_sf == "console"
                   || lang_sf.iequal(
                       string_fragment::from_const("shellsession")))
        {
            static const auto SH_PROMPT
                = lnav::pcre2pp::code::from_const(R"([^\$>#%]*[\$>#%]\s+)");

            attr_line_t new_block_text;
            attr_line_t cmd_block;
            int prompt_size = 0;

            for (auto line : block_text.split_lines()) {
                if (!cmd_block.empty()
                    && endswith(cmd_block.get_string(), "\\\n"))
                {
                    cmd_block.append(line).append("\n");
                    continue;
                }

                if (!cmd_block.empty()) {
                    readline_shlex_highlighter_int(
                        cmd_block,
                        cmd_block.length(),
                        line_range{prompt_size, (int) cmd_block.length()});
                    new_block_text.append(cmd_block);
                    cmd_block.clear();
                }

                auto sh_find_res
                    = SH_PROMPT.find_in(line.get_string()).ignore_error();

                if (sh_find_res) {
                    prompt_size = sh_find_res->f_all.length();
                    line.with_attr(string_attr{
                        line_range{0, prompt_size},
                        VC_ROLE.value(role_t::VCR_LIST_GLYPH),
                    });
                    cmd_block.append(line).append("\n");
                } else {
                    line.with_attr_for_all(VC_ROLE.value(role_t::VCR_COMMENT));
                    new_block_text.append(line).append("\n");
                }
            }
            block_text = new_block_text;
        }

        auto code_lines = block_text.rtrim().split_lines();
        auto max_width = code_lines
            | lnav::itertools::map(&attr_line_t::utf8_length_or_length)
            | lnav::itertools::max(0);
        attr_line_t padded_text;

        for (auto& line : code_lines) {
            line.pad_to(std::max(max_width + 4, ssize_t{40}))
                .with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_CODE));
            padded_text.append(lnav::string::attrs::preformatted(" "))
                .append("\u258c"_code_border)
                .append(line)
                .append("\n");
        }
        if (!padded_text.empty()) {
            padded_text.with_attr_for_all(SA_PREFORMATTED.value());
            last_block.append("\n").append(padded_text);
        }
    } else if (bl.is<block_quote>()) {
        text_wrap_settings tws = {0, 60};
        attr_line_t wrapped_text;

        wrapped_text.append(block_text.rtrim(), &tws);
        auto quoted_lines = wrapped_text.split_lines();
        auto max_width = quoted_lines
            | lnav::itertools::map(&attr_line_t::utf8_length_or_length)
            | lnav::itertools::max(0);
        attr_line_t padded_text;

        for (auto& line : quoted_lines) {
            line.pad_to(max_width + 1)
                .with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_TEXT));
            padded_text.append(" ")
                .append("\u258c"_quote_border)
                .append(line)
                .append("\n");
        }
        if (!padded_text.empty()) {
            padded_text.with_attr_for_all(SA_PREFORMATTED.value());
            last_block.append("\n").append(padded_text);
        }
    } else if (bl.is<MD_BLOCK_TABLE_DETAIL*>()) {
        auto* table_detail = bl.get<MD_BLOCK_TABLE_DETAIL*>();
        auto tab = std::move(this->ml_tables.back());
        this->ml_tables.pop_back();
        std::vector<ssize_t> max_col_sizes;

        block_text.clear();
        block_text.append("\n");
        max_col_sizes.resize(table_detail->col_count);
        for (size_t lpc = 0; lpc < table_detail->col_count; lpc++) {
            if (lpc < tab.t_headers.size()) {
                max_col_sizes[lpc] = tab.t_headers[lpc].utf8_length_or_length();
                tab.t_headers[lpc].with_attr_for_all(
                    VC_ROLE.value(role_t::VCR_TABLE_HEADER));
            }
        }
        for (const auto& row : tab.t_rows) {
            for (size_t lpc = 0; lpc < table_detail->col_count; lpc++) {
                if (lpc >= row.r_columns.size()) {
                    continue;
                }
                auto col_len = row.r_columns[lpc].utf8_length_or_length();
                if (col_len > max_col_sizes[lpc]) {
                    max_col_sizes[lpc] = col_len;
                }
            }
        }
        auto col_sizes
            = max_col_sizes | lnav::itertools::map([](const auto& elem) {
                  return std::min(elem, ssize_t{50});
              });
        auto full_width = col_sizes | lnav::itertools::sum();
        text_wrap_settings tws = {0, 50};
        std::vector<cell_lines> cells;
        size_t max_cell_lines = 0;
        for (size_t lpc = 0; lpc < tab.t_headers.size(); lpc++) {
            tws.with_width(col_sizes[lpc]);

            attr_line_t td_block;
            td_block.append(tab.t_headers[lpc], &tws);
            cells.emplace_back(td_block.rtrim().split_lines());
            if (cells.back().cl_lines.size() > max_cell_lines) {
                max_cell_lines = cells.back().cl_lines.size();
            }
        }
        for (size_t line_index = 0; line_index < max_cell_lines; line_index++) {
            size_t col = 0;
            for (const auto& cell : cells) {
                block_text.append(" ");
                if (line_index < cell.cl_lines.size()) {
                    block_text.append(cell.cl_lines[line_index]);
                    block_text.append(
                        col_sizes[col]
                            - cell.cl_lines[line_index].utf8_length_or_length(),
                        ' ');
                } else {
                    block_text.append(col_sizes[col], ' ');
                }
                col += 1;
            }
            block_text.append("\n")
                .append(lnav::roles::table_border(
                    repeat("\u2550", full_width + col_sizes.size())))
                .append("\n");
        }
        for (const auto& row : tab.t_rows) {
            cells.clear();
            max_cell_lines = 0;
            for (size_t lpc = 0; lpc < row.r_columns.size(); lpc++) {
                tws.with_width(col_sizes[lpc]);

                attr_line_t td_block;
                td_block.append(row.r_columns[lpc], &tws);
                cells.emplace_back(td_block.rtrim().split_lines());
                if (cells.back().cl_lines.size() > max_cell_lines) {
                    max_cell_lines = cells.back().cl_lines.size();
                }
            }
            for (size_t line_index = 0; line_index < max_cell_lines;
                 line_index++)
            {
                size_t col = 0;
                for (const auto& cell : cells) {
                    block_text.append(" ");
                    if (line_index < cell.cl_lines.size()) {
                        block_text.append(cell.cl_lines[line_index]);
                        if (col < col_sizes.size() - 1) {
                            block_text.append(
                                col_sizes[col]
                                    - cell.cl_lines[line_index]
                                          .utf8_length_or_length(),
                                ' ');
                        }
                    } else if (col < col_sizes.size() - 1) {
                        block_text.append(col_sizes[col], ' ');
                    }
                    col += 1;
                }
                block_text.append("\n");
            }
        }
        if (!block_text.empty()) {
            block_text.with_attr_for_all(SA_PREFORMATTED.value());
            last_block.append(block_text);
        }
    } else if (bl.is<block_th>()) {
        this->ml_tables.back().t_headers.push_back(block_text);
    } else if (bl.is<MD_BLOCK_TD_DETAIL*>()) {
        this->ml_tables.back().t_rows.back().r_columns.push_back(block_text);
    } else {
        if (bl.is<block_html>()) {
            if (startswith(block_text.get_string(), "<!--")) {
                return Ok();
            }
        }

        text_wrap_settings tws = {0, this->ml_blocks.size() == 1 ? 70 : 10000};

        if (!last_block.empty()) {
            last_block.append("\n");
        }
        last_block.append(block_text, &tws);
    }
    if (bl.is<block_doc>()) {
        this->flush_footnotes();
    }
    return Ok();
}

Result<void, std::string>
md2attr_line::enter_span(const md4cpp::event_handler::span& sp)
{
    auto& last_block = this->ml_blocks.back();
    this->ml_span_starts.push_back(last_block.length());
    if (sp.is<span_code>()) {
        last_block.append(" ");
        this->ml_code_depth += 1;
    }
    return Ok();
}

Result<void, std::string>
md2attr_line::leave_span(const md4cpp::event_handler::span& sp)
{
    auto& last_block = this->ml_blocks.back();
    if (sp.is<span_code>()) {
        this->ml_code_depth -= 1;
        last_block.append(" ");
        line_range lr{
            static_cast<int>(this->ml_span_starts.back()),
            static_cast<int>(last_block.length()),
        };
        last_block.with_attr({
            lr,
            VC_ROLE.value(role_t::VCR_QUOTED_CODE),
        });
        last_block.with_attr({
            lr,
            SA_PREFORMATTED.value(),
        });
    } else if (sp.is<span_em>()) {
        line_range lr{
            static_cast<int>(this->ml_span_starts.back()),
            static_cast<int>(last_block.length()),
        };
#if defined(A_ITALIC)
        last_block.with_attr({
            lr,
            VC_STYLE.value(text_attrs{(int32_t) A_ITALIC}),
        });
#endif
    } else if (sp.is<span_strong>()) {
        line_range lr{
            static_cast<int>(this->ml_span_starts.back()),
            static_cast<int>(last_block.length()),
        };
        last_block.with_attr({
            lr,
            VC_STYLE.value(text_attrs{A_BOLD}),
        });
    } else if (sp.is<span_u>()) {
        line_range lr{
            static_cast<int>(this->ml_span_starts.back()),
            static_cast<int>(last_block.length()),
        };
        last_block.with_attr({
            lr,
            VC_STYLE.value(text_attrs{A_UNDERLINE}),
        });
    } else if (sp.is<MD_SPAN_A_DETAIL*>()) {
        auto* a_detail = sp.get<MD_SPAN_A_DETAIL*>();
        auto href_str = std::string(a_detail->href.text, a_detail->href.size);

        this->append_url_footnote(href_str);
    } else if (sp.is<MD_SPAN_IMG_DETAIL*>()) {
        auto* img_detail = sp.get<MD_SPAN_IMG_DETAIL*>();
        auto src_str = std::string(img_detail->src.text, img_detail->src.size);

        this->append_url_footnote(src_str);
    }
    this->ml_span_starts.pop_back();
    return Ok();
}

enum class border_side {
    left,
    right,
};

enum class border_line_width {
    thin,
    medium,
    thick,
};

static const char*
left_border_string(border_line_width width)
{
    switch (width) {
        case border_line_width::thin:
            return "\u258F";
        case border_line_width::medium:
            return "\u258E";
        case border_line_width::thick:
            return "\u258C";
    }
}

static const char*
right_border_string(border_line_width width)
{
    switch (width) {
        case border_line_width::thin:
            return "\u2595";
        case border_line_width::medium:
            return "\u2595";
        case border_line_width::thick:
            return "\u2590";
    }
}

static attr_line_t
span_style_border(border_side side, const string_fragment& value)
{
    static const auto NAME_THIN = string_fragment::from_const("thin");
    static const auto NAME_MEDIUM = string_fragment::from_const("medium");
    static const auto NAME_THICK = string_fragment::from_const("thick");
    static const auto NAME_SOLID = string_fragment::from_const("solid");
    static const auto NAME_DASHED = string_fragment::from_const("dashed");
    static const auto NAME_DOTTED = string_fragment::from_const("dotted");
    static const auto& vc = view_colors::singleton();

    text_attrs border_attrs;
    auto border_sf = value;
    auto width = border_line_width::thick;
    auto ch = side == border_side::left ? left_border_string(width)
                                        : right_border_string(width);

    while (!border_sf.empty()) {
        auto border_split_res
            = border_sf.split_when(string_fragment::tag1{' '});
        auto bval = border_split_res.first;

        if (bval == NAME_THIN) {
            width = border_line_width::thin;
        } else if (bval == NAME_MEDIUM) {
            width = border_line_width::medium;
        } else if (bval == NAME_THICK) {
            width = border_line_width::thick;
        } else if (bval == NAME_DOTTED) {
            ch = "\u250A";
        } else if (bval == NAME_DASHED) {
            ch = "\u254F";
        } else if (bval == NAME_SOLID) {
            ch = side == border_side::left ? left_border_string(width)
                                           : right_border_string(width);
        } else {
            auto color_res = styling::color_unit::from_str(bval);
            if (color_res.isErr()) {
                log_error("invalid border color: %.*s -- %s",
                          bval.length(),
                          bval.data(),
                          color_res.unwrapErr().c_str());
            } else {
                border_attrs.ta_fg_color = vc.match_color(color_res.unwrap());
            }
        }
        border_sf = border_split_res.second;
    }
    return attr_line_t(ch).with_attr_for_all(VC_STYLE.value(border_attrs));
}

static attr_line_t
to_attr_line(const pugi::xml_node& doc)
{
    static const auto NAME_SPAN = string_fragment::from_const("span");
    static const auto NAME_PRE = string_fragment::from_const("pre");
    static const auto NAME_FG = string_fragment::from_const("color");
    static const auto NAME_BG = string_fragment::from_const("background-color");
    static const auto NAME_FONT_WEIGHT
        = string_fragment::from_const("font-weight");
    static const auto NAME_TEXT_DECO
        = string_fragment::from_const("text-decoration");
    static const auto NAME_BORDER_LEFT
        = string_fragment::from_const("border-left");
    static const auto NAME_BORDER_RIGHT
        = string_fragment::from_const("border-right");
    static const auto& vc = view_colors::singleton();

    attr_line_t retval;
    if (doc.children().empty()) {
        retval.append(doc.text().get());
    }
    for (const auto& child : doc.children()) {
        if (child.name() == NAME_SPAN) {
            nonstd::optional<attr_line_t> left_border;
            nonstd::optional<attr_line_t> right_border;
            auto styled_span = attr_line_t(child.text().get());

            auto span_class = child.attribute("class");
            if (span_class) {
                auto cl_iter = vc.vc_class_to_role.find(span_class.value());

                if (cl_iter == vc.vc_class_to_role.end()) {
                    log_error("unknown span class: %s", span_class.value());
                } else {
                    styled_span.with_attr_for_all(cl_iter->second);
                }
            }
            text_attrs ta;
            auto span_style = child.attribute("style");
            if (span_style) {
                auto style_sf = string_fragment::from_c_str(span_style.value());

                while (!style_sf.empty()) {
                    auto split_res
                        = style_sf.split_when(string_fragment::tag1{';'});
                    auto colon_split_res = split_res.first.split_pair(
                        string_fragment::tag1{':'});
                    if (colon_split_res) {
                        auto key = colon_split_res->first.trim();
                        auto value = colon_split_res->second.trim();

                        if (key == NAME_FG) {
                            auto color_res
                                = styling::color_unit::from_str(value);

                            if (color_res.isErr()) {
                                log_error("invalid color: %.*s -- %s",
                                          value.length(),
                                          value.data(),
                                          color_res.unwrapErr().c_str());
                            } else {
                                ta.ta_fg_color
                                    = vc.match_color(color_res.unwrap());
                            }
                        } else if (key == NAME_BG) {
                            auto color_res
                                = styling::color_unit::from_str(value);

                            if (color_res.isErr()) {
                                log_error(
                                    "invalid background-color: %.*s -- %s",
                                    value.length(),
                                    value.data(),
                                    color_res.unwrapErr().c_str());
                            } else {
                                ta.ta_bg_color
                                    = vc.match_color(color_res.unwrap());
                            }
                        } else if (key == NAME_FONT_WEIGHT) {
                            if (value == "bold" || value == "bolder") {
                                ta.ta_attrs |= A_BOLD;
                            }
                        } else if (key == NAME_TEXT_DECO) {
                            auto deco_sf = value;

                            while (!deco_sf.empty()) {
                                auto deco_split_res = deco_sf.split_when(
                                    string_fragment::tag1{' '});

                                if (deco_split_res.first.trim() == "underline")
                                {
                                    ta.ta_attrs |= A_UNDERLINE;
                                }

                                deco_sf = deco_split_res.second;
                            }
                        } else if (key == NAME_BORDER_LEFT) {
                            left_border
                                = span_style_border(border_side::left, value);
                        } else if (key == NAME_BORDER_RIGHT) {
                            right_border
                                = span_style_border(border_side::right, value);
                        }
                    }
                    style_sf = split_res.second;
                }
                if (!ta.empty()) {
                    styled_span.with_attr_for_all(VC_STYLE.value(ta));
                }
            }
            if (left_border) {
                retval.append(left_border.value());
            }
            retval.append(styled_span);
            if (right_border) {
                retval.append(right_border.value());
            }
        } else if (child.name() == NAME_PRE) {
            auto pre_al = attr_line_t();

            for (const auto& sub : child.children()) {
                auto child_al = to_attr_line(sub);
                if (pre_al.empty() && startswith(child_al.get_string(), "\n")) {
                    child_al.erase(0, 1);
                }
                pre_al.append(child_al);
            }
            pre_al.with_attr_for_all(SA_PREFORMATTED.value());
            retval.append(pre_al);
        } else {
            retval.append(child.text().get());
        }
    }

    return retval;
}

Result<void, std::string>
md2attr_line::text(MD_TEXTTYPE tt, const string_fragment& sf)
{
    static const auto& entity_map = md4cpp::get_xml_entity_map();

    auto& last_block = this->ml_blocks.back();

    switch (tt) {
        case MD_TEXT_BR:
            last_block.append("\n");
            break;
        case MD_TEXT_SOFTBR: {
            if (!last_block.empty() && !isspace(last_block.get_string().back()))
            {
                last_block.append(" ");
            }
            break;
        }
        case MD_TEXT_ENTITY: {
            auto xe_iter = entity_map.xem_entities.find(sf.to_string());

            if (xe_iter != entity_map.xem_entities.end()) {
                last_block.append(xe_iter->second.xe_chars);
            }
            break;
        }
        case MD_TEXT_HTML: {
            last_block.append(sf);

            struct open_tag {
                std::string ot_name;
            };
            struct close_tag {
                std::string ct_name;
            };

            mapbox::util::variant<open_tag, close_tag> tag{
                mapbox::util::no_init{}};

            if (sf.startswith("</")) {
                tag = close_tag{
                    sf.substr(2)
                        .split_when(string_fragment::tag1{'>'})
                        .first.to_string(),
                };
            } else if (sf.startswith("<")) {
                tag = open_tag{
                    sf.substr(1)
                        .split_when(
                            [](char ch) { return ch == ' ' || ch == '>'; })
                        .first.to_string(),
                };
            }

            if (tag.valid()) {
                tag.match(
                    [this, &sf, &last_block](const open_tag& ot) {
                        if (!this->ml_html_starts.empty()) {
                            return;
                        }
                        this->ml_html_starts.emplace_back(
                            ot.ot_name, last_block.length() - sf.length());
                    },
                    [this, &last_block](const close_tag& ct) {
                        if (this->ml_html_starts.empty()) {
                            return;
                        }
                        if (this->ml_html_starts.back().first != ct.ct_name) {
                            return;
                        }

                        const auto html_span = last_block.get_string().substr(
                            this->ml_html_starts.back().second);

                        pugi::xml_document doc;

                        auto load_res = doc.load_string(html_span.c_str());
                        if (!load_res) {
                            log_error("failed to parse: %s",
                                      load_res.description());
                        } else {
                            last_block.erase(
                                this->ml_html_starts.back().second);
                            last_block.append(to_attr_line(doc));
                        }
                        this->ml_html_starts.pop_back();
                    });
            }
            break;
        }
        default: {
            static const auto REPL_RE = lnav::pcre2pp::code::from_const(
                R"(-{2,3}|:[^:\s]*(?:::[^:\s]*)*:)");
            static const auto& emojis = md4cpp::get_emoji_map();

            if (this->ml_code_depth > 0) {
                last_block.append(sf);
                return Ok();
            }

            std::string span_text;

            auto loop_res = REPL_RE.capture_from(sf).for_each(
                [&span_text](lnav::pcre2pp::match_data& md) {
                    span_text += md.leading();

                    auto matched = *md[0];

                    if (matched == "--") {
                        span_text.append("\u2013");
                    } else if (matched == "---") {
                        span_text.append("\u2014");
                    } else if (matched.startswith(":")) {
                        auto em_iter = emojis.em_shortname2emoji.find(
                            matched.to_string());
                        if (em_iter == emojis.em_shortname2emoji.end()) {
                            span_text += matched;
                        } else {
                            span_text.append(em_iter->second.get().e_value);
                        }
                    }
                });

            if (loop_res.isOk()) {
                span_text += loop_res.unwrap();
            } else {
                log_error("span replacement regex failed: %d",
                          loop_res.unwrapErr().e_error_code);
            }

            text_wrap_settings tws
                = {0, this->ml_blocks.size() == 1 ? 70 : 10000};

            last_block.append(span_text, &tws);
            break;
        }
    }
    return Ok();
}

void
md2attr_line::append_url_footnote(std::string href_str)
{
    if (startswith(href_str, "#")) {
        return;
    }

    auto& last_block = this->ml_blocks.back();
    last_block.appendf(FMT_STRING("[{}]"), this->ml_footnotes.size() + 1);
    last_block.with_attr(string_attr{
        line_range{
            (int) this->ml_span_starts.back(),
            (int) last_block.length(),
        },
        VC_STYLE.value(text_attrs{A_UNDERLINE}),
    });
    if (this->ml_source_path && href_str.find(':') == std::string::npos) {
        auto link_path = ghc::filesystem::absolute(
            this->ml_source_path.value().parent_path() / href_str);

        href_str = fmt::format(FMT_STRING("file://{}"), link_path.string());
    }

    auto href
        = attr_line_t().append(lnav::roles::hyperlink(href_str)).append(" ");
    href.with_attr_for_all(VC_ROLE.value(role_t::VCR_FOOTNOTE_TEXT));
    href.with_attr_for_all(SA_PREFORMATTED.value());
    this->ml_footnotes.emplace_back(href);
}
