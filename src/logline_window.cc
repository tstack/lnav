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

#include "logline_window.hh"

#include "base/ansi_scrubber.hh"
#include "logfile_sub_source.hh"

logline_window::iterator
logline_window::begin()
{
    if (this->lw_start_line < 0_vl) {
        return this->end();
    }

    auto retval = iterator{this->lw_source, this->lw_start_line};
    while (!retval->is_valid() && retval != this->end()) {
        ++retval;
    }

    return retval;
}

logline_window::iterator
logline_window::end()
{
    auto vl = this->lw_end_line;
    while (vl < vis_line_t(this->lw_source.text_line_count())) {
        const auto& line = this->lw_source.find_line(this->lw_source.at(vl));
        if (line->is_message()) {
            break;
        }
        ++vl;
    }

    return {this->lw_source, vl};
}

logline_window::logmsg_info::logmsg_info(logfile_sub_source& lss, vis_line_t vl)
    : li_source(lss), li_line(vl)
{
    if (this->li_line < vis_line_t(this->li_source.text_line_count())) {
        while (true) {
            auto pair_opt = this->li_source.find_line_with_file(vl);
            if (!pair_opt) {
                break;
            }

            auto& [lf, ll] = pair_opt.value();
            if (ll->is_message()) {
                this->li_file = lf.get();
                this->li_logline = ll;
                this->li_line_number
                    = std::distance(this->li_file->begin(), this->li_logline);
                break;
            }
            --vl;
        }
    }
}

bool
logline_window::logmsg_info::is_valid() const
{
    return this->li_file != nullptr;
}

void
logline_window::logmsg_info::next_msg()
{
    this->li_file = nullptr;
    this->li_logline = logfile::iterator{};
    this->li_string_attrs.clear();
    this->li_line_values.clear();

    ++this->li_line;
    while (this->li_line < vis_line_t(this->li_source.text_line_count())) {
        auto pair_opt = this->li_source.find_line_with_file(this->li_line);

        if (!pair_opt) {
            break;
        }

        auto line_pair = pair_opt.value();
        if (line_pair.second->is_message()) {
            this->li_file = line_pair.first.get();
            this->li_logline = line_pair.second;
            this->li_line_number
                = std::distance(this->li_file->begin(), this->li_logline);
            break;
        }
        ++this->li_line;
    }
}

void
logline_window::logmsg_info::prev_msg()
{
    this->li_file = nullptr;
    this->li_logline = logfile::iterator{};
    this->li_string_attrs.clear();
    this->li_line_values.clear();
    while (this->li_line > 0) {
        --this->li_line;
        auto pair_opt = this->li_source.find_line_with_file(this->li_line);

        if (!pair_opt) {
            break;
        }

        auto line_pair = pair_opt.value();
        if (line_pair.second->is_message()) {
            this->li_file = line_pair.first.get();
            this->li_logline = line_pair.second;
            this->li_line_number
                = std::distance(this->li_file->begin(), this->li_logline);
            break;
        }
    }
}

std::optional<bookmark_metadata*>
logline_window::logmsg_info::get_metadata() const
{
    auto line_number = std::distance(this->li_file->begin(), this->li_logline);
    auto& bm = this->li_file->get_bookmark_metadata();
    auto bm_iter = bm.find(line_number);
    if (bm_iter == bm.end()) {
        return std::nullopt;
    }
    return &bm_iter->second;
}

Result<auto_buffer, std::string>
logline_window::logmsg_info::get_line_hash() const
{
    auto fr = this->li_file->get_file_range(this->li_logline, false);
    auto sbr = TRY(this->li_file->read_range(fr));
    auto outbuf = auto_buffer::alloc(3 + hasher::STRING_SIZE);
    outbuf.push_back('v');
    outbuf.push_back('1');
    outbuf.push_back(':');
    hasher line_hasher;
    line_hasher.update(sbr.get_data(), sbr.length())
        .update(this->get_file_line_number())
        .to_string(outbuf);

    return Ok(std::move(outbuf));
}

logline_window::logmsg_info::metadata_edit_guard::~metadata_edit_guard()
{
    auto line_number = std::distance(this->meg_logmsg_info.li_file->begin(),
                                     this->meg_logmsg_info.li_logline);
    auto& bm = this->meg_logmsg_info.li_file->get_bookmark_metadata();
    auto bm_iter = bm.find(line_number);
    if (bm_iter != bm.end()
        && bm_iter->second.empty(bookmark_metadata::categories::any))
    {
        bm.erase(bm_iter);
    }
}

bookmark_metadata&
logline_window::logmsg_info::metadata_edit_guard::operator*()
{
    auto line_number = std::distance(this->meg_logmsg_info.li_file->begin(),
                                     this->meg_logmsg_info.li_logline);
    auto& bm = this->meg_logmsg_info.li_file->get_bookmark_metadata();
    return bm[line_number];
}

size_t
logline_window::logmsg_info::get_line_count() const
{
    size_t retval = 1;
    auto iter = std::next(this->li_logline);
    while (iter != this->li_file->end() && iter->is_continued()) {
        ++iter;
        retval += 1;
    }

    return retval;
}

void
logline_window::logmsg_info::load_msg() const
{
    if (!this->li_string_attrs.empty()) {
        return;
    }

    auto format = this->li_file->get_format();
    this->li_file->read_full_message(this->li_logline,
                                     this->li_line_values.lvv_sbr);
    if (this->li_line_values.lvv_sbr.get_metadata().m_has_ansi) {
        auto* writable_data = this->li_line_values.lvv_sbr.get_writable_data();
        auto str
            = std::string{writable_data, this->li_line_values.lvv_sbr.length()};
        scrub_ansi_string(str, &this->li_string_attrs);
        this->li_line_values.lvv_sbr.get_metadata().m_has_ansi = false;
    }
    format->annotate(this->li_file,
                     std::distance(this->li_file->begin(), this->li_logline),
                     this->li_string_attrs,
                     this->li_line_values,
                     false);

    if (!this->li_line_values.lvv_opid_value) {
        auto bm_opt = this->get_metadata();
        if (bm_opt && !bm_opt.value()->bm_opid.empty()) {
            this->li_line_values.lvv_opid_value = bm_opt.value()->bm_opid;
            this->li_line_values.lvv_opid_provenance
                = logline_value_vector::opid_provenance::user;
        }
    }
}

string_fragment
logline_window::logmsg_info::to_string_fragment(const line_range& lr) const
{
    this->load_msg();

    return this->li_line_values.lvv_sbr.to_string_fragment(lr);
}

logline_window::iterator&
logline_window::iterator::operator++()
{
    this->i_info.next_msg();

    return *this;
}

logline_window::iterator&
logline_window::iterator::operator--()
{
    this->i_info.prev_msg();

    return *this;
}
