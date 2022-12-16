
#include <stdlib.h>

#include "config.h"
#define _XOPEN_SOURCE_EXTENDED 1
#include <locale.h>

#if defined HAVE_NCURSESW_CURSES_H
#    include <ncursesw/curses.h>
#elif defined HAVE_NCURSESW_H
#    include <ncursesw.h>
#elif defined HAVE_NCURSES_CURSES_H
#    include <ncurses/curses.h>
#elif defined HAVE_NCURSES_H
#    include <ncurses.h>
#elif defined HAVE_CURSES_H
#    include <curses.h>
#else
#    error "SysV or X/Open-compatible Curses header file required"
#endif

int
main(int argc, char* argv[])
{
    setenv("LANG", "en_US.UTF-8", 1);
    setlocale(LC_ALL, "");

    WINDOW* stdscr = initscr();
    cbreak();
    char buf[1024];
    FILE* file = fopen(argv[1], "r");
    int row = 0;
    while (!feof(file)) {
        if (fgets(buf, sizeof(buf), file) != nullptr) {
            mvwaddstr(stdscr, row++, 0, buf);
        }
    }
    getch();
    endwin();
}
