
#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <paths.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

#include "piper_proc.hh"

using namespace std;

piper_proc::piper_proc(int pipefd)
    : pp_fd(-1), pp_child(-1)
{
    char piper_tmpname[PATH_MAX];
    const char *tmpdir;

    assert(pipefd >= 0);

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
    this->pp_child = fork();
    switch (this->pp_child) {
    case -1:
	throw error(errno);
    case 0:
	{
	    char buffer[4096];
	    off_t off = 0;
	    int rc;
	    
	    while ((rc = read(pipefd, buffer, sizeof(buffer))) > 0) {
		/* Need to do pwrite here since the fd is used by the main
		 * lnav process as well.
		 */
		int wrc = pwrite(this->pp_fd, buffer, rc, off);

		off += wrc;
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
