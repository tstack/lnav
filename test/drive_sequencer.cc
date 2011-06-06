
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <openssl/sha.h>

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
	    size_t len;
	    char *line;
	    
	    if ((line = this->ms_buffer.read_line(this->ms_offset,
						  len)) != NULL) {
		value_out = string(line, len);
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
    int c, retval = EXIT_SUCCESS;
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
	fd_set read_fds;
	int maxfd;

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

	static bookmark_type_t SEQUENCE;
	
	sequence_matcher sm(fc);
	bookmarks bm;
	sequence_sink ss(sm, bm[&SEQUENCE]);
	
	FD_ZERO(&read_fds);
	
	grep_proc gp(code, ms, maxfd, read_fds);
	
	gp.queue_request();
	gp.start();
	gp.set_sink(&ss);

	while (bm[&SEQUENCE].size() == 0) {
	    fd_set rfds = read_fds;
	    
	    select(maxfd + 1, &rfds, NULL, NULL, NULL);

	    gp.check_fd_set(rfds);
	}

	for (bookmark_vector::iterator iter = bm[&SEQUENCE].begin();
	     iter != bm[&SEQUENCE].end();
	     ++iter) {
	    printf("%d\n", (const int)*iter);
	}
    }
    
    return retval;
}
