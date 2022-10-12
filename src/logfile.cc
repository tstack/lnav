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
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>

#include "base/ansi_scrubber.hh"
#include "base/fs_util.hh"
#include "base/injector.hh"
#include "base/string_util.hh"
#include "config.h"
#include "lnav_util.hh"
#include "log.watch.hh"
#include "log_format.hh"
#include "logfile.cfg.hh"
#include "yajlpp/yajlpp_def.hh"

static auto intern_lifetime = intern_string::get_table_lifetime();

static const size_t INDEX_RESERVE_INCREMENT = 1024;

static const typed_json_path_container<line_buffer::header_data>
    file_header_handlers = {
        yajlpp::property_handler("name").for_field(
            &line_buffer::header_data::hd_name),
        yajlpp::property_handler("mtime").for_field(
            &line_buffer::header_data::hd_mtime),
        yajlpp::property_handler("comment").for_field(
            &line_buffer::header_data::hd_comment),
};

Result<std::shared_ptr<logfile>, std::string>
logfile::open(std::string filename, logfile_open_options& loo)
{
    require(!filename.empty());

    auto lf = std::shared_ptr<logfile>(new logfile(std::move(filename), loo));

    memset(&lf->lf_stat, 0, sizeof(lf->lf_stat));
    if (lf->lf_options.loo_fd == -1) {
        char resolved_path[PATH_MAX];

        errno = 0;
        if (realpath(lf->lf_filename.c_str(), resolved_path) == nullptr) {
            return Err(fmt::format(FMT_STRING("realpath({}) failed with: {}"),
                                   lf->lf_filename,
                                   strerror(errno)));
        }

        if (stat(resolved_path, &lf->lf_stat) == -1) {
            return Err(fmt::format(FMT_STRING("stat({}) failed with: {}"),
                                   lf->lf_filename,
                                   strerror(errno)));
        }

        if (!S_ISREG(lf->lf_stat.st_mode)) {
            return Err(fmt::format(FMT_STRING("{} is not a regular file"),
                                   lf->lf_filename,
                                   strerror(errno)));
        }

        if ((lf->lf_options.loo_fd = ::open(resolved_path, O_RDONLY)) == -1) {
            return Err(fmt::format(FMT_STRING("open({}) failed with: {}"),
                                   lf->lf_filename,
                                   strerror(errno)));
        }

        lf->lf_options.loo_fd.close_on_exec();

        log_info("Creating logfile: fd=%d; size=%" PRId64 "; mtime=%" PRId64
                 "; filename=%s",
                 (int) lf->lf_options.loo_fd,
                 (long long) lf->lf_stat.st_size,
                 (long long) lf->lf_stat.st_mtime,
                 lf->lf_filename.c_str());

        lf->lf_actual_path = lf->lf_filename;
        lf->lf_valid_filename = true;
    } else {
        log_perror(fstat(lf->lf_options.loo_fd, &lf->lf_stat));
        lf->lf_named_file = false;
        lf->lf_valid_filename = false;
    }

    if (!lf->lf_options.loo_filename.empty()) {
        lf->set_filename(lf->lf_options.loo_filename);
        lf->lf_valid_filename = false;
    }

    lf->lf_content_id = hasher().update(lf->lf_filename).to_string();
    lf->lf_line_buffer.set_fd(lf->lf_options.loo_fd);
    lf->lf_index.reserve(INDEX_RESERVE_INCREMENT);

    lf->lf_indexing = lf->lf_options.loo_is_visible;

    const auto& hdr = lf->lf_line_buffer.get_header_data();
    if (!hdr.empty()) {
        lf->lf_embedded_metadata["net.zlib.gzip.header"]
            = {text_format_t::TF_JSON, file_header_handlers.to_string(hdr)};
    }

    ensure(lf->invariant());

    return Ok(lf);
}

logfile::logfile(std::string filename, logfile_open_options& loo)
    : lf_filename(std::move(filename)), lf_options(std::move(loo))
{
    this->lf_opids.writeAccess()->reserve(64);
}

logfile::~logfile() {}

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

void
logfile::reset_state()
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

    log_format::scan_result_t found = log_format::SCAN_NO_MATCH;
    size_t prescan_size = this->lf_index.size();
    time_t prescan_time = 0;
    bool retval = false;

    if (this->lf_format.get() != nullptr) {
        if (!this->lf_index.empty()) {
            prescan_time = this->lf_index[prescan_size - 1].get_time();
        }
        /* We've locked onto a format, just use that scanner. */
        found = this->lf_format->scan(*this, this->lf_index, li, sbr, sbc);
    } else if (this->lf_options.loo_detect_format) {
        const auto& root_formats = log_format::get_root_formats();

        /*
         * Try each scanner until we get a match.  Fortunately, the formats
         * tend to be sufficiently different that there are few ambiguities...
         */
        for (auto iter = root_formats.begin();
             iter != root_formats.end() && (found != log_format::SCAN_MATCH);
             ++iter)
        {
            if (this->lf_index.size()
                >= (*iter)->lf_max_unrecognized_lines.value_or(
                    max_unrecognized_lines))
            {
                continue;
            }

            if (this->lf_mismatched_formats.count((*iter)->get_name()) > 0) {
                continue;
            }

            if (!(*iter)->match_name(this->lf_filename)) {
                if (li.li_file_range.fr_offset == 0) {
                    log_debug("(%s) does not match file name: %s",
                              (*iter)->get_name().get(),
                              this->lf_filename.c_str());
                }
                this->lf_mismatched_formats.insert((*iter)->get_name());
                continue;
            }
            if (!(*iter)->match_mime_type(this->lf_options.loo_file_format)) {
                if (li.li_file_range.fr_offset == 0) {
                    log_debug("(%s) does not match file format: %s",
                              (*iter)->get_name().get(),
                              fmt::to_string(this->lf_options.loo_file_format)
                                  .c_str());
                }
                continue;
            }

            (*iter)->clear();
            this->set_format_base_time(iter->get());
            found = (*iter)->scan(*this, this->lf_index, li, sbr, sbc);
            if (found == log_format::SCAN_MATCH) {
#if 0
                require(this->lf_index.size() == 1 ||
                       (this->lf_index[this->lf_index.size() - 2] <
                        this->lf_index[this->lf_index.size() - 1]));
#endif
                log_info("%s:%d:log format found -- %s",
                         this->lf_filename.c_str(),
                         this->lf_index.size(),
                         (*iter)->get_name().get());

                this->lf_text_format = text_format_t::TF_LOG;
                this->lf_format = (*iter)->specialized();
                this->set_format_base_time(this->lf_format.get());
                this->lf_content_id
                    = hasher().update(sbr.get_data(), sbr.length()).to_string();

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

                /*
                 * We'll go ahead and assume that any previous lines were
                 * written out at the same time as the last one, so we need to
                 * go back and update everything.
                 */
                auto& last_line = this->lf_index[this->lf_index.size() - 1];

                for (size_t lpc = 0; lpc < this->lf_index.size() - 1; lpc++) {
                    if (this->lf_format->lf_multiline) {
                        this->lf_index[lpc].set_time(last_line.get_time());
                        this->lf_index[lpc].set_millis(last_line.get_millis());
                    } else {
                        this->lf_index[lpc].set_ignore(true);
                    }
                }
                break;
            }
        }
    }

    switch (found) {
        case log_format::SCAN_MATCH: {
            if (!this->lf_index.empty()) {
                this->lf_index.back().set_valid_utf(li.li_valid_utf);
                this->lf_index.back().set_has_ansi(li.li_has_ansi);
            }
            if (prescan_size > 0 && this->lf_index.size() >= prescan_size
                && prescan_time != this->lf_index[prescan_size - 1].get_time())
            {
                retval = true;
            }
            if (prescan_size > 0 && prescan_size < this->lf_index.size()) {
                auto& second_to_last = this->lf_index[prescan_size - 1];
                auto& latest = this->lf_index[prescan_size];

                if (!second_to_last.is_ignored() && latest < second_to_last) {
                    if (this->lf_format->lf_time_ordered) {
                        this->lf_out_of_time_order_count += 1;
                        for (size_t lpc = prescan_size;
                             lpc < this->lf_index.size();
                             lpc++)
                        {
                            auto& line_to_update = this->lf_index[lpc];

                            line_to_update.set_time_skew(true);
                            line_to_update.set_time(second_to_last.get_time());
                            line_to_update.set_millis(
                                second_to_last.get_millis());
                        }
                    } else {
                        retval = true;
                    }
                }
            }
            break;
        }
        case log_format::SCAN_NO_MATCH: {
            log_level_t last_level = LEVEL_UNKNOWN;
            time_t last_time = this->lf_index_time;
            short last_millis = 0;
            uint8_t last_mod = 0, last_opid = 0;

            if (!this->lf_index.empty()) {
                logline& ll = this->lf_index.back();

                /*
                 * Assume this line is part of the previous one(s) and copy the
                 * metadata over.
                 */
                last_time = ll.get_time();
                last_millis = ll.get_millis();
                if (this->lf_format.get() != nullptr) {
                    last_level = (log_level_t) (ll.get_level_and_flags()
                                                | LEVEL_CONTINUED);
                }
                last_mod = ll.get_module_id();
                last_opid = ll.get_opid();
            }
            this->lf_index.emplace_back(li.li_file_range.fr_offset,
                                        last_time,
                                        last_millis,
                                        last_level,
                                        last_mod,
                                        last_opid);
            this->lf_index.back().set_valid_utf(li.li_valid_utf);
            this->lf_index.back().set_has_ansi(li.li_has_ansi);
            break;
        }
        case log_format::SCAN_INCOMPLETE:
            break;
    }

    return retval;
}

logfile::rebuild_result_t
logfile::rebuild_index(nonstd::optional<ui_clock::time_point> deadline)
{
    if (!this->lf_indexing) {
        if (this->lf_sort_needed) {
            this->lf_sort_needed = false;
            return rebuild_result_t::NEW_ORDER;
        }
        return rebuild_result_t::NO_NEW_LINES;
    }

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
                log_debug("flushing at %d", check_line_off);
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

        bool sort_needed = this->lf_sort_needed;
        this->lf_sort_needed = false;
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
        sbc.sbc_opids.reserve(32);
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

            if (!this->lf_options.loo_non_utf_is_visible && !li.li_valid_utf) {
                log_info("file is not utf, hiding: %s",
                         this->lf_filename.c_str());
                this->lf_indexing = false;
                this->lf_options.loo_is_visible = false;
                this->lf_notes.writeAccess()->emplace(note_type::not_utf,
                                                      "hiding non-UTF-8 file");
                if (this->lf_logfile_observer != nullptr) {
                    this->lf_logfile_observer->logfile_indexing(
                        this->shared_from_this(), 0, 0);
                }
                break;
            }

            size_t old_size = this->lf_index.size();

            if (old_size == 0
                && this->lf_text_format == text_format_t::TF_UNKNOWN)
            {
                file_range fr = this->lf_line_buffer.get_available();
                auto avail_data = this->lf_line_buffer.read_range(fr);

                this->lf_text_format
                    = avail_data
                          .map([path = this->get_path()](
                                   const shared_buffer_ref& avail_sbr)
                                   -> text_format_t {
                              return detect_text_format(
                                  avail_sbr.to_string_fragment(), path);
                          })
                          .unwrapOr(text_format_t::TF_UNKNOWN);
                log_debug("setting text format to %d", this->lf_text_format);
            }
            if (!li.li_valid_utf
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
            sbr.rtrim(is_line_ending);

            if (li.li_valid_utf && li.li_has_ansi) {
                auto tmp_line = sbr.to_string_fragment().to_string();

                scrub_ansi_string(tmp_line, nullptr);
                memcpy(sbr.get_writable_data(),
                       tmp_line.c_str(),
                       tmp_line.length());
                sbr.narrow(0, tmp_line.length());
            }

            this->lf_longest_line
                = std::max(this->lf_longest_line, sbr.length());
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
                    this->shared_from_this(),
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
                if (!this->lf_applicable_taggers.empty()) {
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
                            curr_ll->set_mark(true);
                            while (curr_ll->is_continued()) {
                                --curr_ll;
                            }
                            auto line_number = static_cast<uint32_t>(
                                std::distance(this->begin(), curr_ll));

                            this->lf_bookmark_metadata[line_number].add_tag(
                                td->ftd_name);
                        }
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
            this->lf_notes.writeAccess()->emplace(
                note_type::indexing_disabled,
                "not indexing large file with no discernible log format");
            if (this->lf_logfile_observer != nullptr) {
                this->lf_logfile_observer->logfile_indexing(
                    this->shared_from_this(), 0, 0);
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
            safe::WriteAccess<logfile::safe_opid_map> writable_opid_map(
                this->lf_opids);

            for (const auto& opid_pair : sbc.sbc_opids) {
                auto opid_iter = writable_opid_map->find(opid_pair.first);

                if (opid_iter == writable_opid_map->end()) {
                    writable_opid_map->emplace(opid_pair);
                } else {
                    if (opid_pair.second.otr_begin
                        < opid_iter->second.otr_begin)
                    {
                        opid_iter->second.otr_begin
                            = opid_pair.second.otr_begin;
                    }
                    if (opid_iter->second.otr_end < opid_pair.second.otr_end) {
                        opid_iter->second.otr_end = opid_pair.second.otr_end;
                    }
                }
            }
        }

        if (sort_needed) {
            retval = rebuild_result_t::NEW_ORDER;
        } else {
            retval = rebuild_result_t::NEW_LINES;
        }
    } else if (this->lf_sort_needed) {
        retval = rebuild_result_t::NEW_ORDER;
        this->lf_sort_needed = false;
    }

    this->lf_index_time = this->lf_line_buffer.get_file_time();
    if (!this->lf_index_time) {
        this->lf_index_time = st.st_mtime;
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

Result<std::string, std::string>
logfile::read_file()
{
    if (this->lf_stat.st_size > line_buffer::MAX_LINE_BUFFER_SIZE) {
        return Err(std::string("file is too large to read"));
    }

    auto retval
        = TRY(this->lf_line_buffer.read_range({0, this->lf_stat.st_size}));

    return Ok(to_string(retval));
}

void
logfile::read_full_message(logfile::const_iterator ll,
                           shared_buffer_ref& msg_out,
                           int max_lines)
{
    require(ll->get_sub_offset() == 0);

#if 0
    log_debug(
        "%s: reading msg at %d", this->lf_filename.c_str(), ll->get_offset());
#endif

    msg_out.disown();
    auto range_for_line = this->get_file_range(ll);
    try {
        auto read_result = this->lf_line_buffer.read_range(range_for_line);

        if (read_result.isErr()) {
            log_error("unable to read range %d:%d",
                      range_for_line.fr_offset,
                      range_for_line.fr_size);
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
                this->shared_from_this(), offset, this->size());
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
            this->shared_from_this(), this->size(), this->size());
        this->lf_logline_observer->logline_eof(*this);
    }
}

ghc::filesystem::path
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
            this->lf_next_line_cache = nonstd::make_optional(
                std::make_pair(ll->get_offset(), retval));
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

nonstd::optional<logfile::const_iterator>
logfile::find_from_time(const timeval& tv) const
{
    auto retval
        = std::lower_bound(this->lf_index.begin(), this->lf_index.end(), tv);
    if (retval == this->lf_index.end()) {
        return nonstd::nullopt;
    }

    return retval;
}

void
logfile::mark_as_duplicate(const std::string& name)
{
    this->lf_indexing = false;
    this->lf_options.loo_is_visible = false;
    this->lf_notes.writeAccess()->emplace(
        note_type::duplicate,
        fmt::format(FMT_STRING("hiding duplicate of {}"), name));
}

void
logfile::adjust_content_time(int line, const timeval& tv, bool abs_offset)
{
    struct timeval old_time = this->lf_time_offset;

    this->lf_time_offset_line = line;
    if (abs_offset) {
        this->lf_time_offset = tv;
    } else {
        timeradd(&old_time, &tv, &this->lf_time_offset);
    }
    for (auto& iter : *this) {
        struct timeval curr, diff, new_time;

        curr = iter.get_timeval();
        timersub(&curr, &old_time, &diff);
        timeradd(&diff, &this->lf_time_offset, &new_time);
        iter.set_time(new_time);
    }
    this->lf_sort_needed = true;
}

void
logfile::set_filename(const std::string& filename)
{
    if (this->lf_filename != filename) {
        this->lf_filename = filename;
        ghc::filesystem::path p(filename);
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

nonstd::optional<logfile::const_iterator>
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
        return nonstd::nullopt;
    }

    auto iter = std::lower_bound(
        this->lf_index.begin(), this->lf_index.end(), off, cmper{});
    if (iter == this->lf_index.end()) {
        if (this->lf_index.back().get_offset() <= off
            && off < this->lf_index_size)
        {
            return nonstd::make_optional(iter);
        }
        return nonstd::nullopt;
    }

    if (off < iter->get_offset() && iter != this->lf_index.begin()) {
        --iter;
    }

    return nonstd::make_optional(iter);
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
