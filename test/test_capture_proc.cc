
#include "config.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "time_T.hh"
#include "auto_temp_file.hh"
#include "capture_proc.hh"

static const int MS_LINE_COUNT = 4;

static struct {
    const char *ml_line;
    const char *ml_direction;
    int ml_value;
} MS_LINES[] = {
    { "download rate: 1000\n", "download", 1000 },
    { "upload rate: 512\n", "upload", 512 },
    { "download rate: 1024\n", "download", 1024 },
    { "download rate: 2048\n", "download", 2048 },
};

class my_source : public grep_proc_source {

public:
    my_source() { };

    bool grep_value_for_line(int line_number, string &value_out) {
	value_out = MS_LINES[line_number].ml_line;
	
	return true;
    };

    string grep_source_name(void) {
	char name[1024];

	snprintf(name, sizeof(name), "test-source.%d", getpid());
	return string(name);
    };
};

class my_sink : public grep_proc_sink {

public:
    my_sink() : ms_finished(false) { };
    
    void grep_match(grep_proc &gp,
		    grep_line_t line,
		    int start,
		    int end) {
	assert(0);
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

    code = pcre_compile("(?P<direction>\\w+) rate: (?P<value>\\d+)",
			PCRE_CASELESS,
			&errptr,
			&eoff,
			NULL);
    assert(code != NULL);

    try {
	auto_temp_file db_file("/tmp/test_capture_proc-db.XXXXXX");
	my_source ms;
	capture_proc cp("/tmp/foo", "test", code, ms, MAXFD, READFDS);

	fprintf(stderr, "got %s\n", cp.columns_list().c_str());
	cp.create_table();
	cp.queue_request(grep_line_t(0), grep_line_t(MS_LINE_COUNT));
	cp.start();
	looper(cp);

	{
	    Session sql(sqlite3, "/tmp/foo");
	    int lpc, count;
	    
	    sql << "select count(*) from test", into(count);
	    assert(count > 0);

	    Rowset<double> rs = (sql.prepare << "select * from test");
	    Rowset<double>::const_iterator iter;
	    
	    for (iter = rs.begin(), lpc = 0;
		 iter != rs.end() && lpc < MS_LINE_COUNT;
		 ++iter, lpc++) {
		assert(*iter == MS_LINES[lpc].ml_value);
	    }
	}
    }
    catch (const exception &e) {
	fprintf(stderr, "%s\n", e.what());
	retval = EXIT_FAILURE;
    }
    
    return retval;
}
