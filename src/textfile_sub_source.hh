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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __textfile_sub_source_hh
#define __textfile_sub_source_hh

#include <list>

#include "logfile.hh"
#include "textview_curses.hh"

class textfile_sub_source : public text_sub_source {
public:
    typedef std::list<logfile *>::iterator file_iterator;

    textfile_sub_source() { };

    size_t text_line_count()
    {
        size_t retval = 0;

        if (!this->tss_files.empty()) {
            retval = this->current_file()->size();
        }

        return retval;
    };

    void text_value_for_line(textview_curses &tc,
                             int line,
                             std::string &value_out,
                             bool raw = false)
    {
        if (!this->tss_files.empty()) {
            this->current_file()->
            read_line(this->current_file()->begin() + line, value_out);
        }
        else {
            value_out.clear();
        }
    };

    void text_attrs_for_line(textview_curses &tc,
                             int row,
                             string_attrs_t &value_out)
    {
        if (this->current_file() == NULL) {
            return;
        }

        struct line_range lr;

        lr.lr_start = 0;
        lr.lr_end   = -1;
        value_out[lr].insert(make_string_attr("file", this->current_file()));
    };

    logfile *current_file(void) const
    {
        if (this->tss_files.empty()) {
            return NULL;
        }

        return *this->tss_files.begin();
    };

    std::list<logfile *> tss_files;
};
#endif
