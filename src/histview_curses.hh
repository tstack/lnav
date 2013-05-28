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

#ifndef __hist_controller_hh
#define __hist_controller_hh

#include <map>
#include <string>

#include "strong_int.hh"
#include "listview_curses.hh"

STRONG_INT_TYPE(int, bucket_group);
STRONG_INT_TYPE(int, bucket_count);

class hist_data_source {
public:
    virtual ~hist_data_source() { };

    virtual int hist_values(void) = 0;
    virtual void hist_value_for(int index, int &value_out) = 0;
};

class hist_label_source {
public:
    virtual ~hist_label_source() { };

    virtual void hist_label_for_group(int group, std::string &label_out) { };
};

class hist_controller : public list_data_source {
public:
    hist_controller();
    virtual ~hist_controller() { };

    void set_bucket_size(int bs) { this->hv_bucket_size = bs; };
    int get_bucket_size(void) const { return this->hv_bucket_size; };

    void set_group_size(int gs) { this->hv_group_size = gs; };
    int get_group_size(void) const { return this->hv_group_size; };

    void set_data_source(hist_data_source *hds)
    {
        this->hv_data_source = hds;
    };
    hist_data_source *get_data_source(void) { return this->hv_data_source; };

    void set_label_source(hist_label_source *hls)
    {
        this->hv_label_source = hls;
    }

    hist_label_source *get_label_source(void)
    {
        return this->hv_label_source;
    };

    size_t listview_rows(void)
    {
        return (this->hv_group_size / this->hv_bucket_size) *
               this->hv_groups.size();
    };

    void listview_value_for_row(vis_line_t row, std::string &value_out);

    void reload_data(void);

private:
    typedef vector<bucket_count_t> buckets_t;

    map<bucket_group_t, buckets_t> hv_groups;
    int hv_bucket_size; /* hours */
    int hv_group_size;  /* days */
    hist_data_source * hv_data_source;
    hist_label_source *hv_label_source;
};
#endif
