
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "line_buffer.hh"

int main(int argc, char *argv[])
{
    int retval = EXIT_SUCCESS;
    auto_fd fd;

    fd = open("/tmp/lb.err", O_WRONLY|O_CREAT|O_APPEND, 0666);
    dup2(fd, STDERR_FILENO);
    fprintf(stderr, "startup\n");
    
    if (argc < 2) {
	fprintf(stderr, "error: no file given\n");
    }
    else if ((fd = open(argv[1], O_RDONLY)) == -1) {
	perror("open");
    }
    else {
	const char *line;
	line_buffer lb;
	size_t len;
	
	lb.set_fd(fd);
	while ((line = lb.read_line(len)) != NULL) {
	    printf("%s\n", line);
	}
    }

    return retval;
}
