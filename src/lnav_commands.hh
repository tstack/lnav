/**
 * @file lnav_commands.hh
 */

#ifndef __lnav_commands_hh
#define __lnav_commands_hh

#include "readline_curses.hh"

/**
 * Initialize the given map with the builtin lnav commands.
 */
void init_lnav_commands(readline_context::command_map_t &cmd_map);

#endif
