/**
 * Copyright (c) 2007-2012, Timothy Stack
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
 * @file logfile.cc
 */

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "logfile.hh"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>

#include "base/ansi_scrubber.hh"
#include "base/attr_line.builder.hh"
#include "base/auto_fd.hh"
#include "base/date_time_scanner.cfg.hh"
#include "base/fs_util.hh"
#include "base/injector.hh"
#include "base/intern_string.hh"
#include "base/is_utf8.hh"
#include "base/result.h"
#include "base/snippet_highlighters.hh"
#include "base/string_util.hh"
#include "base/time_util.hh"
#include "config.h"
#include "file_options.hh"
#include "hasher.hh"
#include "lnav_util.hh"
#include "log.watch.hh"
#include "log_format.hh"
#include "logfile.cfg.hh"
#include "piper.header.hh"
#include "shared_buffer.hh"
#include "text_format.hh"
#include "yajlpp/yajlpp_def.hh"

using namespace lnav::roles::literals;

static auto intern_lifetime = intern_string::get_table_lifetime();

static constexpr size_t INDEX_RESERVE_INCREMENT = 1024;

static constexpr size_t RETRY_MATCH_SIZE = 250;

static const typed_json_path_container<lnav::gzip::header>&
get_file_header_handlers()
{
    static const typed_json_path_container<lnav::gzip::header> retval = {
        yajlpp::property_handler("name").for_field(&lnav::gzip::header::h_name),
        yajlpp::property_handler("mtime").for_field(
            &lnav::gzip::header::h_mtime),
        yajlpp::property_handler("comment").for_field(
            &lnav::gzip::header::h_comment),
    };

    return retval;
}

Result<std::shared_ptr<logfile>, std::string>
logfile::open(std::filesystem::path filename,
              const logfile_open_options& loo,
              auto_fd fd)
{
    require(!filename.empty());

    auto lf = std::shared_ptr<logfile>(new logfile(std::move(filename), loo));

    memset(&lf->lf_stat, 0, sizeof(lf->lf_stat));
    std::filesystem::path resolved_path;

    if (!fd.has_value()) {
        auto rp_res = lnav::filesystem::realpath(lf->lf_filename);
        if (rp_res.isErr()) {
            return Err(fmt::format(FMT_STRING("realpath({}) failed with: {}"),
                                   lf->lf_filename,
                                   rp_res.unwrapErr()));
        }

        resolved_path = rp_res.unwrap();
        if (lnav::filesystem::statp(resolved_path, &lf->lf_stat) == -1) {
            return Err(fmt::format(FMT_STRING("stat({}) failed with: {}"),
                                   lf->lf_filename,
                                   lnav::from_errno()));
        }

        if (!S_ISREG(lf->lf_stat.st_mode)) {
            return Err(fmt::format(FMT_STRING("{} is not a regular file"),
                                   lf->lf_filename));
        }
    }

    auto_fd lf_fd;
    if (fd.has_value()) {
        lf_fd = std::move(fd);
    } else if ((lf_fd
                = lnav::filesystem::openp(resolved_path, O_RDONLY | O_CLOEXEC))
               == -1)
    {
        return Err(fmt::format(FMT_STRING("open({}) failed with: {}"),
                               lf->lf_filename,
                               lnav::from_errno()));
    } else {
        lf->lf_actual_path = lf->lf_filename;
        lf->lf_valid_filename = true;
    }

    lf_fd.close_on_exec();

    log_info("Creating logfile: fd=%d; size=%" PRId64 "; mtime=%" PRId64
             "; filename=%s",
             (int) lf_fd,
             (long long) lf->lf_stat.st_size,
             (long long) lf->lf_stat.st_mtime,
             lf->lf_filename_as_string.c_str());
    if (lf->lf_actual_path) {
        log_info("  actual_path=%s", lf->lf_actual_path->c_str());
    }

    if (!lf->lf_options.loo_filename.empty()) {
        lf->set_filename(lf->lf_options.loo_filename);
        lf->lf_valid_filename = false;
    }

    lf->lf_line_buffer.set_fd(lf_fd);
    lf->lf_index.reserve(INDEX_RESERVE_INCREMENT);

    lf->lf_indexing = lf->lf_options.loo_is_visible;
    lf->lf_text_format = lf->lf_options.loo_text_format;
    lf->lf_format_match_messages = loo.loo_match_details;

    const auto& hdr = lf->lf_line_buffer.get_header_data();
    if (hdr.valid()) {
        log_info("%s: has header %d",
                 lf->lf_filename_as_string.c_str(),
                 hdr.valid());
        hdr.match(
            [&lf](const lnav::gzip::header& gzhdr) {
                if (!gzhdr.empty()) {
                    lf->lf_embedded_metadata["net.zlib.gzip.header"] = {
                        text_format_t::TF_JSON,
                        get_file_header_handlers()
                            .formatter_for(gzhdr)
                            .with_config(yajl_gen_beautify, 1)
                            .to_string(),
                    };
                }
            },
            [&lf](const lnav::piper::header& phdr) {
                static auto& safe_options_hier
                    = injector::get<lnav::safe_file_options_hier&>();

                lf->lf_embedded_metadata["org.lnav.piper.header"] = {
                    text_format_t::TF_JSON,
                    lnav::piper::header_handlers.formatter_for(phdr)
                        .with_config(yajl_gen_beautify, 1)
                        .to_string(),
                };
                log_info("setting file name from piper header: %s",
                         phdr.h_name.c_str());
                lf->set_filename(phdr.h_name);
                lf->lf_valid_filename = false;
                if (phdr.h_demux_output
                    == lnav::piper::demux_output_t::signal) {
                    lf->lf_text_format = text_format_t::TF_LOG;
                }

                lnav::file_options fo;
                if (!phdr.h_timezone.empty()) {
                    log_info("setting default time zone from piper header: %s",
                             phdr.h_timezone.c_str());
                    try {
                        fo.fo_default_zone.pp_value
                            = date::locate_zone(phdr.h_timezone);
                    } catch (const std::runtime_error& e) {
                        log_error("unable to get tz from piper header %s -- %s",
                                  phdr.h_timezone.c_str(),
                                  e.what());
                    }
                }
                if (!fo.empty()) {
                    safe::WriteAccess<lnav::safe_file_options_hier>
                        options_hier(safe_options_hier);

                    auto& coll = options_hier->foh_path_to_collection["/"];
                    auto iter
                        = coll.foc_pattern_to_options.find(lf->get_filename());
                    if (iter == coll.foc_pattern_to_options.end()
                        || !(iter->second == fo))
                    {
                        coll.foc_pattern_to_options[lf->get_filename()] = fo;
                        options_hier->foh_generation += 1;
                    }
                }
            });
    }

    lf->file_options_have_changed();
    lf->lf_content_id = hasher().update(lf->lf_filename_as_string).to_string();

    lf->lf_line_buffer.set_do_preloading(true);
    lf->lf_line_buffer.send_initial_load();

    ensure(lf->invariant());

    return Ok(lf);
}

logfile::logfile(std::filesystem::path filename,
                 const logfile_open_options& loo)
    : lf_filename(std::move(filename)),
      lf_filename_as_string(lf_filename.string()), lf_options(loo),
      lf_basename(lf_filename.filename())
{
    this->lf_line_buffer.set_decompress_extra(true);
    this->lf_opids.writeAccess()->los_opid_ranges.reserve(64);
    this->lf_thread_ids.writeAccess()->ltis_tid_ranges.reserve(64);
}

logfile::~logfile()
{
    log_info("destructing logfile: %s", this->lf_filename_as_string.c_str());
}

bool
logfile::file_options_have_changed()
{
    static auto& safe_options_hier
        = injector::get<lnav::safe_file_options_hier&>();

    bool tz_changed = false;

    {
        safe::ReadAccess<lnav::safe_file_options_hier> options_hier(
            safe_options_hier);

        if (this->lf_file_options_generation == options_hier->foh_generation) {
            return false;
        }
        log_info("%s: checking new generation of file options: %zu -> %zu",
                 this->lf_filename_as_string.c_str(),
                 this->lf_file_options_generation,
                 options_hier->foh_generation);
        auto new_options = options_hier->match(this->get_filename());
        if (this->lf_file_options == new_options) {
            this->lf_file_options_generation = options_hier->foh_generation;
            return false;
        }

        this->lf_file_options = new_options;
        log_info("%s: file options have changed",
                 this->lf_filename_as_string.c_str());
        if (this->lf_file_options) {
            log_info(
                "  tz=%s",
                this->lf_file_options->second.fo_default_zone.pp_value->name()
                    .c_str());
            if (this->lf_file_options->second.fo_default_zone.pp_value
                    != nullptr
                && this->lf_format != nullptr
                && !(this->lf_format->lf_timestamp_flags & ETF_ZONE_SET))
            {
                log_info("  tz change affects this file");
                tz_changed = true;
            }
        } else if (this->lf_format != nullptr
                   && !(this->lf_format->lf_timestamp_flags & ETF_ZONE_SET)
                   && this->lf_format->lf_date_time.dts_default_zone != nullptr)
        {
            tz_changed = true;
        }
        this->lf_file_options_generation = options_hier->foh_generation;
    }

    return tz_changed;
}

logfile::map_entry_result
logfile::find_content_map_entry(file_off_t offset, map_read_requirement req)
{
    static constexpr auto LOOKBACK_SIZE = 32 * 1024;
    static constexpr auto MAX_LOOKBACK_SIZE = 4 * 1024 * 1024;

    auto lookback_size = this->lf_line_buffer.is_compressed()
        ? LOOKBACK_SIZE * 4
        : LOOKBACK_SIZE;

    if (offset < lookback_size) {
        return map_entry_not_found{};
    }
    auto end_range = file_range{
        offset - lookback_size,
        lookback_size,
    };

    auto full_size = this->get_content_size();
    file_size_t lower_offset = 0;
    file_size_t upper_offset = full_size;
    auto looping = true;
    std::optional<content_map_entry> best_lower_bound;
    do {
        std::optional<content_map_entry> lower_retval;
        std::optional<content_map_entry> time_found;
        log_debug(
            "    peeking range (off=%lld; size=%lld;  lower=%lld; upper=%lld)",
            end_range.fr_offset,
            end_range.fr_size,
            lower_offset,
            upper_offset);
        auto peek_res = this->lf_line_buffer.peek_range(end_range);
        if (!peek_res.isOk()) {
            log_error("    peek failed -- %s", peek_res.unwrapErr().c_str());
            return map_entry_not_found{};
        }
        auto peek_buf = peek_res.unwrap();
        auto peek_sf = to_string_fragment(peek_buf);

        if (req.is<map_read_upper_bound>()) {
            if (!peek_sf.endswith("\n")) {
                log_warning("    peek returned partial line");
                this->lf_file_size_at_map_time = full_size;
                return map_entry_not_found{};
            }
            peek_sf.pop_back();
        }
        auto found_line = false;
        while (!peek_sf.empty()) {
            auto rsplit_res = peek_sf.rsplit_pair(string_fragment::tag1{'\n'});
            if (!rsplit_res) {
                log_trace("    did not peek enough to find last line (off=%d)",
                          peek_sf.sf_end);
                if (!found_line && req.is<map_read_upper_bound>()) {
                    if (end_range.fr_offset < lookback_size) {
                        return map_entry_not_found{};
                    }
                    end_range.fr_offset -= lookback_size;
                    end_range.fr_size += lookback_size;
                    if (end_range.next_offset() > full_size) {
                        end_range.fr_offset = 0;
                        end_range.fr_size = full_size;
                    } else if (end_range.fr_size > MAX_LOOKBACK_SIZE) {
                        return map_entry_not_found{};
                    }
                }
                break;
            }

            found_line = true;
            auto [leading, last_line] = rsplit_res.value();
            // log_debug("leading %d", leading.length());
            // log_debug("last %.*s", last_line.length(), last_line.data());
            pattern_locks line_locks;
            scan_batch_context sbc_tmp{
                this->lf_allocator,
                line_locks,
            };
            shared_buffer tmp_sb;
            shared_buffer_ref tmp_sbr;
            tmp_sbr.share(tmp_sb, last_line.data(), last_line.length());
            auto end_lines_fr = file_range{
                end_range.fr_offset + last_line.sf_begin,
                last_line.length(),
            };
            auto utf8_res = is_utf8(last_line, '\n');
            end_lines_fr.fr_metadata.m_has_ansi = utf8_res.usr_has_ansi;
            end_lines_fr.fr_metadata.m_valid_utf = utf8_res.is_valid();
            auto end_li = line_info{
                end_lines_fr,
            };
            end_li.li_utf8_scan_result = utf8_res;
            std::vector<logline> tmp_index;
            auto scan_res = this->lf_format->scan(
                *this, tmp_index, end_li, tmp_sbr, sbc_tmp);
            if (scan_res.is<log_format::scan_match>() && !tmp_index.empty()) {
                auto line_time
                    = tmp_index.back().get_time<std::chrono::microseconds>();

                if (req.is<map_read_lower_bound>()) {
                    auto lb = req.get<map_read_lower_bound>();
                    if (line_time >= lb.mrlb_time) {
                        log_debug("  got lower retval! %s",
                                  lnav::to_rfc3339_string(line_time).c_str());
                        lower_retval = content_map_entry{
                            end_lines_fr,
                            line_time,
                        };
                        if (!best_lower_bound
                            || line_time < best_lower_bound->cme_time)
                        {
                            best_lower_bound = lower_retval;
                        }
                    } else if (lower_retval) {
                        return map_entry_found{lower_retval.value()};
                    } else {
                        // need to move forward
                        time_found = content_map_entry{
                            end_lines_fr,
                            line_time,
                        };
                        peek_sf = string_fragment{};
                        continue;
                    }
                } else {
                    return map_entry_found{content_map_entry{
                        end_lines_fr,
                        line_time,
                    }};
                }
            }
            // log_trace("%s: no match for line, going back",
            // this->lf_filename_as_string.c_str());
            peek_sf = leading;
        }

        log_trace("    no messages found in peek, going back further");
        if (time_found && best_lower_bound
            && end_range.next_offset() >= upper_offset)
        {
            log_info("    lower bound lies in upper half");
            return map_entry_found{best_lower_bound.value()};
        }
        req.match(
            [&](map_read_upper_bound& m) {
                if (end_range.fr_offset < end_range.fr_size
                    || (full_size - end_range.fr_offset) >= MAX_LOOKBACK_SIZE)
                {
                    looping = false;
                } else {
                    // look further back
                    end_range.fr_offset = end_range.fr_offset + peek_sf.sf_end
                        + 1 - end_range.fr_size;
                }
            },
            [&](map_read_lower_bound& m) {
                if (lower_retval) {
                    upper_offset = lower_retval.value().cme_range.fr_offset;
                    log_debug("    first half %lld %s",
                              (upper_offset - lower_offset) / 2,
                              lnav::to_rfc3339_string(lower_retval->cme_time)
                                  .c_str());
                    auto amount = (upper_offset - lower_offset) / 2;
                    end_range.fr_offset = lower_offset + amount;
                    if (end_range.next_offset() > upper_offset) {
                        log_debug("    adjusting end offset");
                        if (end_range.fr_size < upper_offset) {
                            end_range.fr_offset
                                = upper_offset - end_range.fr_size;
                        } else {
                            end_range.fr_offset = 0;
                            end_range.fr_size = upper_offset;
                        }
                    }
                } else if (time_found) {
                    log_debug(
                        "    second half (%lld %lld) %s",
                        end_range.fr_offset,
                        upper_offset,
                        lnav::to_rfc3339_string(time_found.value().cme_time)
                            .c_str());
                    lower_offset = time_found->cme_range.next_offset();
                    end_range.fr_offset
                        = lower_offset + (upper_offset - lower_offset) / 2;
                } else if (end_range.next_offset() <= full_size) {
                    log_debug("    no time found (%lld %lld)",
                              end_range.fr_offset,
                              upper_offset);
                    if (end_range.next_offset() == upper_offset) {
                        upper_offset = end_range.fr_offset;
                    }
                    end_range.fr_offset = upper_offset - end_range.fr_size;
                } else {
                    looping = false;
                }
                if (end_range.next_offset() > full_size) {
                    end_range.fr_offset = full_size - end_range.fr_size;
                }
            });
    } while (looping);

    return map_entry_not_found{};
}

logfile::rebuild_result_t
logfile::build_content_map()
{
    static auto op = lnav_operation{"build_content_map"};

    auto op_guard = lnav_opid_guard::internal(op);

    log_info("%s: trying to build content map",
             this->lf_filename_as_string.c_str());
    if (this->lf_line_buffer.is_compressed()) {
        auto skip_size = file_off_t{512 * 1024};
        auto read_size = file_ssize_t{64 * 1024};
        pattern_locks line_locks;
        scan_batch_context sbc_tmp{
            this->lf_allocator,
            line_locks,
        };

        auto peek_range = file_range{
            0,
            read_size,
        };
        log_info("  file is compressed, doing scan");
        while (true) {
            auto last_peek = peek_range;
            peek_range.fr_offset += skip_size;
            log_debug("    content map peek %lld:%lld",
                      peek_range.fr_offset,
                      peek_range.fr_size);
            auto peek_res = this->lf_line_buffer.peek_range(
                peek_range,
                {
                    line_buffer::peek_options::allow_short_read,
                });
            if (peek_res.isErr()) {
                log_error("    content map peek failed -- %s",
                          peek_res.unwrapErr().c_str());
                break;
            }

            auto buf = peek_res.unwrap();
            if (buf.empty()) {
                if (this->lf_line_buffer.get_file_size() == -1) {
                    log_info("    skipped past end, reversing");
                    skip_size = peek_range.fr_size;
                    peek_range = last_peek;
                    continue;
                }
                log_info("    reached end of file %lld",
                         this->lf_line_buffer.get_file_size());
                break;
            }
            auto buf_sf = to_string_fragment(buf);
            auto split_res = buf_sf.split_pair(string_fragment::tag1{'\n'});
            if (!split_res) {
                log_warning("  cannot find start of line at %lld",
                            peek_range.fr_offset);
                continue;
            }

            auto [_junk, line_start_sf] = split_res.value();
            while (!line_start_sf.empty()) {
                auto utf8_res = is_utf8(line_start_sf, '\n');
                if (!utf8_res.usr_remaining) {
                    log_warning("    cannot find end of line at %lld",
                                peek_range.fr_offset + line_start_sf.sf_begin);
                    break;
                }
                auto line_len = utf8_res.remaining_ptr() - line_start_sf.data();
                shared_buffer tmp_sb;
                shared_buffer_ref tmp_sbr;

                tmp_sbr.share(tmp_sb, line_start_sf.data(), line_len);

                auto map_line_fr = file_range{
                    peek_range.fr_offset + line_start_sf.sf_begin,
                    line_len,
                };
                map_line_fr.fr_metadata.m_has_ansi = utf8_res.usr_has_ansi;
                map_line_fr.fr_metadata.m_valid_utf = utf8_res.is_valid();
                auto map_li = line_info{map_line_fr};
                map_li.li_utf8_scan_result = utf8_res;
                std::vector<logline> tmp_index;
                auto scan_res = this->lf_format->scan(
                    *this, tmp_index, map_li, tmp_sbr, sbc_tmp);
                if (scan_res.is<log_format::scan_match>()) {
                    auto line_time = tmp_index.front()
                                         .get_time<std::chrono::microseconds>();
                    this->lf_content_map.emplace_back(content_map_entry{
                        map_line_fr,
                        line_time,
                    });
                    log_info("  adding content map entry %lld - %s",
                             map_line_fr.fr_offset,
                             lnav::to_rfc3339_string(line_time).c_str());
                    if (skip_size < 1024 * 1024 * 1024) {
                        skip_size *= 2;
                    }
                    break;
                }
                line_start_sf = utf8_res.usr_remaining.value();
            }
        }
    }

    auto retval = rebuild_result_t::NO_NEW_LINES;
    auto full_size = this->get_content_size();

    this->lf_lower_bound_entry = std::nullopt;
    this->lf_upper_bound_entry = std::nullopt;

    log_info("  finding content layout (full_size=%lld)", full_size);
    if (this->lf_options.loo_time_range.has_lower_bound()
        && this->lf_options.loo_time_range.tr_begin
            > this->lf_index.front().get_time<std::chrono::microseconds>()
        && this->lf_options.loo_time_range.tr_begin
            <= this->lf_index.back().get_time<std::chrono::microseconds>())
    {
        auto ll_opt = this->find_from_time(
            to_timeval(this->lf_options.loo_time_range.tr_begin));
        auto ll = ll_opt.value();
        auto first_line_offset = ll->get_offset();
        this->lf_lower_bound_entry = content_map_entry{
            file_range{first_line_offset, full_size - first_line_offset},
            ll->get_time<std::chrono::microseconds>(),
        };
        log_info("  lower bound is within current index, erasing %ld lines",
                 std::distance(this->lf_index.cbegin(), ll));
        this->lf_index_size = first_line_offset;
        this->lf_index.clear();
        retval = rebuild_result_t::NEW_ORDER;
    }

    if (this->lf_index_size == full_size) {
        log_trace("  file has already been scanned, no need to peek");
        const auto& last_line = this->lf_index.back();
        auto last_line_offset = last_line.get_offset();
        this->lf_upper_bound_entry = content_map_entry{
            file_range{last_line_offset, full_size - last_line_offset},
            last_line.get_time<std::chrono::microseconds>(),
        };
        if (this->lf_options.loo_time_range.has_lower_bound()
            && this->lf_options.loo_time_range.tr_begin
                > this->lf_index.back().get_time<std::chrono::microseconds>())
        {
            log_info("  lower bound is past content");
            this->lf_index.clear();
            retval = rebuild_result_t::NEW_ORDER;
        }
        this->lf_file_size_at_map_time = full_size;
        return retval;
    }

    auto end_entry_opt
        = this->find_content_map_entry(full_size, map_read_upper_bound{});
    if (!end_entry_opt.is<map_entry_found>()) {
        log_warning(
            "  skipping content map since the last message could not be "
            "found");
        return retval;
    }

    auto end_entry = end_entry_opt.get<map_entry_found>().mef_entry;
    log_info("  found content end: %llu %s",
             end_entry.cme_range.fr_offset,
             lnav::to_rfc3339_string(to_timeval(end_entry.cme_time)).c_str());
    this->lf_upper_bound_entry = end_entry;
    this->lf_file_size_at_map_time = full_size;

    if (this->lf_options.loo_time_range.has_lower_bound()) {
        if (this->lf_options.loo_time_range.tr_begin > end_entry.cme_time) {
            retval = rebuild_result_t::NEW_ORDER;
        } else if (this->lf_index.empty()
                   || this->lf_options.loo_time_range.tr_begin
                       > this->lf_index.back()
                             .get_time<std::chrono::microseconds>())
        {
            auto offset = full_size / 2;
            log_debug("  searching for lower bound %lld",
                      this->lf_options.loo_time_range.tr_begin.count());
            auto low_entry_opt = this->find_content_map_entry(
                offset,
                map_read_lower_bound{
                    this->lf_options.loo_time_range.tr_begin,
                });
            if (low_entry_opt.is<map_entry_found>()) {
                auto low_entry = low_entry_opt.get<map_entry_found>().mef_entry;
                log_info("  found content start: %llu %s",
                         low_entry.cme_range.fr_offset,
                         lnav::to_rfc3339_string(to_timeval(low_entry.cme_time))
                             .c_str());
                this->lf_lower_bound_entry = low_entry;
                this->lf_index_size = low_entry.cme_range.fr_offset;

                retval = rebuild_result_t::NEW_ORDER;
            }
        }
    }

    if (retval == rebuild_result_t::NEW_ORDER) {
        {
            auto los = this->lf_opids.writeAccess();

            los->los_opid_ranges.clear();
            los->los_sub_in_use.clear();
        }
        {
            auto tids = this->lf_thread_ids.writeAccess();
            tids->ltis_tid_ranges.clear();
        }
        this->lf_pattern_locks.pl_lines.clear();
        this->lf_value_stats.clear();
        this->lf_index.clear();
        this->lf_upper_bound_size = std::nullopt;
    }

    return retval;
}

bool
logfile::in_range() const
{
    if (this->lf_format == nullptr) {
        return true;
    }

    return !this->lf_index.empty() || this->lf_lower_bound_entry.has_value();
}

bool
logfile::exists() const
{
    if (!this->lf_actual_path) {
        return true;
    }

    if (this->lf_options.loo_source == logfile_name_source::ARCHIVE) {
        return true;
    }

    auto stat_res = lnav::filesystem::stat_file(this->lf_actual_path.value());
    if (stat_res.isErr()) {
        log_error("%s: stat failed -- %s",
                  this->lf_actual_path.value().c_str(),
                  stat_res.unwrapErr().c_str());
        return false;
    }

    auto st = stat_res.unwrap();
    return this->lf_stat.st_dev == st.st_dev
        && this->lf_stat.st_ino == st.st_ino;
}

auto
logfile::reset_state() -> void
{
    this->clear_time_offset();
    this->lf_indexing = this->lf_options.loo_is_visible;
}

void
logfile::set_format_base_time(log_format* lf, const line_info& li)
{
    time_t file_time = li.li_timestamp.tv_sec != 0
        ? li.li_timestamp.tv_sec
        : this->lf_line_buffer.get_file_time();

    if (file_time == 0) {
        file_time = this->lf_stat.st_mtime;
    }

    if (!this->lf_cached_base_time
        || this->lf_cached_base_time.value() != file_time)
    {
        tm new_base_tm;
        this->lf_cached_base_time = file_time;
        localtime_r(&file_time, &new_base_tm);
        this->lf_cached_base_tm = new_base_tm;
    }
    lf->lf_date_time.set_base_time(this->lf_cached_base_time.value(),
                                   this->lf_cached_base_tm.value());
}

time_range
logfile::get_content_time_range() const
{
    if (this->lf_format == nullptr || this->lf_index.empty()) {
        return {
            std::chrono::seconds{this->lf_stat.st_ctime},
            std::chrono::seconds{this->lf_stat.st_mtime},
        };
    }

    return {
        this->lf_index.front().get_time<std::chrono::microseconds>(),
        this->lf_index.back().get_time<std::chrono::microseconds>(),
    };
}

bool
logfile::process_prefix(shared_buffer_ref& sbr,
                        const line_info& li,
                        scan_batch_context& sbc)
{
    static auto max_unrecognized_lines
        = injector::get<const lnav::logfile::config&>()
              .lc_max_unrecognized_lines;

    log_format::scan_result_t found = log_format::scan_no_match{};
    size_t prescan_size = this->lf_index.size();
    auto prescan_time = std::chrono::microseconds{0};
    bool retval = false;

    if (this->lf_options.loo_detect_format
        && (this->lf_format == nullptr
            || this->lf_index.size() < RETRY_MATCH_SIZE))
    {
        const auto& root_formats = log_format::get_root_formats();
        std::optional<std::pair<log_format*, log_format::scan_match>>
            best_match;
        size_t scan_count = 0;

        if (!this->lf_index.empty()) {
            prescan_time = this->lf_index[prescan_size - 1]
                               .get_time<std::chrono::microseconds>();
        }
        if (this->lf_format != nullptr) {
            best_match = std::make_pair(
                this->lf_format.get(),
                log_format::scan_match{this->lf_format_quality});
        }

        /*
         * Try each scanner until we get a match.  Fortunately, the formats
         * tend to be sufficiently different that there are few ambiguities...
         */
        log_trace("logfile[%s]: scanning line %zu (offset: %lld; size: %lld)",
                  this->lf_filename_as_string.c_str(),
                  this->lf_index.size(),
                  li.li_file_range.fr_offset,
                  li.li_file_range.fr_size);
        auto starting_index_size = this->lf_index.size();
        size_t prev_index_size = this->lf_index.size();
        pattern_locks line_locks;
        scan_batch_context sbc_tmp{
            this->lf_allocator,
            line_locks,
        };
        sbc_tmp.sbc_value_stats.reserve(64);
        for (const auto& curr : root_formats) {
            if (this->lf_index.size()
                >= curr->lf_max_unrecognized_lines.value_or(
                    max_unrecognized_lines))
            {
                continue;
            }

            if (this->lf_mismatched_formats.count(curr->get_name()) > 0) {
                continue;
            }

            auto match_res = curr->match_name(this->lf_filename_as_string);
            if (match_res.is<log_format::name_mismatched>()) {
                auto nm = match_res.get<log_format::name_mismatched>();
                if (li.li_file_range.fr_offset == 0) {
                    log_debug("(%s) does not match file name: %s",
                              curr->get_name().get(),
                              this->lf_filename_as_string.c_str());
                }
                auto regex_al = attr_line_t(nm.nm_pattern);
                lnav::snippets::regex_highlighter(
                    regex_al, -1, line_range{0, (int) regex_al.length()});
                auto note = attr_line_t("pattern: ")
                                .append(regex_al)
                                .append("\n  ")
                                .append(lnav::roles::quoted_code(
                                    fmt::to_string(this->get_filename())))
                                .append("\n")
                                .append(nm.nm_partial + 2, ' ')
                                .append("^ matched up to here"_snippet_border);
                auto match_um = lnav::console::user_message::info(
                                    attr_line_t()
                                        .append(lnav::roles::identifier(
                                            curr->get_name().to_string()))
                                        .append(" file name pattern required "
                                                "by format does not match"))
                                    .with_note(note)
                                    .move();
                this->lf_format_match_messages.emplace_back(match_um);
                this->lf_mismatched_formats.insert(curr->get_name());
                continue;
            }
            if (this->lf_options.loo_format_name
                && !(curr->get_name()
                     == this->lf_options.loo_format_name.value()))
            {
                if (li.li_file_range.fr_offset == 0) {
                    log_debug("(%s) does not match file format: %s",
                              curr->get_name().get(),
                              fmt::to_string(this->lf_options.loo_file_format)
                                  .c_str());
                }
                continue;
            }

            scan_count += 1;
            curr->clear();
            this->set_format_base_time(curr.get(), li);
            log_format::scan_result_t scan_res{mapbox::util::no_init{}};
            if (this->lf_format != nullptr
                && this->lf_format->lf_root_format == curr.get())
            {
                scan_res = this->lf_format->scan(
                    *this, this->lf_index, li, sbr, sbc);
            } else {
                sbc_tmp.sbc_pattern_locks.pl_lines.clear();
                sbc_tmp.sbc_value_stats.clear();
                sbc_tmp.sbc_opids.los_opid_ranges.clear();
                sbc_tmp.sbc_opids.los_sub_in_use.clear();
                sbc_tmp.sbc_tids.ltis_tid_ranges.clear();
                sbc_tmp.sbc_level_cache = {};
                scan_res = curr->scan(*this, this->lf_index, li, sbr, sbc_tmp);
            }

            scan_res.match(
                [this,
                 &sbc,
                 &sbc_tmp,
                 &found,
                 &curr,
                 &best_match,
                 &prev_index_size,
                 starting_index_size](const log_format::scan_match& sm) {
                    if (best_match && this->lf_format != nullptr
                        && this->lf_format->lf_root_format == curr.get()
                        && best_match->first == this->lf_format.get())
                    {
                        prev_index_size = this->lf_index.size();
                        found = best_match->second;
                    } else if (!best_match
                               || (sm.sm_quality > best_match->second.sm_quality
                                   || (sm.sm_quality
                                           == best_match->second.sm_quality
                                       && sm.sm_strikes
                                           < best_match->second.sm_strikes)))
                    {
                        log_info(
                            "  scan with format (%s) matched with quality of "
                            "%d and %d strikes",
                            curr->get_name().c_str(),
                            sm.sm_quality,
                            sm.sm_strikes);

                        sbc.sbc_opids = sbc_tmp.sbc_opids;
                        sbc.sbc_tids = sbc_tmp.sbc_tids;
                        sbc.sbc_value_stats = sbc_tmp.sbc_value_stats;
                        sbc.sbc_pattern_locks = sbc_tmp.sbc_pattern_locks;
                        auto match_um
                            = lnav::console::user_message::info(
                                  attr_line_t()
                                      .append(lnav::roles::identifier(
                                          curr->get_name().to_string()))
                                      .append(" matched line ")
                                      .append(lnav::roles::number(
                                          fmt::to_string(starting_index_size))))
                                  .with_note(
                                      attr_line_t("match quality is ")
                                          .append(lnav::roles::number(
                                              fmt::to_string(sm.sm_quality)))
                                          .append(" with ")
                                          .append(lnav::roles::number(
                                              fmt::to_string(sm.sm_strikes)))
                                          .append(" strikes"))
                                  .move();
                        this->lf_format_match_messages.emplace_back(match_um);
                        if (best_match) {
                            auto starting_iter = std::next(
                                this->lf_index.begin(), starting_index_size);
                            auto last_iter = std::next(this->lf_index.begin(),
                                                       prev_index_size);
                            this->lf_index.erase(starting_iter, last_iter);
                        }
                        best_match = std::make_pair(curr.get(), sm);
                        prev_index_size = this->lf_index.size();
                    } else {
                        log_trace(
                            "  scan with format (%s) matched, but "
                            "is lower quality (%d < %d) or more strikes (%d "
                            "vs. %d)",
                            curr->get_name().c_str(),
                            sm.sm_quality,
                            best_match->second.sm_quality,
                            sm.sm_strikes,
                            best_match->second.sm_strikes);
                        while (this->lf_index.size() > prev_index_size) {
                            this->lf_index.pop_back();
                        }
                    }
                },
                [curr](const log_format::scan_incomplete& si) {
                    log_trace(
                        "  scan with format (%s) is incomplete, "
                        "more data required",
                        curr->get_name().c_str());
                },
                [this, curr, prescan_size](
                    const log_format::scan_no_match& snm) {
                    if (this->lf_format == nullptr && prescan_size < 5) {
                        log_trace(
                            "  scan with format (%s) does not match -- %s",
                            curr->get_name().c_str(),
                            snm.snm_reason);
                    }
                });
        }

        if (!scan_count) {
            log_info("%s: no formats available to scan, no longer detecting",
                     this->lf_filename_as_string.c_str());
            this->lf_options.loo_detect_format = false;
        }

        if (best_match
            && (this->lf_format == nullptr
                || ((this->lf_format->lf_root_format != best_match->first)
                    && best_match->second.sm_quality
                        > this->lf_format_quality)))
        {
            auto winner = best_match.value();
            auto* curr = winner.first;
            log_info("%s:%zu:log format found -- %s",
                     this->lf_filename_as_string.c_str(),
                     this->lf_index.size(),
                     curr->get_name().get());

            auto match_um = lnav::console::user_message::ok(
                attr_line_t()
                    .append(lnav::roles::identifier(
                        winner.first->get_name().to_string()))
                    .append(" is the best match for line ")
                    .append(lnav::roles::number(
                        fmt::to_string(starting_index_size))));
            this->lf_format_match_messages.emplace_back(match_um);
            this->lf_text_format = text_format_t::TF_LOG;
            this->lf_format = curr->specialized();
            this->lf_level_stats = {};
            for (const auto& ll : this->lf_index) {
                if (ll.is_continued()) {
                    continue;
                }
                this->lf_level_stats.update_msg_count(ll.get_msg_level());
            }
            this->lf_format_quality = winner.second.sm_quality;
            this->set_format_base_time(this->lf_format.get(), li);
            if (this->lf_format->lf_date_time.dts_fmt_lock != -1) {
                this->lf_content_id
                    = hasher().update(sbr.get_data(), sbr.length()).to_string();
            }

            this->lf_applicable_taggers.clear();
            for (auto& td_pair : this->lf_format->lf_tag_defs) {
                bool matches = td_pair.second->ftd_paths.empty();
                for (const auto& pr : td_pair.second->ftd_paths) {
                    if (pr.matches(this->lf_filename_as_string.c_str())) {
                        matches = true;
                        break;
                    }
                }
                if (!matches) {
                    continue;
                }

                log_info("%s: found applicable tag definition /%s/tags/%s",
                         this->lf_filename_as_string.c_str(),
                         this->lf_format->get_name().get(),
                         td_pair.second->ftd_name.c_str());
                this->lf_applicable_taggers.emplace_back(td_pair.second);
            }

            this->lf_applicable_partitioners.clear();
            for (auto& pd_pair : this->lf_format->lf_partition_defs) {
                bool matches = pd_pair.second->fpd_paths.empty();
                for (const auto& pr : pd_pair.second->fpd_paths) {
                    if (pr.matches(this->lf_filename_as_string.c_str())) {
                        matches = true;
                        break;
                    }
                }
                if (!matches) {
                    continue;
                }

                log_info(
                    "%s: found applicable partition definition "
                    "/%s/partitions/%s",
                    this->lf_filename_as_string.c_str(),
                    this->lf_format->get_name().get(),
                    pd_pair.second->fpd_name.c_str());
                this->lf_applicable_partitioners.emplace_back(pd_pair.second);
            }

            /*
             * We'll go ahead and assume that any previous lines were
             * written out at the same time as the last one, so we need to
             * go back and update everything.
             */
            const auto& last_line = this->lf_index.back();

            require_lt(starting_index_size, this->lf_index.size());
            for (size_t lpc = 0; lpc < starting_index_size; lpc++) {
                if (this->lf_format->lf_multiline) {
                    this->lf_index[lpc].set_time(
                        last_line.get_time<std::chrono::microseconds>());
                    if (this->lf_format->lf_structured) {
                        this->lf_index[lpc].set_ignore(true);
                    }
                } else {
                    this->lf_index[lpc].set_time(
                        last_line.get_time<std::chrono::microseconds>());
                    this->lf_index[lpc].set_level(LEVEL_INVALID);
                }
                retval = true;
            }

            found = best_match->second;
        }
    } else if (this->lf_format.get() != nullptr) {
        if (!this->lf_index.empty()) {
            prescan_time = this->lf_index[prescan_size - 1]
                               .get_time<std::chrono::microseconds>();
        }
        /* We've locked onto a format, just use that scanner. */
        found = this->lf_format->scan(*this, this->lf_index, li, sbr, sbc);
    }

    if (found.is<log_format::scan_match>()) {
        if (!this->lf_index.empty()) {
            auto& last_line = this->lf_index.back();

            this->lf_level_stats.update_msg_count(last_line.get_msg_level());
            last_line.set_valid_utf(last_line.is_valid_utf()
                                    && li.li_utf8_scan_result.is_valid());
            last_line.set_has_ansi(last_line.has_ansi()
                                   || li.li_utf8_scan_result.usr_has_ansi);
            if (last_line.get_msg_level() == LEVEL_INVALID) {
                if (this->lf_invalid_lines.ili_lines.size()
                    < invalid_line_info::MAX_INVALID_LINES)
                {
                    this->lf_invalid_lines.ili_lines.push_back(
                        this->lf_index.size() - 1);
                }
                this->lf_invalid_lines.ili_total += 1;
            }
        }
        if (prescan_size > 0 && this->lf_index.size() >= prescan_size
            && prescan_time
                != this->lf_index[prescan_size - 1]
                       .get_time<std::chrono::microseconds>())
        {
            retval = true;
        }
        if (prescan_size > 0 && prescan_size < this->lf_index.size()) {
            auto& second_to_last = this->lf_index[prescan_size - 1];
            auto& latest = this->lf_index[prescan_size];

            if (!second_to_last.is_ignored() && latest < second_to_last) {
                if (this->lf_format->lf_time_ordered) {
                    this->lf_out_of_time_order_count += 1;
                    for (size_t lpc = prescan_size; lpc < this->lf_index.size();
                         lpc++)
                    {
                        auto& line_to_update = this->lf_index[lpc];

                        line_to_update.set_time_skew(true);
                        line_to_update.set_time(
                            second_to_last
                                .get_time<std::chrono::microseconds>());
                    }
                } else {
                    retval = true;
                }
            }
        }
    } else if (found.is<log_format::scan_no_match>()) {
        log_level_t last_level = LEVEL_UNKNOWN;
        auto last_time = this->lf_index_time;
        auto continued = false;

        if (this->lf_format == nullptr && li.li_timestamp.tv_sec != 0) {
            last_time = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::seconds{li.li_timestamp.tv_sec})
                + std::chrono::microseconds(li.li_timestamp.tv_usec);
            last_level = li.li_level;
        } else if (!this->lf_index.empty()) {
            const auto& ll = this->lf_index.back();

            /*
             * Assume this line is part of the previous one(s) and copy the
             * metadata over.
             */
            last_time = ll.get_time<std::chrono::microseconds>();
            if (this->lf_format.get() != nullptr) {
                last_level = ll.get_msg_level();
                continued = true;
            }
        }
        this->lf_index.emplace_back(
            li.li_file_range.fr_offset, last_time, last_level);
        auto& new_line = this->lf_index.back();
        new_line.set_continued(continued);
        new_line.set_valid_utf(li.li_utf8_scan_result.is_valid());
        new_line.set_has_ansi(li.li_utf8_scan_result.usr_has_ansi);
    }

    if (this->lf_format != nullptr
        && this->lf_index.back().get_time<std::chrono::microseconds>()
            > this->lf_options.loo_time_range.tr_end)
    {
        if (!this->lf_upper_bound_size) {
            this->lf_upper_bound_size = this->lf_index.back().get_offset();
            log_debug("%s:%zu: upper found in file found %llu",
                      this->lf_filename_as_string.c_str(),
                      this->lf_index.size(),
                      this->lf_upper_bound_size.value());
        }
        this->lf_index.pop_back();
    }

    return retval;
}

logfile::rebuild_result_t
logfile::rebuild_index(std::optional<ui_clock::time_point> deadline)
{
    static const auto& dts_cfg
        = injector::get<const date_time_scanner_ns::config&>();

    static auto op = lnav_operation{"rebuild_file_index"};
    auto op_guard = lnav_opid_guard::internal(op);

    if (!this->lf_invalidated_opids.empty()) {
        auto writeOpids = this->lf_opids.writeAccess();

        for (auto bm_pair : this->lf_bookmark_metadata) {
            if (bm_pair.second.bm_opid.empty()) {
                continue;
            }

            if (!this->lf_invalidated_opids.contains(bm_pair.second.bm_opid)) {
                continue;
            }

            auto opid_iter
                = writeOpids->los_opid_ranges.find(bm_pair.second.bm_opid);
            if (opid_iter == writeOpids->los_opid_ranges.end()) {
                log_warning("opid not in ranges: %s",
                            bm_pair.second.bm_opid.c_str());
                continue;
            }

            if (bm_pair.first >= this->lf_index.size()) {
                log_warning("stale bookmark: %d", bm_pair.first);
                continue;
            }

            auto& ll = this->lf_index[bm_pair.first];
            opid_iter->second.otr_range.extend_to(
                ll.get_time<std::chrono::microseconds>());
            opid_iter->second.otr_level_stats.update_msg_count(
                ll.get_msg_level());
        }
        this->lf_invalidated_opids.clear();
    }

    if (!this->lf_indexing) {
        if (this->lf_sort_needed) {
            this->lf_sort_needed = false;
            return rebuild_result_t::NEW_ORDER;
        }
        return rebuild_result_t::NO_NEW_LINES;
    }

    if (this->file_options_have_changed()
        || (this->lf_format != nullptr
            && (this->lf_zoned_to_local_state != dts_cfg.c_zoned_to_local
                || this->lf_format->format_changed())))
    {
        log_info("%s: format has changed, rebuilding",
                 this->lf_filename_as_string.c_str());
        this->lf_index.clear();
        this->lf_index_size = 0;
        this->lf_partial_line = false;
        this->lf_longest_line = 0;
        this->lf_sort_needed = true;
        this->lf_pattern_locks.pl_lines.clear();
        this->lf_value_stats.clear();
        {
            safe::WriteAccess<safe_opid_state> writable_opid_map(
                this->lf_opids);

            writable_opid_map->los_opid_ranges.clear();
            writable_opid_map->los_sub_in_use.clear();
        }
        {
            auto tids = this->lf_thread_ids.writeAccess();

            tids->ltis_tid_ranges.clear();
        }
        this->lf_allocator.reset();
        if (this->lf_logline_observer) {
            this->lf_logline_observer->logline_clear(*this);
        }
    }
    this->lf_zoned_to_local_state = dts_cfg.c_zoned_to_local;

    auto retval = rebuild_result_t::NO_NEW_LINES;
    struct stat st;

    this->lf_activity.la_polls += 1;

    if (fstat(this->lf_line_buffer.get_fd(), &st) == -1) {
        if (errno == EINTR) {
            return rebuild_result_t::NO_NEW_LINES;
        }
        return rebuild_result_t::INVALID;
    }

    const auto is_truncated = st.st_size < this->lf_stat.st_size;
    const auto is_user_provided_and_rewritten = (
        // files from other sources can have their mtimes monkeyed with
        this->lf_options.loo_source == logfile_name_source::USER
        && this->lf_stat.st_size == st.st_size
        && this->lf_stat.st_mtime != st.st_mtime);

    // Check the previous stat against the last to see if things are wonky.
    if (this->lf_named_file && (is_truncated || is_user_provided_and_rewritten))
    {
        auto is_overwritten = true;
        if (this->lf_format != nullptr) {
            const auto first_line = this->lf_index.begin();
            const auto first_line_range
                = this->get_file_range(first_line, false);
            auto read_res = this->read_range(first_line_range);
            if (read_res.isOk()) {
                auto sbr = read_res.unwrap();
                if (first_line->has_ansi()) {
                    sbr.erase_ansi();
                }
                auto curr_content_id
                    = hasher().update(sbr.get_data(), sbr.length()).to_string();

                log_info(
                    "%s: overwrite content_id double check: old:%s; now:%s",
                    this->lf_filename_as_string.c_str(),
                    this->lf_content_id.c_str(),
                    curr_content_id.c_str());
                if (this->lf_content_id == curr_content_id) {
                    is_overwritten = false;
                }
            } else {
                auto errmsg = read_res.unwrapErr();
                log_error("unable to read first line for overwrite check: %s",
                          errmsg.c_str());
            }
        }

        if (is_truncated || is_overwritten) {
            log_info("overwritten file detected, closing -- %s  new: %" PRId64
                     "/%" PRId64 "  old: %" PRId64 "/%" PRId64,
                     this->lf_filename_as_string.c_str(),
                     st.st_size,
                     st.st_mtime,
                     this->lf_stat.st_size,
                     this->lf_stat.st_mtime);
            this->close();
            return rebuild_result_t::NO_NEW_LINES;
        }
    }

    if (this->lf_text_format == text_format_t::TF_BINARY) {
        this->lf_index_size = st.st_size;
        this->lf_stat = st;
    } else if (this->lf_upper_bound_size) {
        this->lf_index_size = this->get_content_size();
        this->lf_stat = st;
    } else if (this->lf_line_buffer.is_data_available(this->lf_index_size,
                                                      st.st_size))
    {
        this->lf_activity.la_reads += 1;

        // We haven't reached the end of the file.  Note that we use the
        // line buffer's notion of the file size since it may be compressed.
        bool has_format = this->lf_format.get() != nullptr;
        struct rusage begin_rusage;
        file_off_t off;
        size_t begin_size = this->lf_index.size();
        bool record_rusage = this->lf_index.size() == 1;
        off_t begin_index_size = this->lf_index_size;
        size_t rollback_size = 0, rollback_index_start = 0;

        if (record_rusage) {
            getrusage(RUSAGE_SELF, &begin_rusage);
        }

        if (begin_size == 0 && !has_format) {
            log_debug("scanning file... fd(%d) %s",
                      this->lf_line_buffer.get_fd(),
                      this->lf_filename_as_string.c_str());
        }

        if (!this->lf_index.empty()) {
            off = this->lf_index.back().get_offset();

            /*
             * Drop the last line we read since it might have been a partial
             * read.
             */
            while (this->lf_index.back().get_sub_offset() != 0) {
                this->lf_index.pop_back();
                rollback_size += 1;
            }
            this->lf_index.pop_back();
            rollback_index_start = this->lf_index.size();
            rollback_size += 1;

            if (!this->lf_index.empty()) {
                auto last_line = std::prev(this->lf_index.end());
                if (last_line != this->lf_index.begin()) {
                    auto prev_line = std::prev(last_line);
                    this->lf_line_buffer.flush_at(prev_line->get_offset());
                    auto prev_len_res
                        = this->message_byte_length(prev_line, false);

                    auto read_result = this->lf_line_buffer.read_range({
                        prev_line->get_offset(),
                        prev_len_res.mlr_length + 1,
                    });
                    if (read_result.isErr()) {
                        log_info(
                            "overwritten file detected, closing -- %s (%s)",
                            this->lf_filename_as_string.c_str(),
                            read_result.unwrapErr().c_str());
                        this->close();
                        return rebuild_result_t::INVALID;
                    }

                    auto sbr = read_result.unwrap();
                    if (!sbr.to_string_fragment().endswith("\n")) {
                        log_info("overwritten file detected, closing -- %s",
                                 this->lf_filename_as_string.c_str());
                        this->close();
                        return rebuild_result_t::INVALID;
                    }
                } else {
                    this->lf_line_buffer.flush_at(last_line->get_offset());
                }
                auto last_length_res
                    = this->message_byte_length(last_line, false);

                auto read_result = this->lf_line_buffer.read_range({
                    last_line->get_offset(),
                    last_length_res.mlr_length,
                });

                if (read_result.isErr()) {
                    log_info("overwritten file detected, closing -- %s (%s)",
                             this->lf_filename_as_string.c_str(),
                             read_result.unwrapErr().c_str());
                    this->close();
                    return rebuild_result_t::INVALID;
                }
            } else {
                this->lf_line_buffer.flush_at(0);
            }
        } else {
            this->lf_line_buffer.flush_at(0);
            off = this->lf_index_size;
        }
        if (this->lf_logline_observer != nullptr) {
            this->lf_logline_observer->logline_restart(*this, rollback_size);
        }

        bool sort_needed = std::exchange(this->lf_sort_needed, false);
        size_t limit = SIZE_MAX;

        if (deadline) {
            if (ui_clock::now() > deadline.value()) {
                if (has_format) {
                    log_warning("with format ran past deadline! -- %s",
                                this->lf_filename_as_string.c_str());
                    limit = 1000;
                } else {
                    limit = 100;
                }
            } else if (this->lf_options.loo_detect_format
                       && (!has_format
                           || (this->lf_options.loo_time_range.has_bounds()
                               && this->lf_file_size_at_map_time == 0)))
            {
                limit = 1000;
            } else {
                limit = 1000 * 1000;
            }
        }
        if (!has_format) {
            log_debug("loading file... %s:%zu",
                      this->lf_filename_as_string.c_str(),
                      begin_size);
        }
        scan_batch_context sbc{this->lf_allocator, this->lf_pattern_locks};
        sbc.sbc_opids.los_opid_ranges.reserve(32);
        sbc.sbc_tids.ltis_tid_ranges.reserve(8);
        auto prev_range = file_range{off};
        while (limit > 0) {
            auto load_result = this->lf_line_buffer.load_next_line(prev_range);
            if (load_result.isErr()) {
                log_error("%s: load next line failure -- %s",
                          this->lf_filename_as_string.c_str(),
                          load_result.unwrapErr().c_str());
                this->close();
                return rebuild_result_t::INVALID;
            }

            auto li = load_result.unwrap();
            if (li.li_file_range.empty()) {
                break;
            }
            prev_range = li.li_file_range;

            auto read_result
                = this->lf_line_buffer.read_range(li.li_file_range);
            if (read_result.isErr()) {
                log_error("%s:read failure -- %s",
                          this->lf_filename_as_string.c_str(),
                          read_result.unwrapErr().c_str());
                this->close();
                return rebuild_result_t::INVALID;
            }

            auto sbr = read_result.unwrap();

            if (this->lf_format == nullptr
                && !this->lf_options.loo_non_utf_is_visible
                && !li.li_utf8_scan_result.is_valid())
            {
                log_info("file is not utf, hiding: %s",
                         this->lf_filename_as_string.c_str());
                this->lf_indexing = false;
                this->lf_options.loo_is_visible = false;
                attr_line_t hex;
                attr_line_builder alb(hex);
                alb.append_as_hexdump(sbr.to_string_fragment());
                auto snip = lnav::console::snippet::from(
                    source_location{
                        intern_string::lookup(this->lf_filename),
                        (int) this->lf_index.size() + 1,
                    },
                    hex);
                auto note_um
                    = lnav::console::user_message::warning(
                          attr_line_t("skipping indexing for ")
                              .append_quoted(this->lf_filename))
                          .with_reason("File contains invalid UTF-8")
                          .with_note(
                              attr_line_t(li.li_utf8_scan_result.usr_message)
                                  .append(" at line ")
                                  .append(lnav::roles::number(fmt::to_string(
                                      this->lf_index.size() + 1)))
                                  .append(" column ")
                                  .append(lnav::roles::number(fmt::to_string(
                                      li.li_utf8_scan_result.usr_valid_frag
                                          .sf_end))))
                          .with_snippet(snip)
                          .move();
                this->lf_notes.writeAccess()->insert(note_type::not_utf,
                                                     note_um);
                if (this->lf_logfile_observer != nullptr) {
                    this->lf_logfile_observer->logfile_indexing(this, 0, 0);
                }
                break;
            }
            size_t old_size = this->lf_index.size();

            if (old_size == 0 && !this->lf_text_format) {
                auto fr = this->lf_line_buffer.get_available();
                auto avail_data = this->lf_line_buffer.read_range(fr);

                this->lf_text_format
                    = avail_data
                          .map([path = this->get_path(),
                                this](const shared_buffer_ref& avail_sbr)
                                   -> std::optional<text_format_t> {
                              constexpr auto DETECT_LIMIT = 16 * 1024;
                              auto sbr_str = to_string(avail_sbr);
                              if (sbr_str.size() > DETECT_LIMIT) {
                                  sbr_str.resize(DETECT_LIMIT);
                              }

                              if (this->lf_line_buffer.is_piper()) {
                                  auto lines
                                      = string_fragment::from_str(sbr_str)
                                            .split_lines();
                                  for (auto line_iter = lines.rbegin();
                                       // XXX rejigger read_range() for
                                       // multi-line reads
                                       std::next(line_iter) != lines.rend();
                                       ++line_iter)
                                  {
                                      sbr_str.erase(line_iter->sf_begin, 22);
                                  }
                              }
                              auto utf8_res = is_utf8(sbr_str);
                              if (utf8_res.is_valid()
                                  && utf8_res.usr_has_ansi) {
                                  auto new_size = erase_ansi_escapes(sbr_str);
                                  sbr_str.resize(new_size);
                              }
                              return detect_text_format(sbr_str, path);
                          })
                          .unwrapOr(std::nullopt);
                if (this->lf_text_format) {
                    log_debug(
                        "setting text format to %s",
                        fmt::to_string(this->lf_text_format.value()).c_str());
                    switch (this->lf_text_format.value()) {
                        case text_format_t::TF_DIFF:
                        case text_format_t::TF_MAN:
                        case text_format_t::TF_MARKDOWN:
                            log_debug(
                                "  file is text, disabling log format "
                                "detection");
                            this->lf_options.loo_detect_format = false;
                            break;
                        default:
                            break;
                    }
                }
            }

            if (!li.li_utf8_scan_result.is_valid()) {
                log_warning(
                    "%s: invalid UTF-8 detected at L%zu:C%d/%lld (O:%lld) -- "
                    "%s",
                    this->lf_filename_as_string.c_str(),
                    this->lf_index.size() + 1,
                    li.li_utf8_scan_result.usr_valid_frag.sf_end,
                    li.li_file_range.fr_size,
                    li.li_file_range.fr_offset,
                    li.li_utf8_scan_result.usr_message);
                if (lnav_log_level <= lnav_log_level_t::TRACE) {
                    attr_line_t al;
                    attr_line_builder alb(al);
                    alb.append_as_hexdump(
                        sbr.to_string_fragment().sub_range(0, 256));
                    log_warning("  dump: %s", al.al_string.c_str());
                }
            }

            sbr.rtrim(is_line_ending);

            if (li.li_utf8_scan_result.is_valid()
                && li.li_utf8_scan_result.usr_has_ansi)
            {
                sbr.erase_ansi();
            }

            this->lf_longest_line
                = std::max(this->lf_longest_line,
                           li.li_utf8_scan_result.usr_column_width_guess);
            this->lf_partial_line = li.li_partial;
            sort_needed = this->process_prefix(sbr, li, sbc) || sort_needed;

            if (old_size > this->lf_index.size()) {
                old_size = 0;
            }

            // Update this early so that line_length() works
            this->lf_index_size = li.li_file_range.next_offset();

            if (this->lf_logline_observer != nullptr) {
                auto nl_rc = this->lf_logline_observer->logline_new_lines(
                    *this, this->begin() + old_size, this->end(), sbr);
                if (rollback_size > 0 && old_size == rollback_index_start
                    && nl_rc)
                {
                    log_debug(
                        "%s: rollbacked line %zu matched filter, forcing "
                        "full sort",
                        this->lf_filename_as_string.c_str(),
                        rollback_index_start);
                    sort_needed = true;
                }
            }

            if (this->lf_logfile_observer != nullptr) {
                auto indexing_res = this->lf_logfile_observer->logfile_indexing(
                    this,
                    this->lf_line_buffer.get_read_offset(
                        li.li_file_range.next_offset()),
                    this->get_content_size());

                if (indexing_res == lnav::progress_result_t::interrupt) {
                    break;
                }
            }

            if (!has_format && this->lf_format != nullptr) {
                break;
            }
            if (begin_size == 0 && !has_format
                && li.li_file_range.fr_offset > 16 * 1024)
            {
                break;
            }
#if 0
            if (this->lf_line_buffer.is_likely_to_flush(prev_range)
                && this->lf_index.size() - begin_size > 1)
            {
                log_debug("likely to flush, breaking");
                break;
            }
#endif
            if (this->lf_format) {
                auto sf = sbr.to_string_fragment();

                for (const auto& td : this->lf_applicable_taggers) {
                    auto curr_ll = this->end() - 1;

                    if (td->ftd_level != LEVEL_UNKNOWN
                        && td->ftd_level != curr_ll->get_msg_level())
                    {
                        continue;
                    }

                    if (td->ftd_pattern.pp_value
                            ->find_in(sf, PCRE2_NO_UTF_CHECK)
                            .ignore_error()
                            .has_value())
                    {
                        while (curr_ll->is_continued()) {
                            --curr_ll;
                        }
                        curr_ll->set_meta_mark(true);
                        auto line_number = static_cast<uint32_t>(
                            std::distance(this->begin(), curr_ll));

                        this->lf_bookmark_metadata[line_number].add_tag(
                            td->ftd_name);
                    }
                }

                for (const auto& pd : this->lf_applicable_partitioners) {
                    thread_local auto part_md
                        = lnav::pcre2pp::match_data::unitialized();

                    auto curr_ll = this->end() - 1;

                    if (pd->fpd_level != LEVEL_UNKNOWN
                        && pd->fpd_level != curr_ll->get_msg_level())
                    {
                        continue;
                    }

                    auto match_res = pd->fpd_pattern.pp_value->capture_from(sf)
                                         .into(part_md)
                                         .matches(PCRE2_NO_UTF_CHECK)
                                         .ignore_error();
                    if (match_res) {
                        while (curr_ll->is_continued()) {
                            --curr_ll;
                        }
                        curr_ll->set_meta_mark(true);
                        auto line_number = static_cast<uint32_t>(
                            std::distance(this->begin(), curr_ll));

                        this->lf_bookmark_metadata[line_number].bm_name
                            = part_md.to_string();
                    }
                }

                if (!this->back().is_continued()) {
                    lnav::log::watch::eval_with(*this, this->end() - 1);
                }
            }

            if (li.li_partial) {
                // The last read was at the end of the file, so break.  We'll
                // need to cycle back around to pop off this partial line in
                // order to continue reading correctly.
                break;
            }

            if (this->lf_upper_bound_size) {
                break;
            }

            limit -= 1;
        }

        if (this->lf_format == nullptr
            && this->lf_options.loo_visible_size_limit > 0
            && prev_range.fr_offset > 256 * 1024
            && st.st_size >= this->lf_options.loo_visible_size_limit)
        {
            log_info("file has unknown format and is too large: %s",
                     this->lf_filename_as_string.c_str());
            this->lf_indexing = false;
            auto note_um
                = lnav::console::user_message::warning(
                      "skipping indexing for file")
                      .with_reason(
                          "file is large and has no discernible log format")
                      .move();
            this->lf_notes.writeAccess()->insert(note_type::indexing_disabled,
                                                 note_um);
            if (this->lf_logfile_observer != nullptr) {
                this->lf_logfile_observer->logfile_indexing(this, 0, 0);
            }
        }

        if (this->lf_logline_observer != nullptr) {
            this->lf_logline_observer->logline_eof(*this);
        }

        if (record_rusage
            && (prev_range.fr_offset - begin_index_size) > (500 * 1024))
        {
            rusage end_rusage;

            getrusage(RUSAGE_SELF, &end_rusage);
            rusagesub(end_rusage,
                      begin_rusage,
                      this->lf_activity.la_initial_index_rusage);
            log_info("Resource usage for initial indexing of file: %s:%zu-%zu",
                     this->lf_filename_as_string.c_str(),
                     begin_size,
                     this->lf_index.size());
            log_rusage(lnav_log_level_t::INFO,
                       this->lf_activity.la_initial_index_rusage);
        }

        /*
         * The file can still grow between the above fstat and when we're
         * doing the scanning, so use the line buffer's notion of the file
         * size.
         */
        this->lf_index_size = prev_range.next_offset();
        this->lf_stat = st;

        this->lf_value_stats.resize(sbc.sbc_value_stats.size());
        for (size_t lpc = 0; lpc < sbc.sbc_value_stats.size(); lpc++) {
            this->lf_value_stats[lpc].merge(sbc.sbc_value_stats[lpc]);
        }
        {
            safe::WriteAccess<safe_opid_state> writable_opid_map(
                this->lf_opids);

            for (const auto& opid_pair : sbc.sbc_opids.los_opid_ranges) {
                auto opid_iter
                    = writable_opid_map->los_opid_ranges.find(opid_pair.first);

                if (opid_iter == writable_opid_map->los_opid_ranges.end()) {
                    writable_opid_map->los_opid_ranges.emplace(opid_pair);
                } else {
                    opid_iter->second |= opid_pair.second;
                }
            }
            log_debug(
                "%s: opid_map size: count=%zu; sizeof(otr)=%zu; alloc=%zu",
                this->lf_filename_as_string.c_str(),
                writable_opid_map->los_opid_ranges.size(),
                sizeof(opid_time_range),
                this->lf_allocator.getNumBytesAllocated());
        }
        {
            auto tids = this->lf_thread_ids.writeAccess();

            for (const auto& tid_pair : sbc.sbc_tids.ltis_tid_ranges) {
                auto tid_iter = tids->ltis_tid_ranges.find(tid_pair.first);
                if (tid_iter == tids->ltis_tid_ranges.end()) {
                    tids->ltis_tid_ranges.emplace(tid_pair);
                } else {
                    tid_iter->second |= tid_pair.second;
                }
            }
            log_debug("%s: tid_map size: count=%zu; sizeof(otr)=%zu; alloc=%zu",
                      this->lf_filename_as_string.c_str(),
                      tids->ltis_tid_ranges.size(),
                      sizeof(opid_time_range),
                      this->lf_allocator.getNumBytesAllocated());
        }

        if (begin_size > this->lf_index.size()) {
            log_info("overwritten file detected, closing -- %s",
                     this->lf_filename_as_string.c_str());
            this->close();
            return rebuild_result_t::INVALID;
        }

        if (sort_needed || begin_size > this->lf_index.size()) {
            retval = rebuild_result_t::NEW_ORDER;
        } else {
            retval = rebuild_result_t::NEW_LINES;
        }

        {
            auto est_rem = this->estimated_remaining_lines();
            if (est_rem > 0) {
                this->lf_index.reserve(this->lf_index.size() + est_rem);
            }
        }

        if (this->lf_format != nullptr
            && this->lf_options.loo_time_range.has_bounds()
            && (this->lf_index.size() >= RETRY_MATCH_SIZE
                || this->lf_index_size == this->get_content_size())
            && this->lf_file_size_at_map_time != this->get_content_size())
        {
            switch (this->build_content_map()) {
                case rebuild_result_t::NEW_ORDER:
                    retval = rebuild_result_t::NEW_ORDER;
                    break;
                default:
                    break;
            }
        }

        for (auto& lvs : this->lf_value_stats) {
            {
                lvs.lvs_tdigest.merge();
                auto p25 = lvs.lvs_tdigest.quantile(25);
                auto p50 = lvs.lvs_tdigest.quantile(50);
                auto p75 = lvs.lvs_tdigest.quantile(75);
                log_debug("stats[] p25=%f p50=%f p75=%f", p25, p50, p75);
            }
        }
    } else {
        this->lf_stat = st;
        if (this->lf_sort_needed) {
            retval = rebuild_result_t::NEW_ORDER;
            this->lf_sort_needed = false;
        }
    }

    this->lf_index_time
        = std::chrono::seconds{this->lf_line_buffer.get_file_time()};
    if (this->lf_index_time.count() == 0) {
        this->lf_index_time = std::chrono::seconds{st.st_mtime};
    }

    if (this->lf_out_of_time_order_count) {
        log_info("Detected %d out-of-time-order lines in file: %s",
                 this->lf_out_of_time_order_count,
                 this->lf_filename_as_string.c_str());
        this->lf_out_of_time_order_count = 0;
    }

    return retval;
}

Result<shared_buffer_ref, std::string>
logfile::read_line(iterator ll, subline_options opts)
{
    try {
        auto get_range_res = this->get_file_range(ll, false);
        return this->lf_line_buffer.read_range(get_range_res)
            .map([&ll, &get_range_res, &opts, this](auto sbr) {
                sbr.rtrim(is_line_ending);
                if (!get_range_res.fr_metadata.m_valid_utf) {
                    scrub_to_utf8(sbr.get_writable_data(), sbr.length());
                    sbr.get_metadata().m_valid_utf = true;
                }

                if (this->lf_format != nullptr) {
                    this->lf_format->get_subline(
                        {this->lf_value_stats, this->lf_pattern_locks},
                        *ll,
                        sbr,
                        opts);
                }

                return sbr;
            });
    } catch (const line_buffer::error& e) {
        return Err(std::error_code{e.e_err, std::generic_category()}.message());
    }
}

Result<logfile::read_file_result, std::string>
logfile::read_file(read_format_t format)
{
    if (this->lf_stat.st_size > line_buffer::MAX_LINE_BUFFER_SIZE) {
        return Err(std::string("file is too large to read"));
    }

    auto retval = read_file_result{};
    retval.rfr_content.reserve(this->lf_stat.st_size);

    if (format == read_format_t::with_framing) {
        retval.rfr_content.append(this->lf_line_buffer.get_piper_header_size(),
                                  '\x16');
    }
    for (auto iter = this->begin(); iter != this->end(); ++iter) {
        const auto fr = this->get_file_range(iter);
        retval.rfr_range.fr_metadata |= fr.fr_metadata;
        retval.rfr_range.fr_size = fr.next_offset();
        auto sbr = TRY(this->lf_line_buffer.read_range(fr));

        if (format == read_format_t::with_framing
            && this->lf_line_buffer.is_piper())
        {
            retval.rfr_content.append(22, '\x16');
        }
        retval.rfr_content.append(sbr.get_data(), sbr.length());
        if ((file_ssize_t) retval.rfr_content.size() < this->lf_stat.st_size) {
            retval.rfr_content.push_back('\n');
        }
    }

    return Ok(std::move(retval));
}

Result<shared_buffer_ref, std::string>
logfile::read_range(const file_range& fr)
{
    return this->lf_line_buffer.read_range(fr);
}

void
logfile::read_full_message(const_iterator ll,
                           shared_buffer_ref& msg_out,
                           line_buffer::scan_direction dir,
                           read_format_t format)
{
    require(ll->get_sub_offset() == 0);

#if 0
    log_debug(
        "%s: reading msg at %d", this->lf_filename_as_string.c_str(), ll->get_offset());
#endif

    msg_out.disown();
    auto mlr = this->message_byte_length(ll);
    auto range_for_line
        = file_range{ll->get_offset(), mlr.mlr_length, mlr.mlr_metadata};
    try {
        if (range_for_line.fr_size > line_buffer::MAX_LINE_BUFFER_SIZE) {
            range_for_line.fr_size = line_buffer::MAX_LINE_BUFFER_SIZE;
        }
        if (format == read_format_t::plain && mlr.mlr_line_count > 1
            && this->lf_line_buffer.is_piper())
        {
            this->lf_plain_msg_shared.invalidate_refs();
            this->lf_plain_msg_buffer.expand_to(mlr.mlr_length);
            this->lf_plain_msg_buffer.clear();
            auto curr_ll = ll;
            do {
                const auto curr_range = this->get_file_range(curr_ll, false);
                auto read_result
                    = this->lf_line_buffer.read_range(curr_range, dir);

                if (curr_ll != ll) {
                    this->lf_plain_msg_buffer.push_back('\n');
                }
                if (read_result.isErr()) {
                    auto errmsg = read_result.unwrapErr();
                    log_error("%s:%zu:unable to read range %lld:%lld -- %s",
                              this->get_unique_path().c_str(),
                              std::distance(this->cbegin(), ll),
                              range_for_line.fr_offset,
                              range_for_line.fr_size,
                              errmsg.c_str());
                    return;
                }

                auto curr_buf = read_result.unwrap();
                this->lf_plain_msg_buffer.append(curr_buf.to_string_view());

                ++curr_ll;
            } while (curr_ll != this->end() && curr_ll->is_continued()
                     && curr_ll->get_sub_offset() == 0);
            msg_out.share(this->lf_plain_msg_shared,
                          this->lf_plain_msg_buffer.data(),
                          this->lf_plain_msg_buffer.size());
        } else {
            auto read_result
                = this->lf_line_buffer.read_range(range_for_line, dir);

            if (read_result.isErr()) {
                auto errmsg = read_result.unwrapErr();
                log_error("%s:%zu:unable to read range %lld:%lld -- %s",
                          this->get_unique_path().c_str(),
                          std::distance(this->cbegin(), ll),
                          range_for_line.fr_offset,
                          range_for_line.fr_size,
                          errmsg.c_str());
                return;
            }
            msg_out = read_result.unwrap();
            msg_out.get_metadata() = range_for_line.fr_metadata;
        }
        if (this->lf_format.get() != nullptr) {
            this->lf_format->get_subline(
                {this->lf_value_stats, this->lf_pattern_locks},
                *ll,
                msg_out,
                {true});
        }
    } catch (const line_buffer::error& e) {
        log_error("failed to read line");
    }
}

void
logfile::set_logline_observer(logline_observer* llo)
{
    this->lf_logline_observer = llo;
    if (llo != nullptr) {
        this->reobserve_from(this->begin());
    }
}

void
logfile::reobserve_from(iterator iter)
{
    for (; iter != this->end(); ++iter) {
        off_t offset = std::distance(this->begin(), iter);

        if (iter->get_sub_offset() > 0) {
            continue;
        }

        if (this->lf_logfile_observer != nullptr) {
            auto indexing_res = this->lf_logfile_observer->logfile_indexing(
                this, offset, this->size());
            if (indexing_res == lnav::progress_result_t::interrupt) {
                break;
            }
        }

        this->read_line(iter).then([this, iter](auto sbr) {
            auto iter_end = iter + 1;

            while (iter_end != this->end() && iter_end->get_sub_offset() != 0) {
                ++iter_end;
            }
            this->lf_logline_observer->logline_new_lines(
                *this, iter, iter_end, sbr);
        });
    }
    if (this->lf_logfile_observer != nullptr) {
        this->lf_logfile_observer->logfile_indexing(
            this, this->size(), this->size());
        this->lf_logline_observer->logline_eof(*this);
    }
}

std::filesystem::path
logfile::get_path() const
{
    return this->lf_filename;
}

const logline_value_stats*
logfile::stats_for_value(intern_string_t name) const
{
    const logline_value_stats* retval = nullptr;
    if (this->lf_format != nullptr) {
        auto index_opt = this->lf_format->stats_index_for_value(name);
        if (index_opt.has_value()) {
            retval = &this->lf_value_stats[index_opt.value()];
        }
    }

    return retval;
}

logfile::message_length_result
logfile::message_byte_length(logfile::const_iterator ll, bool include_continues)
{
    auto next_line = ll;
    file_range::metadata meta;
    file_ssize_t retval;
    size_t line_count = 0;

    if (!include_continues && this->lf_next_line_cache) {
        if (ll->get_offset() == (*this->lf_next_line_cache).first) {
            return {
                (file_ssize_t) this->lf_next_line_cache->second,
                1,
                {ll->is_valid_utf(), ll->has_ansi()},
            };
        }
    }

    do {
        line_count += 1;
        meta.m_has_ansi = meta.m_has_ansi || next_line->has_ansi();
        meta.m_valid_utf = meta.m_valid_utf && next_line->is_valid_utf();
        ++next_line;
    } while ((next_line != this->end())
             && ((ll->get_offset() == next_line->get_offset())
                 || (include_continues && next_line->is_continued())));

    if (next_line == this->end()) {
        if (this->lf_upper_bound_size) {
            retval = this->lf_upper_bound_size.value() - ll->get_offset();
        } else {
            retval = this->lf_index_size - ll->get_offset();
        }
        if (retval > line_buffer::MAX_LINE_BUFFER_SIZE) {
            retval = line_buffer::MAX_LINE_BUFFER_SIZE;
        }
        if (retval > 0 && !this->lf_partial_line) {
            retval -= 1;
        }
    } else {
        retval = next_line->get_offset() - ll->get_offset() - 1;
        if (!include_continues) {
            this->lf_next_line_cache
                = std::make_optional(std::make_pair(ll->get_offset(), retval));
        }
    }

    require_ge(retval, 0);

    return {retval, line_count, meta};
}

Result<shared_buffer_ref, std::string>
logfile::read_raw_message(logfile::const_iterator ll)
{
    require(ll->get_sub_offset() == 0);

    return this->lf_line_buffer.read_range(this->get_file_range(ll));
}

intern_string_t
logfile::get_format_name() const
{
    if (this->lf_format) {
        return this->lf_format->get_name();
    }

    return {};
}

std::optional<logfile::const_iterator>
logfile::find_from_time(const timeval& tv) const
{
    auto retval
        = std::lower_bound(this->lf_index.begin(), this->lf_index.end(), tv);
    if (retval == this->lf_index.end()) {
        return std::nullopt;
    }

    return retval;
}

bool
logfile::mark_as_duplicate(const std::string& name)
{
    safe::WriteAccess<safe_notes> notes(this->lf_notes);

    if (notes->contains(note_type::duplicate)) {
        return false;
    }

    this->lf_indexing = false;
    this->lf_options.loo_is_visible = false;
    auto note_um
        = lnav::console::user_message::warning("hiding duplicate file")
              .with_reason(
                  attr_line_t("this file appears to have the same content as ")
                      .append(lnav::roles::file(name)))
              .move();
    notes->insert(note_type::duplicate, note_um);
    return true;
}

void
logfile::adjust_content_time(int line, const timeval& tv, bool abs_offset)
{
    if (this->lf_time_offset == tv) {
        return;
    }

    auto old_time = this->lf_time_offset;

    this->lf_time_offset_line = line;
    if (abs_offset) {
        this->lf_time_offset = tv;
    } else {
        timeradd(&old_time, &tv, &this->lf_time_offset);
    }
    for (auto& iter : *this) {
        timeval curr, diff, new_time;

        curr = iter.get_timeval();
        timersub(&curr, &old_time, &diff);
        timeradd(&diff, &this->lf_time_offset, &new_time);
        iter.set_time(new_time);
    }
    this->lf_sort_needed = true;
    this->lf_index_generation += 1;
}

std::filesystem::path
logfile::get_path_for_key() const
{
    if (this->lf_options.loo_temp_dev == 0 && this->lf_options.loo_temp_ino == 0
        && this->lf_line_buffer.is_piper())
    {
        return this->lf_actual_path.value_or(this->lf_filename);
    }
    return this->lf_filename;
}

void
logfile::set_filename(const std::string& filename)
{
    if (this->lf_filename != filename) {
        this->lf_filename = filename;
        this->lf_filename_as_string = this->lf_filename.string();
        std::filesystem::path p(filename);
        this->lf_basename = p.filename();
    }
}

time_t
logfile::get_origin_mtime() const
{
    if (!this->is_valid_filename()) {
        struct stat st;
        if (lnav::filesystem::statp(this->lf_filename, &st) == 0) {
            return st.st_mtime;
        }
    }

    return this->lf_stat.st_mtime;
}

struct timeval
logfile::original_line_time(iterator ll)
{
    if (this->is_time_adjusted()) {
        auto line_time = ll->get_timeval();
        timeval retval;

        timersub(&line_time, &this->lf_time_offset, &retval);
        return retval;
    }

    return ll->get_timeval();
}

std::optional<logfile::const_iterator>
logfile::line_for_offset(file_off_t off) const
{
    struct cmper {
        bool operator()(const file_off_t& lhs, const logline& rhs) const
        {
            return lhs < rhs.get_offset();
        }

        bool operator()(const logline& lhs, const file_off_t& rhs) const
        {
            return lhs.get_offset() < rhs;
        }
    };

    if (this->lf_index.empty()) {
        return std::nullopt;
    }

    auto iter = std::lower_bound(
        this->lf_index.begin(), this->lf_index.end(), off, cmper{});
    if (iter == this->lf_index.end()) {
        if (this->lf_index.back().get_offset() <= off
            && off < this->lf_index_size)
        {
            return std::make_optional(iter);
        }
        return std::nullopt;
    }

    if (off < iter->get_offset() && iter != this->lf_index.begin()) {
        --iter;
    }

    return std::make_optional(iter);
}

void
logfile::dump_stats()
{
    const auto buf_stats = this->lf_line_buffer.consume_stats();

    if (buf_stats.empty()) {
        return;
    }
    log_info("line buffer stats for file: %s",
             this->lf_filename_as_string.c_str());
    log_info("  file_size=%lld", this->lf_line_buffer.get_file_size());
    log_info("  buffer_size=%ld", this->lf_line_buffer.get_buffer_size());
    log_info("  read_hist=[%4u %4u %4u %4u %4u %4u %4u %4u %4u %4u]",
             buf_stats.s_hist[0],
             buf_stats.s_hist[1],
             buf_stats.s_hist[2],
             buf_stats.s_hist[3],
             buf_stats.s_hist[4],
             buf_stats.s_hist[5],
             buf_stats.s_hist[6],
             buf_stats.s_hist[7],
             buf_stats.s_hist[8],
             buf_stats.s_hist[9]);
    log_info("  decompressions=%u", buf_stats.s_decompressions);
    log_info("  preads=%u", buf_stats.s_preads);
    log_info("  requested_preloads=%u", buf_stats.s_requested_preloads);
    log_info("  used_preloads=%u", buf_stats.s_used_preloads);
}

void
logfile::set_logline_opid(uint32_t line_number, string_fragment opid)
{
    if (line_number >= this->lf_index.size()) {
        log_error("invalid line number: %u", line_number);
        return;
    }

    auto bm_iter = this->lf_bookmark_metadata.find(line_number);
    if (bm_iter != this->lf_bookmark_metadata.end()) {
        if (bm_iter->second.bm_opid == opid) {
            return;
        }
    }

    auto write_opids = this->lf_opids.writeAccess();

    if (bm_iter != this->lf_bookmark_metadata.end()
        && !bm_iter->second.bm_opid.empty())
    {
        auto old_opid_iter = write_opids->los_opid_ranges.find(opid);
        if (old_opid_iter != write_opids->los_opid_ranges.end()) {
            this->lf_invalidated_opids.insert(old_opid_iter->first);
        }
    }

    auto& ll = this->lf_index[line_number];
    auto log_us = ll.get_time<std::chrono::microseconds>();
    auto opid_iter = write_opids->insert_op(
        this->lf_allocator, opid, log_us, timestamp_point_of_reference_t::send);
    auto& otr = opid_iter->second;

    otr.otr_level_stats.update_msg_count(ll.get_msg_level());
    ll.merge_bloom_bits(opid.bloom_bits());
    this->lf_bookmark_metadata[line_number].bm_opid = opid.to_string();
}

void
logfile::set_opid_description(string_fragment opid, string_fragment desc)
{
    auto opid_guard = this->lf_opids.writeAccess();

    auto opid_iter = opid_guard->los_opid_ranges.find(opid);
    if (opid_iter == opid_guard->los_opid_ranges.end()) {
        return;
    }
    opid_iter->second.otr_description.lod_index = std::nullopt;
    opid_iter->second.otr_description.lod_elements.clear();
    opid_iter->second.otr_description.lod_elements.insert(0, desc.to_string());
}

void
logfile::clear_logline_opid(uint32_t line_number)
{
    if (line_number >= this->lf_index.size()) {
        return;
    }

    auto iter = this->lf_bookmark_metadata.find(line_number);
    if (iter == this->lf_bookmark_metadata.end()) {
        return;
    }

    if (iter->second.bm_opid.empty()) {
        return;
    }

    auto& ll = this->lf_index[line_number];
    auto opid = std::move(iter->second.bm_opid);
    auto opid_sf = string_fragment::from_str(opid);

    if (iter->second.empty(bookmark_metadata::categories::any)) {
        this->lf_bookmark_metadata.erase(iter);

        auto writeOpids = this->lf_opids.writeAccess();

        auto otr_iter = writeOpids->los_opid_ranges.find(opid_sf);
        if (otr_iter == writeOpids->los_opid_ranges.end()) {
            return;
        }

        if (otr_iter->second.otr_range.tr_begin
                != ll.get_time<std::chrono::microseconds>()
            && otr_iter->second.otr_range.tr_end
                != ll.get_time<std::chrono::microseconds>())
        {
            otr_iter->second.otr_level_stats.update_msg_count(
                ll.get_msg_level(), -1);
            return;
        }

        otr_iter->second.clear();
        this->lf_invalidated_opids.insert(opid_sf);
    }
}

size_t
logfile::estimated_remaining_lines() const
{
    if (this->lf_index.empty() || this->is_compressed()) {
        return 10;
    }

    const auto bytes_per_line = this->lf_index_size / this->lf_index.size();
    if (this->lf_index_size > this->lf_stat.st_size) {
        return 0;
    }
    const auto remaining_bytes = this->lf_stat.st_size - this->lf_index_size;

    return remaining_bytes / bytes_per_line;
}
