/**
 * Copyright (c) 2020, Timothy Stack
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
 *
 * @file file_format.hh
 */

#include "file_format.hh"

#include <sqlite3.h>

#include "archive_manager.hh"
#include "base/auto_fd.hh"
#include "base/fs_util.hh"
#include "base/injector.hh"
#include "base/intern_string.hh"
#include "base/lnav_log.hh"
#include "config.h"
#include "lnav_config.hh"
#include "readline_highlighters.hh"
#include "safe/safe.h"
#include "sql_util.hh"
#include "sqlite-extension-func.hh"

file_format_t
detect_file_format(const ghc::filesystem::path& filename)
{
    if (archive_manager::is_archive(filename)) {
        return file_format_t::ARCHIVE;
    }

    file_format_t retval = file_format_t::UNKNOWN;
    auto_fd fd;

    if ((fd = lnav::filesystem::openp(filename, O_RDONLY)) != -1) {
        uint8_t buffer[32];
        ssize_t rc;

        if ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
            static auto SQLITE3_HEADER = "SQLite format 3";
            auto header_frag = string_fragment(buffer, 0, rc);

            if (header_frag.startswith(SQLITE3_HEADER)) {
                retval = file_format_t::SQLITE_DB;
            }
        }
    }

    return retval;
}

mime_type
mime_type::from_str(const std::string& str)
{
    auto slash_index = str.find('/');

    if (slash_index == std::string::npos) {
        return {"application", str};
    }

    return {str.substr(0, slash_index), str.substr(slash_index + 1)};
}

struct compiled_header_expr {
    auto_mem<sqlite3_stmt> che_stmt{sqlite3_finalize};
    bool che_enabled{true};
};

struct file_format_expressions : public lnav_config_listener {
    void reload_config(error_reporter& reporter) override
    {
        log_debug("reloading file-format header expressions");

        safe::WriteAccess<safe::Safe<inner>> in(instance);

        if (in->e_db.in() == nullptr) {
            if (sqlite3_open(":memory:", in->e_db.out()) != SQLITE_OK) {
                log_error("unable to open memory DB");
                return;
            }
            register_sqlite_funcs(in->e_db.in(), sqlite_registration_funcs);
        }

        in->e_header_exprs.clear();
        const auto& cfg = injector::get<const lnav::file_formats::config&>();
        for (const auto& fpair : cfg.c_defs) {
            for (const auto& hpair : fpair.second.fd_header.h_exprs.he_exprs) {
                auto stmt_str = fmt::format(FMT_STRING("SELECT 1 WHERE {}"),
                                            hpair.second);
                compiled_header_expr che;

                log_info("preparing file-format header expression: %s",
                         stmt_str.c_str());
                auto retcode = sqlite3_prepare_v2(in->e_db.in(),
                                                  stmt_str.c_str(),
                                                  stmt_str.size(),
                                                  che.che_stmt.out(),
                                                  nullptr);
                if (retcode != SQLITE_OK) {
                    auto sql_al
                        = attr_line_t(hpair.second)
                              .with_attr_for_all(SA_PREFORMATTED.value())
                              .with_attr_for_all(
                                  VC_ROLE.value(role_t::VCR_QUOTED_CODE));
                    readline_sqlite_highlighter(sql_al, -1);
                    intern_string_t watch_expr_path
                        = intern_string::lookup(fmt::format(
                            FMT_STRING(
                                "/tuning/file-formats/{}/header/expr/{}"),
                            json_ptr::encode_str(fpair.first.c_str()),
                            hpair.first));
                    auto snippet = lnav::console::snippet::from(
                        source_location(watch_expr_path), sql_al);

                    auto um = lnav::console::user_message::error(
                                  "SQL expression is invalid")
                                  .with_reason(sqlite3_errmsg(in->e_db.in()))
                                  .with_snippet(snippet);

                    reporter(&hpair.second, um);
                    continue;
                }

                in->e_header_exprs[fpair.first][hpair.first] = std::move(che);
            }

            if (fpair.second.fd_header.h_exprs.he_exprs.empty()) {
                auto um
                    = lnav::console::user_message::error(
                          "At least one header expression is required for "
                          "a file format")
                          .with_reason(
                              "Header expressions are used to detect a format");
                reporter(&fpair.second.fd_header.h_exprs, um);
            }
            if (fpair.second.fd_converter.pp_value.empty()) {
                auto um = lnav::console::user_message::error(
                              "A converter is required for a file format")
                              .with_reason(
                                  "The converter script transforms the file "
                                  "into a format that can be consumed by lnav");
                reporter(&fpair.second.fd_converter, um);
            }
        }
    }

    void unload_config() override
    {
        safe::WriteAccess<safe::Safe<inner>> in(instance);

        in->e_header_exprs.clear();
    }

    struct inner {
        auto_sqlite3 e_db;
        std::map<std::string, std::map<std::string, compiled_header_expr>>
            e_header_exprs;
    };

    safe::Safe<inner> instance;
};

static file_format_expressions format_exprs;

nonstd::optional<external_file_format>
detect_mime_type(const ghc::filesystem::path& filename)
{
    uint8_t buffer[1024];
    size_t buffer_size = 0;

    {
        auto_fd fd;

        if ((fd = lnav::filesystem::openp(filename, O_RDONLY)) == -1) {
            return nonstd::nullopt;
        }

        ssize_t rc;

        if ((rc = read(fd, buffer, sizeof(buffer))) == -1) {
            return nonstd::nullopt;
        }
        buffer_size = rc;
    }

    auto hexbuf = auto_buffer::alloc(buffer_size * 2);

    for (int lpc = 0; lpc < buffer_size; lpc++) {
        fmt::format_to(
            std::back_inserter(hexbuf), FMT_STRING("{:02x}"), buffer[lpc]);
    }

    safe::WriteAccess<safe::Safe<file_format_expressions::inner>> in(
        format_exprs.instance);

    const auto& cfg = injector::get<const lnav::file_formats::config&>();
    for (const auto& fpair : cfg.c_defs) {
        if (buffer_size < fpair.second.fd_header.h_size) {
            log_debug("file content too small (%d) for header detection: %s",
                      buffer_size,
                      fpair.first.c_str());
            continue;
        }
        for (const auto& hpair : fpair.second.fd_header.h_exprs.he_exprs) {
            auto& he = in->e_header_exprs[fpair.first][hpair.first];

            if (!he.che_enabled) {
                continue;
            }

            auto* stmt = he.che_stmt.in();

            if (stmt == nullptr) {
                continue;
            }
            sqlite3_reset(stmt);
            auto count = sqlite3_bind_parameter_count(stmt);
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
                if (strcmp(name, ":header") == 0) {
                    sqlite3_bind_text(stmt,
                                      lpc + 1,
                                      hexbuf.in(),
                                      hexbuf.size(),
                                      SQLITE_STATIC);
                    continue;
                }
                if (strcmp(name, ":filepath") == 0) {
                    sqlite3_bind_text(
                        stmt, lpc + 1, filename.c_str(), -1, SQLITE_STATIC);
                    continue;
                }
            }

            auto step_res = sqlite3_step(stmt);

            switch (step_res) {
                case SQLITE_OK:
                case SQLITE_DONE:
                    continue;
                case SQLITE_ROW:
                    break;
                default: {
                    log_error(
                        "failed to execute file-format header expression: "
                        "%s:%s -- %s",
                        fpair.first.c_str(),
                        hpair.first.c_str(),
                        sqlite3_errmsg(in->e_db));
                    he.che_enabled = false;
                    continue;
                }
            }

            log_info("detected MIME type for: %s -- %s (header-expr: %s)",
                     filename.c_str(),
                     fpair.first.c_str(),
                     hpair.first.c_str());
            return external_file_format{
                mime_type::from_str(fpair.first),
                fpair.second.fd_converter.pp_value,
                fpair.second.fd_converter.pp_location.sl_source.to_string(),
            };
        }
    }

    return nonstd::nullopt;
}
