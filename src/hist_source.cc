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

#include "config.h"

#include <math.h>
#include <limits.h>

#include "lnav_util.hh"
#include "hist_source.hh"

using namespace std;

hist_source::hist_source()
    : hs_bucket_size(1),
      hs_group_size(100),
      hs_analyzed(false),
      hs_label_source(NULL),
      hs_token_bucket(NULL)
{ }

void hist_source::text_value_for_line(textview_curses &tc,
				      int row,
				      std::string &value_out,
				      bool no_scrub)
{
    int grow = row / (this->buckets_per_group() + 1);
    int brow = row % (this->buckets_per_group() + 1);

    assert(this->hs_analyzed);

    if (brow == 0) {
	unsigned long width;
	vis_line_t    height;
	
	tc.get_dimensions(height, width);
	value_out.insert((unsigned int)0, width, '-');
	
	if (this->hs_label_source != NULL) {
	    this->hs_label_source->hist_label_for_group(grow, value_out);
	}
	this->hs_token_bucket = NULL;
    }
    else {
	bucket_group_t bg = this->hs_group_keys[grow];
	bucket_t::iterator iter;
	int bucket_index;

	bucket_index = brow - 1;
	this->hs_token_bucket = &(this->hs_groups[bg][bucket_index]);
	if (this->hs_label_source != NULL) {
	    this->hs_label_source->
		hist_label_for_bucket((bg * this->hs_group_size) +
				      (bucket_index * this->hs_bucket_size),
				      *this->hs_token_bucket,
				      value_out);
	}
    }
}

void hist_source::text_attrs_for_line(textview_curses &tc,
				      int row,
				      string_attrs_t &value_out)
{
    assert(this->hs_analyzed);

    if (this->hs_token_bucket != NULL) {
	view_colors &vc = view_colors::singleton();
	unsigned long width, avail_width;
	bucket_t::iterator iter;
	vis_line_t         height;
	struct line_range lr;

	tc.get_dimensions(height, width);
	avail_width = width - this->hs_token_bucket->size();
	
	lr.lr_start = 0;
	for (iter = this->hs_token_bucket->begin();
	     iter != this->hs_token_bucket->end();
	     iter++) {
	    double percent = (double)(iter->second - this->hs_min_count) /
		(this->hs_max_count - this->hs_min_count);
	    int amount, attrs;

	    attrs = vc.
		reverse_attrs_for_role(this->get_role_for_type(iter->first));
	    amount = (int)rint(percent * avail_width);
	    if (iter->second == 0.0) {
		amount = 0;
	    }
	    else {
		amount = max(1, amount);
	    }
	    
	    lr.lr_end = lr.lr_start + amount;
	    value_out[lr].insert(make_string_attr("style", attrs));
	    
	    lr.lr_start = lr.lr_end;
	}
    }
}

void hist_source::add_value(unsigned int value,
                            bucket_type_t bt,
                            bucket_count_t amount)
{
    bucket_group_t bg;

    this->hs_analyzed = false;

    bg = bucket_group_t(value / this->hs_group_size);

    bucket_array_t &ba = this->hs_groups[bg];

    if (ba.empty()) {
	ba.resize(this->buckets_per_group());
    }

    bucket_count_t &bc = ba[(value % this->hs_group_size) /
			    this->hs_bucket_size][bt];

    bc += amount;
}

void hist_source::analyze(void)
{
    std::map<bucket_group_t, bucket_array_t>::iterator iter;

    this->hs_group_keys.clear();
    this->hs_min_count = 3.40282347e+38F;
    this->hs_max_count = 0.0;
    for (iter = this->hs_groups.begin();
	 iter != this->hs_groups.end();
	 iter++) {
	bucket_array_t::iterator ba_iter;

	for (ba_iter = iter->second.begin();
	     ba_iter != iter->second.end();
	     ba_iter++) {
	    bucket_count_t     total = 0.0;
	    bucket_t::iterator b_iter;

	    for (b_iter = ba_iter->begin();
		 b_iter != ba_iter->end();
		 b_iter++) {
		if (b_iter->second != 0.0 &&
		    b_iter->second < this->hs_min_count) {
		    this->hs_min_count = b_iter->second;
		}
		total += b_iter->second;
	    }
	    if (total > this->hs_max_count) {
		this->hs_max_count = total;
	    }
	}

	this->hs_group_keys.push_back(iter->first);
    }

    sort(this->hs_group_keys.begin(), this->hs_group_keys.end());

    this->hs_analyzed = true;
}
