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

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <map>
#include <list>
#include <vector>
#include <algorithm>

#include "pcrepp.hh"
#include "logfile.hh"
#include "sequence_sink.hh"
#include "sequence_matcher.hh"

using namespace std;

class my_source : public grep_proc_source {
    
public:
    my_source(auto_fd &fd) : ms_offset(0) {
	this->ms_buffer.set_fd(fd);
    };

    bool grep_value_for_line(int line_number, string &value_out) {
	bool retval = false;

	try {
        line_value lv;
	    
	    if (this->ms_buffer.read_line(this->ms_offset, lv)) {
		value_out = string(lv.lv_start, lv.lv_len);
		retval = true;
	    }
	}
	catch (line_buffer::error &e) {
	    fprintf(stderr,
		    "error: source buffer error %d %s\n",
		    this->ms_buffer.get_fd(),
		    strerror(e.e_err));
	}
	
	return retval;
    };
    
private:
    line_buffer ms_buffer;
    off_t ms_offset;
    
};


int main(int argc, char *argv[])
{
    int retval = EXIT_SUCCESS;
    const char *errptr;
    auto_fd fd;
    pcre *code;
    int eoff;
    
    if (argc < 3) {
	fprintf(stderr, "error: expecting pattern and file arguments\n");
	retval = EXIT_FAILURE;
    }
    else if ((fd = open(argv[2], O_RDONLY)) == -1) {
	perror("open");
	retval = EXIT_FAILURE;
    }
    else if ((code = pcre_compile(argv[1],
				  PCRE_CASELESS,
				  &errptr,
				  &eoff,
				  NULL)) == NULL) {
      fprintf(stderr, "error: invalid pattern -- %s\n", errptr);
    }
    else {
	my_source ms(fd);

	sequence_matcher::field_col_t fc;
	
	fc.resize(2);
	
	sequence_matcher::field_row_t &frf = fc.front();
	frf.resize(2);
	frf[0] = "eth0";
	frf[1] = "eth0";
	
	sequence_matcher::field_row_t &frb = fc.back();
	frb.resize(2);
	frb[0] = "up";
	frb[1] = "down";

	static bookmark_type_t SEQUENCE("sequence");
	
	sequence_matcher sm(fc);
	vis_bookmarks bm;
	sequence_sink ss(sm, bm[&SEQUENCE]);

	grep_proc gp(code, ms);
	
	gp.queue_request();
	gp.start();
	gp.set_sink(&ss);

	while (bm[&SEQUENCE].size() == 0) {
		vector<struct pollfd> pollfds;

		poll(&pollfds[0], pollfds.size(), -1);

	    gp.check_poll_set(pollfds);
	}

	for (bookmark_vector<vis_line_t>::iterator iter = bm[&SEQUENCE].begin();
	     iter != bm[&SEQUENCE].end();
	     ++iter) {
	    printf("%d\n", (const int)*iter);
	}
    }
    
    return retval;
}
