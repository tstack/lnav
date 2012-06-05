
#include "config.h"

#include <stdio.h>
#include <assert.h>

#include "hist_source.hh"

int main(int argc, char *argv[])
{
	int retval = EXIT_SUCCESS;
	hist_source hs;

	assert(hs.text_line_count() == 0);
	hs.analyze();

	hs.add_value(1, bucket_type_t(1));
	assert(hs.text_line_count() == 101);

	hs.add_value(2, bucket_type_t(1));
	assert(hs.text_line_count() == 101);

	return retval;
}