#ifndef NOTCURSES_TERMDESC
#define NOTCURSES_TERMDESC

#ifdef __cplusplus
extern "C" {
#endif

// internal header, not installed

#include "version.h"
#include "builddef.h"
#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>
#include <notcurses/notcurses.h>
#include "sprite.h"
#include "blit.h"
#include "fbuf.h"
#include "in.h"

// kitty keyboard protocol pop, used at end when kitty is verified.
#define KKEYBOARD_POP  "\x1b[=0u\x1b[<u"

// disable key modifier options; this corresponds to a resource value of
// "-1", which cannot be set with the [>m sequence. supposedly, "[>m" by
// itself ought reset all of them, but this doesn't seem to work FIXME.
#define XTMODKEYSUNDO "\x1b[>2m\x1b[>4m"

struct ncpile;
struct sprixel;
struct notcurses;
struct ncsharedstats;

// we store all our escape sequences in a single large block, and use
// 16-bit one-biased byte-granularity indices to get the location in said
// block. we'd otherwise be using 32 or 64-bit pointers to get locations
// scattered all over memory. this way the lookup elements require two or four
// times fewer cachelines total, and the actual escape sequences are packed
// tightly into minimal cachelines. if an escape is not defined, that index
// is 0. the first escape defined has an index of 1, and so on. an escape
// thus cannot actually start at byte 65535.

// indexes into the table of fixed-width (16-bit) indices
typedef enum {
  ESCAPE_CUP,     // "cup" move cursor to absolute x, y position
  ESCAPE_HPA,     // "hpa" move cursor to absolute horizontal position
  ESCAPE_VPA,     // "vpa" move cursor to absolute vertical position
  ESCAPE_SETAF,   // "setaf" set foreground color
  ESCAPE_SETAB,   // "setab" set background color
  ESCAPE_OP,      // "op" set foreground and background color to defaults
  ESCAPE_FGOP,    // set foreground only to default
  ESCAPE_BGOP,    // set background only to default
  ESCAPE_SGR0,    // "sgr0" turn off all styles
  ESCAPE_CIVIS,   // "civis" make the cursor invisiable
  ESCAPE_CNORM,   // "cnorm" restore the cursor to normal
  ESCAPE_OC,      // "oc" restore original colors
  ESCAPE_SITM,    // "sitm" start italics
  ESCAPE_RITM,    // "ritm" end italics
  ESCAPE_CUU,     // "cuu" move n cells up
  ESCAPE_CUB,     // "cub" move n cells back (left)
  ESCAPE_CUF,     // "cuf" move n cells forward (right)
  ESCAPE_BOLD,    // "bold" enter bold mode
  ESCAPE_NOBOLD,  // disable bold (ANSI but not terminfo, SGR 22)
  ESCAPE_CUD,     // "cud" move n cells down
  ESCAPE_SMKX,    // "smkx" keypad_xmit (keypad transmit mode)
  ESCAPE_RMKX,    // "rmkx" keypad_local
  ESCAPE_EL,      // "el" clear to end of line, inclusive
  ESCAPE_SMCUP,   // "smcup" enter alternate screen
  ESCAPE_RMCUP,   // "rmcup" leave alternate screen
  ESCAPE_SMXX,    // "smxx" start struckout
  ESCAPE_SMUL,    // "smul" start underline
  ESCAPE_RMUL,    // "rmul" end underline
  ESCAPE_SMULX,   // "Smulx" deparameterized: start extended underline
  ESCAPE_SMULNOX, // "Smulx" deparameterized: kill underline
  ESCAPE_RMXX,    // "rmxx" end struckout
  ESCAPE_IND,     // "ind" scroll 1 line up
  ESCAPE_INDN,    // "indn" scroll n lines up
  ESCAPE_SC,      // "sc" push the cursor onto the stack
  ESCAPE_RC,      // "rc" pop the cursor off the stack
  ESCAPE_CLEAR,   // "clear" clear screen and home cursor
  ESCAPE_INITC,   // "initc" set up palette entry
  ESCAPE_U7,      // "u7" cursor position report
  // Application synchronized updates, not present in terminfo
  // (https://gitlab.com/gnachman/iterm2/-/wikis/synchronized-updates-spec)
  ESCAPE_BSUM,     // Begin Synchronized Update Mode
  ESCAPE_ESUM,     // End Synchronized Update Mode
  ESCAPE_SAVECOLORS,    // XTPUSHCOLORS (push palette/fg/bg)
  ESCAPE_RESTORECOLORS, // XTPOPCOLORS  (pop palette/fg/bg)
  ESCAPE_DECERA,   // rectangular erase
  ESCAPE_SMACS,
  ESCAPE_RMACS,
  ESCAPE_BLINK,
  ESCAPE_NOBLINK,
  ESCAPE_MAX
} escape_e;

// when we read a cursor report, we put it on the queue for internal
// processing. this is necessary since it can be arbitrarily interleaved with
// other input when stdin is connected to our terminal. these are already
// processed to be 0-based.
typedef struct cursorreport {
  int x, y;
  struct cursorreport* next;
} cursorreport;

// terminal interface description. most of these are acquired from terminfo(5)
// (using a database entry specified by TERM). some are determined via
// heuristics based off terminal interrogation or the TERM environment
// variable. some are determined via ioctl(2). treat all of them as if they
// can change over the program's life (don't cache them locally).
typedef struct tinfo {
  uint16_t escindices[ESCAPE_MAX]; // table of 1-biased indices into esctable
  int ttyfd;                       // connected to true terminal, might be -1
  char* esctable;                  // packed table of escape sequences
  nccapabilities caps;             // exported to the user, when requested
  unsigned pixy;                   // total pixel geometry, height
  unsigned pixx;                   // total pixel geometry, width
  // we use the cell's size in pixels for pixel blitting. this information can
  // be acquired on all terminals with pixel support.
  unsigned cellpxy;                // cell pixel height, might be 0
  unsigned cellpxx;                // cell pixel width, might be 0
  unsigned dimy, dimx;             // most recent cell geometry

  unsigned supported_styles; // bitmask over NCSTYLE_* driven via sgr/ncv

  // kitty interprets an RGB background that matches the default background
  // color *as* the default background, meaning it'll be translucent if
  // background_opaque is in use. detect this, and avoid the default if so.
  // bg_collides_default is either:
  // 0xfexxxxxxx (unknown), 0x00RRGGBB (no collide), or 0x01RRGGBB (collides).
  uint32_t bg_collides_default;

  // 0xffxxxxxxx (unknown), or 0x00RRGGBB (foreground)
  uint32_t fg_default;

  // bitmap support. if we support bitmaps, pixel_implementation will be a
  // value other than NCPIXEL_NONE.
  ncpixelimpl_e pixel_implementation;
  // wipe out a cell's worth of pixels from within a sprixel. for sixel, this
  // means leaving out the pixels (and likely resizes the string). for kitty,
  // this means dialing down their alpha to 0 (in equivalent space).
  int (*pixel_wipe)(struct sprixel* s, int y, int x);
  // perform the inverse of pixel_wipe, restoring an annihilated sprixcell.
  int (*pixel_rebuild)(struct sprixel* s, int y, int x, uint8_t* auxvec);
  // called in phase 1 when INVALIDATED; this damages cells that have been
  // redrawn in a sixel (when old was not transparent, and new is not opaque).
  // it leaves the sprixel in INVALIDATED so that it's drawn in phase 2.
  void (*pixel_refresh)(const struct ncpile* p, struct sprixel* s);
  int (*pixel_remove)(int id, fbuf* f); // kitty only, issue actual delete command
  int (*pixel_init)(struct tinfo* ti, int fd); // called when support is detected
  int (*pixel_draw)(const struct tinfo*, const struct ncpile* p,
                    struct sprixel* s, fbuf* f, int y, int x);
  int (*pixel_draw_late)(const struct tinfo*, struct sprixel* s, int yoff, int xoff);
  // execute move (erase old graphic, place at new location) if non-NULL
  int (*pixel_move)(struct sprixel* s, fbuf* f, unsigned noscroll, int yoff, int xoff);
  int (*pixel_scrub)(const struct ncpile* p, struct sprixel* s);
  int (*pixel_clear_all)(fbuf* f);  // called during context startup
  // make a loaded graphic visible. only used with kitty.
  int (*pixel_commit)(fbuf* f, struct sprixel* s, unsigned noscroll);
  // scroll all graphics up. only used with fbcon.
  void (*pixel_scroll)(const struct ncpile* p, struct tinfo*, int rows);
  void (*pixel_cleanup)(struct tinfo*); // called at shutdown
  uint8_t* (*pixel_trans_auxvec)(const struct ncpile* p); // create tranparent auxvec
  // sprixel parameters. there are several different sprixel protocols, of
  // which we support sixel and kitty. the kitty protocol is used based
  // on TERM heuristics. otherwise, we attempt to detect sixel support, and
  // query the details of the implementation.
  int color_registers; // sixel color registers (post pixel_query_done)
  unsigned sixel_maxx; // maximum theoretical sixel width
  // in sixel, we can't render to the bottom row, lest we force a one-line
  // scroll. we thus clamp sixel_maxy_pristine to the minimum of
  // sixel_maxy_pristine (the reported sixel_maxy), and the number of rows
  // less one times the cell height. sixel_maxy is thus recomputed whenever
  // we get a resize event. it is only defined if we have sixel_maxy_pristine,
  // so kitty graphics (which don't force a scroll) never deal with this.
  unsigned sixel_maxy;          // maximum working sixel height
  unsigned sixel_maxy_pristine; // maximum theoretical sixel height, as queried
  unsigned sprixel_scale_height;// sprixel must be a multiple of this many rows
  void* sixelengine;         // opaque threaded engine used by sixel dispatch
  const char* termname;      // terminal name from environment variables/init
  char* termversion;         // terminal version (freeform) from query responses
  queried_terminals_e qterm; // detected terminal class
  // we heap-allocate this one (if we use it), as it's not fully defined on Windows
  struct termios *tpreserved;// terminal state upon entry
  struct inputctx* ictx;     // new input layer
  unsigned stdio_blocking_save; // was stdio blocking at entry? restore on stop.
  // ought we issue gratuitous HPAs to work around ambiguous widths?
  unsigned gratuitous_hpa;

  // if we get a reply to our initial \e[18t cell geometry query, it will
  // replace these values. note that LINES/COLUMNS cannot be used to limit
  // the output region; use margins for that, if necessary.
  int default_rows;          // LINES environment var / lines terminfo / 24
  int default_cols;          // COLUMNS environment var / cols terminfo / 80

  ncpalette originalpalette; // palette as read from initial queries
  int maxpaletteread;        // maximum palette entry read
  pthread_t gpmthread;       // thread handle for GPM watcher
  int gpmfd;                 // connection to GPM daemon
  char mouseproto;           // DECSET level (100x, '0', '2', '3')
  bool pixelmice;            // do we support pixel-precision mice?
#ifdef __linux__
  int linux_fb_fd;           // linux framebuffer device fd
  char* linux_fb_dev;        // device corresponding to linux_fb_dev
  uint8_t* linux_fbuffer;    // mmap()ed framebuffer
  size_t linux_fb_len;       // size of map
#elif defined(__MINGW32__)
  HANDLE inhandle;
  HANDLE outhandle;
#endif

  // kitty keyboard protocol level. we initialize this to UINT_MAX, in case we
  // crash while running the initialization automata (in that case, we want to
  // pop the keyboard support level, which we normally do only if we detected
  // actual support. at that point, we obviously haven't detected anything).
  // after getting the initialization package back, if it's still UINT_MAX, we
  // set it to 0, and also indicate a lack of support via kittykbdsupport (we
  // need distinguish between level 0, used with DRAININPUT, and an absolute
  // lack of support, in which case we move to XTMODKEYS, for notcurses-info).
  unsigned kbdlevel;         // kitty keyboard support level
  bool kittykbdsupport;      // do we support the kitty keyboard protocol?
  bool bce;                  // is the bce property advertised?
  bool in_alt_screen;        // are we in the alternate screen?
} tinfo;

// retrieve the terminfo(5)-style escape 'e' from tdesc (NULL if undefined).
static inline __attribute__ ((pure)) const char*
get_escape(const tinfo* tdesc, escape_e e){
  unsigned idx = tdesc->escindices[e];
  if(idx){
    return tdesc->esctable + idx - 1;
  }
  return NULL;
}

static inline uint16_t
term_supported_styles(const tinfo* ti){
  return ti->supported_styles;
}

// prepare |ti| from the terminfo database and other sources. set |utf8| if
// we've verified UTF8 output encoding. set |noaltscreen| to inhibit alternate
// screen detection. |stats| may be NULL; either way, it will be handed to the
// input layer so that its stats can be recorded.
int interrogate_terminfo(tinfo* ti, FILE* out, unsigned utf8,
                         unsigned noaltscreen, unsigned nocbreak,
                         unsigned nonewfonts, int* cursor_y, int* cursor_x,
                         struct ncsharedstats* stats, int lmargin, int tmargin,
                         int rmargin, int bmargin, unsigned draininput)
  __attribute__ ((nonnull (1, 2, 9)));

void free_terminfo_cache(tinfo* ti);

// return a heap-allocated copy of termname + termversion
char* termdesc_longterm(const tinfo* ti);

int locate_cursor(tinfo* ti, unsigned* cursor_y, unsigned* cursor_x);

int grow_esc_table(tinfo* ti, const char* tstr, escape_e esc,
                   size_t* tlen, size_t* tused);

static inline int
ncfputs(const char* ext, FILE* out){
  int r;
#ifdef __USE_GNU
  r = fputs_unlocked(ext, out);
#else
  r = fputs(ext, out);
#endif
  return r;
}

static inline int
ncfputc(char c, FILE* out){
#ifdef __USE_GNU
  return putc_unlocked(c, out);
#else
  return putc(c, out);
#endif
}

// reliably flush a FILE*...except you can't, so far as i can tell. at least
// on glibc, a single fflush() error latches the FILE* error, but ceases to
// perform any work (even following a clearerr()), despite returning 0 from
// that point on. thus, after a fflush() error, even on EAGAIN and friends,
// you can't use the stream any further. doesn't this make fflush() pretty
// much useless? it sure would seem to, which is why we use an fbuf for
// all our important I/O, which we then blit with blocking_write(). if you
// care about your data, you'll do the same.
static inline int
ncflush(FILE* out){
  if(ferror(out)){
    logerror("Not attempting a flush following error\n");
  }
  if(fflush(out) == EOF){
    logerror("Unrecoverable error flushing io (%s)\n", strerror(errno));
    return -1;
  }
  return 0;
}

static inline int
term_emit(const char* seq, FILE* out, bool flush){
  if(!seq){
    return -1;
  }
  if(ncfputs(seq, out) == EOF){
    logerror("Error emitting %lub escape (%s)\n",
             (unsigned long)strlen(seq), strerror(errno));
    return -1;
  }
  return flush ? ncflush(out) : 0;
}

// |drain| is set iff we're draining input.
int enter_alternate_screen(int ttyfd, FILE* ttyfp, tinfo* ti, unsigned drain);
int leave_alternate_screen(int ttyfd, FILE* ttyfp, tinfo* ti, unsigned drain);

int cbreak_mode(tinfo* ti);

// execute termios's TIOCGWINSZ ioctl(). returns -1 on failure.
int tiocgwinsz(int fd, struct winsize* ws);

#ifdef __cplusplus
}
#endif

#endif
