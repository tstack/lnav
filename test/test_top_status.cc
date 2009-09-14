
#include "config.h"

#include <assert.h>
#include <stdlib.h>

#include "top_status_source.hh"

using namespace std;

static time_t current_time = 1;

time_t time(time_t *arg)
{
    if (arg != NULL)
	*arg = current_time;
    
    return current_time;
}

int main(int argc, char *argv[])
{
    int retval = EXIT_SUCCESS;

    top_status_source tss;

    {
	status_field &sf = tss.
	    statusview_value_for_field(top_status_source::TSF_TIME);
	attr_line_t val;

	tss.update_time();
	val = sf.get_value();
	assert(val.get_string() == sf.get_value().get_string());
	current_time += 2;
	tss.update_time();
	assert(val.get_string() != sf.get_value().get_string());
    }
    
    return retval;
}
