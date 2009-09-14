
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "logfile.hh"
#include "grep_proc.hh"
#include "line_buffer.hh"

class my_source : public grep_proc_source {

public:
    logfile *ms_lf;

    my_source(logfile *lf) : ms_lf(lf) { };
    
    size_t grep_lines(void) {
	return this->ms_lf->size();
    };
    
    void grep_value_for_line(int line,
			     std::string &value_out,
			     int pass) {
	value_out = this->ms_lf->read_line(this->ms_lf->begin() + line);
    };
    
};

class my_sink : public grep_proc_sink {

public:

    void grep_match(grep_line_t line, int start, int end) {
	printf("%d - %d:%d\n", (int)line, start, end);
    };
    
};

int main(int argc, char *argv[])
{
    int retval = EXIT_SUCCESS;
    auto_fd fd;

    fd = open("/tmp/gp.err", O_WRONLY|O_CREAT|O_APPEND, 0666);
    dup2(fd, STDERR_FILENO);
    fprintf(stderr, "startup\n");
    
    if (argc < 2) {
	fprintf(stderr, "error: no file given\n");
    }
    else {
	logfile lf(argv[1]);
	lf.rebuild_index();
	my_source ms(&lf);
	my_sink msink;
	grep_proc gp("pnp", ms);

	gp.start();
	gp.set_sink(&msink);

	fd_set read_fds;

	int maxfd = gp.update_fd_set(read_fds);

	while (1) {
	    fd_set rfds = read_fds;
	    select(maxfd + 1, &rfds, NULL, NULL, NULL);

	    gp.check_fd_set(rfds);
	    if (!FD_ISSET(maxfd, &read_fds))
		break;
	}
    }

    return retval;
}
