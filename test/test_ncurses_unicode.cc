
#include <stdlib.h>

#include "config.h"
#define _XOPEN_SOURCE_EXTENDED 1
#include <locale.h>

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
