/**
 * Copyright (c) 2007-2012, Timothy Stack
 *
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include <algorithm>

#include "logfile.hh"
#include "log_format.hh"
#include "log_format_loader.hh"

using namespace std;

typedef enum {
    MODE_NONE,
    MODE_ECHO,
    MODE_LINE_COUNT,
    MODE_TIMES,
    MODE_LEVELS,
} dl_mode_t;

time_t time(time_t *_unused) {
  return 1194107018;
}

string execute_any(exec_context &ec, const string &cmdline_with_mode)
{
  return "";
}

int main(int argc, char *argv[]) {
  int c, retval = EXIT_SUCCESS;
  dl_mode_t mode = MODE_NONE;
  string expected_format;

  {
    std::vector<std::string> paths, errors;

    if (getenv("test_dir") != NULL) {
      paths.push_back(getenv("test_dir"));
    }
    load_formats(paths, errors);
  }

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
  } else if (argc == 0) {
    fprintf(stderr, "error: expecting log file name\n");
  } else {
    try {
      logfile_open_options default_loo;
      logfile lf(argv[0], default_loo);
      struct stat st;

      stat(argv[0], &st);
      assert(strcmp(argv[0], lf.get_filepath().c_str()) == 0);

      lf.rebuild_index();
      assert(!lf.is_closed());
      lf.rebuild_index();
      assert(!lf.is_closed());
      lf.rebuild_index();
      assert(!lf.is_closed());
      assert(lf.get_activity().la_polls == 3);
      if (lf.size() > 1) {
        assert(lf.get_activity().la_reads == 2);
      }
      if (expected_format == "") {
        assert(lf.get_format() == NULL);
      } else {
        //printf("%s %s\n", lf.get_format()->get_name().c_str(), expected_format.c_str());
        assert(lf.get_format() != NULL);
        assert(lf.get_format()->get_name().to_string() == expected_format);
      }
      if (!lf.is_compressed()) {
        assert(lf.get_modified_time() == st.st_mtime);
      }

      switch (mode) {
        case MODE_NONE:
          break;
        case MODE_ECHO:
          for (logfile::iterator iter = lf.begin(); iter != lf.end(); ++iter) {
            printf("%s\n", lf.read_line(iter).c_str());
          }
              break;
        case MODE_LINE_COUNT:
          printf("%zd\n", lf.size());
              break;
        case MODE_TIMES:
          for (logfile::iterator iter = lf.begin(); iter != lf.end(); ++iter) {
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
          for (logfile::iterator iter = lf.begin(); iter != lf.end(); ++iter) {
            printf("0x%02x\n", iter->get_level_and_flags());
          }
              break;
      }
    } catch (const logfile::error &e) {
      fprintf(stderr, "logfile error -- %s (%d)", e.e_filename.c_str(),
              e.e_err);
      assert(false);
    }
  }

  return retval;
}
