
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>

#include "logfile.hh"

using namespace std;

typedef enum {
  MODE_NONE,
  MODE_ECHO,
  MODE_LINE_COUNT,
  MODE_TIMES,
  MODE_LEVELS,
} dl_mode_t;

time_t time(time_t *_unused)
{
    return 1194107018;
}

int main(int argc, char *argv[])
{
  int c, retval = EXIT_SUCCESS;
  dl_mode_t mode = MODE_NONE;
  string expected_format;

  while ((c = getopt(argc, argv, "ef:ltv")) != -1) {
    switch (c) {
    case 'f':
	expected_format = optarg;
      break;
    case 'e':
      mode = MODE_ECHO;
      break;
    case 'l':
      mode = MODE_LINE_COUNT;
      break;
    case 't':
      mode = MODE_TIMES;
      break;
    case 'v':
      mode = MODE_LEVELS;
      break;
    }
  }

  argc -= optind;
  argv += optind;

  if (retval == EXIT_FAILURE) {
  }
  else if (argc == 0) {
    fprintf(stderr, "error: expecting log file name\n");
  }
  else {
    logfile lf(argv[0]);
    struct stat st;
    
    stat(argv[0], &st);
    assert(strcmp(argv[0], lf.get_filename().c_str()) == 0);

    lf.rebuild_index();
    if (expected_format == "") {
	assert(lf.get_format() == NULL);
    }
    else {
	// printf("%s %s\n", lf.get_format()->get_name().c_str(), expected_format.c_str());
	assert(lf.get_format()->get_name() == expected_format);
    }
    assert(lf.get_modified_time() == st.st_mtime);

    switch (mode) {
    case MODE_NONE:
      break;
    case MODE_ECHO:
      for (logfile::iterator iter = lf.begin(); iter != lf.end(); iter++) {
	printf("%s\n", lf.read_line(iter).c_str());
      }
      break;
    case MODE_LINE_COUNT:
      printf("%d\n", lf.size());
      break;
    case MODE_TIMES:
      for (logfile::iterator iter = lf.begin(); iter != lf.end(); iter++) {
	char buffer[1024];
	time_t lt;

	lt = iter->get_time();
	strftime(buffer, sizeof(buffer),
		 "%b %d %H:%M:%S %Y",
		 gmtime(&lt));
	printf("%s -- %03d\n", buffer, iter->get_millis());
      }
      break;
    case MODE_LEVELS:
      for (logfile::iterator iter = lf.begin(); iter != lf.end(); iter++) {
	printf("0x%02x\n", iter->get_level());
      }
      break;
    }
  }

  return retval;
}
