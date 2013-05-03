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

#ifndef __db_sub_source_hh
#define __db_sub_source_hh

#include <string>
#include <vector>

#include "hist_source.hh"

class db_label_source : public hist_source::label_source {
public:
    db_label_source() { };

    ~db_label_source() { };

    void hist_label_for_group(int group, std::string &label_out) {
	label_out.clear();
	for (int lpc = 0; lpc < (int)this->dls_headers.size(); lpc++) {
	    int before, total_fill =
		this->dls_column_sizes[lpc] - this->dls_headers[lpc].length();

	    before = total_fill / 2;
	    total_fill -= before;
	    label_out.append(before, ' ');
	    label_out.append(this->dls_headers[lpc]);
	    label_out.append(total_fill, ' ');
	}
    };
    
    void hist_label_for_bucket(int bucket_start_value,
			       const hist_source::bucket_t &bucket,
			       std::string &label_out) {
	/*
	 * start_value is the result rowid, each bucket type is a column value
	 * label_out should be the raw text output.
	 */

	label_out.clear();
	if (bucket_start_value >= (int)this->dls_rows.size())
	    return;
	for (int lpc = 0; lpc < (int)this->dls_rows[bucket_start_value].size(); lpc++) {
	    label_out.append(this->dls_column_sizes[lpc] - this->dls_rows[bucket_start_value][lpc].length(), ' ');
	    label_out.append(this->dls_rows[bucket_start_value][lpc]);
	}
    };

    void push_column(const char *colstr) {
	int index = this->dls_rows.back().size();
	
	this->dls_rows.back().push_back(colstr);
	if (this->dls_rows.back().size() > this->dls_column_sizes.size()) {
	    this->dls_column_sizes.push_back(1);
	}
	this->dls_column_sizes[index] =
	    std::max(this->dls_column_sizes[index], strlen(colstr) + 1);
    };
    
    void push_header(const std::string &colstr) {
	int index = this->dls_headers.size();
	
	this->dls_headers.push_back(colstr);
	if (this->dls_headers.size() > this->dls_column_sizes.size()) {
	    this->dls_column_sizes.push_back(1);
	}
	this->dls_column_sizes[index] =
	    std::max(this->dls_column_sizes[index], colstr.length() + 1);
    }

    void clear(void) {
        this->dls_headers.clear();
        this->dls_rows.clear();
        this->dls_column_sizes.clear();
    }

    std::vector< std::string > dls_headers;
    std::vector< std::vector< std::string > > dls_rows;
    std::vector< size_t > dls_column_sizes;
};

#endif
