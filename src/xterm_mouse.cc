
#include "config.h"

#include "xterm_mouse.hh"

const char *xterm_mouse::XT_TERMCAP = "\033[?1000%?%p1%{1}%=%th%el%;";
const char *xterm_mouse::XT_TERMCAP_TRACKING = "\033[?1002%?%p1%{1}%=%th%el%;";
