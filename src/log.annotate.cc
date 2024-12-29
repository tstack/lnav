/**
 * Copyright (c) 2023, Timothy Stack
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

#include <future>

#include "log.annotate.hh"

#include "base/auto_fd.hh"
#include "base/auto_pid.hh"
#include "base/fs_util.hh"
#include "base/paths.hh"
#include "line_buffer.hh"
#include "lnav.hh"
#include "log.annotate.cfg.hh"
#include "log_data_helper.hh"
#include "md4cpp.hh"
#include "readline_highlighters.hh"
#include "yajlpp/yajlpp.hh"

namespace lnav::log::annotate {

struct compiled_cond_expr {
    auto_mem<sqlite3_stmt> cce_stmt{sqlite3_finalize};
    bool cce_enabled{true};
};

struct expressions : public lnav_config_listener {
    expressions() : lnav_config_listener(__FILE__) {}

    void reload_config(error_reporter& reporter) override
    {
        auto& lnav_db = injector::get<auto_sqlite3&>();

        if (lnav_db.in() == nullptr) {
            log_warning("db not initialized yet!");
            return;
        }

        const auto& cfg = injector::get<const config&>();

        this->e_cond_exprs.clear();
        for (const auto& pair : cfg.a_definitions) {
            if (pair.second.a_handler.pp_value.empty()) {
                auto um
                    = lnav::console::user_message::error(
                          "no handler specified for annotation")
                          .with_reason("Every annotation requires a handler")
                          .move();
                reporter(&pair.second.a_handler, um);
                continue;
            }

            auto stmt_str = fmt::format(FMT_STRING("SELECT 1 WHERE {}"),
                                        pair.second.a_condition);
            compiled_cond_expr cce;

            log_info("preparing annotation condition expression: %s",
                     stmt_str.c_str());
            auto retcode = sqlite3_prepare_v2(lnav_db,
                                              stmt_str.c_str(),
                                              stmt_str.size(),
                                              cce.cce_stmt.out(),
                                              nullptr);
            if (retcode != SQLITE_OK) {
                auto sql_al = attr_line_t(pair.second.a_condition)
                                  .with_attr_for_all(SA_PREFORMATTED.value())
                                  .with_attr_for_all(
                                      VC_ROLE.value(role_t::VCR_QUOTED_CODE))
                                  .move();
                readline_sqlite_highlighter(sql_al, std::nullopt);
                intern_string_t cond_expr_path = intern_string::lookup(
                    fmt::format(FMT_STRING("/log/annotations/{}/condition"),
                                pair.first));
                auto snippet = lnav::console::snippet::from(
                    source_location(cond_expr_path), sql_al);

                auto um = lnav::console::user_message::error(
                              "SQL expression is invalid")
                              .with_reason(sqlite3_errmsg(lnav_db))
                              .with_snippet(snippet)
                              .move();

                reporter(&pair.second.a_condition, um);
                continue;
            }

            this->e_cond_exprs.emplace(pair.first, std::move(cce));
        }
    }

    void unload_config() override { this->e_cond_exprs.clear(); }

    std::map<intern_string_t, compiled_cond_expr> e_cond_exprs;
};

static expressions exprs;

std::vector<intern_string_t>
applicable(vis_line_t vl)
{
    std::vector<intern_string_t> retval;
    auto& lss = lnav_data.ld_log_source;
    auto cl = lss.at(vl);
    auto ld = lss.find_data(cl);
    log_data_helper ldh(lss);

    ldh.parse_line(vl, true);
    for (auto& expr : exprs.e_cond_exprs) {
        if (!expr.second.cce_enabled) {
            continue;
        }

        auto eval_res
            = lss.eval_sql_filter(expr.second.cce_stmt.in(), ld, ldh.ldh_line);

        if (eval_res.isErr()) {
            log_error("eval failed: %s",
                      eval_res.unwrapErr().to_attr_line().get_string().c_str());
            expr.second.cce_enabled = false;
        } else {
            if (eval_res.unwrap()) {
                retval.emplace_back(expr.first);
            }
        }
    }
    return retval;
}

Result<void, lnav::console::user_message>
apply(vis_line_t vl, std::vector<intern_string_t> annos)
{
    const auto& cfg = injector::get<const config&>();
    auto& lss = lnav_data.ld_log_source;
    auto cl = lss.at(vl);
    auto ld = lss.find_data(cl);
    auto lf = (*ld)->get_file();
    logmsg_annotations la;
    log_data_helper ldh(lss);

    if (!ldh.parse_line(vl, true)) {
        log_error("failed to parse line %d", vl);
        return Err(lnav::console::user_message::error("Failed to parse line"));
    }
    auto line_number = content_line_t{ldh.ldh_line_index - ldh.ldh_y_offset};
    lss.set_user_mark(&textview_curses::BM_META,
                      content_line_t{ldh.ldh_source_line - ldh.ldh_y_offset});

    yajlpp_gen gen;

    {
        auto bm_opt = lss.find_bookmark_metadata(vl);
        yajlpp_map root(gen);

        root.gen("log_line");
        root.gen((int64_t) vl);
        root.gen("log_tags");
        {
            yajlpp_array tag_array(gen);

            if (bm_opt) {
                const auto& bm = *(bm_opt.value());

                for (const auto& tag : bm.bm_tags) {
                    tag_array.gen(tag);
                }
            }
        }
        root.gen("log_path");
        root.gen(lf->get_filename().native());
        root.gen("log_format");
        root.gen(lf->get_format_name());
        root.gen("log_format_regex");
        root.gen(lf->get_format()->get_pattern_name(line_number));
        root.gen("log_msg");
        root.gen(ldh.ldh_line_values.lvv_sbr.to_string_fragment());
        for (const auto& val : ldh.ldh_line_values.lvv_values) {
            root.gen(val.lv_meta.lvm_name);
            switch (val.lv_meta.lvm_kind) {
                case value_kind_t::VALUE_NULL:
                    root.gen();
                    break;
                case value_kind_t::VALUE_INTEGER:
                    root.gen(val.lv_value.i);
                    break;
                case value_kind_t::VALUE_FLOAT:
                    root.gen(val.lv_value.d);
                    break;
                case value_kind_t::VALUE_BOOLEAN:
                    root.gen(val.lv_value.i ? true : false);
                    break;
                default:
                    root.gen(val.to_string());
                    break;
            }
        }
    }

    for (const auto& anno : annos) {
        auto iter = cfg.a_definitions.find(anno);
        if (iter == cfg.a_definitions.end()) {
            log_error("unknown annotation: %s", anno.c_str());
            continue;
        }

        la.la_pairs[anno.to_string()] = "Loading...";
        auto child_fds_res = auto_pipe::for_child_fds(
            STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
        if (child_fds_res.isErr()) {
            auto um
                = lnav::console::user_message::error("unable to create pipes")
                      .with_reason(child_fds_res.unwrapErr())
                      .move();
            return Err(um);
        }

        auto child_res = lnav::pid::from_fork();
        if (child_res.isErr()) {
            auto um
                = lnav::console::user_message::error("unable to fork() child")
                      .with_reason(child_res.unwrapErr())
                      .move();
            return Err(um);
        }

        auto child_fds = child_fds_res.unwrap();

        auto child = child_res.unwrap();
        for (auto& child_fd : child_fds) {
            child_fd.after_fork(child.in());
        }
        if (child.in_child()) {
            const char* exec_args[] = {
                getenv_opt("SHELL").value_or("bash"),
                "-c",
                iter->second.a_handler.pp_value.c_str(),
                nullptr,
            };

            std::vector<std::filesystem::path> path_v;

            auto src_path
                = std::filesystem::path(
                      iter->second.a_handler.pp_location.sl_source.to_string())
                      .parent_path();
            path_v.push_back(src_path);
            path_v.push_back(lnav::paths::dotlnav() / "formats/default");
            auto path_var = lnav::filesystem::build_path(path_v);

            log_debug("annotate PATH: %s", path_var.c_str());
            setenv("PATH", path_var.c_str(), 1);
            execvp(exec_args[0], (char**) exec_args);
            _exit(EXIT_FAILURE);
        }

        auto out_reader = std::async(
            std::launch::async,
            [out_fd = std::move(child_fds[1].read_end())]() mutable {
                std::string retval;
                file_range last_range;
                line_buffer lb;

                lb.set_fd(out_fd);
                while (true) {
                    auto load_res = lb.load_next_line(last_range);
                    if (load_res.isErr()) {
                        log_error("unable to load next line: %s",
                                  load_res.unwrapErr().c_str());
                        break;
                    }

                    auto li = load_res.unwrap();
                    if (li.li_file_range.empty()) {
                        break;
                    }
                    auto read_res = lb.read_range(li.li_file_range);
                    if (read_res.isErr()) {
                        log_error("unable to read next line: %s",
                                  load_res.unwrapErr().c_str());
                        break;
                    }

                    auto sbr = read_res.unwrap();
                    retval.append(sbr.get_data(), sbr.length());

                    last_range = li.li_file_range;
                }

                return retval;
            });

        auto err_reader = std::async(
            std::launch::async,
            [err_fd = std::move(child_fds[2].read_end()),
             handler = iter->second.a_handler.pp_value]() mutable {
                std::string retval;
                file_range last_range;
                line_buffer lb;

                lb.set_fd(err_fd);
                while (true) {
                    auto load_res = lb.load_next_line(last_range);
                    if (load_res.isErr()) {
                        log_error("unable to load next line: %s",
                                  load_res.unwrapErr().c_str());
                        break;
                    }

                    auto li = load_res.unwrap();
                    if (li.li_file_range.empty()) {
                        break;
                    }
                    auto read_res = lb.read_range(li.li_file_range);
                    if (read_res.isErr()) {
                        log_error("unable to read next line: %s",
                                  load_res.unwrapErr().c_str());
                        break;
                    }

                    auto sbr = read_res.unwrap();
                    retval.append(sbr.get_data(), sbr.length());
                    sbr.rtrim(is_line_ending);
                    log_debug("%s: %.*s",
                              handler.c_str(),
                              sbr.length(),
                              sbr.get_data());

                    last_range = li.li_file_range;
                }

                return retval;
            });

        auto write_res
            = child_fds[0].write_end().write_fully(gen.to_string_fragment());
        if (write_res.isErr()) {
            log_error("bah %s", write_res.unwrapErr().c_str());
        }
        child_fds[0].write_end().reset();
        auto finalizer = [anno,
                          out_reader1 = out_reader.share(),
                          err_reader1 = err_reader.share(),
                          lf,
                          line_number,
                          handler = iter->second.a_handler.pp_value](
                             auto& fc,
                             auto_pid<process_state::finished>& child) mutable {
            auto& line_anno
                = lf->get_bookmark_metadata()[line_number].bm_annotations;
            auto content = out_reader1.get();
            if (!child.was_normal_exit()) {
                content.append(fmt::format(
                    FMT_STRING(
                        "\n\n\u2718 annotation handler \u201c{}\u201d failed "
                        "with signal {}:\n\n<pre>\n{}\n</pre>\n"),
                    handler,
                    child.term_signal(),
                    err_reader1.get()));
            } else if (child.exit_status() != 0) {
                content.append(fmt::format(
                    FMT_STRING(
                        "\n\n<span "
                        "class=\"-lnav_log-level-styles_error\">"
                        "\u2718 annotation handler \u201c{}\u201d exited "
                        "with status {}:</span>\n\n<pre>{}</pre>"),
                    handler,
                    child.exit_status(),
                    md4cpp::escape_html(err_reader1.get())));
            }
            line_anno.la_pairs[anno.to_string()] = content;
            lnav_data.ld_views[LNV_LOG].reload_data();
            lnav_data.ld_views[LNV_LOG].set_needs_update();
        };

        lnav_data.ld_child_pollers.emplace_back(
            (*ld)->get_file_ptr()->get_filename(),
            std::move(child),
            std::move(finalizer));
    }
    lf->get_bookmark_metadata()[line_number].bm_annotations = la;
    return Ok();
}

}  // namespace lnav::log::annotate