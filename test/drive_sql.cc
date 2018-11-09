#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include <sqlite3.h>

#include <iostream>

#include "lnav.hh"
#include "auto_mem.hh"
#include "sqlite-extension-func.hh"
#include "regexp_vtab.hh"

struct callback_state {
    int cs_row;
};

struct _lnav_data lnav_data;

static int sql_callback(void *ptr,
                        int ncols,
                        char **colvalues,
                        char **colnames)
{
    struct callback_state *cs = (struct callback_state *)ptr;

    printf("Row %d:\n", cs->cs_row);
    for (int lpc = 0; lpc < ncols; lpc++) {
        printf("  Column %10s: %s\n", colnames[lpc], colvalues[lpc]);
    }

    cs->cs_row += 1;
    
    return 0;
}

void rebuild_hist()
{
}

bool setup_logline_table(exec_context &ec)
{
    return false;
}

bool rescan_files(bool required)
{
    return false;
}

void wait_for_children()
{

}

void rebuild_indexes()
{
}

readline_context::command_map_t lnav_commands;

extern "C" {
const char *help_txt = "";
}

extern "C" {
const char *default_log_formats_json = "";
}
int main(int argc, char *argv[])
{
    int retval = EXIT_SUCCESS;
    auto_mem<sqlite3> db(sqlite3_close);
    std::string stmt;

    log_argv(argc, argv);

    if (argc == 2) {
        stmt = argv[1];
    } else {
        std::getline(std::cin, stmt, '\0');
    }

    if (sqlite3_open(":memory:", db.out()) != SQLITE_OK) {
        fprintf(stderr, "error: unable to make sqlite memory database\n");
        retval = EXIT_FAILURE;
    }
    else {
        auto_mem<char> errmsg(sqlite3_free);
        struct callback_state state;

        memset(&state, 0, sizeof(state));

        {
            int register_collation_functions(sqlite3 * db);

            register_sqlite_funcs(db.in(), sqlite_registration_funcs);
            register_collation_functions(db.in());
        }

        register_regexp_vtab(db.in());

        if (sqlite3_exec(db.in(),
            stmt.c_str(),
            sql_callback,
            &state,
            errmsg.out()) != SQLITE_OK) {
            fprintf(stderr, "error: sqlite3_exec failed -- %s\n", errmsg.in());
            retval = EXIT_FAILURE;
        }
    }

    return retval;
}
