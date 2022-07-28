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
#include "pcrepp/pcrepp.hh"
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
                && !endswith(last_block.get_string(), "\n\n")) {
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
                 line_index++) {
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
        default: {
            static const pcrepp REPL_RE(R"(-{2,3}|:[^:\s]*(?:::[^:\s]*)*:)");
            static const auto& emojis = md4cpp::get_emoji_map();

            if (this->ml_code_depth > 0) {
                last_block.append(sf);
                return Ok();
            }

            pcre_input pi(sf);
            pcre_context_static<30> pc;

            while (REPL_RE.match(pc, pi)) {
                auto prev = pi.get_up_to(pc.all());
                last_block.append(prev);

                auto matched = pi.get_string_fragment(pc.all());

                if (matched == "--") {
                    last_block.append("\u2013");
                } else if (matched == "---") {
                    last_block.append("\u2014");
                } else if (matched.startswith(":")) {
                    auto em_iter
                        = emojis.em_shortname2emoji.find(matched.to_string());
                    if (em_iter == emojis.em_shortname2emoji.end()) {
                        last_block.append(matched);
                    } else {
                        last_block.append(em_iter->second.get().e_value);
                    }
                }
            }

            this->ml_blocks.back().append(sf.substr(pi.pi_offset));
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
