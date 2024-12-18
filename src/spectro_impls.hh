/**
 * Copyright (c) 2007-2022, Timothy Stack
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

#ifndef lnav_spectro_impls_hh
#define lnav_spectro_impls_hh

#include "log_format.hh"
#include "spectro_source.hh"

class log_spectro_value_source : public spectrogram_value_source {
public:
    log_spectro_value_source(intern_string_t colname);

    void update_stats();

    void spectro_bounds(spectrogram_bounds& sb_out) override;

    void spectro_row(spectrogram_request& sr,
                     spectrogram_row& row_out) override;

    void spectro_mark(textview_curses& tc,
                      std::chrono::microseconds begin_time,
                      std::chrono::microseconds end_time,
                      double range_min,
                      double range_max) override;

    intern_string_t lsvs_colname;
    logline_value_stats lsvs_stats;
    std::chrono::microseconds lsvs_begin_time{0};
    std::chrono::microseconds lsvs_end_time{0};
    bool lsvs_found{false};
};

class db_spectro_value_source : public spectrogram_value_source {
public:
    db_spectro_value_source(std::string colname);

    void update_stats();

    void spectro_bounds(spectrogram_bounds& sb_out) override;

    void spectro_row(spectrogram_request& sr,
                     spectrogram_row& row_out) override;

    void spectro_mark(textview_curses& tc,
                      std::chrono::microseconds begin_time,
                      std::chrono::microseconds end_time,
                      double range_min,
                      double range_max) override
    {
    }

    std::string dsvs_colname;
    logline_value_stats dsvs_stats;
    std::chrono::microseconds dsvs_begin_time{0};
    std::chrono::microseconds dsvs_end_time{0};
    std::optional<size_t> dsvs_column_index;
    std::optional<lnav::console::user_message> dsvs_error_msg;
};

#endif
