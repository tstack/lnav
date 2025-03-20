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

#include "archive_manager.hh"
#include "base/auto_fd.hh"
#include "base/fs_util.hh"
#include "base/intern_string.hh"
#include "base/lnav_log.hh"
#include "config.h"
#include "line_buffer.hh"
#include "piper.match.hh"
#include "text_format.hh"

detect_file_format_result
detect_file_format(const std::filesystem::path& filename)
{
    log_debug("detecting format of file: %s", filename.c_str());

    detect_file_format_result retval = {file_format_t::UNKNOWN};
    auto describe_res = archive_manager::describe(filename);
    if (describe_res.isOk()
        && describe_res.unwrap().is<archive_manager::archive_info>())
    {
        return {file_format_t::ARCHIVE};
    }

    auto open_res = lnav::filesystem::open_file(filename, O_RDONLY);
    if (open_res.isErr()) {
        log_error("unable to open file for format detection: %s -- %s",
                  filename.c_str(),
                  open_res.unwrapErr().c_str());
    } else {
        auto fd = open_res.unwrap();
        uint8_t buffer[32];
        auto rc = read(fd, buffer, sizeof(buffer));

        if (rc < 0) {
            log_error("unable to read file for format detection: %s -- %s",
                      filename.c_str(),
                      strerror(errno));
        } else {
            static auto SQLITE3_HEADER = "SQLite format 3";
            auto header_frag = string_fragment::from_bytes(buffer, rc);

            if (header_frag.startswith(SQLITE3_HEADER)) {
                log_info("%s: appears to be a SQLite DB", filename.c_str());
                retval.dffr_file_format = file_format_t::SQLITE_DB;
            } else {
                auto tf = detect_text_format(header_frag, filename);
                auto looping = true;

                switch (tf) {
                    case text_format_t::TF_UNKNOWN:
                    case text_format_t::TF_BINARY:
                    case text_format_t::TF_LOG:
                    case text_format_t::TF_JSON:
                        log_info("file does not have a known text format: %s",
                                 filename.c_str());
                        break;
                    default:
                        log_info("file has text format: %s -> %d",
                                 filename.c_str(),
                                 tf);
                        looping = false;
                        break;
                }

                lnav::piper::multiplex_matcher mm;
                file_range next_range;
                line_buffer lb;
                lb.set_fd(fd);

                while (looping) {
                    auto load_res = lb.load_next_line(next_range);
                    if (load_res.isErr()) {
                        log_error(
                            "unable to load line for demux matching: %s -- %s",
                            filename.c_str(),
                            load_res.unwrapErr().c_str());
                        break;
                    }
                    if (!lb.is_header_utf8()) {
                        log_info("file is not UTF-8: %s", filename.c_str());
                        break;
                    }
                    if (lb.is_piper()) {
                        log_info("skipping demux match for piper file: %s",
                                 filename.c_str());
                        break;
                    }
                    const auto li = load_res.unwrap();
                    if (li.li_partial) {
                        log_info("skipping demux match for partial line");
                        break;
                    }
                    auto read_res = lb.read_range(li.li_file_range);
                    if (read_res.isErr()) {
                        log_error(
                            "unable to read line for demux matching: %s -- %s",
                            filename.c_str(),
                            read_res.unwrapErr().c_str());
                        break;
                    }
                    auto sbr = read_res.unwrap();
                    auto match_res = mm.match(sbr.to_string_fragment());

                    looping = match_res.match(
                        [&retval, &filename](
                            lnav::piper::multiplex_matcher::found_regex f) {
                            log_info("%s: is multiplexed using pattern %s",
                                     filename.c_str(),
                                     f.f_id.c_str());
                            retval.dffr_file_format
                                = file_format_t::MULTIPLEXED;
                            return false;
                        },
                        [&retval, &filename](
                            lnav::piper::multiplex_matcher::found_json f) {
                            log_info("%s: is multiplexed using JSON %s",
                                     filename.c_str(),
                                     f.fj_id.c_str());
                            retval.dffr_file_format
                                = file_format_t::MULTIPLEXED;
                            return false;
                        },
                        [](lnav::piper::multiplex_matcher::not_found nf) {
                            return false;
                        },
                        [](lnav::piper::multiplex_matcher::partial p) {
                            return true;
                        });

                    next_range = li.li_file_range;
                }
                retval.dffr_details = std::move(mm.mm_details);
            }
        }
    }

    return retval;
}
