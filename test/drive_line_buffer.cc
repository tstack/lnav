
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <string>
#include <vector>
#include <algorithm>

#include "lnav_util.hh"
#include "auto_fd.hh"
#include "line_buffer.hh"

using namespace std;

int main(int argc, char *argv[])
{
    int c, rnd_iters = 5, retval = EXIT_SUCCESS;
    vector<pair<int, int> > index;
    auto_fd fd = STDIN_FILENO;
    char delim = '\n';
    off_t offset = 0;
    struct stat st;
    
    while ((c = getopt(argc, argv, "o:d:i:n:")) != -1) {
	switch (c) {
	case 'o':
	    if (sscanf(optarg, FORMAT_OFF_T, &offset) != 1) {
		fprintf(stderr,
			"error: offset is not an integer -- %s\n",
			optarg);
		retval = EXIT_FAILURE;
	    }
	    break;
	case 'n':
	    if (sscanf(optarg, "%d", &rnd_iters) != 1) {
		fprintf(stderr,
			"error: offset is not an integer -- %s\n",
			optarg);
		retval = EXIT_FAILURE;
	    }
	    break;
	case 'i':
	    {
		FILE *file;

		if ((file = fopen(optarg, "r")) == NULL) {
		    perror("open");
		    retval = EXIT_FAILURE;
		}
		else {
		    int line_number = 1, line_offset;
		    
		    while (fscanf(file, "%d", &line_offset) == 1) {
			index.push_back(
				make_pair(line_number, line_offset));
			line_number += 1;
		    }
		    fclose(file);
		    file = NULL;
		}
	    }
	    break;
	case 'd':
	    delim = optarg[0];
	    break;
	default:
	    retval = EXIT_FAILURE;
	    break;
	}
    }

    argc -= optind;
    argv += optind;

    if (retval != EXIT_SUCCESS) {
    }
    else if ((argc == 0) && (index.size() > 0)) {
	fprintf(stderr, "error: cannot randomize stdin\n");
	retval = EXIT_FAILURE;
    }
    else if ((argc > 0) && (fd = open(argv[0], O_RDONLY)) == -1) {
	perror("open");
	retval = EXIT_FAILURE;
    }
    else if ((argc > 0) && (fstat(fd, &st) == -1)) {
	perror("fstat");
	retval = EXIT_FAILURE;
    }
    else {
	try {
	    off_t last_offset = offset;
	    line_buffer lb;
	    char *maddr;
	    char *line;
	    size_t len;

	    lb.set_fd(fd);
	    if (index.size() == 0) {
		while ((line = lb.read_line(offset, len, delim)) != NULL) {
		    line[len] = '\0';
		    printf("%s", line);
		    if ((last_offset + len) < offset)
			printf("%c", delim);
		    last_offset = offset;
		}
	    }
	    else if ((maddr = (char *)mmap(NULL,
					   st.st_size,
					   PROT_READ,
					   MAP_FILE | MAP_PRIVATE,
					   lb.get_fd(),
					   0)) == MAP_FAILED) {
		perror("mmap");
		retval = EXIT_FAILURE;
	    }
	    else {
                off_t seq_offset = 0;

                while (lb.read_line(seq_offset, len) != NULL) { }
		do {
		    int lpc;

		    random_shuffle(index.begin(), index.end());
		    for (lpc = 0; lpc < index.size(); lpc++) {
			string line_str;

			offset = index[lpc].second;
			line = lb.read_line(offset, len, delim);

                        assert(line != NULL);
			assert(offset >= 0);
			assert(offset <= st.st_size);
			assert(memcmp(line,
				      &maddr[index[lpc].second],
				      len) == 0);
		    }

		    rnd_iters -= 1;
		} while (rnd_iters);

		printf("All done\n");
	    }
	}
	catch (line_buffer::error &e) {
	    fprintf(stderr, "error: %s\n", strerror(e.e_err));
	    retval = EXIT_FAILURE;
	}
    }
    
    return retval;
}
