/**
 * Copyright (c) 2015, Timothy Stack
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

#ifndef lnav_all_logs_vtab_hh
#define lnav_all_logs_vtab_hh

#include <cstdint>
#include <vector>

#include "log_format.hh"
#include "log_vtab_impl.hh"
#include "logfile.hh"

/**
 * A virtual table that provides access to all log messages from all formats.
 *
 * @feature f0:sql.tables.all_logs
 */
class all_logs_vtab : public log_vtab_impl {
public:
    all_logs_vtab();

    void get_columns(std::vector<vtab_column>& cols) const override;

    void extract(logfile* lf,
                 uint64_t line_number,
                 logline_value_vector& values) override;

    bool next(log_cursor& lc, logfile_sub_source& lss) override;

private:
    logline_value_meta alv_msg_meta;
    logline_value_meta alv_schema_meta;
    logline_value_meta alv_values_meta;
};

#endif  // LNAV_ALL_LOGS_VTAB_HH
