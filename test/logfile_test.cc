
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "logfile.hh"

int main(int argc, char *argv[])
{
    int retval = EXIT_SUCCESS;

    try {
	logfile::iterator iter;
	logfile lf("test.log");

	for (iter = lf.begin(); iter != lf.end(); iter++) {
	    printf("%qd %d -- %s\n",
		   iter->get_offset(),
		   iter->get_time(),
		   lf.read_line(iter).c_str());

	    assert(lf.find_after_time(iter->get_time()) != lf.end());
	    assert(lf.find_after_time(iter->get_time() + 1000000) == lf.end());
	}
    }
    catch (logfile::error &e) {
	printf("error: could not open log file -- %s\n", strerror(e.e_err));
    }

    return retval;
}
