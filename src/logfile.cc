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

#include <utility>

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
#include "base/date_time_scanner.cfg.hh"
#include "base/fs_util.hh"
#include "base/injector.hh"
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
#include "yajlpp/yajlpp_def.hh"

using namespace lnav::roles::literals;

static auto intern_lifetime = intern_string::get_table_lifetime();

static constexpr size_t INDEX_RESERVE_INCREMENT = 1024;

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
                                   strerror(errno)));
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
                               strerror(errno)));
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
             lf->lf_filename.c_str());
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
    lf->lf_text_format
        = lf->lf_options.loo_text_format.value_or(text_format_t::TF_UNKNOWN);
    lf->lf_format_match_messages = loo.loo_match_details;

    const auto& hdr = lf->lf_line_buffer.get_header_data();
    if (hdr.valid()) {
        log_info("%s: has header %d", lf->lf_filename.c_str(), hdr.valid());
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
                if (phdr.h_demux_output == lnav::piper::demux_output_t::signal)
                {
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

                    options_hier->foh_generation += 1;
                    auto& coll = options_hier->foh_path_to_collection["/"];
                    coll.foc_pattern_to_options[lf->get_filename()] = fo;
                }
            });
    }

    lf->file_options_have_changed();
    lf->lf_content_id = hasher().update(lf->lf_filename).to_string();

    ensure(lf->invariant());

    return Ok(lf);
}

logfile::logfile(std::filesystem::path filename,
                 const logfile_open_options& loo)
    : lf_filename(std::move(filename)), lf_options(loo)
{
    this->lf_opids.writeAccess()->los_opid_ranges.reserve(64);
}

logfile::~logfile()
{
    log_info("destructing logfile: %s", this->lf_filename.c_str());
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
        auto new_options = options_hier->match(this->get_filename());
        if (this->lf_file_options == new_options) {
            this->lf_file_options_generation = options_hier->foh_generation;
            return false;
        }

        this->lf_file_options = new_options;
        log_info("%s: file options have changed", this->lf_filename.c_str());
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
        && this->lf_stat.st_ino == st.st_ino
        && this->lf_stat.st_size <= st.st_size;
}

auto
logfile::reset_state() -> void
{
    this->clear_time_offset();
    this->lf_indexing = this->lf_options.loo_is_visible;
}

void
logfile::set_format_base_time(log_format* lf)
{
    time_t file_time = this->lf_line_buffer.get_file_time();

    if (file_time == 0) {
        file_time = this->lf_stat.st_mtime;
    }

    if (!this->lf_cached_base_time
        || this->lf_cached_base_time.value() != file_time)
    {
        struct tm new_base_tm;
        this->lf_cached_base_time = file_time;
        localtime_r(&file_time, &new_base_tm);
        this->lf_cached_base_tm = new_base_tm;
    }
    lf->lf_date_time.set_base_time(this->lf_cached_base_time.value(),
                                   this->lf_cached_base_tm.value());
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
        && (this->lf_format == nullptr || this->lf_index.size() < 250))
    {
        const auto& root_formats = log_format::get_root_formats();
        std::optional<std::pair<log_format*, log_format::scan_match>>
            best_match;
        size_t scan_count = 0;

        if (this->lf_format != nullptr) {
            best_match = std::make_pair(
                this->lf_format.get(),
                log_format::scan_match{this->lf_format_quality});
        }

        /*
         * Try each scanner until we get a match.  Fortunately, the formats
         * tend to be sufficiently different that there are few ambiguities...
         */
        log_trace("logfile[%s]: scanning line %d (offset: %d; size: %d)",
                  this->lf_filename.c_str(),
                  this->lf_index.size(),
                  li.li_file_range.fr_offset,
                  li.li_file_range.fr_size);
        auto starting_index_size = this->lf_index.size();
        size_t prev_index_size = this->lf_index.size();
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

            auto match_res = curr->match_name(this->lf_filename);
            if (match_res.is<log_format::name_mismatched>()) {
                auto nm = match_res.get<log_format::name_mismatched>();
                if (li.li_file_range.fr_offset == 0) {
                    log_debug("(%s) does not match file name: %s",
                              curr->get_name().get(),
                              this->lf_filename.c_str());
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
            this->set_format_base_time(curr.get());
            log_format::scan_result_t scan_res{mapbox::util::no_init{}};
            if (this->lf_format != nullptr
                && this->lf_format->lf_root_format == curr.get())
            {
                scan_res = this->lf_format->scan(
                    *this, this->lf_index, li, sbr, sbc);
            } else {
                scan_res = curr->scan(*this, this->lf_index, li, sbr, sbc);
            }

            scan_res.match(
                [this,
                 &found,
                 &curr,
                 &best_match,
                 &prev_index_size,
                 starting_index_size](const log_format::scan_match& sm) {
                    if (best_match && this->lf_format != nullptr
                        && this->lf_format->lf_root_format == curr.get())
                    {
                        prev_index_size = this->lf_index.size();
                        found = best_match->second;
                    } else if (!best_match
                               || sm.sm_quality > best_match->second.sm_quality)
                    {
                        log_info(
                            "  scan with format (%s) matched with quality (%d)",
                            curr->get_name().c_str(),
                            sm.sm_quality);

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
                                              fmt::to_string(sm.sm_quality))))
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
                        log_info(
                            "  scan with format (%s) matched, but "
                            "is low quality (%d)",
                            curr->get_name().c_str(),
                            sm.sm_quality);
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
                [this, curr](const log_format::scan_no_match& snm) {
                    if (this->lf_format == nullptr) {
                        log_trace(
                            "  scan with format (%s) does not match -- %s",
                            curr->get_name().c_str(),
                            snm.snm_reason);
                    }
                });
        }

        if (!scan_count) {
            log_info("%s: no formats available to scan, no longer detecting",
                     this->lf_filename.c_str());
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
            log_info("%s:%d:log format found -- %s",
                     this->lf_filename.c_str(),
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
            this->lf_format_quality = winner.second.sm_quality;
            this->set_format_base_time(this->lf_format.get());
            if (this->lf_format->lf_date_time.dts_fmt_lock != -1) {
                this->lf_content_id
                    = hasher().update(sbr.get_data(), sbr.length()).to_string();
            }

            this->lf_applicable_taggers.clear();
            for (auto& td_pair : this->lf_format->lf_tag_defs) {
                bool matches = td_pair.second->ftd_paths.empty();
                for (const auto& pr : td_pair.second->ftd_paths) {
                    if (pr.matches(this->lf_filename.c_str())) {
                        matches = true;
                        break;
                    }
                }
                if (!matches) {
                    continue;
                }

                log_info("%s: found applicable tag definition /%s/tags/%s",
                         this->lf_filename.c_str(),
                         this->lf_format->get_name().get(),
                         td_pair.second->ftd_name.c_str());
                this->lf_applicable_taggers.emplace_back(td_pair.second);
            }

            this->lf_applicable_partitioners.clear();
            for (auto& pd_pair : this->lf_format->lf_partition_defs) {
                bool matches = pd_pair.second->fpd_paths.empty();
                for (const auto& pr : pd_pair.second->fpd_paths) {
                    if (pr.matches(this->lf_filename.c_str())) {
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
                    this->lf_filename.c_str(),
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
        uint8_t last_mod = 0, last_opid = 0;

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
                last_level = (log_level_t) (ll.get_level_and_flags()
                                            | LEVEL_CONTINUED);
            }
            last_mod = ll.get_module_id();
            last_opid = ll.get_opid();
        }
        this->lf_index.emplace_back(li.li_file_range.fr_offset,
                                    last_time,
                                    last_level,
                                    last_mod,
                                    last_opid);
        this->lf_index.back().set_valid_utf(li.li_utf8_scan_result.is_valid());
        this->lf_index.back().set_has_ansi(li.li_utf8_scan_result.usr_has_ansi);
    }

    return retval;
}

logfile::rebuild_result_t
logfile::rebuild_index(std::optional<ui_clock::time_point> deadline)
{
    static const auto& dts_cfg
        = injector::get<const date_time_scanner_ns::config&>();

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
            opid_iter->second.otr_range.extend_to(ll.get_timeval());
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
                 this->lf_filename.c_str());
        this->lf_index.clear();
        this->lf_index_size = 0;
        this->lf_partial_line = false;
        this->lf_longest_line = 0;
        this->lf_sort_needed = true;
        {
            safe::WriteAccess<logfile::safe_opid_state> writable_opid_map(
                this->lf_opids);

            writable_opid_map->los_opid_ranges.clear();
            writable_opid_map->los_sub_in_use.clear();
        }
        this->lf_allocator.reset();
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
        log_info("overwritten file detected, closing -- %s  new: %" PRId64
                 "/%" PRId64 "  old: %" PRId64 "/%" PRId64,
                 this->lf_filename.c_str(),
                 st.st_size,
                 st.st_mtime,
                 this->lf_stat.st_size,
                 this->lf_stat.st_mtime);
        this->close();
        return rebuild_result_t::NO_NEW_LINES;
    }

    if (this->lf_text_format == text_format_t::TF_BINARY) {
        this->lf_index_size = st.st_size;
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
        size_t rollback_size = 0;

        if (record_rusage) {
            getrusage(RUSAGE_SELF, &begin_rusage);
        }

        if (begin_size == 0 && !has_format) {
            log_debug("scanning file... %s", this->lf_filename.c_str());
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
            rollback_size += 1;

            if (!this->lf_index.empty()) {
                auto last_line = this->lf_index.end();
                --last_line;
                auto check_line_off = last_line->get_offset();
                auto last_length_res
                    = this->message_byte_length(last_line, false);
                log_debug("flushing at %" PRIu64, check_line_off);
                this->lf_line_buffer.flush_at(check_line_off);

                auto read_result = this->lf_line_buffer.read_range({
                    check_line_off,
                    last_length_res.mlr_length,
                });

                if (read_result.isErr()) {
                    log_info("overwritten file detected, closing -- %s (%s)",
                             this->lf_filename.c_str(),
                             read_result.unwrapErr().c_str());
                    this->close();
                    return rebuild_result_t::INVALID;
                }
            } else {
                this->lf_line_buffer.flush_at(0);
            }
        } else {
            this->lf_line_buffer.flush_at(0);
            off = 0;
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
                                this->lf_filename.c_str());
                    limit = 1000;
                } else {
                    limit = 100;
                }
            } else if (!has_format) {
                limit = 1000;
            } else {
                limit = 1000 * 1000;
            }
        }
        if (!has_format) {
            log_debug(
                "loading file... %s:%d", this->lf_filename.c_str(), begin_size);
        }
        scan_batch_context sbc{this->lf_allocator};
        sbc.sbc_opids.los_opid_ranges.reserve(32);
        auto prev_range = file_range{off};
        while (limit > 0) {
            auto load_result = this->lf_line_buffer.load_next_line(prev_range);

            if (load_result.isErr()) {
                log_error("%s: load next line failure -- %s",
                          this->lf_filename.c_str(),
                          load_result.unwrapErr().c_str());
                this->close();
                return rebuild_result_t::INVALID;
            }

            auto li = load_result.unwrap();

            if (li.li_file_range.empty()) {
                break;
            }
            prev_range = li.li_file_range;

            if (this->lf_format == nullptr
                && !this->lf_options.loo_non_utf_is_visible
                && !li.li_utf8_scan_result.is_valid())
            {
                log_info("file is not utf, hiding: %s",
                         this->lf_filename.c_str());
                this->lf_indexing = false;
                this->lf_options.loo_is_visible = false;
                auto utf8_error_um
                    = lnav::console::user_message::error("invalid UTF-8")
                          .with_reason(
                              attr_line_t(li.li_utf8_scan_result.usr_message)
                                  .append(" at line ")
                                  .append(lnav::roles::number(fmt::to_string(
                                      this->lf_index.size() + 1)))
                                  .append(" column ")
                                  .append(lnav::roles::number(fmt::to_string(
                                      li.li_utf8_scan_result.usr_valid_frag
                                          .sf_end))))
                          .move();
                auto note_um = lnav::console::user_message::warning(
                                   "skipping indexing for file")
                                   .with_reason(utf8_error_um)
                                   .move();
                this->lf_notes.writeAccess()->emplace(note_type::not_utf,
                                                      note_um);
                if (this->lf_logfile_observer != nullptr) {
                    this->lf_logfile_observer->logfile_indexing(this, 0, 0);
                }
                break;
            }
            size_t old_size = this->lf_index.size();

            if (old_size == 0
                && this->lf_text_format == text_format_t::TF_UNKNOWN)
            {
                auto fr = this->lf_line_buffer.get_available();
                auto avail_data = this->lf_line_buffer.read_range(fr);

                this->lf_text_format
                    = avail_data
                          .map([path = this->get_path(),
                                this](const shared_buffer_ref& avail_sbr)
                                   -> text_format_t {
                              auto sbr_str = to_string(avail_sbr);

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
                              if (is_utf8(sbr_str).is_valid()) {
                                  auto new_size = erase_ansi_escapes(sbr_str);
                                  sbr_str.resize(new_size);
                              }
                              return detect_text_format(sbr_str, path);
                          })
                          .unwrapOr(text_format_t::TF_UNKNOWN);
                log_debug("setting text format to %s",
                          fmt::to_string(this->lf_text_format).c_str());
                switch (this->lf_text_format) {
                    case text_format_t::TF_DIFF:
                    case text_format_t::TF_MAN:
                    case text_format_t::TF_MARKDOWN:
                        log_debug(
                            "  file is text, disabling log format detection");
                        this->lf_options.loo_detect_format = false;
                        break;
                    default:
                        break;
                }
            }
            if (!li.li_utf8_scan_result.is_valid()
                && this->lf_text_format != text_format_t::TF_MARKDOWN
                && this->lf_text_format != text_format_t::TF_LOG)
            {
                this->lf_text_format = text_format_t::TF_BINARY;
            }

            auto read_result
                = this->lf_line_buffer.read_range(li.li_file_range);
            if (read_result.isErr()) {
                log_error("%s:read failure -- %s",
                          this->lf_filename.c_str(),
                          read_result.unwrapErr().c_str());
                this->close();
                return rebuild_result_t::INVALID;
            }

            auto sbr = read_result.unwrap();

            if (!li.li_utf8_scan_result.is_valid()) {
                attr_line_t al;
                attr_line_builder alb(al);
                log_warning(
                    "%s: invalid UTF-8 detected at L%d:C%d/%d (O:%lld) -- %s",
                    this->lf_filename.c_str(),
                    this->lf_index.size() + 1,
                    li.li_utf8_scan_result.usr_valid_frag.sf_end,
                    li.li_file_range.fr_size,
                    li.li_file_range.fr_offset,
                    li.li_utf8_scan_result.usr_message);
                alb.append_as_hexdump(sbr.to_string_fragment());
                log_warning("  dump: %s", al.al_string.c_str());
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
                this->lf_logline_observer->logline_new_lines(
                    *this, this->begin() + old_size, this->end(), sbr);
            }

            if (this->lf_logfile_observer != nullptr) {
                auto indexing_res = this->lf_logfile_observer->logfile_indexing(
                    this,
                    this->lf_line_buffer.get_read_offset(
                        li.li_file_range.next_offset()),
                    st.st_size);

                if (indexing_res == logfile_observer::indexing_result::BREAK) {
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

            limit -= 1;
        }

        if (this->lf_format == nullptr
            && this->lf_options.loo_visible_size_limit > 0
            && prev_range.fr_offset > 256 * 1024
            && st.st_size >= this->lf_options.loo_visible_size_limit)
        {
            log_info("file has unknown format and is too large: %s",
                     this->lf_filename.c_str());
            this->lf_indexing = false;
            auto note_um
                = lnav::console::user_message::warning(
                      "skipping indexing for file")
                      .with_reason(
                          "file is large and has no discernible log format")
                      .move();
            this->lf_notes.writeAccess()->emplace(note_type::indexing_disabled,
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
            struct rusage end_rusage;

            getrusage(RUSAGE_SELF, &end_rusage);
            rusagesub(end_rusage,
                      begin_rusage,
                      this->lf_activity.la_initial_index_rusage);
            log_info("Resource usage for initial indexing of file: %s:%d-%d",
                     this->lf_filename.c_str(),
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

        {
            safe::WriteAccess<logfile::safe_opid_state> writable_opid_map(
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
                this->lf_filename.c_str(),
                writable_opid_map->los_opid_ranges.size(),
                sizeof(opid_time_range),
                this->lf_allocator.getNumBytesAllocated());
        }

        if (sort_needed) {
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
    } else if (this->lf_sort_needed) {
        retval = rebuild_result_t::NEW_ORDER;
        this->lf_sort_needed = false;
    }

    this->lf_index_time
        = std::chrono::seconds{this->lf_line_buffer.get_file_time()};
    if (this->lf_index_time.count() == 0) {
        this->lf_index_time = std::chrono::seconds{st.st_mtime};
    }

    if (this->lf_out_of_time_order_count) {
        log_info("Detected %d out-of-time-order lines in file: %s",
                 this->lf_out_of_time_order_count,
                 this->lf_filename.c_str());
        this->lf_out_of_time_order_count = 0;
    }

    return retval;
}

Result<shared_buffer_ref, std::string>
logfile::read_line(logfile::iterator ll)
{
    try {
        auto get_range_res = this->get_file_range(ll, false);
        return this->lf_line_buffer.read_range(get_range_res)
            .map([&ll, &get_range_res, this](auto sbr) {
                sbr.rtrim(is_line_ending);
                if (!get_range_res.fr_metadata.m_valid_utf) {
                    scrub_to_utf8(sbr.get_writable_data(), sbr.length());
                    sbr.get_metadata().m_valid_utf = true;
                }

                if (this->lf_format != nullptr) {
                    this->lf_format->get_subline(*ll, sbr);
                }

                return sbr;
            });
    } catch (const line_buffer::error& e) {
        return Err(std::string(strerror(e.e_err)));
    }
}

Result<logfile::read_file_result, std::string>
logfile::read_file()
{
    if (this->lf_stat.st_size > line_buffer::MAX_LINE_BUFFER_SIZE) {
        return Err(std::string("file is too large to read"));
    }

    auto retval = read_file_result{};
    retval.rfr_content.reserve(this->lf_stat.st_size);

    retval.rfr_content.append(this->lf_line_buffer.get_piper_header_size(),
                              '\x16');
    for (auto iter = this->begin(); iter != this->end(); ++iter) {
        const auto fr = this->get_file_range(iter);
        retval.rfr_range.fr_metadata |= fr.fr_metadata;
        retval.rfr_range.fr_size = fr.next_offset();
        auto sbr = TRY(this->lf_line_buffer.read_range(fr));

        if (this->lf_line_buffer.is_piper()) {
            retval.rfr_content.append(22, '\x16');
        }
        retval.rfr_content.append(sbr.get_data(), sbr.length());
        if (retval.rfr_content.size() < this->lf_stat.st_size) {
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
                           line_buffer::scan_direction dir)
{
    require(ll->get_sub_offset() == 0);

#if 0
    log_debug(
        "%s: reading msg at %d", this->lf_filename.c_str(), ll->get_offset());
#endif

    msg_out.disown();
    auto range_for_line = this->get_file_range(ll);
    try {
        if (range_for_line.fr_size > line_buffer::MAX_LINE_BUFFER_SIZE) {
            range_for_line.fr_size = line_buffer::MAX_LINE_BUFFER_SIZE;
        }
        auto read_result = this->lf_line_buffer.read_range(range_for_line, dir);

        if (read_result.isErr()) {
            auto errmsg = read_result.unwrapErr();
            log_error("%s:%d:unable to read range %d:%d -- %s",
                      this->get_unique_path().c_str(),
                      std::distance(this->cbegin(), ll),
                      range_for_line.fr_offset,
                      range_for_line.fr_size,
                      errmsg.c_str());
            return;
        }
        msg_out = read_result.unwrap();
        msg_out.get_metadata() = range_for_line.fr_metadata;
        if (this->lf_format.get() != nullptr) {
            this->lf_format->get_subline(*ll, msg_out, true);
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
            if (indexing_res == logfile_observer::indexing_result::BREAK) {
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

logfile::message_length_result
logfile::message_byte_length(logfile::const_iterator ll, bool include_continues)
{
    auto next_line = ll;
    file_range::metadata meta;
    size_t retval;

    if (!include_continues && this->lf_next_line_cache) {
        if (ll->get_offset() == (*this->lf_next_line_cache).first) {
            return {
                (file_ssize_t) this->lf_next_line_cache->second,
                {ll->is_valid_utf(), ll->has_ansi()},
            };
        }
    }

    do {
        meta.m_has_ansi = meta.m_has_ansi || next_line->has_ansi();
        meta.m_valid_utf = meta.m_valid_utf && next_line->is_valid_utf();
        ++next_line;
    } while ((next_line != this->end())
             && ((ll->get_offset() == next_line->get_offset())
                 || (include_continues && next_line->is_continued())));

    if (next_line == this->end()) {
        retval = this->lf_index_size - ll->get_offset();
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

    return {(file_ssize_t) retval, meta};
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

    const auto iter = notes->find(note_type::duplicate);
    if (iter != notes->end()) {
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
    notes->emplace(note_type::duplicate, note_um);
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

void
logfile::set_filename(const std::string& filename)
{
    if (this->lf_filename != filename) {
        this->lf_filename = filename;
        std::filesystem::path p(filename);
        this->lf_basename = p.filename();
    }
}

struct timeval
logfile::original_line_time(logfile::iterator ll)
{
    if (this->is_time_adjusted()) {
        struct timeval line_time = ll->get_timeval();
        struct timeval retval;

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
    log_info("line buffer stats for file: %s", this->lf_filename.c_str());
    log_info("  file_size=%lld", this->lf_line_buffer.get_file_size());
    log_info("  buffer_size=%ld", this->lf_line_buffer.get_buffer_size());
    log_info("  read_hist=[%4lu %4lu %4lu %4lu %4lu %4lu %4lu %4lu %4lu %4lu]",
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
    log_info("  decompressions=%lu", buf_stats.s_decompressions);
    log_info("  preads=%lu", buf_stats.s_preads);
    log_info("  requested_preloads=%lu", buf_stats.s_requested_preloads);
    log_info("  used_preloads=%lu", buf_stats.s_used_preloads);
}

void
logfile::set_logline_opid(uint32_t line_number, string_fragment opid)
{
    if (line_number >= this->lf_index.size()) {
        log_error("invalid line number: %s", line_number);
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
    auto log_tv = ll.get_timeval();
    auto opid_iter = write_opids->insert_op(this->lf_allocator, opid, log_tv);
    auto& otr = opid_iter->second;

    otr.otr_level_stats.update_msg_count(ll.get_msg_level());
    ll.set_opid(opid.hash());
    this->lf_bookmark_metadata[line_number].bm_opid = opid.to_string();
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
    ll.set_opid(0);
    auto opid = std::move(iter->second.bm_opid);
    auto opid_sf = string_fragment::from_str(opid);

    if (iter->second.empty(bookmark_metadata::categories::any)) {
        this->lf_bookmark_metadata.erase(iter);

        auto writeOpids = this->lf_opids.writeAccess();

        auto otr_iter = writeOpids->los_opid_ranges.find(opid_sf);
        if (otr_iter == writeOpids->los_opid_ranges.end()) {
            return;
        }

        if (otr_iter->second.otr_range.tr_begin != ll.get_timeval()
            && otr_iter->second.otr_range.tr_end != ll.get_timeval())
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
