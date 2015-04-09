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
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "grep_proc.hh"
#include "line_buffer.hh"

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

class my_sink : public grep_proc_sink {

public:
    my_sink() : ms_finished(false) { };
    
    void grep_match(grep_proc &gp,
		    grep_line_t line,
		    int start,
		    int end) {
	printf("%d:%d:%d\n", (int)line, start, end);
    };

    void grep_capture(grep_proc &gp,
		      grep_line_t line,
		      int start,
		      int end,
		      char *capture) {
	fprintf(stderr, "%d(%d:%d)%s\n", (int)line, start, end, capture);
    };

    void grep_end(grep_proc &gp) {
	this->ms_finished = true;
    };

    bool ms_finished;
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
	my_sink msink;

	grep_proc gp(code, ms);
	
	gp.queue_request();
	gp.start();
	gp.set_sink(&msink);

	while (!msink.ms_finished) {
		vector<struct pollfd> pollfds;

		gp.update_poll_set(pollfds);
		poll(&pollfds[0], pollfds.size(), -1);

	    gp.check_poll_set(pollfds);
	}
    }
    
    return retval;
}
