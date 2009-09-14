
#include "config.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "auto_fd.hh"
#include "line_buffer.hh"

static void single_line(const char *data, int lendiff)
{
    line_buffer lb;
    auto_fd pi[2];
    off_t off = 0;
    size_t len;
    char *line;
    
    assert(auto_fd::pipe(pi) == 0);
    write(pi[1], data, strlen(data));
    pi[1].reset();
    
    lb.set_fd(pi[0]);
    line = lb.read_line(off, len);
    assert(line != NULL);
    assert(off == strlen(data));
    assert(len == strlen(data) - lendiff);
    
    line = lb.read_line(off, len);
    assert(line == NULL);
    assert(lb.get_file_size() == strlen(data));
}

int main(int argc, char *argv[])
{
    int retval = EXIT_SUCCESS;

    single_line("Dexter Morgan", 0);
    single_line("Rudy Morgan\n", 1);
    
    return retval;
}
