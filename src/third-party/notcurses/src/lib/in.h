#ifndef NOTCURSES_IN
#define NOTCURSES_IN

#ifdef __cplusplus
extern "C" {
#endif

// internal header, not installed

#include <stdio.h>

struct tinfo;
struct inputctx;
struct ncsharedstats;

int init_inputlayer(struct tinfo* ti, FILE* infp, int lmargin, int tmargin,
                    int rmargin, int bmargin, struct ncsharedstats* stats,
                    unsigned drain, int linesigs_enabled)
  __attribute__ ((nonnull (1, 2, 7)));

int stop_inputlayer(struct tinfo* ti);

int inputready_fd(const struct inputctx* ictx)
  __attribute__ ((nonnull (1)));

// allow another source provide raw input for distribution to client code.
// drops input if there is no room in appropriate output queue.
int ncinput_shovel(struct inputctx* ictx, const void* buf, int len)
  __attribute__ ((nonnull (1, 2)));

typedef enum {
    TERMINAL_UNKNOWN,       // no useful information from queries; use termname
    // the very limited linux VGA/serial console, or possibly the (deprecated,
    // pixel-drawable, RGBA8888) linux framebuffer console. *not* fbterm.
    TERMINAL_LINUX,         // ioctl()s
    // the linux KMS/DRM console, *not* kmscon, but DRM direct dumb buffers
    TERMINAL_LINUXDRM,      // ioctl()s
    TERMINAL_XTERM,         // XTVERSION == 'XTerm(ver)'
    TERMINAL_VTE,           // TDA: "~VTE"
    TERMINAL_KITTY,         // XTGETTCAP['TN'] == 'xterm-kitty'
    TERMINAL_FOOT,          // TDA: "\EP!|464f4f54\E\\"
    TERMINAL_MLTERM,        // XTGETTCAP['TN'] == 'mlterm'
    TERMINAL_TMUX,          // XTVERSION == "tmux ver"
    TERMINAL_GNUSCREEN,     // SDA: "83;ver;0c"
    TERMINAL_WEZTERM,       // XTVERSION == 'WezTerm *'
    TERMINAL_ALACRITTY,     // can't be detected; match TERM+SDA
    TERMINAL_CONTOUR,       // XTVERSION == 'contour ver'
    TERMINAL_ITERM,         // XTVERSION == 'iTerm2 [ver]'
    TERMINAL_TERMINOLOGY,   // TDA: "~~TY"
    TERMINAL_APPLE,         // Terminal.App, determined by TERM_PROGRAM + macOS
    TERMINAL_RXVT,          // rxvt/urxvt, determined by TERM + UNIX
    TERMINAL_MSTERMINAL,    // Microsoft Windows Terminal
    TERMINAL_MINTTY,        // XTVERSION == 'mintty ver' MinTTY (Cygwin, MSYS2)
    TERMINAL_KONSOLE,       // TDA: "~KDE" (7e4b4445)
    TERMINAL_GHOSTTY,       // XTVERSION == 'ghostty '
} queried_terminals_e;

// after spawning the input layer, send initial queries to the terminal. its
// responses will be built up herein. it's dangerous to go alone! take this!
struct initial_responses {
  int cursory;                 // cursor location, -1 for none
  int cursorx;                 // cursor location, -1 for none
  unsigned appsync_supported;  // is application-synchronized mode supported?
  queried_terminals_e qterm;   // determined terminal
  unsigned kitty_graphics;     // kitty graphics supported
  uint32_t bg;                 // default background
  uint32_t fg;                 // default foreground
  bool got_bg;                 // have we read default background?
  bool got_fg;                 // have we read default foreground?
  bool rgb;                    // was RGB DirectColor advertised?
  bool rectangular_edits;      // were rectangular edits advertised?
  int pixx;                    // screen geometry in pixels
  int pixy;                    // screen geometry in pixels
  int dimx;                    // screen geometry in cells
  int dimy;                    // screen geometry in cells
  // these next three might be set even if there is no actual Sixel support
  // (see e.g. XTerm prior to 370). we determine whether there is Sixel
  // support by checking the DA1 attributes, and scrub them if necessary.
  int color_registers;         // sixel color registers
  int sixely;                  // maximum sixel height
  int sixelx;                  // maximum sixel width
  char* version;               // version string, heap-allocated
  unsigned kbdlevel;           // enabled kitty keyboard functions
  ncpalette palette;           // palette entries
  int maxpaletteread;          // maximum palette index read
  bool pixelmice;              // have we pixel-based mice events?
  char* hpa;                   // control sequence for hpa via XTGETTCAP
};

// Blocking call. Waits until the input thread has processed all responses to
// our initial queries, and returns them.
struct initial_responses* inputlayer_get_responses(struct inputctx* ictx)
  __attribute__ ((nonnull (1)));

int get_cursor_location(struct inputctx* ictx, const char* u7, unsigned* y, unsigned* x)
  __attribute__ ((nonnull (1, 2)));

#ifdef __cplusplus
}
#endif

#endif
