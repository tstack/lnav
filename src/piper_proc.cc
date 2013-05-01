/**
 * @file piper_proc.cc
 */

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <paths.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

#include "piper_proc.hh"
#include "line_buffer.hh"

using namespace std;

piper_proc::piper_proc(int pipefd, bool timestamp, const char *filename)
    : pp_fd(-1), pp_timestamp(timestamp), pp_child(-1)
{
    assert(pipefd >= 0);

    if (filename) {
        if ((this->pp_fd = open(filename, O_RDWR|O_CREAT|O_TRUNC, 0600)) == -1) {
                perror("Unable to open output file for stdin");
                throw error(errno);
        }
    }
    else {
        char piper_tmpname[PATH_MAX];
        const char *tmpdir;

        if ((tmpdir = getenv("TMPDIR")) == NULL) {
            tmpdir = _PATH_VARTMP;
        }
        snprintf(piper_tmpname, sizeof(piper_tmpname),
                 "%s/lnav.piper.XXXXXX",
                 tmpdir);
        if ((this->pp_fd = mkstemp(piper_tmpname)) == -1) {
            throw error(errno);
        }

        unlink(piper_tmpname);
    }

    this->pp_child = fork();
    switch (this->pp_child) {
    case -1:
	throw error(errno);
    case 0:
	{
	    auto_fd infd(pipefd);
	    line_buffer lb;
	    off_t woff = 0;
	    off_t off = 0;
	    char *line;
	    size_t len;

	    lb.set_fd(infd);
	    while ((line = lb.read_line(off, len)) != NULL) {
                int wrc;

	    	if (timestamp) {
	    	    char time_str[64];
	    	    struct timeval tv;
	    	    char ms_str[8];

	    	    gettimeofday(&tv, NULL);
	    	    strftime(time_str, sizeof(time_str), "%FT%T", gmtime(&tv.tv_sec));
	    	    snprintf(ms_str, sizeof(ms_str), ".%03d", tv.tv_usec / 1000);
	    	    strcat(time_str, ms_str);
	    	    strcat(time_str, "Z  ");
	    	    wrc = pwrite(this->pp_fd, time_str, strlen(time_str), woff);
                    if (wrc == -1) {
                        perror("Unable to write to output file for stdin");
                        break;
                    }
                    woff += wrc;
	    	}

	    	line[len] = '\n';
		/* Need to do pwrite here since the fd is used by the main
		 * lnav process as well.
		 */
		wrc = pwrite(this->pp_fd, line, len + 1, woff);
                if (wrc == -1) {
                    perror("Unable to write to output file for stdin");
                    break;
                }
		woff += wrc;
	    }
	}
	exit(0);
	break;
    default:
	break;
    }
}

piper_proc::~piper_proc()
{
    if (this->pp_child > 0) {
	int status;
	
	kill(this->pp_child, SIGTERM);
	while (waitpid(this->pp_child, &status, 0) < 0 && (errno == EINTR)) {
	    ;
	}

	this->pp_child = -1;
    }
}
