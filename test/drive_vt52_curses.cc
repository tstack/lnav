
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>

#include "view_curses.hh"
#include "vt52_curses.hh"

int main(int argc, char *argv[])
{
  int lpc, c, fd, retval = EXIT_SUCCESS;
  vt52_curses vt;
  
  fd = open("/tmp/lnav.err", O_WRONLY|O_CREAT|O_APPEND, 0666);
  dup2(fd, STDERR_FILENO);
  close(fd);
  fprintf(stderr, "startup\n");

  while ((c = getopt(argc, argv, "y:")) != -1) {
    switch (c) {
    case 'y':
      vt.set_y(atoi(optarg));
      break;
    }
  }
  
  for (lpc = 0; lpc < 1000; lpc++) {
    int len;
    
    assert(vt.map_input(random(), len) != NULL);
    assert(len > 0);
  }

  tgetent(NULL, "vt52");
  {
    static const char *CANNED_INPUT[] = {
      "abc",
      "\r",
      tgetstr("ce", NULL),
      "de",
      "\n",
      "1\n",
      "2\n",
      "3\n",
      "4\n",
      "5\n",
      "6\n",
      "7\n",
      "8\n",
      "9\n",
      "abc",
      "\x2",
      "\a",
      "ab\bcdef",
      0
    };

    screen_curses sc;

    noecho();
    vt.set_window(sc.get_window());
    
    for (lpc = 0; CANNED_INPUT[lpc]; lpc++) {
      vt.map_output(CANNED_INPUT[lpc], strlen(CANNED_INPUT[lpc]));
      vt.do_update();
      refresh();
      getch();
    }

    getch();
  }
  
  return retval;
}
