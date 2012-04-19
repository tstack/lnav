
#include "config.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/wait.h>

#include "grep_proc.hh"

using namespace std;

static struct {
    int l_number;
    const char *l_value;
} MS_LINES[] = {
    { 10, "" },
    { 11, "" },
    { 12, "" },
    { 13, "" },
    { 0, "" },
    { 1, "" },
    { 2, "" },
};

class my_source : public grep_proc_source {

public:
    my_source() : ms_current_line(0) { };

    bool grep_value_for_line(int line_number, string &value_out) {
	bool retval = true;

	assert(line_number == MS_LINES[this->ms_current_line].l_number);
	value_out = MS_LINES[this->ms_current_line].l_value;

	this->ms_current_line += 1;
	
	return retval;
    };

    int ms_current_line;
};

class my_sleeper_source : public grep_proc_source {
    bool grep_value_for_line(int line_number, string &value_out) {
	sleep(1000);
	return true;
    };
};

class my_sink : public grep_proc_sink {

public:
    my_sink() : ms_finished(false) { };
    
    void grep_match(grep_proc &gp,
		    grep_line_t line,
		    int start,
		    int end) {
    };

    void grep_end(grep_proc &gp) {
	this->ms_finished = true;
    };

    bool ms_finished;
};

static fd_set READFDS;
static int MAXFD;

static void looper(grep_proc &gp)
{
    my_sink msink;
    
    gp.set_sink(&msink);
    
    while (!msink.ms_finished) {
	fd_set rfds = READFDS;
	
	select(MAXFD + 1, &rfds, NULL, NULL, NULL);
	
	gp.check_fd_set(rfds);
    }
}

int main(int argc, char *argv[])
{
    int eoff, retval = EXIT_SUCCESS;
    const char *errptr;
    pcre *code;
    
    FD_ZERO(&READFDS);

    code = pcre_compile("foobar",
			PCRE_CASELESS,
			&errptr,
			&eoff,
			NULL);
    pcre_refcount(code, 1);
    assert(code != NULL);

    {
	my_source ms;
	grep_proc gp(code, ms, MAXFD, READFDS);
	
	gp.queue_request(grep_line_t(10), grep_line_t(14));
	gp.queue_request(grep_line_t(0), grep_line_t(3));
	gp.start();
	looper(gp);
    }

    {
	my_sleeper_source mss;
	grep_proc *gp = new grep_proc(code, mss, MAXFD, READFDS);
	int status;
	
	gp->queue_request();
	gp->start();

	assert(wait3(&status, WNOHANG, NULL) == 0);
	
	delete gp;

	assert(wait(&status) == -1);
	assert(errno == ECHILD);
    }
    
    return retval;
}
