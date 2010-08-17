
#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <string>

#include "pcrepp.hh"

int main(int argc, char *argv[])
{
    pcre_context_static<30> context;
    int retval = EXIT_SUCCESS;
    
    {
	pcrepp nomatch("nothing-to-match");
	pcre_input pi("dummy");
	
	assert(!nomatch.match(context, pi));
    }

    {
	pcrepp match1("(\\w*)=(\\d+)");
	pcre_input pi("a=1  b=2");
	pcre_context::capture_t *cap;
	
	assert(match1.match(context, pi));
	
	cap = context.all();
	assert(cap->c_begin == 0);
	assert(cap->c_end == 3);

	assert((context.end() - context.begin()) == 2);
	assert(pi.get_substr(context.begin()) == "a");
	assert(pi.get_substr(context.begin() + 1) == "1");

	assert(match1.match(context, pi));
	assert((context.end() - context.begin()) == 2);
	assert(pi.get_substr(context.begin()) == "b");
	assert(pi.get_substr(context.begin() + 1) == "2");
    }

    {
	pcrepp match2("");
    }
    
    return retval;
}
