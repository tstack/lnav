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

#include "log.watch.hh"

#include <sqlite3.h>

#include "base/injector.hh"
#include "bound_tags.hh"
#include "lnav.events.hh"
#include "lnav_config_fwd.hh"
#include "log_format.hh"
#include "logfile_sub_source.cfg.hh"
#include "readline_highlighters.hh"
#include "sql_util.hh"
#include "sqlitepp.hh"

namespace lnav {
namespace log {
namespace watch {

struct compiled_watch_expr {
    auto_mem<sqlite3_stmt> cwe_stmt{sqlite3_finalize};
    bool cwe_enabled{true};
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

        const auto& cfg = injector::get<const logfile_sub_source_ns::config&>();

        this->e_watch_exprs.clear();
        for (const auto& pair : cfg.c_watch_exprs) {
            auto stmt_str = fmt::format(FMT_STRING("SELECT 1 WHERE {}"),
                                        pair.second.we_expr);
            compiled_watch_expr cwe;

            log_info("preparing watch expression: %s", stmt_str.c_str());
            auto retcode = sqlite3_prepare_v2(lnav_db,
                                              stmt_str.c_str(),
                                              stmt_str.size(),
                                              cwe.cwe_stmt.out(),
                                              nullptr);
            if (retcode != SQLITE_OK) {
                auto sql_al = attr_line_t(pair.second.we_expr)
                                  .with_attr_for_all(SA_PREFORMATTED.value())
                                  .with_attr_for_all(
                                      VC_ROLE.value(role_t::VCR_QUOTED_CODE));
                readline_sqlite_highlighter(sql_al, -1);
                intern_string_t watch_expr_path = intern_string::lookup(
                    fmt::format(FMT_STRING("/log/watch-expressions/{}/expr"),
                                pair.first));
                auto snippet = lnav::console::snippet::from(
                    source_location(watch_expr_path), sql_al);

                auto um = lnav::console::user_message::error(
                              "SQL expression is invalid")
                              .with_reason(sqlite3_errmsg(lnav_db))
                              .with_snippet(snippet);

                reporter(&pair.second.we_expr, um);
                continue;
            }

            this->e_watch_exprs.emplace(pair.first, std::move(cwe));
        }
    }

    void unload_config() override { this->e_watch_exprs.clear(); }

    std::map<std::string, compiled_watch_expr> e_watch_exprs;
};

static expressions exprs;

void
eval_with(logfile& lf, logfile::iterator ll)
{
    if (std::none_of(exprs.e_watch_exprs.begin(),
                     exprs.e_watch_exprs.end(),
                     [](const auto& elem) { return elem.second.cwe_enabled; }))
    {
        return;
    }

    static auto& lnav_db = injector::get<auto_sqlite3&>();

    char timestamp_buffer[64] = "";
    shared_buffer_ref raw_sbr;
    logline_value_vector values;
    lf.read_full_message(ll, values.lvv_sbr);
    values.lvv_sbr.erase_ansi();
    auto format = lf.get_format();
    string_attrs_t sa;
    auto line_number = std::distance(lf.begin(), ll);
    format->annotate(line_number, sa, values);

    for (auto& watch_pair : exprs.e_watch_exprs) {
        if (!watch_pair.second.cwe_enabled) {
            continue;
        }

        auto* stmt = watch_pair.second.cwe_stmt.in();
        sqlite3_reset(stmt);

        auto count = sqlite3_bind_parameter_count(stmt);
        auto missing_column = false;
        for (int lpc = 0; lpc < count; lpc++) {
            const auto* name = sqlite3_bind_parameter_name(stmt, lpc + 1);

            if (name[0] == '$') {
                const char* env_value;

                if ((env_value = getenv(&name[1])) != nullptr) {
                    sqlite3_bind_text(
                        stmt, lpc + 1, env_value, -1, SQLITE_STATIC);
                }
                continue;
            }
            if (strcmp(name, ":log_level") == 0) {
                sqlite3_bind_text(
                    stmt, lpc + 1, ll->get_level_name(), -1, SQLITE_STATIC);
                continue;
            }
            if (strcmp(name, ":log_time") == 0) {
                auto len = sql_strftime(timestamp_buffer,
                                        sizeof(timestamp_buffer),
                                        ll->get_timeval(),
                                        'T');
                sqlite3_bind_text(
                    stmt, lpc + 1, timestamp_buffer, len, SQLITE_STATIC);
                continue;
            }
            if (strcmp(name, ":log_time_msecs") == 0) {
                sqlite3_bind_int64(stmt, lpc + 1, ll->get_time_in_millis());
                continue;
            }
            if (strcmp(name, ":log_format") == 0) {
                const auto format_name = format->get_name();
                sqlite3_bind_text(stmt,
                                  lpc + 1,
                                  format_name.get(),
                                  format_name.size(),
                                  SQLITE_STATIC);
                continue;
            }
            if (strcmp(name, ":log_format_regex") == 0) {
                const auto pat_name = format->get_pattern_name(line_number);
                sqlite3_bind_text(stmt,
                                  lpc + 1,
                                  pat_name.get(),
                                  pat_name.size(),
                                  SQLITE_STATIC);
                continue;
            }
            if (strcmp(name, ":log_path") == 0) {
                const auto& filename = lf.get_filename();
                sqlite3_bind_text(stmt,
                                  lpc + 1,
                                  filename.c_str(),
                                  filename.native().length(),
                                  SQLITE_STATIC);
                continue;
            }
            if (strcmp(name, ":log_unique_path") == 0) {
                const auto& filename = lf.get_unique_path();
                sqlite3_bind_text(stmt,
                                  lpc + 1,
                                  filename.c_str(),
                                  filename.native().length(),
                                  SQLITE_STATIC);
                continue;
            }
            if (strcmp(name, ":log_text") == 0) {
                sqlite3_bind_text(stmt,
                                  lpc + 1,
                                  values.lvv_sbr.get_data(),
                                  values.lvv_sbr.length(),
                                  SQLITE_STATIC);
                continue;
            }
            if (strcmp(name, ":log_body") == 0) {
                auto body_attr_opt = get_string_attr(sa, SA_BODY);
                if (body_attr_opt) {
                    const auto& sar
                        = body_attr_opt.value().saw_string_attr->sa_range;

                    sqlite3_bind_text(stmt,
                                      lpc + 1,
                                      values.lvv_sbr.get_data_at(sar.lr_start),
                                      sar.length(),
                                      SQLITE_STATIC);
                } else {
                    sqlite3_bind_null(stmt, lpc + 1);
                }
                continue;
            }
            if (strcmp(name, ":log_opid") == 0) {
                if (values.lvv_opid_value) {
                    sqlite3_bind_text(stmt,
                                      lpc + 1,
                                      values.lvv_opid_value->c_str(),
                                      values.lvv_opid_value->length(),
                                      SQLITE_STATIC);
                } else {
                    sqlite3_bind_null(stmt, lpc + 1);
                }
                continue;
            }
            if (strcmp(name, ":log_raw_text") == 0) {
                auto res = lf.read_raw_message(ll);

                if (res.isOk()) {
                    raw_sbr = res.unwrap();
                    sqlite3_bind_text(stmt,
                                      lpc + 1,
                                      raw_sbr.get_data(),
                                      raw_sbr.length(),
                                      SQLITE_STATIC);
                }
                continue;
            }
            if (strcmp(name, ":log_tags") == 0) {
                const auto& bm = lf.get_bookmark_metadata();
                auto bm_iter = bm.find(line_number);
                if (bm_iter != bm.end() && !bm_iter->second.bm_tags.empty()) {
                    const auto& meta = bm_iter->second;
                    yajlpp_gen gen;

                    yajl_gen_config(gen, yajl_gen_beautify, false);

                    {
                        yajlpp_array arr(gen);

                        for (const auto& str : meta.bm_tags) {
                            arr.gen(str);
                        }
                    }

                    string_fragment sf = gen.to_string_fragment();

                    sqlite3_bind_text(stmt,
                                      lpc + 1,
                                      sf.data(),
                                      sf.length(),
                                      SQLITE_TRANSIENT);
                }
                continue;
            }
            auto found = false;
            for (const auto& lv : values.lvv_values) {
                if (lv.lv_meta.lvm_name != &name[1]) {
                    continue;
                }

                found = true;
                switch (lv.lv_meta.lvm_kind) {
                    case value_kind_t::VALUE_BOOLEAN:
                        sqlite3_bind_int64(stmt, lpc + 1, lv.lv_value.i);
                        break;
                    case value_kind_t::VALUE_FLOAT:
                        sqlite3_bind_double(stmt, lpc + 1, lv.lv_value.d);
                        break;
                    case value_kind_t::VALUE_INTEGER:
                        sqlite3_bind_int64(stmt, lpc + 1, lv.lv_value.i);
                        break;
                    case value_kind_t::VALUE_NULL:
                        sqlite3_bind_null(stmt, lpc + 1);
                        break;
                    default:
                        sqlite3_bind_text(stmt,
                                          lpc + 1,
                                          lv.text_value(),
                                          lv.text_length(),
                                          SQLITE_TRANSIENT);
                        break;
                }
                break;
            }
            if (!found) {
                missing_column = true;
                break;
            }
        }

        if (missing_column) {
            continue;
        }

        auto step_res = sqlite3_step(stmt);

        switch (step_res) {
            case SQLITE_OK:
            case SQLITE_DONE:
                continue;
            case SQLITE_ROW:
                break;
            default: {
                log_error("failed to execute watch expression: %s -- %s",
                          watch_pair.first.c_str(),
                          sqlite3_errmsg(lnav_db));
                watch_pair.second.cwe_enabled = false;
                continue;
            }
        }

        if (!timestamp_buffer[0]) {
            sql_strftime(timestamp_buffer,
                         sizeof(timestamp_buffer),
                         ll->get_timeval(),
                         'T');
        }
        auto lmd = lnav::events::log::msg_detected{
            watch_pair.first,
            lf.get_filename(),
            lf.get_format_name().to_string(),
            (uint32_t) line_number,
            timestamp_buffer,
        };
        for (const auto& lv : values.lvv_values) {
            switch (lv.lv_meta.lvm_kind) {
                case value_kind_t::VALUE_NULL:
                    lmd.md_values[lv.lv_meta.lvm_name.to_string()]
                        = json_null_t{};
                    break;
                case value_kind_t::VALUE_BOOLEAN:
                    lmd.md_values[lv.lv_meta.lvm_name.to_string()]
                        = lv.lv_value.i ? true : false;
                    break;
                case value_kind_t::VALUE_INTEGER:
                    lmd.md_values[lv.lv_meta.lvm_name.to_string()]
                        = lv.lv_value.i;
                    break;
                case value_kind_t::VALUE_FLOAT:
                    lmd.md_values[lv.lv_meta.lvm_name.to_string()]
                        = lv.lv_value.d;
                    break;
                default:
                    lmd.md_values[lv.lv_meta.lvm_name.to_string()]
                        = lv.to_string();
                    break;
            }
        }
        lnav::events::publish(lnav_db, lmd);
    }
}

}  // namespace watch
}  // namespace log
}  // namespace lnav
