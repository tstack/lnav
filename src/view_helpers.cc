/**
 * Copyright (c) 2018, Timothy Stack
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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "lnav.hh"
#include "sql_util.hh"
#include "pretty_printer.hh"
#include "environ_vtab.hh"
#include "vtab_module.hh"
#include "shlex.hh"
#include "help-txt.h"

using namespace std;

static void open_schema_view()
{
    textview_curses *schema_tc = &lnav_data.ld_views[LNV_SCHEMA];
    string schema;

    dump_sqlite_schema(lnav_data.ld_db, schema);

    schema += "\n\n-- Virtual Table Definitions --\n\n";
    schema += ENVIRON_CREATE_STMT;
    schema += vtab_module_schemas;
    for (const auto &vtab_iter : *lnav_data.ld_vtab_manager) {
        schema += "\n" + vtab_iter.second->get_table_statement();
    }

    delete schema_tc->get_sub_source();

    auto *pts = new plain_text_source(schema);
    pts->set_text_format(text_format_t::TF_SQL);

    schema_tc->set_sub_source(pts);
}

static void open_pretty_view(void)
{
    static const char *NOTHING_MSG =
        "Nothing to pretty-print";

    textview_curses *top_tc = *lnav_data.ld_view_stack.top();
    textview_curses *pretty_tc = &lnav_data.ld_views[LNV_PRETTY];
    textview_curses *log_tc = &lnav_data.ld_views[LNV_LOG];
    textview_curses *text_tc = &lnav_data.ld_views[LNV_TEXT];
    attr_line_t full_text;

    delete pretty_tc->get_sub_source();
    pretty_tc->set_sub_source(nullptr);
    if (top_tc->get_inner_height() == 0) {
        pretty_tc->set_sub_source(new plain_text_source(NOTHING_MSG));
        return;
    }

    if (top_tc == log_tc) {
        logfile_sub_source &lss = lnav_data.ld_log_source;
        bool first_line = true;

        for (vis_line_t vl = log_tc->get_top(); vl <= log_tc->get_bottom(); ++vl) {
            content_line_t cl = lss.at(vl);
            shared_ptr<logfile> lf = lss.find(cl);
            auto ll = lf->begin() + cl;
            shared_buffer_ref sbr;

            if (!first_line && ll->is_continued()) {
                continue;
            }
            auto ll_start = lf->message_start(ll);
            attr_line_t al;

            vl -= vis_line_t(distance(ll_start, ll));
            lss.text_value_for_line(*log_tc, vl, al.get_string(),
                                    text_sub_source::RF_FULL|
                                    text_sub_source::RF_REWRITE);
            lss.text_attrs_for_line(*log_tc, vl, al.get_attrs());

            line_range orig_lr = find_string_attr_range(
                al.get_attrs(), &textview_curses::SA_ORIGINAL_LINE);
            attr_line_t orig_al = al.subline(orig_lr.lr_start, orig_lr.length());
            attr_line_t prefix_al = al.subline(0, orig_lr.lr_start);

            data_scanner ds(orig_al.get_string());
            pretty_printer pp(&ds, orig_al.get_attrs());
            attr_line_t pretty_al;
            vector<attr_line_t> pretty_lines;

            // TODO: dump more details of the line in the output.
            pp.append_to(pretty_al);
            pretty_al.split_lines(pretty_lines);

            for (auto &pretty_line : pretty_lines) {
                if (pretty_line.empty() && &pretty_line == &pretty_lines.back()) {
                    break;
                }
                pretty_line.insert(0, prefix_al);
                pretty_line.append("\n");
                full_text.append(pretty_line);
            }

            first_line = false;
        }

        if (!full_text.empty()) {
            full_text.erase(full_text.length() - 1, 1);
        }
    }
    else if (top_tc == text_tc) {
        shared_ptr<logfile> lf = lnav_data.ld_text_source.current_file();

        for (vis_line_t vl = text_tc->get_top(); vl <= text_tc->get_bottom(); ++vl) {
            auto ll = lf->begin() + vl;
            shared_buffer_ref sbr;

            lf->read_full_message(ll, sbr);
            data_scanner ds(sbr);
            string_attrs_t sa;
            pretty_printer pp(&ds, sa);

            pp.append_to(full_text);
        }
    }
    auto *pts = new plain_text_source();
    pts->replace_with(full_text);
    pretty_tc->set_sub_source(pts);
    if (lnav_data.ld_last_pretty_print_top != log_tc->get_top()) {
        pretty_tc->set_top(vis_line_t(0));
    }
    lnav_data.ld_last_pretty_print_top = log_tc->get_top();
    pretty_tc->redo_search();
}

static void build_all_help_text()
{
    if (!lnav_data.ld_help_source.empty()) {
        return;
    }

    attr_line_t all_help_text;
    shlex lexer((const char *) help_txt.bsf_data, help_txt.bsf_size);
    string sub_help_text;

    lexer.with_ignore_quotes(true)
        .eval(sub_help_text, lnav_data.ld_exec_context.ec_global_vars);
    all_help_text.with_ansi_string(sub_help_text);

    map<string, help_text *> sql_funcs;
    map<string, help_text *> sql_keywords;

    for (const auto &iter : sqlite_function_help) {
        switch (iter.second->ht_context) {
            case help_context_t::HC_SQL_FUNCTION:
            case help_context_t::HC_SQL_TABLE_VALUED_FUNCTION:
                sql_funcs[iter.second->ht_name] = iter.second;
                break;
            case help_context_t::HC_SQL_KEYWORD:
                sql_keywords[iter.second->ht_name] = iter.second;
                break;
            default:
                break;
        }
    }

    for (const auto &iter : sql_funcs) {
        all_help_text.append(2, '\n');
        format_help_text_for_term(*iter.second, 79, all_help_text);
        if (!iter.second->ht_example.empty()) {
            all_help_text.append(1, '\n');
            format_example_text_for_term(*iter.second, eval_example, 90, all_help_text);
        }
    }

    for (const auto &iter : sql_keywords) {
        all_help_text.append(2, '\n');
        format_help_text_for_term(*iter.second, 79, all_help_text);
        if (!iter.second->ht_example.empty()) {
            all_help_text.append(1, '\n');
            format_example_text_for_term(*iter.second, eval_example, 79, all_help_text);
        }
    }

    lnav_data.ld_help_source.replace_with(all_help_text);
}

void layout_views()
{
    unsigned long width, height;

    getmaxyx(lnav_data.ld_window, height, width);
    int doc_height;
    bool doc_side_by_side = width > (90 + 60);
    bool preview_status_open = !lnav_data.ld_preview_status_source
                                         .get_description().empty();
    bool filter_status_open = false;

    lnav_data.ld_view_stack.top() | [&] (auto tc) {
        text_sub_source *tss = tc->get_sub_source();

        if (tss == nullptr) {
            return;
        }

        if (tss->tss_supports_filtering) {
            filter_status_open = true;
        }
    };

    if (doc_side_by_side) {
        doc_height = std::max(
            lnav_data.ld_doc_source.text_line_count(),
            lnav_data.ld_example_source.text_line_count());
    } else {
        doc_height =
            lnav_data.ld_doc_source.text_line_count() +
            lnav_data.ld_example_source.text_line_count();
    }

    int preview_height = lnav_data.ld_preview_hidden ? 0 :
                         lnav_data.ld_preview_source.text_line_count();
    int match_rows = lnav_data.ld_match_source.text_line_count();
    int match_height = min((unsigned long)match_rows, (height - 4) / 2);

    lnav_data.ld_match_view.set_height(vis_line_t(match_height));

    if (doc_height + 14 > ((int) height - match_height - preview_height - 2)) {
        doc_height = 0;
        preview_height = 0;
        preview_status_open = false;
    }

    bool doc_open = doc_height > 0;
    bool filters_open = lnav_data.ld_mode == LNM_FILTER &&
                        !preview_status_open &&
                        !doc_open;
    int filter_height = filters_open ? 3 : 0;

    int bottom_height =
        (doc_open ? 1 : 0)
        + doc_height
        + (preview_status_open ? 1 : 0)
        + preview_height
        + 1 // bottom status
        + match_height
        + lnav_data.ld_rl_view->get_height();

    for (auto &tc : lnav_data.ld_views) {
        tc.set_height(vis_line_t(-(bottom_height
                                   + (filter_status_open ? 1 : 0)
                                   + (filters_open ? 1 : 0)
                                   + filter_height)));
    }
    lnav_data.ld_status[LNS_TOP].set_enabled(!filters_open);
    lnav_data.ld_status[LNS_FILTER].set_visible(filter_status_open);
    lnav_data.ld_status[LNS_FILTER].set_enabled(filters_open);
    lnav_data.ld_status[LNS_FILTER].set_top(
        -(bottom_height + filter_height + 1 + (filters_open ? 1 : 0)));
    lnav_data.ld_status[LNS_FILTER_HELP].set_visible(filters_open);
    lnav_data.ld_status[LNS_FILTER_HELP].set_top(-(bottom_height + filter_height + 1));
    lnav_data.ld_status[LNS_BOTTOM].set_top(-(match_height + 2));
    lnav_data.ld_status[LNS_DOC].set_top(height - bottom_height);
    lnav_data.ld_status[LNS_DOC].set_visible(doc_open);
    lnav_data.ld_status[LNS_PREVIEW].set_top(height
                                             - bottom_height
                                             + (doc_open ? 1 : 0)
                                             + doc_height);
    lnav_data.ld_status[LNS_PREVIEW].set_visible(preview_status_open);

    if (!doc_open || doc_side_by_side) {
        lnav_data.ld_doc_view.set_height(vis_line_t(doc_height));
    } else {
        lnav_data.ld_doc_view.set_height(vis_line_t(lnav_data.ld_doc_source.text_line_count()));
    }
    lnav_data.ld_doc_view.set_y(height - bottom_height + 1);

    if (!doc_open || doc_side_by_side) {
        lnav_data.ld_example_view.set_height(vis_line_t(doc_height));
        lnav_data.ld_example_view.set_x(90);
        lnav_data.ld_example_view.set_y(height - bottom_height + 1);
    } else {
        lnav_data.ld_example_view.set_height(vis_line_t(lnav_data.ld_example_source.text_line_count()));
        lnav_data.ld_example_view.set_x(0);
        lnav_data.ld_example_view.set_y(height - bottom_height + lnav_data.ld_doc_view.get_height() + 1);
    }

    lnav_data.ld_filter_view.set_height(vis_line_t(filter_height));
    lnav_data.ld_filter_view.set_y(height - bottom_height - filter_height);
    lnav_data.ld_filter_view.set_width(width);

    lnav_data.ld_preview_view.set_height(vis_line_t(preview_height));
    lnav_data.ld_preview_view.set_y(height
                                    - bottom_height
                                    + 1
                                    + (doc_open ? 1 : 0)
                                    + doc_height);
    lnav_data.ld_match_view.set_y(
        height
        - lnav_data.ld_rl_view->get_height()
        - match_height);
    lnav_data.ld_rl_view->set_width(width);
}

static unordered_map<string, attr_line_t> EXAMPLE_RESULTS;

void execute_examples()
{
    exec_context &ec = lnav_data.ld_exec_context;
    db_label_source &dls = lnav_data.ld_db_row_source;
    db_overlay_source &dos = lnav_data.ld_db_overlay;
    textview_curses &db_tc = lnav_data.ld_views[LNV_DB];

    for (auto &help_iter : sqlite_function_help) {
        struct help_text &ht = *(help_iter.second);

        for (auto &ex : ht.ht_example) {
            string alt_msg;
            attr_line_t result;

            switch (ht.ht_context) {
                case help_context_t::HC_SQL_FUNCTION:
                case help_context_t::HC_SQL_TABLE_VALUED_FUNCTION: {
                    execute_sql(ec, ex.he_cmd, alt_msg);

                    if (dls.dls_rows.size() == 1 &&
                        dls.dls_rows[0].size() == 1) {
                        result.append(dls.dls_rows[0][0]);
                    } else {
                        attr_line_t al;
                        dos.list_value_for_overlay(db_tc,
                                                   0, 1,
                                                   vis_line_t(0),
                                                   al);
                        result.append(al);
                        for (int lpc = 0;
                             lpc < (int)dls.text_line_count(); lpc++) {
                            al.clear();
                            dls.text_value_for_line(db_tc, lpc,
                                                    al.get_string(),
                                                    false);
                            dls.text_attrs_for_line(db_tc, lpc,
                                                    al.get_attrs());
                            result.append("\n")
                                .append(al);
                        }
                    }

                    EXAMPLE_RESULTS[ex.he_cmd] = result;

                    log_debug("example: %s", ex.he_cmd);
                    log_debug("example result: %s",
                              result.get_string().c_str());
                    break;
                }
                default:
                    log_warning("Not executing example: %s", ex.he_cmd);
                    break;
            }
        }
    }

    dls.clear();
}

attr_line_t eval_example(const help_text &ht, const help_example &ex)
{
    auto iter = EXAMPLE_RESULTS.find(ex.he_cmd);

    if (iter != EXAMPLE_RESULTS.end()) {
        return iter->second;
    }

    return "";
}

bool toggle_view(textview_curses *toggle_tc)
{
    textview_curses *tc = lnav_data.ld_view_stack.top().value_or(nullptr);
    bool             retval = false;

    require(toggle_tc != NULL);
    require(toggle_tc >= &lnav_data.ld_views[0]);
    require(toggle_tc < &lnav_data.ld_views[LNV__MAX]);

    if (tc == toggle_tc) {
        if (lnav_data.ld_view_stack.vs_views.size() == 1) {
            return false;
        }
        lnav_data.ld_last_view = tc;
        lnav_data.ld_view_stack.vs_views.pop_back();
    }
    else {
        if (toggle_tc == &lnav_data.ld_views[LNV_SCHEMA]) {
            open_schema_view();
        }
        else if (toggle_tc == &lnav_data.ld_views[LNV_PRETTY]) {
            open_pretty_view();
        }
        else if (toggle_tc == &lnav_data.ld_views[LNV_HISTOGRAM]) {
            // Rebuild to reflect changes in marks.
            rebuild_hist();
        }
        else if (toggle_tc == &lnav_data.ld_views[LNV_HELP]) {
            build_all_help_text();
        }
        lnav_data.ld_last_view = nullptr;
        lnav_data.ld_view_stack.vs_views.push_back(toggle_tc);
        retval = true;
    }
    tc = *lnav_data.ld_view_stack.top();
    tc->set_needs_update();
    lnav_data.ld_view_stack_broadcaster.invoke(tc);

    return retval;
}

/**
 * Ensure that the view is on the top of the view stack.
 *
 * @param expected_tc The text view that should be on top.
 * @return True if the view was already on the top of the stack.
 */
bool ensure_view(textview_curses *expected_tc)
{
    textview_curses *tc = lnav_data.ld_view_stack.top().value_or(nullptr);
    bool retval = true;

    if (tc != expected_tc) {
        toggle_view(expected_tc);
        retval = false;
    }
    return retval;
}
