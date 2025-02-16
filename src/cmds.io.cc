/**
 * Copyright (c) 2025, Timothy Stack
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

#include <regex>

#include <fnmatch.h>
#include <glob.h>

#include "base/attr_line.builder.hh"
#include "base/fs_util.hh"
#include "base/humanize.hh"
#include "base/humanize.network.hh"
#include "base/itertools.hh"
#include "base/paths.hh"
#include "bound_tags.hh"
#include "curl_looper.hh"
#include "external_opener.hh"
#include "field_overlay_source.hh"
#include "fmt/color.h"
#include "fmt/printf.h"
#include "lnav.hh"
#include "lnav_commands.hh"
#include "lnav_util.hh"
#include "scn/scan.h"
#include "service_tags.hh"
#include "shlex.hh"
#include "sql_util.hh"
#include "sysclip.hh"
#include "tailer/tailer.looper.hh"
#include "text_anonymizer.hh"
#include "url_handler.cfg.hh"
#include "url_loader.hh"
#include "vtab_module.hh"
#include "yajlpp/json_op.hh"

#if !CURL_AT_LEAST_VERSION(7, 80, 0)
extern "C"
{
    const char* curl_url_strerror(CURLUcode error);
}
#endif

static bool
csv_needs_quoting(const std::string& str)
{
    return (str.find_first_of(",\"\r\n") != std::string::npos);
}

static std::string
csv_quote_string(const std::string& str)
{
    static const std::regex csv_column_quoter("\"");

    std::string retval = std::regex_replace(str, csv_column_quoter, "\"\"");

    retval.insert(0, 1, '\"');
    retval.append(1, '\"');

    return retval;
}

static void
csv_write_string(FILE* outfile, const std::string& str)
{
    if (csv_needs_quoting(str)) {
        std::string quoted_str = csv_quote_string(str);

        fmt::fprintf(outfile, "%s", quoted_str);
    } else {
        fmt::fprintf(outfile, "%s", str);
    }
}

static void
yajl_writer(void* context, const char* str, size_t len)
{
    FILE* file = (FILE*) context;

    fwrite(str, len, 1, file);
}

static void
json_write_row(yajl_gen handle,
               int row,
               lnav::text_anonymizer& ta,
               bool anonymize)
{
    auto& dls = lnav_data.ld_db_row_source;
    yajlpp_map obj_map(handle);

    auto cursor = dls.dls_row_cursors[row].sync();
    for (size_t col = 0; col < dls.dls_headers.size();
         col++, cursor = cursor->next())
    {
        const auto& hm = dls.dls_headers[col];

        obj_map.gen(hm.hm_name);

        switch (cursor->get_type()) {
            case lnav::cell_type::CT_NULL:
                obj_map.gen();
                break;
            case lnav::cell_type::CT_INTEGER:
                obj_map.gen(cursor->get_int());
                break;
            case lnav::cell_type::CT_FLOAT:
                obj_map.gen(cursor->get_float());
                break;
            case lnav::cell_type::CT_TEXT: {
                if (hm.hm_sub_type == JSON_SUBTYPE) {
                    unsigned char* err;
                    json_ptr jp("");
                    json_op jo(jp);

                    jo.jo_ptr_callbacks = json_op::gen_callbacks;
                    jo.jo_ptr_data = handle;
                    auto parse_handle
                        = yajlpp::alloc_handle(&json_op::ptr_callbacks, &jo);

                    const auto json_in = cursor->get_text();
                    switch (yajl_parse(
                        parse_handle.in(), json_in.udata(), json_in.length()))
                    {
                        case yajl_status_error:
                        case yajl_status_client_canceled: {
                            err = yajl_get_error(parse_handle.in(),
                                                 0,
                                                 json_in.udata(),
                                                 json_in.length());
                            log_error("unable to parse JSON cell: %s", err);
                            obj_map.gen(cursor->get_text());
                            yajl_free_error(parse_handle.in(), err);
                            return;
                        }
                        default:
                            break;
                    }

                    switch (yajl_complete_parse(parse_handle.in())) {
                        case yajl_status_error:
                        case yajl_status_client_canceled: {
                            err = yajl_get_error(parse_handle.in(),
                                                 0,
                                                 json_in.udata(),
                                                 json_in.length());
                            log_error("unable to parse JSON cell: %s", err);
                            obj_map.gen(cursor->get_text());
                            yajl_free_error(parse_handle.in(), err);
                            return;
                        }
                        default:
                            break;
                    }
                } else if (anonymize) {
                    obj_map.gen(ta.next(cursor->get_text()));
                } else {
                    obj_map.gen(cursor->get_text());
                }
                break;
            }
        }
    }
}

static Result<std::string, lnav::console::user_message>
com_save_to(exec_context& ec,
            std::string cmdline,
            std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("path");

    FILE *outfile = nullptr, *toclose = nullptr;
    const char* mode = "";
    std::string fn, retval;
    bool to_term = false;
    bool anonymize = false;
    int (*closer)(FILE*) = fclose;

    fn = trim(remaining_args(cmdline, args));

    shlex lexer(fn);

    auto split_args_res = lexer.split(ec.create_resolver());
    if (split_args_res.isErr()) {
        auto split_err = split_args_res.unwrapErr();
        auto um
            = lnav::console::user_message::error("unable to parse file name")
                  .with_reason(split_err.te_msg)
                  .with_snippet(lnav::console::snippet::from(
                      SRC, lexer.to_attr_line(split_err)))
                  .move();

        return Err(um);
    }

    auto split_args = split_args_res.unwrap()
        | lnav::itertools::map([](const auto& elem) { return elem.se_value; });
    auto anon_iter
        = std::find(split_args.begin(), split_args.end(), "--anonymize");
    if (anon_iter != split_args.end()) {
        split_args.erase(anon_iter);
        anonymize = true;
    }

    auto* tc = *lnav_data.ld_view_stack.top();
    auto opt_view_name = find_arg(split_args, "--view");
    if (opt_view_name) {
        auto opt_view_index = view_from_string(opt_view_name->c_str());

        if (!opt_view_index) {
            return ec.make_error("invalid view name: {}", *opt_view_name);
        }

        tc = &lnav_data.ld_views[*opt_view_index];
    }

    if (split_args.empty()) {
        return ec.make_error(
            "expecting file name or '-' to write to the terminal");
    }

    if (split_args.size() > 1) {
        return ec.make_error("more than one file name was matched");
    }

    if (args[0] == "append-to") {
        mode = "ae";
    } else {
        mode = "we";
    }

    auto& dls = lnav_data.ld_db_row_source;
    bookmark_vector<vis_line_t> all_user_marks;
    lnav::text_anonymizer ta;

    if (args[0] == "write-csv-to" || args[0] == "write-json-to"
        || args[0] == "write-jsonlines-to" || args[0] == "write-cols-to"
        || args[0] == "write-table-to")
    {
        if (dls.dls_headers.empty()) {
            return ec.make_error(
                "no query result to write, use ';' to execute a query");
        }
    } else if (args[0] == "write-raw-to" && tc == &lnav_data.ld_views[LNV_DB]) {
    } else if (args[0] != "write-screen-to" && args[0] != "write-view-to") {
        all_user_marks = combined_user_marks(tc->get_bookmarks());
        if (all_user_marks.empty()) {
            return ec.make_error(
                "no lines marked to write, use 'm' to mark lines");
        }
    }

    if (ec.ec_dry_run) {
        outfile = tmpfile();
        toclose = outfile;
    } else if (split_args[0] == "-" || split_args[0] == "/dev/stdout") {
        auto ec_out = ec.get_output();

        if (!ec_out) {
            outfile = stdout;
            if (ec.ec_ui_callbacks.uc_pre_stdout_write) {
                ec.ec_ui_callbacks.uc_pre_stdout_write();
            }
            setvbuf(stdout, nullptr, _IONBF, 0);
            to_term = true;
            fprintf(outfile,
                    "\n---------------- Press any key to exit lo-fi display "
                    "----------------\n\n");
        } else {
            outfile = *ec_out;
        }
        if (outfile == stdout) {
            lnav_data.ld_stdout_used = true;
        }
    } else if (split_args[0] == "/dev/clipboard") {
        auto open_res = sysclip::open(sysclip::type_t::GENERAL);
        if (open_res.isErr()) {
            alerter::singleton().chime("cannot open clipboard");
            return ec.make_error("Unable to copy to clipboard: {}",
                                 open_res.unwrapErr());
        }
        auto holder = open_res.unwrap();
        toclose = outfile = holder.release();
        closer = holder.get_free_func<int (*)(FILE*)>();
    } else if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    } else if ((outfile = fopen(split_args[0].c_str(), mode)) == nullptr) {
        return ec.make_error("unable to open file -- {}", split_args[0]);
    } else {
        toclose = outfile;
    }

    int line_count = 0;

    if (args[0] == "write-csv-to") {
        bool first = true;

        for (auto& dls_header : dls.dls_headers) {
            if (!first) {
                fprintf(outfile, ",");
            }
            csv_write_string(outfile, dls_header.hm_name);
            first = false;
        }
        fprintf(outfile, "\n");

        ArenaAlloc::Alloc<char> cell_alloc{1024};
        for (const auto& row_cursor : dls.dls_row_cursors) {
            if (ec.ec_dry_run && line_count > 10) {
                break;
            }

            first = true;
            auto cursor = row_cursor.sync();
            for (size_t lpc = 0; lpc < dls.dls_headers.size();
                 lpc++, cursor = cursor->next())
            {
                if (!first) {
                    fprintf(outfile, ",");
                }

                auto cell_sf = cursor->to_string_fragment(cell_alloc);
                auto cell_str = anonymize ? ta.next(cell_sf)
                                          : cell_sf.to_string();
                csv_write_string(outfile, cell_str);
                first = false;
                cell_alloc.reset();
            }
            fprintf(outfile, "\n");

            line_count += 1;
        }
    } else if (args[0] == "write-cols-to" || args[0] == "write-table-to") {
        bool first = true;
        auto tf = ec.get_output_format();

        if (tf != text_format_t::TF_MARKDOWN) {
            fprintf(outfile, "\u250f");
            for (const auto& hdr : dls.dls_headers) {
                auto cell_line = repeat("\u2501", hdr.hm_column_size);

                if (!first) {
                    fprintf(outfile, "\u2533");
                }
                fprintf(outfile, "%s", cell_line.c_str());
                first = false;
            }
            fprintf(outfile, "\u2513\n");
        }

        for (const auto& hdr : dls.dls_headers) {
            auto centered_hdr = center_str(hdr.hm_name, hdr.hm_column_size);
            auto style = fmt::text_style{};

            fprintf(outfile, tf == text_format_t::TF_MARKDOWN ? "|" : "\u2503");
            if (tf != text_format_t::TF_MARKDOWN) {
                style |= fmt::emphasis::bold;
            }
            fmt::print(outfile, style, FMT_STRING("{}"), centered_hdr);
        }
        fprintf(outfile, tf == text_format_t::TF_MARKDOWN ? "|\n" : "\u2503\n");

        first = true;
        fprintf(outfile, tf == text_format_t::TF_MARKDOWN ? "|" : "\u2521");
        for (const auto& hdr : dls.dls_headers) {
            auto cell_line
                = repeat(tf == text_format_t::TF_MARKDOWN ? "-" : "\u2501",
                         hdr.hm_column_size);

            if (tf == text_format_t::TF_MARKDOWN) {
                switch (hdr.hm_align) {
                    case text_align_t::start:
                        cell_line.front() = ':';
                        break;
                    case text_align_t::center:
                        cell_line.front() = ':';
                        cell_line.back() = ':';
                        break;
                    case text_align_t::end:
                        cell_line.back() = ':';
                        break;
                }
            }
            if (!first) {
                fprintf(outfile,
                        tf == text_format_t::TF_MARKDOWN ? "|" : "\u2547");
            }
            fprintf(outfile, "%s", cell_line.c_str());
            first = false;
        }
        fprintf(outfile, tf == text_format_t::TF_MARKDOWN ? "|\n" : "\u2529\n");

        ArenaAlloc::Alloc<char> cell_alloc{1024};
        for (size_t row = 0; row < dls.text_line_count(); row++) {
            if (ec.ec_dry_run && row > 10) {
                break;
            }

            auto cursor = dls.dls_row_cursors[row].sync();
            for (size_t col = 0; col < dls.dls_headers.size();
                 col++, cursor = cursor->next())
            {
                const auto& hdr = dls.dls_headers[col];

                fprintf(outfile,
                        tf == text_format_t::TF_MARKDOWN ? "|" : "\u2502");

                auto sf = cursor->to_string_fragment(cell_alloc);
                auto cell = attr_line_t::from_table_cell_content(sf, 200);
                if (anonymize) {
                    cell = ta.next(cell.al_string);
                }
                auto cell_length = cell.utf8_length_or_length();
                auto padding = anonymize ? 1 : hdr.hm_column_size - cell_length;
                auto rjust = hdr.hm_align == text_align_t::end;

                if (rjust) {
                    fprintf(outfile, "%s", std::string(padding, ' ').c_str());
                }
                fprintf(outfile, "%s", cell.al_string.c_str());
                if (!rjust) {
                    fprintf(outfile, "%s", std::string(padding, ' ').c_str());
                }
                cell_alloc.reset();
            }
            fprintf(outfile,
                    tf == text_format_t::TF_MARKDOWN ? "|\n" : "\u2502\n");

            line_count += 1;
        }

        if (tf != text_format_t::TF_MARKDOWN) {
            first = true;
            fprintf(outfile, "\u2514");
            for (const auto& hdr : dls.dls_headers) {
                auto cell_line = repeat("\u2501", hdr.hm_column_size);

                if (!first) {
                    fprintf(outfile, "\u2534");
                }
                fprintf(outfile, "%s", cell_line.c_str());
                first = false;
            }
            fprintf(outfile, "\u2518\n");
        }
    } else if (args[0] == "write-json-to") {
        yajlpp_gen gen;

        yajl_gen_config(gen, yajl_gen_beautify, 1);
        yajl_gen_config(gen, yajl_gen_print_callback, yajl_writer, outfile);

        {
            yajlpp_array root_array(gen);

            for (size_t row = 0; row < dls.dls_row_cursors.size(); row++) {
                if (ec.ec_dry_run && row > 10) {
                    break;
                }

                json_write_row(gen, row, ta, anonymize);
                line_count += 1;
            }
        }
    } else if (args[0] == "write-jsonlines-to") {
        yajlpp_gen gen;

        yajl_gen_config(gen, yajl_gen_beautify, 0);
        yajl_gen_config(gen, yajl_gen_print_callback, yajl_writer, outfile);

        for (size_t row = 0; row < dls.dls_row_cursors.size(); row++) {
            if (ec.ec_dry_run && row > 10) {
                break;
            }

            json_write_row(gen, row, ta, anonymize);
            yajl_gen_reset(gen, "\n");
            line_count += 1;
        }
    } else if (args[0] == "write-screen-to") {
        bool wrapped = tc->get_word_wrap();
        vis_line_t orig_top = tc->get_top();
        auto inner_height = tc->get_inner_height();

        tc->set_word_wrap(to_term);

        vis_line_t top = tc->get_top();
        vis_line_t bottom = tc->get_bottom();
        if (lnav_data.ld_flags & LNF_HEADLESS && inner_height > 0_vl) {
            bottom = inner_height - 1_vl;
        }
        auto screen_height = inner_height == 0 ? 0 : bottom - top + 1;
        auto y = 0_vl;
        auto wrapped_count = 0_vl;
        std::vector<attr_line_t> rows(screen_height);
        auto dim = tc->get_dimensions();
        attr_line_t ov_al;

        auto* los = tc->get_overlay_source();
        while (
            los != nullptr
            && los->list_static_overlay(*tc, y, tc->get_inner_height(), ov_al))
        {
            write_line_to(outfile, ov_al);
            ov_al.clear();
            ++y;
        }
        tc->listview_value_for_rows(*tc, top, rows);
        for (auto& al : rows) {
            wrapped_count += vis_line_t((al.length() - 1) / (dim.second - 2));
            if (anonymize) {
                al.al_attrs.clear();
                al.al_string = ta.next(al.al_string);
            }
            write_line_to(outfile, al);

            ++y;
            if (los != nullptr) {
                std::vector<attr_line_t> row_overlay_content;
                los->list_value_for_overlay(*tc, top, row_overlay_content);
                for (const auto& ov_row : row_overlay_content) {
                    write_line_to(outfile, ov_row);
                    line_count += 1;
                    ++y;
                }
            }
            line_count += 1;
            ++top;
        }

        tc->set_word_wrap(wrapped);
        tc->set_top(orig_top);

        if (!(lnav_data.ld_flags & LNF_HEADLESS)) {
            while (y + wrapped_count < dim.first + 2_vl) {
                fmt::print(outfile, FMT_STRING("\n"));
                ++y;
            }
        }
    } else if (args[0] == "write-raw-to") {
        if (tc == &lnav_data.ld_views[LNV_DB]) {
            ArenaAlloc::Alloc<char> cell_alloc{1024};
            for (const auto& row_cursor : dls.dls_row_cursors) {
                if (ec.ec_dry_run && line_count > 10) {
                    break;
                }

                auto cursor = row_cursor.sync();
                for (size_t lpc = 0; lpc < dls.dls_headers.size();
                     lpc++, cursor = cursor->next())
                {
                    auto sf = cursor->to_string_fragment(cell_alloc);
                    if (anonymize) {
                        fputs(ta.next(sf).c_str(), outfile);
                    } else {
                        fwrite(sf.data(), sf.length(), 1, outfile);
                    }
                    cell_alloc.reset();
                }
                fprintf(outfile, "\n");

                line_count += 1;
            }
        } else if (tc == &lnav_data.ld_views[LNV_LOG]) {
            std::optional<std::pair<logfile*, content_line_t>> last_line;
            bookmark_vector<vis_line_t> visited;
            auto& lss = lnav_data.ld_log_source;
            std::vector<attr_line_t> rows(1);
            size_t count = 0;
            std::string line;

            for (auto iter = all_user_marks.begin();
                 iter != all_user_marks.end();
                 iter++, count++)
            {
                if (ec.ec_dry_run && count > 10) {
                    break;
                }
                auto cl = lss.at(*iter);
                auto lf = lss.find(cl);
                auto lf_iter = lf->begin() + cl;

                while (lf_iter->get_sub_offset() != 0) {
                    --lf_iter;
                }

                auto line_pair = std::make_pair(
                    lf.get(),
                    content_line_t(std::distance(lf->begin(), lf_iter)));
                if (last_line && last_line.value() == line_pair) {
                    continue;
                }
                last_line = line_pair;
                auto read_res = lf->read_raw_message(lf_iter);
                if (read_res.isErr()) {
                    log_error("unable to read message: %s",
                              read_res.unwrapErr().c_str());
                    continue;
                }
                auto sbr = read_res.unwrap();
                if (anonymize) {
                    auto msg = ta.next(sbr.to_string_fragment().to_string());
                    fprintf(outfile, "%s\n", msg.c_str());
                } else {
                    fprintf(
                        outfile, "%.*s\n", (int) sbr.length(), sbr.get_data());
                }

                line_count += 1;
            }
        }
    } else if (args[0] == "write-view-to") {
        bool wrapped = tc->get_word_wrap();
        auto tss = tc->get_sub_source();

        tc->set_word_wrap(to_term);

        for (size_t lpc = 0; lpc < tss->text_line_count(); lpc++) {
            if (ec.ec_dry_run && lpc >= 10) {
                break;
            }

            std::string line;

            tss->text_value_for_line(*tc, lpc, line, text_sub_source::RF_RAW);
            if (anonymize) {
                line = ta.next(line);
            }
            fprintf(outfile, "%s\n", line.c_str());

            line_count += 1;
        }

        tc->set_word_wrap(wrapped);
    } else {
        auto* los = tc->get_overlay_source();
        auto* fos = dynamic_cast<field_overlay_source*>(los);
        std::vector<attr_line_t> rows(1);
        attr_line_t ov_al;
        size_t count = 0;

        if (fos != nullptr) {
            fos->fos_contexts.push(
                field_overlay_source::context{"", false, false, false});
        }

        auto y = 0_vl;
        while (
            los != nullptr
            && los->list_static_overlay(*tc, y, tc->get_inner_height(), ov_al))
        {
            write_line_to(outfile, ov_al);
            ov_al.clear();
            ++y;
        }
        for (auto iter = all_user_marks.begin(); iter != all_user_marks.end();
             ++iter, count++)
        {
            if (ec.ec_dry_run && count > 10) {
                break;
            }
            tc->listview_value_for_rows(*tc, *iter, rows);
            if (anonymize) {
                rows[0].al_attrs.clear();
                rows[0].al_string = ta.next(rows[0].al_string);
            }
            write_line_to(outfile, rows[0]);

            y = 0_vl;
            if (los != nullptr) {
                std::vector<attr_line_t> row_overlay_content;
                los->list_value_for_overlay(*tc, (*iter), row_overlay_content);
                for (const auto& ov_row : row_overlay_content) {
                    write_line_to(outfile, ov_row);
                    line_count += 1;
                    ++y;
                }
            }
            line_count += 1;
        }

        if (fos != nullptr) {
            fos->fos_contexts.pop();
            ensure(!fos->fos_contexts.empty());
        }
    }

    fflush(outfile);

    if (to_term) {
        if (ec.ec_ui_callbacks.uc_post_stdout_write) {
            ec.ec_ui_callbacks.uc_post_stdout_write();
        } else {
            log_debug("no post stdout write callback");
        }
    }
    if (ec.ec_dry_run) {
        rewind(outfile);

        char buffer[32 * 1024];
        size_t rc = fread(buffer, 1, sizeof(buffer), outfile);

        attr_line_t al(std::string(buffer, rc));

        lnav_data.ld_preview_view[0].set_sub_source(
            &lnav_data.ld_preview_source[0]);
        lnav_data.ld_preview_source[0]
            .replace_with(al)
            .set_text_format(detect_text_format(al.get_string()))
            .truncate_to(10);
        lnav_data.ld_preview_status_source[0].get_description().set_value(
            "First lines of file: %s", split_args[0].c_str());
    } else {
        retval = fmt::format(FMT_STRING("info: Wrote {:L} rows to {}"),
                             line_count,
                             split_args[0]);
    }
    if (toclose != nullptr) {
        closer(toclose);
    }
    outfile = nullptr;

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_open(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("path");
    std::string retval;

    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    }

    if (args.size() < 2) {
        return ec.make_error("expecting file name to open");
    }

    std::vector<std::string> word_exp;
    std::string pat;
    file_collection fc;

    pat = trim(remaining_args(cmdline, args));

    shlex lexer(pat);
    auto split_args_res = lexer.split(ec.create_resolver());
    if (split_args_res.isErr()) {
        auto split_err = split_args_res.unwrapErr();
        auto um
            = lnav::console::user_message::error("unable to parse file names")
                  .with_reason(split_err.te_msg)
                  .with_snippet(lnav::console::snippet::from(
                      SRC, lexer.to_attr_line(split_err)))
                  .move();

        return Err(um);
    }

    auto split_args = split_args_res.unwrap()
        | lnav::itertools::map([](const auto& elem) { return elem.se_value; });

    std::vector<std::pair<std::string, file_location_t>> files_to_front;
    std::vector<std::string> closed_files;
    logfile_open_options loo;

    auto prov = ec.get_provenance<exec_context::file_open>();
    if (prov) {
        loo.with_filename(prov->fo_name);
    }

    for (auto fn : split_args) {
        file_location_t file_loc;

        if (access(fn.c_str(), R_OK) != 0) {
            auto colon_index = fn.rfind(':');
            auto hash_index = fn.rfind('#');
            if (colon_index != std::string::npos) {
                auto top_range = std::string_view{&fn[colon_index + 1],
                                                  fn.size() - colon_index - 1};
                auto scan_res = scn::scan_value<int>(top_range);

                if (scan_res) {
                    fn = fn.substr(0, colon_index);
                    file_loc = vis_line_t(scan_res->value());
                }
            } else if (hash_index != std::string::npos) {
                file_loc = fn.substr(hash_index);
                fn = fn.substr(0, hash_index);
            }
            loo.with_init_location(file_loc);
        }

        auto file_iter = lnav_data.ld_active_files.fc_files.begin();
        for (; file_iter != lnav_data.ld_active_files.fc_files.end();
             ++file_iter)
        {
            auto lf = *file_iter;

            if (lf->get_filename() == fn) {
                if (lf->get_format() != nullptr) {
                    retval = "info: log file already loaded";
                    break;
                }

                files_to_front.emplace_back(fn, file_loc);
                retval = "";
                break;
            }
        }
        if (file_iter == lnav_data.ld_active_files.fc_files.end()) {
            auto_mem<char> abspath;
            struct stat st;
            size_t url_index;

#ifdef HAVE_LIBCURL
            if (startswith(fn, "file:")) {
                auto_mem<CURLU> cu(curl_url_cleanup);
                cu = curl_url();
                auto set_rc = curl_url_set(cu, CURLUPART_URL, fn.c_str(), 0);
                if (set_rc != CURLUE_OK) {
                    return Err(lnav::console::user_message::error(
                                   attr_line_t("invalid URL: ")
                                       .append(lnav::roles::file(fn)))
                                   .with_reason(curl_url_strerror(set_rc)));
                }

                auto_mem<char> path_part;
                auto get_rc
                    = curl_url_get(cu, CURLUPART_PATH, path_part.out(), 0);
                if (get_rc != CURLUE_OK) {
                    return Err(lnav::console::user_message::error(
                                   attr_line_t("cannot get path from URL: ")
                                       .append(lnav::roles::file(fn)))
                                   .with_reason(curl_url_strerror(get_rc)));
                }
                auto_mem<char> frag_part;
                get_rc
                    = curl_url_get(cu, CURLUPART_FRAGMENT, frag_part.out(), 0);
                if (get_rc != CURLUE_OK && get_rc != CURLUE_NO_FRAGMENT) {
                    return Err(lnav::console::user_message::error(
                                   attr_line_t("cannot get fragment from URL: ")
                                       .append(lnav::roles::file(fn)))
                                   .with_reason(curl_url_strerror(get_rc)));
                }

                if (frag_part != nullptr && frag_part[0]) {
                    fn = fmt::format(
                        FMT_STRING("{}#{}"), path_part.in(), frag_part.in());
                } else {
                    fn = path_part;
                }
            }
#endif

            if (is_url(fn.c_str())) {
#ifndef HAVE_LIBCURL
                retval = "error: lnav was not compiled with libcurl";
#else
                if (!ec.ec_dry_run) {
                    auto ul = std::make_shared<url_loader>(fn);

                    lnav_data.ld_active_files.fc_file_names[ul->get_path()]
                        .with_filename(fn)
                        .with_init_location(file_loc);
                    lnav_data.ld_active_files.fc_files_generation += 1;
                    isc::to<curl_looper&, services::curl_streamer_t>().send(
                        [ul](auto& clooper) { clooper.add_request(ul); });
                    lnav_data.ld_files_to_front.emplace_back(fn, file_loc);
                    closed_files.push_back(fn);
                    retval = "info: opened URL";
                } else {
                    retval = "";
                }
#endif
            } else if ((url_index = fn.find("://")) != std::string::npos) {
                const auto& cfg
                    = injector::get<const lnav::url_handler::config&>();
                const auto HOST_REGEX
                    = lnav::pcre2pp::code::from_const("://(?:\\?|$)");

                auto find_res = HOST_REGEX.find_in(fn).ignore_error();
                if (find_res) {
                    fn.insert(url_index + 3, "localhost");
                }

                auto_mem<CURLU> cu(curl_url_cleanup);
                cu = curl_url();
                auto set_rc = curl_url_set(
                    cu, CURLUPART_URL, fn.c_str(), CURLU_NON_SUPPORT_SCHEME);
                if (set_rc != CURLUE_OK) {
                    return Err(lnav::console::user_message::error(
                                   attr_line_t("invalid URL: ")
                                       .append(lnav::roles::file(fn)))
                                   .with_reason(curl_url_strerror(set_rc)));
                }

                auto_mem<char> scheme_part(curl_free);
                auto get_rc
                    = curl_url_get(cu, CURLUPART_SCHEME, scheme_part.out(), 0);
                if (get_rc != CURLUE_OK) {
                    return Err(lnav::console::user_message::error(
                                   attr_line_t("cannot get scheme from URL: ")
                                       .append(lnav::roles::file(fn)))
                                   .with_reason(curl_url_strerror(set_rc)));
                }

                auto proto_iter = cfg.c_schemes.find(scheme_part.in());
                if (proto_iter == cfg.c_schemes.end()) {
                    return Err(
                        lnav::console::user_message::error(
                            attr_line_t("no defined handler for URL scheme: ")
                                .append(lnav::roles::file(scheme_part.in())))
                            .with_reason(curl_url_strerror(set_rc)));
                }

                auto path_and_args
                    = fmt::format(FMT_STRING("{} {}"),
                                  proto_iter->second.p_handler.pp_value,
                                  fn);

                exec_context::provenance_guard pg(&ec,
                                                  exec_context::file_open{fn});

                auto cb_guard = ec.push_callback(internal_sql_callback);

                auto exec_res = execute_file(ec, path_and_args);
                if (exec_res.isErr()) {
                    return exec_res;
                }

                retval = "info: watching -- " + fn;
            } else if (lnav::filesystem::is_glob(fn.c_str())) {
                fc.fc_file_names.emplace(fn, loo);
                files_to_front.emplace_back(
                    loo.loo_filename.empty() ? fn : loo.loo_filename, file_loc);
                retval = "info: watching -- " + fn;
            } else if (stat(fn.c_str(), &st) == -1) {
                if (fn.find(':') != std::string::npos) {
                    fc.fc_file_names.emplace(fn, loo);
                    retval = "info: watching -- " + fn;
                } else {
                    auto um = lnav::console::user_message::error(
                                  attr_line_t("cannot open file: ")
                                      .append(lnav::roles::file(fn)))
                                  .with_errno_reason()
                                  .with_snippets(ec.ec_source)
                                  .with_help(
                                      "make sure the file exists and is "
                                      "accessible")
                                  .move();
                    return Err(um);
                }
            } else if (is_dev_null(st)) {
                return ec.make_error("cannot open /dev/null");
            } else if (S_ISFIFO(st.st_mode)) {
                auto_fd fifo_fd;

                if ((fifo_fd = open(fn.c_str(), O_RDONLY)) == -1) {
                    auto um = lnav::console::user_message::error(
                                  attr_line_t("cannot open FIFO: ")
                                      .append(lnav::roles::file(fn)))
                                  .with_errno_reason()
                                  .with_snippets(ec.ec_source)
                                  .move();
                    return Err(um);
                } else if (ec.ec_dry_run) {
                    retval = "";
                } else {
                    auto desc = fmt::format(FMT_STRING("FIFO [{}]"),
                                            lnav_data.ld_fifo_counter++);
                    if (prov) {
                        desc = prov->fo_name;
                    }
                    auto create_piper_res = lnav::piper::create_looper(
                        desc, std::move(fifo_fd), auto_fd{});
                    if (create_piper_res.isErr()) {
                        auto um = lnav::console::user_message::error(
                                      attr_line_t("cannot create piper: ")
                                          .append(lnav::roles::file(fn)))
                                      .with_reason(create_piper_res.unwrapErr())
                                      .with_snippets(ec.ec_source)
                                      .move();
                        return Err(um);
                    }
                    lnav_data.ld_active_files.fc_file_names[desc].with_piper(
                        create_piper_res.unwrap());
                }
            } else if ((abspath = realpath(fn.c_str(), nullptr)) == nullptr) {
                auto um = lnav::console::user_message::error(
                              attr_line_t("cannot open file: ")
                                  .append(lnav::roles::file(fn)))
                              .with_errno_reason()
                              .with_snippets(ec.ec_source)
                              .with_help(
                                  "make sure the file exists and is "
                                  "accessible")
                              .move();
                return Err(um);
            } else if (S_ISDIR(st.st_mode)) {
                std::string dir_wild(abspath.in());

                if (dir_wild[dir_wild.size() - 1] == '/') {
                    dir_wild.resize(dir_wild.size() - 1);
                }
                fc.fc_file_names.emplace(dir_wild + "/*", loo);
                retval = "info: watching -- " + dir_wild;
            } else if (!S_ISREG(st.st_mode)) {
                auto um = lnav::console::user_message::error(
                              attr_line_t("cannot open file: ")
                                  .append(lnav::roles::file(fn)))
                              .with_reason("not a regular file or directory")
                              .with_snippets(ec.ec_source)
                              .with_help(
                                  "only regular files, directories, and FIFOs "
                                  "can be opened")
                              .move();
                return Err(um);
            } else if (access(fn.c_str(), R_OK) == -1) {
                auto um = lnav::console::user_message::error(
                              attr_line_t("cannot read file: ")
                                  .append(lnav::roles::file(fn)))
                              .with_errno_reason()
                              .with_snippets(ec.ec_source)
                              .with_help(
                                  "make sure the file exists and is "
                                  "accessible")
                              .move();
                return Err(um);
            } else {
                fn = abspath.in();
                fc.fc_file_names.emplace(fn, loo);
                retval = "info: opened -- " + fn;
                files_to_front.emplace_back(fn, file_loc);

                closed_files.push_back(fn);
                if (!loo.loo_filename.empty()) {
                    closed_files.push_back(loo.loo_filename);
                }
#if 0
                if (lnav_data.ld_rl_view != nullptr) {
                    lnav_data.ld_rl_view->set_alt_value(
                        HELP_MSG_1(X, "to close the file"));
                }
#endif
            }
        }
    }

    if (ec.ec_dry_run) {
        lnav_data.ld_preview_view[0].set_sub_source(
            &lnav_data.ld_preview_source[0]);
        lnav_data.ld_preview_source[0].clear();
        if (!fc.fc_file_names.empty()) {
            auto iter = fc.fc_file_names.begin();
            std::string fn_str = iter->first;

            if (fn_str.find(':') != std::string::npos) {
                auto id = lnav_data.ld_preview_generation;
                lnav_data.ld_preview_status_source[0]
                    .get_description()
                    .set_cylon(true)
                    .set_value("Loading %s...", fn_str.c_str());
                lnav_data.ld_preview_view[0].set_sub_source(
                    &lnav_data.ld_preview_source[0]);
                lnav_data.ld_preview_source[0].clear();

                isc::to<tailer::looper&, services::remote_tailer_t>().send(
                    [id, fn_str](auto& tlooper) {
                        auto rp_opt = humanize::network::path::from_str(fn_str);
                        if (rp_opt) {
                            tlooper.load_preview(id, *rp_opt);
                        }
                    });
                lnav_data.ld_preview_view[0].set_needs_update();
            } else if (lnav::filesystem::is_glob(fn_str)) {
                static_root_mem<glob_t, globfree> gl;

                if (glob(fn_str.c_str(), GLOB_NOCHECK, nullptr, gl.inout())
                    == 0)
                {
                    attr_line_t al;

                    for (size_t lpc = 0; lpc < gl->gl_pathc && lpc < 10; lpc++)
                    {
                        al.append(gl->gl_pathv[lpc]).append("\n");
                    }
                    if (gl->gl_pathc > 10) {
                        al.append(" ... ")
                            .append(lnav::roles::number(
                                std::to_string(gl->gl_pathc - 10)))
                            .append(" files not shown ...");
                    }
                    lnav_data.ld_preview_status_source[0]
                        .get_description()
                        .set_value("The following files will be loaded:");
                    lnav_data.ld_preview_view[0].set_sub_source(
                        &lnav_data.ld_preview_source[0]);
                    lnav_data.ld_preview_source[0].replace_with(al);
                } else {
                    return ec.make_error("failed to evaluate glob -- {}",
                                         fn_str);
                }
            } else {
                auto fn = std::filesystem::path(fn_str);
                auto detect_res = detect_file_format(fn);
                attr_line_t al;
                attr_line_builder alb(al);

                switch (detect_res.dffr_file_format) {
                    case file_format_t::ARCHIVE: {
                        auto describe_res = archive_manager::describe(fn);

                        if (describe_res.isOk()) {
                            auto arc_res = describe_res.unwrap();

                            if (arc_res.is<archive_manager::archive_info>()) {
                                auto ai
                                    = arc_res
                                          .get<archive_manager::archive_info>();
                                auto lines_remaining = size_t{9};

                                al.append("Archive: ")
                                    .append(
                                        lnav::roles::symbol(ai.ai_format_name))
                                    .append("\n");
                                for (const auto& entry : ai.ai_entries) {
                                    if (lines_remaining == 0) {
                                        break;
                                    }
                                    lines_remaining -= 1;

                                    char timebuf[64];
                                    sql_strftime(timebuf,
                                                 sizeof(timebuf),
                                                 entry.e_mtime,
                                                 0,
                                                 'T');
                                    al.append("    ")
                                        .append(entry.e_mode)
                                        .append(" ")
                                        .appendf(
                                            FMT_STRING("{:>8}"),
                                            humanize::file_size(
                                                entry.e_size.value(),
                                                humanize::alignment::columnar))
                                        .append(" ")
                                        .append(timebuf)
                                        .append(" ")
                                        .append(lnav::roles::file(entry.e_name))
                                        .append("\n");
                                }
                            }
                        } else {
                            al.append(describe_res.unwrapErr());
                        }
                        break;
                    }
                    case file_format_t::MULTIPLEXED:
                    case file_format_t::UNKNOWN: {
                        auto open_res
                            = lnav::filesystem::open_file(fn, O_RDONLY);

                        if (open_res.isErr()) {
                            return ec.make_error("unable to open -- {}", fn);
                        }
                        auto preview_fd = open_res.unwrap();
                        line_buffer lb;
                        file_range range;

                        lb.set_fd(preview_fd);
                        for (int lpc = 0; lpc < 10; lpc++) {
                            auto load_result = lb.load_next_line(range);

                            if (load_result.isErr()) {
                                break;
                            }

                            auto li = load_result.unwrap();

                            range = li.li_file_range;
                            if (!li.li_utf8_scan_result.is_valid()) {
                                range.fr_size = 16;
                            }
                            auto read_result = lb.read_range(range);
                            if (read_result.isErr()) {
                                break;
                            }

                            auto sbr = read_result.unwrap();
                            auto sf = sbr.to_string_fragment();
                            if (li.li_utf8_scan_result.is_valid()) {
                                alb.append(sf);
                            } else {
                                {
                                    auto ag = alb.with_attr(
                                        VC_ROLE.value(role_t::VCR_FILE_OFFSET));
                                    alb.appendf(FMT_STRING("{: >16x} "),
                                                range.fr_offset);
                                }
                                alb.append_as_hexdump(sf);
                                alb.append("\n");
                            }
                        }
                        break;
                    }
                    case file_format_t::SQLITE_DB: {
                        alb.append(fmt::to_string(detect_res.dffr_file_format));
                        break;
                    }
                    case file_format_t::REMOTE: {
                        break;
                    }
                }

                lnav_data.ld_preview_view[0].set_sub_source(
                    &lnav_data.ld_preview_source[0]);
                lnav_data.ld_preview_source[0].replace_with(al).set_text_format(
                    detect_text_format(al.get_string()));
                lnav_data.ld_preview_status_source[0]
                    .get_description()
                    .set_value("For file: %s", fn.c_str());
            }
        }
    } else {
        lnav_data.ld_files_to_front.insert(lnav_data.ld_files_to_front.end(),
                                           files_to_front.begin(),
                                           files_to_front.end());
        for (const auto& fn : closed_files) {
            lnav_data.ld_active_files.fc_closed_files.erase(fn);
        }

        lnav_data.ld_active_files.merge(fc);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_xopen(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("path");
    std::string retval;

    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    }

    if (args.size() < 2) {
        return ec.make_error("expecting file name to open");
    }

    std::vector<std::string> word_exp;
    std::string pat;
    file_collection fc;

    pat = trim(remaining_args(cmdline, args));

    shlex lexer(pat);
    auto split_args_res = lexer.split(ec.create_resolver());
    if (split_args_res.isErr()) {
        auto split_err = split_args_res.unwrapErr();
        auto um
            = lnav::console::user_message::error("unable to parse file names")
                  .with_reason(split_err.te_msg)
                  .with_snippet(lnav::console::snippet::from(
                      SRC, lexer.to_attr_line(split_err)))
                  .move();

        return Err(um);
    }

    auto split_args = split_args_res.unwrap()
        | lnav::itertools::map([](const auto& elem) { return elem.se_value; });
    for (auto fn : split_args) {
        auto open_res = lnav::external_opener::for_href(fn);
        if (open_res.isErr()) {
            auto um = lnav::console::user_message::error(
                          attr_line_t("Unable to open file: ")
                              .append(lnav::roles::file(fn)))
                          .with_reason(open_res.unwrapErr())
                          .move();
            return Err(um);
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_close(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("path");
    std::string retval;

    auto* tc = *lnav_data.ld_view_stack.top();
    std::vector<std::optional<std::filesystem::path>> actual_path_v;
    std::vector<std::string> fn_v;

    if (args.size() > 1) {
        auto lexer = shlex(cmdline);

        auto split_args_res = lexer.split(ec.create_resolver());
        if (split_args_res.isErr()) {
            auto split_err = split_args_res.unwrapErr();
            auto um = lnav::console::user_message::error(
                          "unable to parse file name")
                          .with_reason(split_err.te_msg)
                          .with_snippet(lnav::console::snippet::from(
                              SRC, lexer.to_attr_line(split_err)))
                          .move();

            return Err(um);
        }

        auto args = split_args_res.unwrap()
            | lnav::itertools::map(
                        [](const auto& elem) { return elem.se_value; });
        args.erase(args.begin());

        for (const auto& lf : lnav_data.ld_active_files.fc_files) {
            if (lf.get() == nullptr) {
                continue;
            }

            auto find_iter
                = find_if(args.begin(), args.end(), [&lf](const auto& arg) {
                      return fnmatch(arg.c_str(), lf->get_filename().c_str(), 0)
                          == 0;
                  });

            if (find_iter == args.end()) {
                continue;
            }

            actual_path_v.push_back(lf->get_actual_path());
            fn_v.emplace_back(lf->get_filename());
            if (!ec.ec_dry_run) {
                lnav_data.ld_active_files.request_close(lf);
            }
        }
    } else if (tc == &lnav_data.ld_views[LNV_TEXT]) {
        auto& tss = lnav_data.ld_text_source;

        if (tss.empty()) {
            return ec.make_error("no text files are opened");
        } else if (!ec.ec_dry_run) {
            auto lf = tss.current_file();
            actual_path_v.emplace_back(lf->get_actual_path());
            fn_v.emplace_back(lf->get_filename());
            lnav_data.ld_active_files.request_close(lf);

            if (tss.size() == 1) {
                lnav_data.ld_view_stack.pop_back();
            }
        } else {
            retval = fmt::format(FMT_STRING("closing -- {}"),
                                 tss.current_file()->get_filename());
        }
    } else if (tc == &lnav_data.ld_views[LNV_LOG]) {
        if (tc->get_inner_height() == 0) {
            return ec.make_error("no log files loaded");
        } else {
            auto& lss = lnav_data.ld_log_source;
            auto vl = tc->get_selection();
            auto cl = lss.at(vl);
            auto lf = lss.find(cl);

            actual_path_v.push_back(lf->get_actual_path());
            fn_v.emplace_back(lf->get_filename());
            if (!ec.ec_dry_run) {
                lnav_data.ld_active_files.request_close(lf);
            }
        }
    } else {
        return ec.make_error("close must be run in the log or text file views");
    }
    if (!fn_v.empty()) {
        if (ec.ec_dry_run) {
            retval = "";
        } else {
            for (size_t lpc = 0; lpc < actual_path_v.size(); lpc++) {
                const auto& fn = fn_v[lpc];
                const auto& actual_path = actual_path_v[lpc];

                if (is_url(fn.c_str())) {
                    isc::to<curl_looper&, services::curl_streamer_t>().send(
                        [fn](auto& clooper) { clooper.close_request(fn); });
                }
                if (actual_path) {
                    lnav_data.ld_active_files.fc_file_names.erase(
                        actual_path.value().string());
                }
                lnav_data.ld_active_files.fc_closed_files.insert(fn);
            }
            retval = fmt::format(FMT_STRING("info: closed -- {}"),
                                 fmt::join(fn_v, ", "));
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_pipe_to(exec_context& ec,
            std::string cmdline,
            std::vector<std::string>& args)
{
    std::string retval;

    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    }

    if (args.size() < 2) {
        return ec.make_error("expecting command to execute");
    }

    if (ec.ec_dry_run) {
        return Ok(std::string());
    }

    auto* tc = *lnav_data.ld_view_stack.top();
    auto bv = combined_user_marks(tc->get_bookmarks());
    bool pipe_line_to = (args[0] == "pipe-line-to");
    auto path_v = ec.ec_path_stack;
    std::map<std::string, std::string> extra_env;

    if (pipe_line_to && tc == &lnav_data.ld_views[LNV_LOG]) {
        log_data_helper ldh(lnav_data.ld_log_source);
        char tmp_str[64];

        ldh.parse_line(ec.ec_top_line, true);
        auto format = ldh.ldh_file->get_format();
        auto source_path = format->get_source_path();
        path_v.insert(path_v.end(), source_path.begin(), source_path.end());

        extra_env["log_line"] = fmt::to_string((int) ec.ec_top_line);
        sql_strftime(tmp_str, sizeof(tmp_str), ldh.ldh_line->get_timeval());
        extra_env["log_time"] = tmp_str;
        extra_env["log_path"] = ldh.ldh_file->get_filename();
        extra_env["log_level"] = ldh.ldh_line->get_level_name();
        if (ldh.ldh_line_values.lvv_opid_value) {
            extra_env["log_opid"] = ldh.ldh_line_values.lvv_opid_value.value();
        }
        auto read_res = ldh.ldh_file->read_raw_message(ldh.ldh_line);
        if (read_res.isOk()) {
            auto raw_text = to_string(read_res.unwrap());
            extra_env["log_raw_text"] = raw_text;
        }
        for (auto& ldh_line_value : ldh.ldh_line_values.lvv_values) {
            extra_env[ldh_line_value.lv_meta.lvm_name.to_string()]
                = ldh_line_value.to_string();
        }
        auto iter = ldh.ldh_parser->dp_pairs.begin();
        for (size_t lpc = 0; lpc < ldh.ldh_parser->dp_pairs.size();
             lpc++, ++iter)
        {
            std::string colname = ldh.ldh_parser->get_element_string(
                iter->e_sub_elements->front());
            colname = ldh.ldh_namer->add_column(colname).to_string();
            std::string val = ldh.ldh_parser->get_element_string(
                iter->e_sub_elements->back());
            extra_env[colname] = val;
        }
    }

    std::string cmd = trim(remaining_args(cmdline, args));
    auto for_child_res = auto_pipe::for_child_fds(STDIN_FILENO, STDOUT_FILENO);

    if (for_child_res.isErr()) {
        return ec.make_error(FMT_STRING("unable to open pipe to child: {}"),
                             for_child_res.unwrapErr());
    }

    auto child_fds = for_child_res.unwrap();

    pid_t child_pid = fork();

    for (auto& child_fd : child_fds) {
        child_fd.after_fork(child_pid);
    }

    switch (child_pid) {
        case -1:
            return ec.make_error("unable to fork child process -- {}",
                                 strerror(errno));

        case 0: {
            const char* exec_args[] = {
                "sh",
                "-c",
                cmd.c_str(),
                nullptr,
            };
            std::string path;

            dup2(STDOUT_FILENO, STDERR_FILENO);
            path_v.emplace_back(lnav::paths::dotlnav() / "formats/default");

            setenv("PATH", lnav::filesystem::build_path(path_v).c_str(), 1);
            for (const auto& pair : extra_env) {
                setenv(pair.first.c_str(), pair.second.c_str(), 1);
            }
            execvp(exec_args[0], (char* const*) exec_args);
            _exit(1);
            break;
        }

        default: {
            bookmark_vector<vis_line_t>::iterator iter;
            std::string line;

            log_info("spawned pipe child %d -- %s", child_pid, cmd.c_str());
            lnav_data.ld_children.push_back(child_pid);

            std::future<std::string> reader;

            if (child_fds[1].read_end() != -1) {
                reader
                    = ec.ec_pipe_callback(ec, cmdline, child_fds[1].read_end());
            }

            if (pipe_line_to) {
                if (tc->get_inner_height() == 0) {
                    // Nothing to do
                } else if (tc == &lnav_data.ld_views[LNV_LOG]) {
                    logfile_sub_source& lss = lnav_data.ld_log_source;
                    content_line_t cl = lss.at(tc->get_top());
                    std::shared_ptr<logfile> lf = lss.find(cl);
                    shared_buffer_ref sbr;
                    lf->read_full_message(lf->message_start(lf->begin() + cl),
                                          sbr);
                    if (write(child_fds[0].write_end(),
                              sbr.get_data(),
                              sbr.length())
                        == -1)
                    {
                        return ec.make_error("Unable to write to pipe -- {}",
                                             strerror(errno));
                    }
                    log_perror(write(child_fds[0].write_end(), "\n", 1));
                } else {
                    tc->grep_value_for_line(tc->get_top(), line);
                    if (write(
                            child_fds[0].write_end(), line.c_str(), line.size())
                        == -1)
                    {
                        return ec.make_error("Unable to write to pipe -- {}",
                                             strerror(errno));
                    }
                    log_perror(write(child_fds[0].write_end(), "\n", 1));
                }
            } else {
                for (iter = bv.begin(); iter != bv.end(); ++iter) {
                    tc->grep_value_for_line(*iter, line);
                    if (write(
                            child_fds[0].write_end(), line.c_str(), line.size())
                        == -1)
                    {
                        return ec.make_error("Unable to write to pipe -- {}",
                                             strerror(errno));
                    }
                    log_perror(write(child_fds[0].write_end(), "\n", 1));
                }
            }

            child_fds[0].write_end().reset();

            if (reader.valid()) {
                retval = reader.get();
            } else {
                retval = "";
            }
            break;
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_redirect_to(exec_context& ec,
                std::string cmdline,
                std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("path");

    if (args.size() == 1) {
        if (ec.ec_dry_run) {
            return Ok(std::string("info: redirect will be cleared"));
        }

        ec.clear_output();
        return Ok(std::string("info: cleared redirect"));
    }

    std::string fn = trim(remaining_args(cmdline, args));
    shlex lexer(fn);

    auto split_args_res = lexer.split(ec.create_resolver());
    if (split_args_res.isErr()) {
        auto split_err = split_args_res.unwrapErr();
        auto um
            = lnav::console::user_message::error("unable to parse file name")
                  .with_reason(split_err.te_msg)
                  .with_snippet(lnav::console::snippet::from(
                      SRC, lexer.to_attr_line(split_err)))
                  .move();

        return Err(um);
    }
    auto split_args = split_args_res.unwrap()
        | lnav::itertools::map([](const auto& elem) { return elem.se_value; });
    if (split_args.size() > 1) {
        return ec.make_error("more than one file name was matched");
    }

    if (ec.ec_dry_run) {
        return Ok("info: output will be redirected to -- " + split_args[0]);
    }

    if (split_args[0] == "-") {
        ec.clear_output();
    } else if (split_args[0] == "/dev/clipboard") {
        auto out = sysclip::open(sysclip::type_t::GENERAL);
        if (out.isErr()) {
            alerter::singleton().chime("cannot open clipboard");
            return ec.make_error("Unable to copy to clipboard: {}",
                                 out.unwrapErr());
        }

        auto holder = out.unwrap();
        ec.set_output(split_args[0],
                      holder.release(),
                      holder.get_free_func<int (*)(FILE*)>());
    } else if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    } else {
        FILE* file = fopen(split_args[0].c_str(), "w");
        if (file == nullptr) {
            return ec.make_error("unable to open file -- {}", split_args[0]);
        }

        ec.set_output(split_args[0], file, fclose);
    }

    return Ok("info: redirecting output to file -- " + split_args[0]);
}

static readline_context::command_t IO_COMMANDS[] = {
    {
        "append-to",
        com_save_to,

        help_text(":append-to")
            .with_summary("Append marked lines in the current view to "
                          "the given file")
            .with_parameter(
                help_text("path", "The path to the file to append to")
                    .with_format(help_parameter_format_t::HPF_FILENAME))
            .with_tags({"io"})
            .with_example({"To append marked lines to the file "
                           "/tmp/interesting-lines.txt",
                           "/tmp/interesting-lines.txt"}),
    },
    {
        "write-to",
        com_save_to,

        help_text(":write-to")
            .with_summary("Overwrite the given file with any marked "
                          "lines in the "
                          "current view")
            .with_parameter(
                help_text("--anonymize", "Anonymize the lines").optional())
            .with_parameter(
                help_text("path", "The path to the file to write")
                    .with_format(help_parameter_format_t::HPF_FILENAME))
            .with_tags({"io", "scripting"})
            .with_example({"To write marked lines to the file "
                           "/tmp/interesting-lines.txt",
                           "/tmp/interesting-lines.txt"}),
    },
    {
        "write-csv-to",
        com_save_to,

        help_text(":write-csv-to")
            .with_summary("Write SQL results to the given file in CSV format")
            .with_parameter(
                help_text("--anonymize", "Anonymize the row contents")
                    .optional())
            .with_parameter(
                help_text("path", "The path to the file to write")
                    .with_format(help_parameter_format_t::HPF_FILENAME))
            .with_tags({"io", "scripting", "sql"})
            .with_example({"To write SQL results as CSV to /tmp/table.csv",
                           "/tmp/table.csv"}),
    },
    {
        "write-json-to",
        com_save_to,

        help_text(":write-json-to")
            .with_summary("Write SQL results to the given file in JSON format")
            .with_parameter(
                help_text("--anonymize", "Anonymize the JSON values")
                    .optional())
            .with_parameter(
                help_text("path", "The path to the file to write")
                    .with_format(help_parameter_format_t::HPF_FILENAME))
            .with_tags({"io", "scripting", "sql"})
            .with_example({"To write SQL results as JSON to /tmp/table.json",
                           "/tmp/table.json"}),
    },
    {
        "write-jsonlines-to",
        com_save_to,

        help_text(":write-jsonlines-to")
            .with_summary("Write SQL results to the given file in "
                          "JSON Lines format")
            .with_parameter(
                help_text("--anonymize", "Anonymize the JSON values")
                    .optional())
            .with_parameter(
                help_text("path", "The path to the file to write")
                    .with_format(help_parameter_format_t::HPF_FILENAME))
            .with_tags({"io", "scripting", "sql"})
            .with_example({"To write SQL results as JSON Lines to "
                           "/tmp/table.json",
                           "/tmp/table.json"}),
    },
    {
        "write-table-to",
        com_save_to,

        help_text(":write-table-to")
            .with_summary("Write SQL results to the given file in a "
                          "tabular format")
            .with_parameter(
                help_text("--anonymize", "Anonymize the table contents")
                    .optional())
            .with_parameter(
                help_text("path", "The path to the file to write")
                    .with_format(help_parameter_format_t::HPF_FILENAME))
            .with_tags({"io", "scripting", "sql"})
            .with_example({"To write SQL results as text to /tmp/table.txt",
                           "/tmp/table.txt"}),
    },
    {
        "write-raw-to",
        com_save_to,

        help_text(":write-raw-to")
            .with_summary(
                "In the log view, write the original log file content "
                "of the marked messages to the file.  In the DB view, "
                "the contents of the cells are written to the output "
                "file.")
            .with_parameter(help_text("--view={log,db}",
                                      "The view to use as the source of data")
                                .optional())
            .with_parameter(
                help_text("--anonymize", "Anonymize the lines").optional())
            .with_parameter(
                help_text("path", "The path to the file to write")
                    .with_format(help_parameter_format_t::HPF_FILENAME))
            .with_tags({"io", "scripting", "sql"})
            .with_example({"To write the marked lines in the log view "
                           "to /tmp/table.txt",
                           "/tmp/table.txt"}),
    },
    {
        "write-view-to",
        com_save_to,

        help_text(":write-view-to")
            .with_summary("Write the text in the top view to the given file "
                          "without any formatting")
            .with_parameter(
                help_text("--anonymize", "Anonymize the lines").optional())
            .with_parameter(
                help_text("path", "The path to the file to write")
                    .with_format(help_parameter_format_t::HPF_FILENAME))
            .with_tags({"io", "scripting", "sql"})
            .with_example(
                {"To write the top view to /tmp/table.txt", "/tmp/table.txt"}),
    },
    {
        "write-screen-to",
        com_save_to,

        help_text(":write-screen-to")
            .with_summary(
                "Write the displayed text or SQL results to the given "
                "file without any formatting")
            .with_parameter(
                help_text("--anonymize", "Anonymize the lines").optional())
            .with_parameter(
                help_text("path", "The path to the file to write")
                    .with_format(help_parameter_format_t::HPF_FILENAME))
            .with_tags({"io", "scripting", "sql"})
            .with_example({"To write only the displayed text to /tmp/table.txt",
                           "/tmp/table.txt"}),
    },
    {
        "open",
        com_open,

        help_text(":open")
            .with_summary("Open the given file(s) in lnav.  Opening files on "
                          "machines "
                          "accessible via SSH can be done using the syntax: "
                          "[user@]host:/path/to/logs")
            .with_parameter(
                help_text{"path", "The path to the file to open"}
                    .with_format(help_parameter_format_t::HPF_FILENAME)
                    .one_or_more())
            .with_example({"To open the file '/path/to/file'", "/path/to/file"})
            .with_example({"To open the remote file '/var/log/syslog.log'",
                           "dean@host1.example.com:/var/log/syslog.log"})
            .with_tags({"io"}),
    },
    {
        "xopen",
        com_xopen,

        help_text(":xopen")
            .with_summary("Use an external command to open the given file(s)")
            .with_parameter(
                help_text{"path", "The path to the file to open"}.one_or_more())
            .with_example({"To open the file '/path/to/file'", "/path/to/file"})
            .with_tags({"io"}),
    },
    {
        "close",
        com_close,

        help_text(":close")
            .with_summary(
                "Close the given file(s) or the focused file in the view")
            .with_parameter(
                help_text{"path",
                          "A path or glob pattern that "
                          "specifies the files to close"}
                    .zero_or_more()
                    .with_format(help_parameter_format_t::HPF_LOADED_FILE))
            .with_opposites({"open"})
            .with_tags({"io"}),
    },
    {
        "pipe-to",
        com_pipe_to,

        help_text(":pipe-to")
            .with_summary("Pipe the marked lines to the given shell command")
            .with_parameter(
                help_text("shell-cmd", "The shell command-line to execute"))
            .with_tags({"io"})
            .with_example({"To write marked lines to 'sed' for processing",
                           "sed -e s/foo/bar/g"}),
    },
    {
        "pipe-line-to",
        com_pipe_to,

        help_text(":pipe-line-to")
            .with_summary("Pipe the focused line to the given shell "
                          "command.  Any fields "
                          "defined by the format will be set as "
                          "environment variables.")
            .with_parameter(
                help_text("shell-cmd", "The shell command-line to execute"))
            .with_tags({"io"})
            .with_example({"To write the focused line to 'sed' for processing",
                           "sed -e 's/foo/bar/g'"}),
    },
    {
        "redirect-to",
        com_redirect_to,

        help_text(":redirect-to")
            .with_summary("Redirect the output of commands that write to "
                          "stdout to the given file")
            .with_parameter(
                help_text("path",
                          "The path to the file to write."
                          "  If not specified, the current redirect "
                          "will be cleared")
                    .optional()
                    .with_format(help_parameter_format_t::HPF_FILENAME))
            .with_tags({"io", "scripting"})
            .with_example({"To write the output of lnav commands to the file "
                           "/tmp/script-output.txt",
                           "/tmp/script-output.txt"}),
    },
};

void
init_lnav_io_commands(readline_context::command_map_t& cmd_map)
{
    for (auto& cmd : IO_COMMANDS) {
        cmd.c_help.index_tags();
        cmd_map[cmd.c_name] = &cmd;
    }
}
