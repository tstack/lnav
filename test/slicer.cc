
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "line_buffer.hh"

using namespace std;

int main(int argc, char *argv[])
{
    int retval = EXIT_SUCCESS;
    vector<off_t> index;
    auto_fd fd;
    
    if (argc < 2) {
	fprintf(stderr, "error: expecting file argument\n");
	retval = EXIT_FAILURE;
    }
    else if ((fd = open(argv[1], O_RDONLY)) == -1) {
	perror("open");
	retval = EXIT_FAILURE;
    }
    else {
	int line_number, start, end;
	off_t offset = 0;
	line_buffer lb;
	char *line;
	size_t len;
	
	lb.set_fd(fd);
	index.push_back(offset);
	while ((line = lb.read_line(offset, len)) != NULL) {
	    index.push_back(offset);
	}
	index.pop_back();

	try {
	    while (scanf("%d:%d:%d", &line_number, &start, &end) == 3) {
		string str;
		
		offset = index[line_number];
		line = lb.read_line(offset, len);
		str = string(line, len);
		str = str.substr(start, end);
		printf("%s\n", str.c_str());
	    }
	}
	catch (line_buffer::error &e) {
	    fprintf(stderr, "error: line buffer %s\n", strerror(e.e_err));
	}
    }

    return retval;
}
