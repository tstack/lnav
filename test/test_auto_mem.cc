
#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include "auto_mem.hh"

struct my_data {
    int dummy1;
    int dummy2;
};

int free_count;
void *last_free;

void my_free(void *mem)
{
    free_count += 1;
    last_free = mem;
}

int main(int argc, char *argv[])
{
    int retval = EXIT_SUCCESS;
    auto_mem<struct my_data, my_free> md1, md2;
    struct my_data md1_val, md2_val;
    
    md1 = &md1_val;
    assert(free_count == 1);
    md1 = md2;
    assert(free_count == 2);
    assert(last_free == &md1_val);
    assert(md1 == NULL);

    md1 = &md2_val;
    assert(free_count == 3);
    assert(last_free == NULL);
    *md1.out() = &md1_val;
    assert(free_count == 4);
    assert(last_free == &md2_val);
    assert(md1.in() == &md1_val);

    {
	auto_mem<struct my_data, my_free> md_cp(md1);

	assert(md1 == NULL);
	assert(free_count == 4);
	assert(md_cp == &md1_val);
    }

    assert(free_count == 5);
    assert(last_free == &md1_val);
    
    return retval;
}
