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
#include "base/itertools.enumerate.hh"
#include "base/itertools.hh"
#include "base/lnav_log.hh"
#include "base/map_util.hh"
#include "base/types.hh"
#include "document.sections.hh"
#include "pcrepp/pcre2pp.hh"
#include "pugixml/pugixml.hpp"
#include "readline_highlighters.hh"
#include "text_format.hh"
#include "textfile_highlighters.hh"
#include "view_curses.hh"

using namespace lnav::roles::literals;
using namespace md4cpp::literals;

static const std::map<string_fragment, text_format_t> CODE_NAME_TO_TEXT_FORMAT
    = {
        {"c"_frag, text_format_t::TF_C_LIKE},
        {"c++"_frag, text_format_t::TF_C_LIKE},
        {"java"_frag, text_format_t::TF_JAVA},
        {"python"_frag, text_format_t::TF_PYTHON},
        {"rust"_frag, text_format_t::TF_RUST},
        {"toml"_frag, text_format_t::TF_TOML},
        {"yaml"_frag, text_format_t::TF_YAML},
        {"xml"_frag, text_format_t::TF_XML},
};

static highlight_map_t
get_highlight_map()
{
    highlight_map_t retval;

    setup_highlights(retval);
    return retval;
}

void
md2attr_line::flush_footnotes()
{
    if (this->ml_footnotes.empty()) {
        return;
    }

    auto& block_text = this->ml_blocks.back();
    auto longest_foot = this->ml_footnotes
        | lnav::itertools::map(&attr_line_t::column_width)
        | lnav::itertools::max(0);

    block_text.append("\n");
    for (const auto& [index, foot] :
         lnav::itertools::enumerate(this->ml_footnotes, 1))
    {
        auto footline
            = attr_line_t(" ")
                  .append("\u258c"_footnote_border)
                  .append(lnav::roles::footnote_text(
                      index < 10 && this->ml_footnotes.size() >= 10 ? " " : ""))
                  .append(lnav::roles::footnote_text(
                      fmt::format(FMT_STRING("[{}] - "), index)))
                  .append(foot.pad_to(longest_foot))
                  .with_attr_for_all(SA_PREFORMATTED.value())
                  .move();

        block_text.append(footline).append("\n");
    }
    this->ml_footnotes.clear();
}

Result<void, std::string>
md2attr_line::enter_block(const md4cpp::event_handler::block& bl)
{
    if (this->ml_source_path) {
        log_trace("enter_block %s",
                  mapbox::util::apply_visitor(type_visitor(), bl));
    }

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
    if (this->ml_source_path) {
        log_trace("leave_block %s",
                  mapbox::util::apply_visitor(type_visitor(), bl));
    }

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
                         .with_attr_for_all(SA_PREFORMATTED.value())
                         .move();
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
        const auto* li_detail = bl.get<MD_BLOCK_LI_DETAIL*>();
        auto tws = text_wrap_settings{
            0,
            63 - (int) (this->ml_list_stack.size() * 3),
        };

        attr_line_builder alb(last_block);
        {
            auto prefix = alb.with_attr(SA_PREFORMATTED.value());

            alb.append(" ")
                .append(last_list_block.match(
                    [this, li_detail, &tws](const MD_BLOCK_UL_DETAIL*) {
                        static const std::string glyph1 = "\u2022";
                        static const std::string glyph2 = "\u2014";
                        static const std::string unchecked = "[ ]";
                        static const std::string checked = "[\u2713]";
                        tws.tws_indent = 3;

                        if (li_detail->is_task) {
                            return lnav::roles::list_glyph(
                                li_detail->task_mark == ' ' ? unchecked
                                                            : checked);
                        }

                        return lnav::roles::list_glyph(
                            this->ml_list_stack.size() % 2 == 1 ? glyph1
                                                                : glyph2);
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
        auto tf_opt = lnav::map::find(CODE_NAME_TO_TEXT_FORMAT, lang_sf);
        if (tf_opt) {
            static const auto highlighters = get_highlight_map();

            lnav::document::discover(block_text)
                .with_text_format(tf_opt.value())
                .perform();
            for (const auto& hl_pair : highlighters) {
                const auto& hl = hl_pair.second;

                if (!hl.applies_to_format(tf_opt.value())) {
                    continue;
                }
                hl.annotate(block_text, 0);
            }
        } else if (lang_sf == "lnav") {
            readline_lnav_highlighter(block_text, block_text.length());
        } else if (lang_sf == "sql" || lang_sf == "sqlite" || lang_sf == "prql")
        {
            readline_sqlite_highlighter(block_text, block_text.length());
        } else if (lang_sf == "shell" || lang_sf == "bash") {
            readline_shlex_highlighter(block_text, block_text.length());
        } else if (lang_sf == "console" || lang_sf.iequal("shellsession"_frag))
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
            if (!cmd_block.empty()) {
                new_block_text.append(cmd_block);
            }
            block_text = new_block_text.move();
        }

        auto code_lines = block_text.rtrim().split_lines();
        auto max_width = code_lines
            | lnav::itertools::map(&attr_line_t::column_width)
            | lnav::itertools::max(0);
        attr_line_t padded_text;

        for (auto& line : code_lines) {
            line.pad_to(std::max(max_width + 4, size_t{40}))
                .with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_CODE));
            padded_text.append(" ")
                .append("\u258c"_code_border)
                .append(line)
                .append("\n");
        }
        if (!padded_text.empty()) {
            padded_text.with_attr_for_all(SA_PREFORMATTED.value());
            last_block.append("\n").append(padded_text);
        }
    } else if (bl.is<block_quote>()) {
        const static auto ALERT_TYPE = lnav::pcre2pp::code::from_const(
            R"(^\s*\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\])");

        text_wrap_settings tws = {0, 60};
        attr_line_t wrapped_text;
        auto md = ALERT_TYPE.create_match_data();
        std::optional<role_t> border_role;

        block_text.rtrim();
        if (ALERT_TYPE.capture_from(block_text.al_string)
                .into(md)
                .matches()
                .ignore_error())
        {
            attr_line_t replacement;

            if (md[1] == "NOTE") {
                replacement.append("\u24d8  Note\n"_footnote_border);
                border_role = role_t::VCR_FOOTNOTE_BORDER;
            } else if (md[1] == "TIP") {
                replacement.append(":bulb:"_emoji)
                    .append(" Tip\n")
                    .with_attr_for_all(VC_ROLE.value(role_t::VCR_OK));
                border_role = role_t::VCR_OK;
            } else if (md[1] == "IMPORTANT") {
                replacement.append(":star2:"_emoji)
                    .append(" Important\n")
                    .with_attr_for_all(VC_ROLE.value(role_t::VCR_INFO));
                border_role = role_t::VCR_INFO;
            } else if (md[1] == "WARNING") {
                replacement.append(":warning:"_emoji)
                    .append(" Warning\n")
                    .with_attr_for_all(VC_ROLE.value(role_t::VCR_WARNING));
                border_role = role_t::VCR_WARNING;
            } else if (md[1] == "CAUTION") {
                replacement.append(":small_red_triangle:"_emoji)
                    .append(" Caution\n")
                    .with_attr_for_all(VC_ROLE.value(role_t::VCR_ERROR));
                border_role = role_t::VCR_ERROR;
            } else {
                ensure(0);
            }
            block_text.erase(md[0]->sf_begin, md[0]->length());
            block_text.insert(0, replacement);
        }

        wrapped_text.append(block_text, &tws);
        auto quoted_lines = wrapped_text.split_lines();
        auto max_width = quoted_lines
            | lnav::itertools::map(&attr_line_t::column_width)
            | lnav::itertools::max(tws.tws_width);
        attr_line_t padded_text;

        for (auto& line : quoted_lines) {
            line.pad_to(max_width + 1)
                .with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_TEXT));
            padded_text.append(" ");
            auto start_index = padded_text.length();
            padded_text.append("\u258c"_quote_border);
            if (border_role) {
                padded_text.with_attr(string_attr{
                    line_range{
                        (int) start_index,
                        (int) padded_text.length(),
                    },
                    VC_ROLE_FG.value(border_role.value()),
                });
            }
            padded_text.append(line).append("\n");
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
            if (lpc >= tab.t_headers.size()) {
                continue;
            }
            max_col_sizes[lpc] = tab.t_headers[lpc].column_width();
            tab.t_headers[lpc].with_attr_for_all(
                VC_ROLE.value(role_t::VCR_TABLE_HEADER));
        }
        for (const auto& row : tab.t_rows) {
            for (size_t lpc = 0; lpc < table_detail->col_count; lpc++) {
                if (lpc >= row.r_columns.size()) {
                    continue;
                }
                auto col_len = row.r_columns[lpc].c_contents.column_width();
                if ((ssize_t) col_len > max_col_sizes[lpc]) {
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
            cells.emplace_back(MD_ALIGN_CENTER, td_block.rtrim().split_lines());
            if (cells.back().cl_lines.size() > max_cell_lines) {
                max_cell_lines = cells.back().cl_lines.size();
            }
        }
        for (size_t line_index = 0; line_index < max_cell_lines; line_index++) {
            for (const auto& [col, cell] : lnav::itertools::enumerate(cells)) {
                block_text.append(" ");
                if (line_index < cell.cl_lines.size()) {
                    block_text.append(cell.cl_lines[line_index]);
                    block_text.append(
                        col_sizes[col]
                            - cell.cl_lines[line_index].column_width(),
                        ' ');
                } else {
                    block_text.append(col_sizes[col], ' ');
                }
            }
            block_text.append("\n")
                .append(lnav::roles::table_border(
                    repeat("\u2550", full_width + col_sizes.size())))
                .append("\n");
        }
        size_t row_index = 0;
        for (const auto& row : tab.t_rows) {
            cells.clear();
            max_cell_lines = 0;
            for (const auto& [col_index, cell] :
                 lnav::itertools::enumerate(row.r_columns))
            {
                tws.with_width(col_sizes[col_index]);

                attr_line_t td_block;
                td_block.append(cell.c_contents, &tws);
                cells.emplace_back(cell.c_align,
                                   td_block.rtrim().split_lines());
                if (cells.back().cl_lines.size() > max_cell_lines) {
                    max_cell_lines = cells.back().cl_lines.size();
                }
            }
            auto alt_row_index = row_index % 4;
            auto line_lr = line_range{(int) block_text.al_string.size(), 0};
            for (size_t line_index = 0; line_index < max_cell_lines;
                 line_index++)
            {
                size_t col = 0;
                for (const auto& cell : cells) {
                    block_text.append(" ");
                    if (line_index < cell.cl_lines.size()) {
                        auto padding = size_t{0};

                        if (col < col_sizes.size()) {
                            if (col_sizes[col]
                                > (ssize_t) cell.cl_lines[line_index]
                                      .column_width())
                            {
                                padding = col_sizes[col]
                                    - cell.cl_lines[line_index].column_width();
                            }
                        }

                        auto lpadding = size_t{0};
                        auto rpadding = size_t{0};
                        switch (cell.cl_align) {
                            case MD_ALIGN_DEFAULT:
                            case MD_ALIGN_LEFT:
                                rpadding = padding;
                                break;
                            case MD_ALIGN_CENTER:
                                lpadding = padding / 2;
                                rpadding = padding - lpadding;
                                break;
                            case MD_ALIGN_RIGHT:
                                lpadding = padding;
                                break;
                        }
                        block_text.append(lpadding, ' ');
                        block_text.append(cell.cl_lines[line_index]);
                        block_text.append(rpadding, ' ');
                    } else if (col < col_sizes.size() - 1) {
                        block_text.append(col_sizes[col], ' ');
                    }
                    col += 1;
                }
                block_text.append("\n");
            }
            if (alt_row_index == 2 || alt_row_index == 3) {
                line_lr.lr_end = block_text.al_string.size();
                block_text.al_attrs.emplace_back(
                    line_lr, VC_ROLE.value(role_t::VCR_ALT_ROW));
            }
            if (max_cell_lines > 0) {
                row_index += 1;
            }
        }
        if (!block_text.empty()) {
            block_text.with_attr_for_all(SA_PREFORMATTED.value());
            last_block.append(block_text);
        }
    } else if (bl.is<block_th>()) {
        this->ml_tables.back().t_headers.push_back(block_text);
    } else if (bl.is<MD_BLOCK_TD_DETAIL*>()) {
        auto td_detail = bl.get<MD_BLOCK_TD_DETAIL*>();
        this->ml_tables.back().t_rows.back().r_columns.emplace_back(
            td_detail->align, block_text);
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
    if (this->ml_source_path) {
        log_trace("enter_span %s",
                  mapbox::util::apply_visitor(type_visitor(), sp));
    }

    auto& last_block = this->ml_blocks.back();
    this->ml_span_starts.push_back(last_block.length());
    if (sp.is<span_code>()) {
        last_block.append(" ");
        this->ml_code_depth += 1;
    } else if (sp.is<MD_SPAN_IMG_DETAIL*>()) {
        last_block.append(":framed_picture:"_emoji).append("  ");
    }
    return Ok();
}

Result<void, std::string>
md2attr_line::leave_span(const md4cpp::event_handler::span& sp)
{
    if (this->ml_source_path) {
        log_trace("leave_span %s",
                  mapbox::util::apply_visitor(type_visitor(), sp));
    }

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
            VC_ROLE.value(role_t::VCR_INLINE_CODE),
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
        last_block.with_attr({
            lr,
            VC_STYLE.value(text_attrs{text_attrs::with_italic()}),
        });
    } else if (sp.is<span_strong>()) {
        line_range lr{
            static_cast<int>(this->ml_span_starts.back()),
            static_cast<int>(last_block.length()),
        };
        last_block.with_attr({
            lr,
            VC_STYLE.value(text_attrs::with_bold()),
        });
    } else if (sp.is<span_u>()) {
        line_range lr{
            static_cast<int>(this->ml_span_starts.back()),
            static_cast<int>(last_block.length()),
        };
        last_block.with_attr({
            lr,
            VC_STYLE.value(text_attrs::with_underline()),
        });
    } else if (sp.is<span_del>()) {
        auto lr = line_range{
            static_cast<int>(this->ml_span_starts.back()),
            static_cast<int>(last_block.length()),
        };
        last_block.with_attr({
            lr,
            VC_STYLE.value(text_attrs::with_struck()),
        });
    } else if (sp.is<MD_SPAN_A_DETAIL*>()) {
        const auto* a_detail = sp.get<MD_SPAN_A_DETAIL*>();
        auto href_str = std::string(a_detail->href.text, a_detail->href.size);
        line_range lr{
            static_cast<int>(this->ml_span_starts.back()),
            static_cast<int>(last_block.length()),
        };
        auto abs_href = this->append_url_footnote(href_str);
        last_block.with_attr({
            lr,
            VC_HYPERLINK.value(abs_href),
        });
    } else if (sp.is<MD_SPAN_IMG_DETAIL*>()) {
        const auto* img_detail = sp.get<MD_SPAN_IMG_DETAIL*>();
        const auto src_str
            = std::string(img_detail->src.text, img_detail->src.size);

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
    ensure(false);
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
    ensure(false);
}

static attr_line_t
span_style_border(border_side side, const string_fragment& value)
{
    static constexpr auto NAME_THIN = "thin"_frag;
    static constexpr auto NAME_MEDIUM = "medium"_frag;
    static constexpr auto NAME_THICK = "thick"_frag;
    static constexpr auto NAME_SOLID = "solid"_frag;
    static constexpr auto NAME_DASHED = "dashed"_frag;
    static constexpr auto NAME_DOTTED = "dotted"_frag;
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

attr_line_t
md2attr_line::to_attr_line(const pugi::xml_node& doc)
{
    static const auto NAME_IMG = "img"_frag;
    static const auto NAME_SPAN = "span"_frag;
    static const auto NAME_PRE = "pre"_frag;
    static const auto NAME_FG = "color"_frag;
    static const auto NAME_BG = "background-color"_frag;
    static const auto NAME_FONT_WEIGHT = "font-weight"_frag;
    static const auto NAME_TEXT_DECO = "text-decoration"_frag;
    static const auto NAME_BORDER_LEFT = "border-left"_frag;
    static const auto NAME_BORDER_RIGHT = "border-right"_frag;
    static const auto& vc = view_colors::singleton();

    if (this->ml_source_path) {
        log_trace("converting HTML to attr_line");
    }

    attr_line_t retval;
    if (doc.children().empty()) {
        retval.append(doc.text().get());
    }
    for (const auto& child : doc.children()) {
        if (child.name() == NAME_IMG) {
            std::optional<std::string> src_href;
            std::string link_label;
            auto img_src = child.attribute("src");
            auto img_alt = child.attribute("alt");
            if (img_alt) {
                link_label = img_alt.value();
            } else if (img_src) {
                link_label = std::filesystem::path(img_src.value())
                                 .filename()
                                 .string();
            } else {
                link_label = "img";
            }

            if (img_src) {
                auto src_value = std::string(img_src.value());
                if (is_url(src_value)) {
                    src_href = src_value;
                } else {
                    auto src_path = std::filesystem::path(src_value);
                    std::error_code ec;

                    if (src_path.is_relative() && this->ml_source_path) {
                        src_path = this->ml_source_path.value().parent_path()
                            / src_path;
                    }
                    auto canon_path = std::filesystem::canonical(src_path, ec);
                    if (!ec) {
                        src_path = canon_path;
                    }

                    src_href = fmt::format(FMT_STRING("file://{}"),
                                           src_path.string());
                }
            }

            if (src_href) {
                retval.append(":framed_picture:"_emoji)
                    .append("  ")
                    .append(
                        lnav::string::attrs::href(link_label, src_href.value()))
                    .append(to_superscript(this->ml_footnotes.size() + 1));

                auto href
                    = attr_line_t()
                          .append(lnav::roles::hyperlink(src_href.value()))
                          .append(" ")
                          .with_attr_for_all(
                              VC_ROLE.value(role_t::VCR_FOOTNOTE_TEXT))
                          .with_attr_for_all(SA_PREFORMATTED.value())
                          .move();
                this->ml_footnotes.emplace_back(href);
            } else {
                retval.append(link_label);
            }
        } else if (child.name() == NAME_SPAN) {
            std::optional<attr_line_t> left_border;
            std::optional<attr_line_t> right_border;
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
                                ta |= text_attrs::style::bold;
                            }
                        } else if (key == NAME_TEXT_DECO) {
                            auto deco_sf = value;

                            while (!deco_sf.empty()) {
                                auto deco_split_res = deco_sf.split_when(
                                    string_fragment::tag1{' '});

                                if (deco_split_res.first.trim() == "underline")
                                {
                                    ta |= text_attrs::style::underline;
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
                auto child_al = this->to_attr_line(sub);
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
            auto last_block_start_length = last_block.length();
            last_block.append(sf);

            struct open_tag {
                std::string ot_name;
            };
            struct close_tag {
                std::string ct_name;
            };
            struct empty_tag {};

            using html_tag_t
                = mapbox::util::variant<open_tag, close_tag, empty_tag>;

            html_tag_t tag{mapbox::util::no_init{}};

            auto lbracket = sf.find('<');
            if (!lbracket) {
            } else if (lbracket && lbracket.value() + 1 < sf.length()
                       && sf[lbracket.value() + 1] == '/')
            {
                tag = close_tag{
                    sf.substr(lbracket.value() + 2)
                        .split_when(string_fragment::tag1{'>'})
                        .first.to_string(),
                };
            } else if (sf.startswith("<")) {
                if (sf.endswith("/>")) {
                    tag = empty_tag{};
                } else {
                    tag = open_tag{
                        sf.substr(1)
                            .split_when(
                                [](char ch) { return ch == ' ' || ch == '>'; })
                            .first.to_string(),
                    };
                }
            }

            if (tag.valid()) {
                tag.match(
                    [this, last_block_start_length](const open_tag& ot) {
                        if (!this->ml_html_starts.empty()) {
                            return;
                        }
                        this->ml_html_starts.emplace_back(
                            ot.ot_name, last_block_start_length);
                    },
                    [this, &last_block](const close_tag& ct) {
                        if (this->ml_html_starts.empty()) {
                            log_warning("closing tag %s with no open tag",
                                        ct.ct_name.c_str());
                            return;
                        }
                        if (this->ml_html_starts.back().first != ct.ct_name) {
                            log_warning(
                                "closing tag %s with no matching open tag",
                                ct.ct_name.c_str());
                            return;
                        }

                        const auto html_span = last_block.get_string().substr(
                            this->ml_html_starts.back().second);

                        pugi::xml_document doc;

                        auto load_res = doc.load_string(html_span.c_str());
                        if (!load_res) {
                            log_error("XML parsing failure at %d: %s",
                                      load_res.offset,
                                      load_res.description());

                            auto sf = string_fragment::from_str(html_span);
                            auto error_line = sf.find_boundaries_around(
                                load_res.offset, string_fragment::tag1{'\n'});
                            log_error("  %.*s",
                                      error_line.length(),
                                      error_line.data());
                        } else {
                            last_block.erase(
                                this->ml_html_starts.back().second);
                            last_block.append(this->to_attr_line(doc));
                        }
                        this->ml_html_starts.pop_back();
                    },
                    [this, &sf, &last_block, last_block_start_length](
                        const empty_tag&) {
                        const auto html_span = sf.to_string();

                        pugi::xml_document doc;

                        auto load_res = doc.load_string(html_span.c_str());
                        if (!load_res) {
                            log_error("XML parsing failure at %d: %s",
                                      load_res.offset,
                                      load_res.description());

                            auto error_line = sf.find_boundaries_around(
                                load_res.offset, string_fragment::tag1{'\n'});
                            log_error("  %.*s",
                                      error_line.length(),
                                      error_line.data());
                        } else {
                            last_block.erase(last_block_start_length);
                            last_block.append(this->to_attr_line(doc));
                        }
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
                [&span_text](const lnav::pcre2pp::match_data& md) {
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

            auto span_al = attr_line_t::from_ansi_str(span_text);
            last_block.append(span_al, &tws);
            break;
        }
    }
    return Ok();
}

std::string
md2attr_line::append_url_footnote(std::string href_str)
{
    auto is_internal = startswith(href_str, "#");
    auto& last_block = this->ml_blocks.back();
    last_block.with_attr(string_attr{
        line_range{
            (int) this->ml_span_starts.back(),
            (int) last_block.length(),
        },
        VC_STYLE.value(text_attrs::with_underline()),
    });
    if (is_internal) {
        return href_str;
    }

    if (this->ml_last_superscript_index == last_block.length()) {
        last_block.append("\u02d2");
    }
    last_block.append(to_superscript(this->ml_footnotes.size() + 1));
    this->ml_last_superscript_index = last_block.length();
    if (this->ml_source_path && href_str.find(':') == std::string::npos) {
        auto link_path = std::filesystem::absolute(
            this->ml_source_path.value().parent_path() / href_str);

        href_str = fmt::format(FMT_STRING("file://{}"), link_path.string());
    }

    auto href = attr_line_t()
                    .append(lnav::roles::hyperlink(href_str))
                    .append(" ")
                    .with_attr_for_all(VC_ROLE.value(role_t::VCR_FOOTNOTE_TEXT))
                    .with_attr_for_all(SA_PREFORMATTED.value())
                    .move();
    this->ml_footnotes.emplace_back(href);

    return href_str;
}
