#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdatomic.h>
#include "automaton.h"
#include "internal.h"
#include "unixsig.h"
#include "render.h"
#include "in.h"

// Notcurses takes over stdin, and if it is not connected to a terminal, also
// tries to make a connection to the controlling terminal. If such a connection
// is made, it will read from that source (in addition to stdin). We dump one or
// both into distinct buffers. We then try to lex structured elements out of
// the buffer(s). We can extract cursor location reports, mouse events, and
// UTF-8 characters. Completely extracted ones are placed in their appropriate
// queues, and removed from the depository buffer. We aim to consume the
// entirety of the deposit before going back to read more data, but let anyone
// blocking on data wake up as soon as we've processed any input.
//
// The primary goal is to react to terminal messages (mostly cursor location
// reports) as quickly as possible, and definitely not with unbounded latency,
// without unbounded allocation, and also without losing data. We'd furthermore
// like to reliably differentiate escapes and regular input, even when that
// latter contains escapes. Unbounded input will hopefully only be present when
// redirected from a file.

static sig_atomic_t cont_seen;
static sig_atomic_t resize_seen;

// called for SIGWINCH and SIGCONT, and causes block_on_input to return
void sigwinch_handler(int signo){
  if(signo == SIGWINCH){
    resize_seen = signo;
    sigcont_seen_for_render = 1;
  }else if(signo == SIGCONT){
    cont_seen = signo;
    sigcont_seen_for_render = 1;
  }
}

typedef struct cursorloc {
  int y, x;             // 0-indexed cursor location
} cursorloc;

#ifndef __MINGW32__
typedef int ipipe;
#else
typedef HANDLE ipipe;
#endif

// local state for the input thread. don't put this large struct on the stack.
typedef struct inputctx {
  // these two are not ringbuffers; we always move any leftover materia to the
  // front of the queue (it ought be a handful of bytes at most).
  unsigned char tbuf[BUFSIZ]; // only used if we have distinct terminal fd
  unsigned char ibuf[BUFSIZ]; // might be intermingled bulk/control data

  int stdinfd;          // bulk in fd. always >= 0 (almost always 0). we do not
                        //  own this descriptor, and must not close() it.
  int termfd;           // terminal fd: -1 with no controlling terminal, or
                        //  if stdin is a terminal, or on MSFT Terminal.
#ifdef __MINGW32__
  HANDLE stdinhandle;   // handle to input terminal for MSFT Terminal
#endif

  int lmargin, tmargin; // margins in use at left and top
  int rmargin, bmargin; // margins in use at right and bottom

  automaton amata;
  int ibufvalid;      // we mustn't read() if ibufvalid == sizeof(ibuf)
  int tbufvalid;      // only used if we have distinct terminal connection

  uint8_t backspace;  // backspace is usually not an escape sequence, but
                      // instead ^H or ^? or something. only escape sequences
                      // go into our automaton, so we handle this one
                      // out-of-band. set to non-zero; match with ctrl.
  // ringbuffers for processed, structured input
  cursorloc* csrs;    // cursor reports are dumped here
  ncinput* inputs;    // processed input is dumped here
  int coutstanding;   // outstanding cursor location requests
  int csize, isize;   // total number of slots in csrs/inputs
  int cvalid, ivalid; // population count of csrs/inputs
  int cwrite, iwrite; // slot where we'll write the next csr/input;
                      //  we cannot write if valid == size
  int cread, iread;   // slot from which clients read the next csr/input;
                      //  they cannot read if valid == 0
  pthread_mutex_t ilock; // lock for ncinput ringbuffer, also initial state
  pthread_cond_t icond;  // condvar for ncinput ringbuffer
  pthread_mutex_t clock; // lock for csrs ringbuffer
  pthread_cond_t ccond;  // condvar for csrs ringbuffer
  tinfo* ti;          // link back to tinfo
  pthread_t tid;      // tid for input thread

  unsigned midescape; // we're in the middle of a potential escape. we need
                      //  to do a nonblocking read and try to complete it.
  unsigned stdineof;  // have we seen an EOF on stdin?

  unsigned linesigs;  // are line discipline signals active?
  unsigned drain;     // drain away bulk input?
  ncsharedstats *stats; // stats shared with notcurses context

  ipipe ipipes[2];
  ipipe readypipes[2]; // pipes[0]: poll()able fd indicating the presence of user input
  // initially, initdata is non-NULL and initdata_complete is NULL. once we
  // get DA1, initdata_complete is non-NULL (it is the same value as
  // initdata). once we complete reading the input payload that the DA1 arrived
  // in, initdata is set to NULL, and we broadcast availability. once it has
  // been taken, both become NULL.
  struct initial_responses* initdata;
  struct initial_responses* initdata_complete;
  int kittykbd;        // kitty keyboard protocol support level
  bool failed;         // error initializing input automaton, abort
    atomic_int looping;
    bool bracked_paste_enabled;
    bool in_bracketed_paste;
    fbuf paste_buffer;
} inputctx;

static inline void
inc_input_events(inputctx* ictx){
  pthread_mutex_lock(&ictx->stats->lock);
  ++ictx->stats->s.input_events;
  pthread_mutex_unlock(&ictx->stats->lock);
}

static inline void
inc_input_errors(inputctx* ictx){
  pthread_mutex_lock(&ictx->stats->lock);
  ++ictx->stats->s.input_errors;
  pthread_mutex_unlock(&ictx->stats->lock);
}

// load representations used by XTMODKEYS
static int
prep_xtmodkeys(inputctx* ictx){
  // XTMODKEYS enables unambiguous representations of certain inputs. We
  // enable XTMODKEYS where supported.
  static const struct {
    const char* esc;
    uint32_t key;
    unsigned modifiers;
  } keys[] = {
    { .esc = "\x1b\x8", .key = NCKEY_BACKSPACE,
      .modifiers = NCKEY_MOD_ALT, },
    { .esc = "\x1b[2P", .key = NCKEY_F01,
      .modifiers = NCKEY_MOD_SHIFT, },
    { .esc = "\x1b[5P", .key = NCKEY_F01,
      .modifiers = NCKEY_MOD_CTRL, },
    { .esc = "\x1b[6P", .key = NCKEY_F01,
      .modifiers = NCKEY_MOD_CTRL | NCKEY_MOD_SHIFT, },
    { .esc = "\x1b[2Q", .key = NCKEY_F02,
      .modifiers = NCKEY_MOD_SHIFT, },
    { .esc = "\x1b[5Q", .key = NCKEY_F02,
      .modifiers = NCKEY_MOD_CTRL, },
    { .esc = "\x1b[6Q", .key = NCKEY_F02,
      .modifiers = NCKEY_MOD_CTRL | NCKEY_MOD_SHIFT, },
    { .esc = "\x1b[2R", .key = NCKEY_F03,
      .modifiers = NCKEY_MOD_SHIFT, },
    { .esc = "\x1b[5R", .key = NCKEY_F03,
      .modifiers = NCKEY_MOD_CTRL, },
    { .esc = "\x1b[6R", .key = NCKEY_F03,
      .modifiers = NCKEY_MOD_CTRL | NCKEY_MOD_SHIFT, },
    { .esc = "\x1b[2S", .key = NCKEY_F04,
      .modifiers = NCKEY_MOD_SHIFT, },
    { .esc = "\x1b[5S", .key = NCKEY_F04,
      .modifiers = NCKEY_MOD_CTRL, },
    { .esc = "\x1b[6S", .key = NCKEY_F04,
      .modifiers = NCKEY_MOD_CTRL | NCKEY_MOD_SHIFT, },

    { .esc = "\033b", .key = NCKEY_LEFT,
      .modifiers = NCKEY_MOD_ALT },
    { .esc = "\033f", .key = NCKEY_RIGHT,
        .modifiers = NCKEY_MOD_ALT },


    { .esc = NULL, .key = 0, },
  }, *k;
  for(k = keys ; k->esc ; ++k){
    if(inputctx_add_input_escape(&ictx->amata, k->esc, k->key,
                                 k->modifiers)){
      return -1;
    }
    logdebug("added %u", k->key);
  }
  loginfo("added all xtmodkeys");
  return 0;
}

// load all known special keys from terminfo, and build the input sequence trie
static int
prep_special_keys(inputctx* ictx){
#ifndef __MINGW32__
  static const struct {
    const char* tinfo;
    uint32_t key;
    bool shift, ctrl, alt;
  } keys[] = {
    // backspace (kbs) is handled seperately at the end
    { .tinfo = "kbeg",  .key = NCKEY_BEGIN, },
    { .tinfo = "kcbt",  .key = '\t', .shift = true, }, // "back-tab"
    { .tinfo = "kcub1", .key = NCKEY_LEFT, },
    { .tinfo = "kcuf1", .key = NCKEY_RIGHT, },
    { .tinfo = "kcuu1", .key = NCKEY_UP, },
    { .tinfo = "kcud1", .key = NCKEY_DOWN, },
    { .tinfo = "kdch1", .key = NCKEY_DEL, },
    { .tinfo = "kbs",   .key = NCKEY_BACKSPACE, },
    { .tinfo = "kich1", .key = NCKEY_INS, },
    { .tinfo = "kend",  .key = NCKEY_END, },
    { .tinfo = "khome", .key = NCKEY_HOME, },
    { .tinfo = "knp",   .key = NCKEY_PGDOWN, },
    { .tinfo = "kpp",   .key = NCKEY_PGUP, },
    { .tinfo = "kf0",   .key = NCKEY_F01, },
    { .tinfo = "kf1",   .key = NCKEY_F01, },
    { .tinfo = "kf2",   .key = NCKEY_F02, },
    { .tinfo = "kf3",   .key = NCKEY_F03, },
    { .tinfo = "kf4",   .key = NCKEY_F04, },
    { .tinfo = "kf5",   .key = NCKEY_F05, },
    { .tinfo = "kf6",   .key = NCKEY_F06, },
    { .tinfo = "kf7",   .key = NCKEY_F07, },
    { .tinfo = "kf8",   .key = NCKEY_F08, },
    { .tinfo = "kf9",   .key = NCKEY_F09, },
    { .tinfo = "kf10",  .key = NCKEY_F10, },
    { .tinfo = "kf11",  .key = NCKEY_F11, },
    { .tinfo = "kf12",  .key = NCKEY_F12, },
    { .tinfo = "kf13",  .key = NCKEY_F13, },
    { .tinfo = "kf14",  .key = NCKEY_F14, },
    { .tinfo = "kf15",  .key = NCKEY_F15, },
    { .tinfo = "kf16",  .key = NCKEY_F16, },
    { .tinfo = "kf17",  .key = NCKEY_F17, },
    { .tinfo = "kf18",  .key = NCKEY_F18, },
    { .tinfo = "kf19",  .key = NCKEY_F19, },
    { .tinfo = "kf20",  .key = NCKEY_F20, },
    { .tinfo = "kf21",  .key = NCKEY_F21, },
    { .tinfo = "kf22",  .key = NCKEY_F22, },
    { .tinfo = "kf23",  .key = NCKEY_F23, },
    { .tinfo = "kf24",  .key = NCKEY_F24, },
    { .tinfo = "kf25",  .key = NCKEY_F25, },
    { .tinfo = "kf26",  .key = NCKEY_F26, },
    { .tinfo = "kf27",  .key = NCKEY_F27, },
    { .tinfo = "kf28",  .key = NCKEY_F28, },
    { .tinfo = "kf29",  .key = NCKEY_F29, },
    { .tinfo = "kf30",  .key = NCKEY_F30, },
    { .tinfo = "kf31",  .key = NCKEY_F31, },
    { .tinfo = "kf32",  .key = NCKEY_F32, },
    { .tinfo = "kf33",  .key = NCKEY_F33, },
    { .tinfo = "kf34",  .key = NCKEY_F34, },
    { .tinfo = "kf35",  .key = NCKEY_F35, },
    { .tinfo = "kf36",  .key = NCKEY_F36, },
    { .tinfo = "kf37",  .key = NCKEY_F37, },
    { .tinfo = "kf38",  .key = NCKEY_F38, },
    { .tinfo = "kf39",  .key = NCKEY_F39, },
    { .tinfo = "kf40",  .key = NCKEY_F40, },
    { .tinfo = "kf41",  .key = NCKEY_F41, },
    { .tinfo = "kf42",  .key = NCKEY_F42, },
    { .tinfo = "kf43",  .key = NCKEY_F43, },
    { .tinfo = "kf44",  .key = NCKEY_F44, },
    { .tinfo = "kf45",  .key = NCKEY_F45, },
    { .tinfo = "kf46",  .key = NCKEY_F46, },
    { .tinfo = "kf47",  .key = NCKEY_F47, },
    { .tinfo = "kf48",  .key = NCKEY_F48, },
    { .tinfo = "kf49",  .key = NCKEY_F49, },
    { .tinfo = "kf50",  .key = NCKEY_F50, },
    { .tinfo = "kf51",  .key = NCKEY_F51, },
    { .tinfo = "kf52",  .key = NCKEY_F52, },
    { .tinfo = "kf53",  .key = NCKEY_F53, },
    { .tinfo = "kf54",  .key = NCKEY_F54, },
    { .tinfo = "kf55",  .key = NCKEY_F55, },
    { .tinfo = "kf56",  .key = NCKEY_F56, },
    { .tinfo = "kf57",  .key = NCKEY_F57, },
    { .tinfo = "kf58",  .key = NCKEY_F58, },
    { .tinfo = "kf59",  .key = NCKEY_F59, },
    { .tinfo = "kent",  .key = NCKEY_ENTER, },
    { .tinfo = "kclr",  .key = NCKEY_CLS, },
    { .tinfo = "kc1",   .key = NCKEY_DLEFT, },
    { .tinfo = "kc3",   .key = NCKEY_DRIGHT, },
    { .tinfo = "ka1",   .key = NCKEY_ULEFT, },
    { .tinfo = "ka3",   .key = NCKEY_URIGHT, },
    { .tinfo = "kb2",   .key = NCKEY_CENTER, },
    { .tinfo = "kbeg",  .key = NCKEY_BEGIN, },
    { .tinfo = "kcan",  .key = NCKEY_CANCEL, },
    { .tinfo = "kclo",  .key = NCKEY_CLOSE, },
    { .tinfo = "kcmd",  .key = NCKEY_COMMAND, },
    { .tinfo = "kcpy",  .key = NCKEY_COPY, },
    { .tinfo = "kext",  .key = NCKEY_EXIT, },
    { .tinfo = "kprt",  .key = NCKEY_PRINT, },
    { .tinfo = "krfr",  .key = NCKEY_REFRESH, },
    { .tinfo = "kBEG",  .key = NCKEY_BEGIN, .shift = 1, },
    { .tinfo = "kBEG3", .key = NCKEY_BEGIN, .alt = 1, },
    { .tinfo = "kBEG4", .key = NCKEY_BEGIN, .alt = 1, .shift = 1, },
    { .tinfo = "kBEG5", .key = NCKEY_BEGIN, .ctrl = 1, },
    { .tinfo = "kBEG6", .key = NCKEY_BEGIN, .ctrl = 1, .shift = 1, },
    { .tinfo = "kBEG7", .key = NCKEY_BEGIN, .alt = 1, .ctrl = 1, },
    { .tinfo = "kDC",   .key = NCKEY_DEL, .shift = 1, },
    { .tinfo = "kDC3",  .key = NCKEY_DEL, .alt = 1, },
    { .tinfo = "kDC4",  .key = NCKEY_DEL, .alt = 1, .shift = 1, },
    { .tinfo = "kDC5",  .key = NCKEY_DEL, .ctrl = 1, },
    { .tinfo = "kDC6",  .key = NCKEY_DEL, .ctrl = 1, .shift = 1, },
    { .tinfo = "kDC7",  .key = NCKEY_DEL, .alt = 1, .ctrl = 1, },
    { .tinfo = "kDN",   .key = NCKEY_DOWN, .shift = 1, },
    { .tinfo = "kDN3",  .key = NCKEY_DOWN, .alt = 1, },
    { .tinfo = "kDN4",  .key = NCKEY_DOWN, .alt = 1, .shift = 1, },
    { .tinfo = "kDN5",  .key = NCKEY_DOWN, .ctrl = 1, },
    { .tinfo = "kDN6",  .key = NCKEY_DOWN, .ctrl = 1, .shift = 1, },
    { .tinfo = "kDN7",  .key = NCKEY_DOWN, .alt = 1, .ctrl = 1, },
    { .tinfo = "kEND",  .key = NCKEY_END, .shift = 1, },
    { .tinfo = "kEND3", .key = NCKEY_END, .alt = 1, },
    { .tinfo = "kEND4", .key = NCKEY_END, .alt = 1, .shift = 1, },
    { .tinfo = "kEND5", .key = NCKEY_END, .ctrl = 1, },
    { .tinfo = "kEND6", .key = NCKEY_END, .ctrl = 1, .shift = 1, },
    { .tinfo = "kEND7", .key = NCKEY_END, .alt = 1, .ctrl = 1, },
    { .tinfo = "kHOM",  .key = NCKEY_HOME, .shift = 1, },
    { .tinfo = "kHOM3", .key = NCKEY_HOME, .alt = 1, },
    { .tinfo = "kHOM4", .key = NCKEY_HOME, .alt = 1, .shift = 1, },
    { .tinfo = "kHOM5", .key = NCKEY_HOME, .ctrl = 1, },
    { .tinfo = "kHOM6", .key = NCKEY_HOME, .ctrl = 1, .shift = 1, },
    { .tinfo = "kHOM7", .key = NCKEY_HOME, .alt = 1, .ctrl = 1, },
    { .tinfo = "kIC",   .key = NCKEY_INS, .shift = 1, },
    { .tinfo = "kIC3",  .key = NCKEY_INS, .alt = 1, },
    { .tinfo = "kIC4",  .key = NCKEY_INS, .alt = 1, .shift = 1, },
    { .tinfo = "kIC5",  .key = NCKEY_INS, .ctrl = 1, },
    { .tinfo = "kIC6",  .key = NCKEY_INS, .ctrl = 1, .shift = 1, },
    { .tinfo = "kIC7",  .key = NCKEY_INS, .alt = 1, .ctrl = 1, },
    { .tinfo = "kLFT",  .key = NCKEY_LEFT, .shift = 1, },
    { .tinfo = "kLFT3", .key = NCKEY_LEFT, .alt = 1, },
    { .tinfo = "kLFT4", .key = NCKEY_LEFT, .alt = 1, .shift = 1, },
    { .tinfo = "kLFT5", .key = NCKEY_LEFT, .ctrl = 1, },
    { .tinfo = "kLFT6", .key = NCKEY_LEFT, .ctrl = 1, .shift = 1, },
    { .tinfo = "kLFT7", .key = NCKEY_LEFT, .alt = 1, .ctrl = 1, },
    { .tinfo = "kNXT",  .key = NCKEY_PGDOWN, .shift = 1, },
    { .tinfo = "kNXT3", .key = NCKEY_PGDOWN, .alt = 1, },
    { .tinfo = "kNXT4", .key = NCKEY_PGDOWN, .alt = 1, .shift = 1, },
    { .tinfo = "kNXT5", .key = NCKEY_PGDOWN, .ctrl = 1, },
    { .tinfo = "kNXT6", .key = NCKEY_PGDOWN, .ctrl = 1, .shift = 1, },
    { .tinfo = "kNXT7", .key = NCKEY_PGDOWN, .alt = 1, .ctrl = 1, },
    { .tinfo = "kPRV",  .key = NCKEY_PGUP, .shift = 1, },
    { .tinfo = "kPRV3", .key = NCKEY_PGUP, .alt = 1, },
    { .tinfo = "kPRV4", .key = NCKEY_PGUP, .alt = 1, .shift = 1, },
    { .tinfo = "kPRV5", .key = NCKEY_PGUP, .ctrl = 1, },
    { .tinfo = "kPRV6", .key = NCKEY_PGUP, .ctrl = 1, .shift = 1, },
    { .tinfo = "kPRV7", .key = NCKEY_PGUP, .alt = 1, .ctrl = 1, },
    { .tinfo = "kRIT",  .key = NCKEY_RIGHT, .shift = 1, },
    { .tinfo = "kRIT3", .key = NCKEY_RIGHT, .alt = 1, },
    { .tinfo = "kRIT4", .key = NCKEY_RIGHT, .alt = 1, .shift = 1, },
    { .tinfo = "kRIT5", .key = NCKEY_RIGHT, .ctrl = 1, },
    { .tinfo = "kRIT6", .key = NCKEY_RIGHT, .ctrl = 1, .shift = 1, },
    { .tinfo = "kRIT7", .key = NCKEY_RIGHT, .alt = 1, .ctrl = 1, },
    { .tinfo = "kUP",   .key = NCKEY_UP, .shift = 1, },
    { .tinfo = "kUP3",  .key = NCKEY_UP, .alt = 1, },
    { .tinfo = "kUP4",  .key = NCKEY_UP, .alt = 1, .shift = 1, },
    { .tinfo = "kUP5",  .key = NCKEY_UP, .ctrl = 1, },
    { .tinfo = "kUP6",  .key = NCKEY_UP, .ctrl = 1, .shift = 1, },
    { .tinfo = "kUP7",  .key = NCKEY_UP, .alt = 1, .ctrl = 1, },
    { .tinfo = NULL,    .key = 0, }
  }, *k;
  for(k = keys ; k->tinfo ; ++k){
    char* seq = tigetstr(k->tinfo);
    if(seq == NULL || seq == (char*)-1){
      loginfo("no terminfo declaration for %s", k->tinfo);
      continue;
    }
    if(seq[0] != NCKEY_ESC || strlen(seq) < 2){ // assume ESC prefix + content
      logwarn("invalid escape: %s (0x%x)", k->tinfo, k->key);
      continue;
    }
    unsigned modifiers = (k->shift ? NCKEY_MOD_SHIFT : 0)
                       | (k->alt ? NCKEY_MOD_ALT : 0)
                       | (k->ctrl ? NCKEY_MOD_CTRL : 0);
    if(inputctx_add_input_escape(&ictx->amata, seq, k->key, modifiers)){
      return -1;
    }
    logdebug("support for terminfo's %s: %s", k->tinfo, seq);
  }
  const char* bs = tigetstr("kbs");
  if(bs == NULL){
    logwarn("no backspace key was defined");
  }else{
    if(bs[0] == NCKEY_ESC){
      if(inputctx_add_input_escape(&ictx->amata, bs, NCKEY_BACKSPACE, 0)){
        return -1;
      }
    }else{
      ictx->backspace = bs[0];
    }
  }
#else
  (void)ictx;
#endif
  return 0;
}

// starting from the current amata match point, match any necessary prefix, then
// extract the (possibly empty) content, then match the follow. as we are only
// called from a callback context, and know we've been properly matched, there
// is no error-checking per se (we do require prefix/follow matches, but if
// missed, we just return NULL). indicate empty prefix with "", not NULL.
// updates ictx->amata.matchstart to be pointing past the follow. follow ought
// not be NUL.
static char*
amata_next_kleene(automaton* amata, const char* prefix, char follow1, char follow2){
  char c;
  while( (c = *prefix++) ){
    if(*amata->matchstart != c){
      logerror("matchstart didn't match prefix (%c vs %c)", c, *amata->matchstart);
      return NULL;
    }
    ++amata->matchstart;
  }
  // prefix has been matched. mark start of string and find follow.
  const unsigned char* start = amata->matchstart;
  while(*amata->matchstart != follow1 && *amata->matchstart != follow2){
    ++amata->matchstart;
  }
  char* ret = malloc(amata->matchstart - start + 1);
  if(ret){
    memcpy(ret, start, amata->matchstart - start);
    ret[amata->matchstart - start] = '\0';
  }
  return ret;
}

// starting from the current amata match point, match any necessary prefix, then
// extract the numeric (possibly empty), then match the follow. as we are only
// called from a callback context, and know we've been properly matched, there
// is no error-checking per se (we do require prefix/follow matches, but if
// missed, we just return 0). indicate empty prefix with "", not NULL.
// updates ictx->amata.matchstart to be pointing past the follow. follow ought
// not be a digit nor NUL.
static unsigned
amata_next_numeric(automaton* amata, const char* prefix, char follow){
  char c;
  while( (c = *prefix++) ){
    if(*amata->matchstart != c){
      logerror("matchstart didn't match prefix (%c vs %c)", c, *amata->matchstart);
      return 0;
    }
    ++amata->matchstart;
  }
  // prefix has been matched
  unsigned ret = 0;
  while(isdigit(*amata->matchstart)){
    int addend = *amata->matchstart - '0';
    if((UINT_MAX - addend) / 10 < ret){
      logerror("overflow: %u * 10 + %u > %u", ret, addend, UINT_MAX);
    }
    ret *= 10;
    ret += addend;
    ++amata->matchstart;
  }
  char candidate = *amata->matchstart++;
  if(candidate != follow){
    logerror("didn't see follow (%c vs %c)", candidate, follow);
    return 0;
  }
  return ret;
}

// same deal as amata_next_numeric, but returns a heap-allocated string.
// strings always end with ST ("x1b\\"). this one *does* return NULL on
// either a match failure or an alloc failure.
static char*
amata_next_string(automaton* amata, const char* prefix){
  return amata_next_kleene(amata, prefix, '\x1b', '\x07');
}

static inline void
send_synth_signal(int sig){
  if(sig){
#ifndef __MINGW32__
    raise(sig);
#endif
  }
}

static void
mark_pipe_ready(ipipe pipes[static 2]){
  char sig = 1;
#ifndef __MINGW32__
  if(write(pipes[1], &sig, sizeof(sig)) != 1){
    logwarn("error writing to pipe (%d) (%s)", pipes[1], strerror(errno));
#else
  DWORD wrote;
  if(!WriteFile(pipes[1], &sig, sizeof(sig), &wrote, NULL) || wrote != sizeof(sig)){
    logwarn("error writing to pipe");
#endif
  }else{
    loginfo("wrote to readiness pipe");
  }
}

// shove the assembled input |tni| into the input queue (if there's room, and
// we're not draining, and we haven't hit EOF). send any synthesized signal as
// the last thing we do. if Ctrl or Shift are among the modifiers, we replace
// any lowercase letter with its uppercase form, to maintain compatibility with
// other input methods.
//
// note that this w orks entirely off 'modifiers', not the obsolete
// shift/alt/ctrl booleans, which it neither sets nor tests!
static void
load_ncinput(inputctx* ictx, ncinput *tni){
  int synth = 0;
  if(tni->modifiers & (NCKEY_MOD_CTRL | NCKEY_MOD_SHIFT | NCKEY_MOD_CAPSLOCK)){
    // when ctrl/shift are used with an ASCII (0..127) lowercase letter, always
    // supply the capitalized form, to maintain compatibility among solutions.
    if(tni->id < 0x7f){
      if(islower(tni->id)){
        tni->id = toupper(tni->id);
      }
    }
  }
  // if the kitty keyboard protocol is in use, any input without an explicit
  // evtype can be safely considered a PRESS.
  if(ictx->kittykbd){
    if(tni->evtype == NCTYPE_UNKNOWN){
      tni->evtype = NCTYPE_PRESS;
    }
  }
  if(tni->modifiers == NCKEY_MOD_CTRL){ // exclude all other modifiers
    if(ictx->linesigs){
      if(tni->id == 'C'){
        synth = SIGINT;
      }else if(tni->id == 'Z'){
        synth = SIGSTOP;
      }else if(tni->id == '\\'){
        synth = SIGQUIT;
      }
    }
  }
  inc_input_events(ictx);
  if(ictx->drain || ictx->stdineof){
    send_synth_signal(synth);
    return;
  }
  pthread_mutex_lock(&ictx->ilock);
  if(ictx->ivalid == ictx->isize){
    pthread_mutex_unlock(&ictx->ilock);
    logwarn("dropping input 0x%08x", tni->id);
    inc_input_errors(ictx);
    send_synth_signal(synth);
    return;
  }
  ncinput* ni = ictx->inputs + ictx->iwrite;
  memcpy(ni, tni, sizeof(*tni));
  // perform final normalizations
  if(ni->id == 0x7f || ni->id == 0x8){
    ni->id = NCKEY_BACKSPACE;
  }else if(ni->id == '\n' || ni->id == '\r'){
    ni->id = NCKEY_ENTER;
  }else if(ni->id == ictx->backspace){
    ni->id = NCKEY_BACKSPACE;
  }else if(ni->id > 0 && ni->id <= 26 && ni->id != '\t'){
    ni->id = ni->id + 'A' - 1;
    ni->modifiers |= NCKEY_MOD_CTRL;
  }
  if(++ictx->iwrite == ictx->isize){
    ictx->iwrite = 0;
  }
  ++ictx->ivalid;
  // FIXME we don't always need to write here; write if ictx->ivalid was 0, and
  // also write *from the client context* if we empty the input buffer there..?
  mark_pipe_ready(ictx->readypipes);
  pthread_mutex_unlock(&ictx->ilock);
  pthread_cond_broadcast(&ictx->icond);
  send_synth_signal(synth);
}

static void
pixelmouse_click(inputctx* ictx, ncinput* ni, long y, long x){
  --x;
  --y;
  if(ictx->ti->cellpxy == 0 || ictx->ti->cellpxx == 0){
    logerror("pixelmouse event without pixel info (%ld/%ld)", y, x);
    inc_input_errors(ictx);
    return;
  }
  ni->ypx = y % ictx->ti->cellpxy;
  ni->xpx = x % ictx->ti->cellpxx;
  y /= ictx->ti->cellpxy;
  x /= ictx->ti->cellpxx;
  x -= ictx->lmargin;
  y -= ictx->tmargin;
  // convert from 1- to 0-indexing, and account for margins
  if(x < 0 || y < 0){ // click was in margins, drop it
    logwarn("dropping click in margins %ld/%ld", y, x);
    return;
  }
  if((unsigned)x >= ictx->ti->dimx - (ictx->rmargin + ictx->lmargin)){
    logwarn("dropping click in margins %ld/%ld", y, x);
    return;
  }
  if((unsigned)y >= ictx->ti->dimy - (ictx->bmargin + ictx->tmargin)){
    logwarn("dropping click in margins %ld/%ld", y, x);
    return;
  }
  ni->y = y;
  ni->x = x;
  load_ncinput(ictx, ni);
}

// ictx->numeric, ictx->p3, and ictx->p2 have the two parameters. we're using
// SGR (1006) mouse encoding, so use the final character to determine release
// ('M' for click, 'm' for release).
static void
mouse_click(inputctx* ictx, unsigned release, char follow){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[<", ';');
  long x = amata_next_numeric(&ictx->amata, "", ';');
  long y = amata_next_numeric(&ictx->amata, "", follow);
  ncinput tni = {
    .ctrl = mods & 0x10,
    .alt = mods & 0x08,
    .shift = mods & 0x04,
  };
  tni.modifiers = (tni.shift ? NCKEY_MOD_SHIFT : 0)
                  | (tni.ctrl ? NCKEY_MOD_CTRL : 0)
                  | (tni.alt ? NCKEY_MOD_ALT : 0);
      if (mods & 32) {
      tni.modifiers |= NCKEY_MOD_MOTION;
      }
  // SGR mouse reporting: lower two bits signify base button + {0, 1, 2} press
  // and no button pressed/release/{3}. bit 5 indicates motion. bits 6 and 7
  // select device groups: 64 is buttons 4--7, 128 is 8--11. a pure motion
  // report (no button) is 35 (32 + 3 (no button pressed)) with (oddly enough)
  // 'M' (i.e. release == true).
  if(release){
    tni.evtype = NCTYPE_RELEASE;
  }else{
    tni.evtype = NCTYPE_PRESS;
  }
  if(mods % 4 == 3){
    tni.id = NCKEY_MOTION;
    tni.evtype = NCTYPE_RELEASE;
  }else{
    if(mods < 64){
      tni.id = NCKEY_BUTTON1 + (mods % 4);
    }else if(mods >= 64 && mods < 128){
      tni.id = NCKEY_BUTTON4 + (mods % 4);
    }else if(mods >= 128 && mods < 192){
      tni.id = NCKEY_BUTTON8 + (mods % 4);
    }
  }
  if(ictx->ti->pixelmice){
    if(ictx->ti->cellpxx == 0){
      logerror("pixelmouse but no pixel info");
    }
    return pixelmouse_click(ictx, &tni, y, x);
  }
  x -= (1 + ictx->lmargin);
  y -= (1 + ictx->tmargin);
  // convert from 1- to 0-indexing, and account for margins
  if(x < 0 || y < 0){ // click was in margins, drop it
    logwarn("dropping click in margins %ld/%ld", y, x);
    return;
  }
  if((unsigned)x >= ictx->ti->dimx - (ictx->rmargin + ictx->lmargin)){
    logwarn("dropping click in margins %ld/%ld", y, x);
    return;
  }
  if((unsigned)y >= ictx->ti->dimy - (ictx->bmargin + ictx->tmargin)){
    logwarn("dropping click in margins %ld/%ld", y, x);
    return;
  }
  tni.x = x;
  tni.y = y;
  tni.ypx = -1;
  tni.xpx = -1;
  load_ncinput(ictx, &tni);
}

static int
mouse_press_cb(inputctx* ictx){
  mouse_click(ictx, 0, 'M');
  return 2;
}

static int
mouse_release_cb(inputctx* ictx){
  mouse_click(ictx, 1, 'm');
  return 2;
}

static int
cursor_location_cb(inputctx* ictx){
  unsigned y = amata_next_numeric(&ictx->amata, "\x1b[", ';') - 1;
  unsigned x = amata_next_numeric(&ictx->amata, "", 'R') - 1;
  // the first one doesn't go onto the queue; consume it here
  pthread_mutex_lock(&ictx->clock);
  --ictx->coutstanding;
  if(ictx->initdata){
    pthread_mutex_unlock(&ictx->clock);
    ictx->initdata->cursory = y;
    ictx->initdata->cursorx = x;
    return 2;
  }
  if(ictx->cvalid == ictx->csize){
    pthread_mutex_unlock(&ictx->clock);
    logwarn("dropping cursor location report %u/%u", y, x);
    inc_input_errors(ictx);
  }else{
    cursorloc* cloc = &ictx->csrs[ictx->cwrite];
    if(++ictx->cwrite == ictx->csize){
      ictx->cwrite = 0;
    }
    cloc->y = y;
    cloc->x = x;
    ++ictx->cvalid;
    pthread_mutex_unlock(&ictx->clock);
    pthread_cond_broadcast(&ictx->ccond);
    loginfo("cursor location: %u/%u", y, x);
  }
  return 2;
}

static int
geom_cb(inputctx* ictx){
  unsigned kind = amata_next_numeric(&ictx->amata, "\x1b[", ';');
  unsigned y = amata_next_numeric(&ictx->amata, "", ';');
  unsigned x = amata_next_numeric(&ictx->amata, "", 't');
  if(kind == 4){ // pixel geometry
    if(ictx->initdata){
      ictx->initdata->pixy = y;
      ictx->initdata->pixx = x;
    }
    loginfo("pixel geom report %d/%d", y, x);
  }else if(kind == 8){ // cell geometry
    if(ictx->initdata){
      ictx->initdata->dimy = y;
      ictx->initdata->dimx = x;
    }
    loginfo("cell geom report %d/%d", y, x);
  }else{
    logerror("invalid geom report type: %d", kind);
    return -1;
  }
  return 2;
}

static void
xtmodkey(inputctx* ictx, int val, int mods){
  assert(mods >= 0);
  assert(val > 0);
  logdebug("v/m %d %d", val, mods);
  ncinput tni = {
    .id = val,
    .evtype = NCTYPE_UNKNOWN,
  };
  if(mods == 2 || mods == 4 || mods == 6 || mods == 8 || mods == 10
      || mods == 12 || mods == 14 || mods == 16){
    tni.shift = 1;
    tni.modifiers |= NCKEY_MOD_SHIFT;
  }
  if(mods == 5 || mods == 6 || mods == 7 || mods == 8 ||
      (mods >= 13 && mods <= 16)){
    tni.ctrl = 1;
    tni.modifiers |= NCKEY_MOD_CTRL;
  }
  if(mods == 3 || mods == 4 || mods == 7 || mods == 8 || mods == 11
      || mods == 12 || mods == 15 || mods == 16){
    tni.alt = 1;
    tni.modifiers |= NCKEY_MOD_ALT;
  }
  if(mods >= 9 && mods <= 16){
    tni.modifiers |= NCKEY_MOD_META;
  }
  load_ncinput(ictx, &tni);
}

static uint32_t
kitty_functional(uint32_t val){
  if(val >= 57344 && val <= 63743){
    if(val >= 57376 && val <= 57398){
      val = NCKEY_F13 + val - 57376;
    }else if(val >= 57428 && val <= 57440){
      val = NCKEY_MEDIA_PLAY + val - 57428;
    }else if(val >= 57399 && val <= 57408){
      val = '0' + val - 57399;
    }else if(val >= 57441 && val <= 57454){ // up through NCKEY_L5SHIFT
      val = NCKEY_LSHIFT + val - 57441;
    }else switch(val){
      case 57358: val = NCKEY_CAPS_LOCK; break;
      case 57400: val = '1'; break;
      case 57359: val = NCKEY_SCROLL_LOCK; break;
      case 57360: val = NCKEY_NUM_LOCK; break;
      case 57361: val = NCKEY_PRINT_SCREEN; break;
      case 57362: val = NCKEY_PAUSE; break;
      case 57363: val = NCKEY_MENU; break;
      case 57409: val = '.'; break;
      case 57410: val = '/'; break;
      case 57411: val = '*'; break;
      case 57412: val = '-'; break;
      case 57413: val = '+'; break;
      case 57414: val = NCKEY_ENTER; break;
      case 57415: val = '='; break;
      case 57416: val = NCKEY_SEPARATOR; break;
      case 57417: val = NCKEY_LEFT; break;
      case 57418: val = NCKEY_RIGHT; break;
      case 57419: val = NCKEY_UP; break;
      case 57420: val = NCKEY_DOWN; break;
      case 57421: val = NCKEY_PGUP; break;
      case 57422: val = NCKEY_PGDOWN; break;
      case 57423: val = NCKEY_HOME; break;
      case 57424: val = NCKEY_END; break;
      case 57425: val = NCKEY_INS; break;
      case 57426: val = NCKEY_DEL; break;
      case 57427: val = NCKEY_BEGIN; break;
    }
  }else{
    switch(val){
      case 0xd: val = NCKEY_ENTER; break;
    }
  }
  return val;
}

static void
kitty_kbd_txt(inputctx* ictx, int val, int mods, uint32_t *txt, int evtype){
  assert(evtype >= 0);
  assert(mods >= 0);
  assert(val > 0);
  logdebug("v/m/e %d %d %d", val, mods, evtype);
  // "If the modifier field is not present in the escape code, its default value
  //  is 1 which means no modifiers."
  if(mods == 0){
    mods = 1;
  }
  ncinput tni = {
    .id = kitty_functional(val),
    .shift = mods && !!((mods - 1) & 0x1),
    .alt = mods && !!((mods - 1) & 0x2),
    .ctrl = mods && !!((mods - 1) & 0x4),
    .modifiers = mods - 1,
  };
  switch(evtype){
    case 0:
      __attribute__ ((fallthrough));
    case 1:
      tni.evtype = NCTYPE_PRESS;
      break;
    case 2:
      tni.evtype = NCTYPE_REPEAT;
      break;
    case 3:
      tni.evtype = NCTYPE_RELEASE;
      break;
    default:
      tni.evtype = NCTYPE_UNKNOWN;
      break;
  }
  //note: if we don't set eff_text here, it will be set to .id later.
  if(txt && txt[0]!=0){
    for(int i=0 ; i<NCINPUT_MAX_EFF_TEXT_CODEPOINTS ; i++){
      tni.eff_text[i] = txt[i];
    }
      if (ncinput_ctrl_p(&tni) && txt[0] < 127 && txt[1] == 0) {
          tni.eff_text[0] &= 0x1f;
      }
  }
  load_ncinput(ictx, &tni);
}

static void
kitty_kbd(inputctx* ictx, int val, int mods, int evtype){
  kitty_kbd_txt(ictx, val, mods, NULL, evtype);
}

static int
kitty_cb_simple(inputctx* ictx){
  unsigned val = amata_next_numeric(&ictx->amata, "\x1b[", 'u');
  val = kitty_functional(val);
  kitty_kbd(ictx, val, 0, 0);
  return 2;
}

static int
kitty_cb(inputctx* ictx){
  unsigned val = amata_next_numeric(&ictx->amata, "\x1b[", ';');
  unsigned mods = amata_next_numeric(&ictx->amata, "", 'u');
  kitty_kbd(ictx, val, mods, 0);
  return 2;
}

static int 
kitty_cb_atxtn(inputctx* ictx, int n, int with_event){
  uint32_t txt[5]={0};
  unsigned val = amata_next_numeric(&ictx->amata, "\x1b[", ';');
  unsigned ev = 0;
  unsigned mods = 0;
  if (with_event) {
    mods = amata_next_numeric(&ictx->amata, "", ':');
    ev = amata_next_numeric(&ictx->amata, "", ';');
  } else {
    mods = amata_next_numeric(&ictx->amata, "", ';');
  }
  for (int i = 0; i<n; i++) {
    txt[i] = amata_next_numeric(&ictx->amata, "", (i==n-1)?'u':';');
  }
  kitty_kbd_txt(ictx, val, mods, txt, ev);
  return 2;
}

static int
kitty_cb_atxt1(inputctx* ictx){
  return kitty_cb_atxtn(ictx, 1, 0);
}

static int
kitty_cb_atxt2(inputctx* ictx){
  return kitty_cb_atxtn(ictx, 2, 0);
}

static int
kitty_cb_atxt3(inputctx* ictx){
  return kitty_cb_atxtn(ictx, 3, 0);
}

static int
kitty_cb_atxt4(inputctx* ictx){
  return kitty_cb_atxtn(ictx, 4, 0);
}


static int
kitty_cb_complex_atxt1(inputctx* ictx){
  return kitty_cb_atxtn(ictx, 1, 1);
}

static int
kitty_cb_complex_atxt2(inputctx* ictx){
  return kitty_cb_atxtn(ictx, 2, 1);
}

static int
kitty_cb_complex_atxt3(inputctx* ictx){
  return kitty_cb_atxtn(ictx, 3, 1);
}

static int
kitty_cb_complex_atxt4(inputctx* ictx){
  return kitty_cb_atxtn(ictx, 4, 1);
}

static uint32_t
legacy_functional(uint32_t id){
  switch(id){
    case 2: id = NCKEY_INS; break;
    case 3: id = NCKEY_DEL; break;
    case 5: id = NCKEY_PGUP; break;
    case 6: id = NCKEY_PGDOWN; break;
    case 7: id = NCKEY_HOME; break;
    case 8: id = NCKEY_END; break;
    case 11: id = NCKEY_F01; break;
    case 12: id = NCKEY_F02; break;
    case 13: id = NCKEY_F03; break;
    case 14: id = NCKEY_F04; break;
    case 15: id = NCKEY_F05; break;
    case 17: id = NCKEY_F06; break;
    case 18: id = NCKEY_F07; break;
    case 19: id = NCKEY_F08; break;
    case 20: id = NCKEY_F09; break;
    case 21: id = NCKEY_F10; break;
    case 23: id = NCKEY_F11; break;
    case 24: id = NCKEY_F12; break;
  }
  return id;
}

static int
simple_cb_begin(inputctx* ictx){
  kitty_kbd(ictx, NCKEY_BEGIN, 0, 0);
  return 2;
}

static int
kitty_cb_functional(inputctx* ictx){
  unsigned val = amata_next_numeric(&ictx->amata, "\x1b[", ';');
  unsigned mods = amata_next_numeric(&ictx->amata, "", ':');
  unsigned ev = amata_next_numeric(&ictx->amata, "", '~');
  uint32_t kval = kitty_functional(val);
  if(kval == val){
    kval = legacy_functional(val);
  }
  kitty_kbd(ictx, kval, mods, ev);
  return 2;
}

static int
wezterm_cb(inputctx* ictx){
  unsigned val = amata_next_numeric(&ictx->amata, "\x1b[", ';');
  unsigned mods = amata_next_numeric(&ictx->amata, "", '~');
  uint32_t kval = legacy_functional(val);
  kitty_kbd(ictx, kval, mods, 0);
  return 2;
}

static int
legacy_cb_f1(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", 'P');
  kitty_kbd(ictx, NCKEY_F01, mods, 0);
  return 2;
}

static int
legacy_cb_f2(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", 'Q');
  kitty_kbd(ictx, NCKEY_F02, mods, 0);
  return 2;
}

static int
legacy_cb_f4(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", 'S');
  kitty_kbd(ictx, NCKEY_F04, mods, 0);
  return 2;
}

static int
kitty_cb_f1(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", ':');
  unsigned ev = amata_next_numeric(&ictx->amata, "", 'P');
  kitty_kbd(ictx, NCKEY_F01, mods, ev);
  return 2;
}

static int
kitty_cb_f2(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", ':');
  unsigned ev = amata_next_numeric(&ictx->amata, "", 'Q');
  kitty_kbd(ictx, NCKEY_F02, mods, ev);
  return 2;
}

static int
kitty_cb_f3(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", ':');
  unsigned ev = amata_next_numeric(&ictx->amata, "", 'R');
  kitty_kbd(ictx, NCKEY_F03, mods, ev);
  return 2;
}

static int
kitty_cb_f4(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", ':');
  unsigned ev = amata_next_numeric(&ictx->amata, "", 'S');
  kitty_kbd(ictx, NCKEY_F04, mods, ev);
  return 2;
}

static int
legacy_cb_right(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", 'C');
  kitty_kbd(ictx, NCKEY_RIGHT, mods, 0);
  return 2;
}

static int
legacy_cb_left(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", 'D');
  kitty_kbd(ictx, NCKEY_LEFT, mods, 0);
  return 2;
}

static int
legacy_cb_down(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", 'B');
  kitty_kbd(ictx, NCKEY_DOWN, mods, 0);
  return 2;
}

static int
legacy_cb_up(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", 'A');
  kitty_kbd(ictx, NCKEY_UP, mods, 0);
  return 2;
}

static int
kitty_cb_right(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", ':');
  unsigned ev = amata_next_numeric(&ictx->amata, "", 'C');
  kitty_kbd(ictx, NCKEY_RIGHT, mods, ev);
  return 2;
}

static int
kitty_cb_left(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", ':');
  unsigned ev = amata_next_numeric(&ictx->amata, "", 'D');
  kitty_kbd(ictx, NCKEY_LEFT, mods, ev);
  return 2;
}

static int
kitty_cb_down(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", ':');
  unsigned ev = amata_next_numeric(&ictx->amata, "", 'B');
  kitty_kbd(ictx, NCKEY_DOWN, mods, ev);
  return 2;
}

static int
kitty_cb_up(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", ':');
  unsigned ev = amata_next_numeric(&ictx->amata, "", 'A');
  kitty_kbd(ictx, NCKEY_UP, mods, ev);
  return 2;
}

static int
legacy_cb_begin(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", 'E');
  kitty_kbd(ictx, NCKEY_BEGIN, mods, 0);
  return 2;
}

static int
legacy_cb_end(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", 'F');
  kitty_kbd(ictx, NCKEY_END, mods, 0);
  return 2;
}

static int
legacy_cb_home(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", 'H');
  kitty_kbd(ictx, NCKEY_HOME, mods, 0);
  return 2;
}

static int
kitty_cb_begin(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", ':');
  unsigned ev = amata_next_numeric(&ictx->amata, "", 'E');
  kitty_kbd(ictx, NCKEY_BEGIN, mods, ev);
  return 2;
}

static int
kitty_cb_end(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", ':');
  unsigned ev = amata_next_numeric(&ictx->amata, "", 'F');
  kitty_kbd(ictx, NCKEY_END, mods, ev);
  return 2;
}

static int
kitty_cb_home(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[1;", ':');
  unsigned ev = amata_next_numeric(&ictx->amata, "", 'H');
  kitty_kbd(ictx, NCKEY_HOME, mods, ev);
  return 2;
}

static int
kitty_cb_complex(inputctx* ictx){
  unsigned val = amata_next_numeric(&ictx->amata, "\x1b[", ';');
  unsigned mods = amata_next_numeric(&ictx->amata, "", ':');
  unsigned ev = amata_next_numeric(&ictx->amata, "", 'u');
  val = kitty_functional(val);
  kitty_kbd(ictx, val, mods, ev);
  return 2;
}

static int
kitty_keyboard_cb(inputctx* ictx){
  unsigned level = amata_next_numeric(&ictx->amata, "\x1b[?", 'u');
  if(ictx->initdata){
    ictx->initdata->kbdlevel = level;
  }
  loginfo("kitty keyboard level %u (was %u)", level, ictx->kittykbd);
  ictx->kittykbd = level;
  return 2;
}

static int
xtmodkey_cb(inputctx* ictx){
  unsigned mods = amata_next_numeric(&ictx->amata, "\x1b[27;", ';');
  unsigned val = amata_next_numeric(&ictx->amata, "", '~');
  xtmodkey(ictx, val, mods);
  return 2;
}

// the only xtsmgraphics reply with a single Pv arg is color registers
static int
xtsmgraphics_cregs_cb(inputctx* ictx){
  unsigned pv = amata_next_numeric(&ictx->amata, "\x1b[?1;0;", 'S');
  if(ictx->initdata){
    ictx->initdata->color_registers = pv;
  }
  loginfo("sixel color registers: %d", pv);
  return 2;
}

// the only xtsmgraphics reply with a dual Pv arg we want is sixel geometry
static int
xtsmgraphics_sixel_cb(inputctx* ictx){
  unsigned width = amata_next_numeric(&ictx->amata, "\x1b[?2;0;", ';');
  unsigned height = amata_next_numeric(&ictx->amata, "", 'S');
  if(ictx->initdata){
    ictx->initdata->sixelx = width;
    ictx->initdata->sixely = height;
  }
  loginfo("max sixel geometry: %dx%d", height, width);
  return 2;
}

static void
handoff_initial_responses_late(inputctx* ictx){
  bool sig = false;
  pthread_mutex_lock(&ictx->ilock);
  if(ictx->initdata_complete && ictx->initdata){
      loginfo("handoff late");
     ictx->initdata = NULL;
     sig = true;
  }
  pthread_mutex_unlock(&ictx->ilock);
  if(sig){
    pthread_cond_broadcast(&ictx->icond);
    loginfo("handing off initial responses");
  }
}


// mark the initdata as complete, but don't yet broadcast it off.
static void
handoff_initial_responses_early(inputctx* ictx)
{
    loginfo("handoff early %p", ictx->initdata);
  pthread_mutex_lock(&ictx->ilock);
  // set initdata_complete, but don't clear initdata
  ictx->initdata_complete = ictx->initdata;
  pthread_mutex_unlock(&ictx->ilock);
}

// if XTSMGRAPHICS responses were provided, but DA1 didn't advertise sixel
// support, we need scrub those responses, lest we try to use Sixel.
static inline void
scrub_sixel_responses(struct initial_responses* idata){
  if(idata->color_registers || idata->sixelx || idata->sixely){
    logwarn("answered XTSMGRAPHICS, but no sixel in DA1");
    idata->color_registers = 0;
    idata->sixelx = 0;
    idata->sixely = 0;
  }
}

// annoyingly, alacritty (well, branches of alacritty) supports Sixel, but
// does not indicate this in their Primary Device Attributes response (there
// is no room for attributes in a VT102-style DA1, which alacritty uses).
// so, iff we've determined we're alacritty, don't scrub out Sixel details.
static int
da1_vt102_cb(inputctx* ictx){
  loginfo("read primary device attributes");
  if(ictx->initdata){
    if(ictx->initdata->qterm != TERMINAL_ALACRITTY){
      scrub_sixel_responses(ictx->initdata);
    }
    handoff_initial_responses_early(ictx);
  }
  return 1;
}

static int
da1_cb(inputctx* ictx){
  loginfo("read primary device attributes");
  if(ictx->initdata){
    scrub_sixel_responses(ictx->initdata);
    handoff_initial_responses_early(ictx);
  }
  return 1;
}

static int
da1_attrs_cb(inputctx* ictx){
  loginfo("read primary device attributes");
  unsigned val = amata_next_numeric(&ictx->amata, "\x1b[?", ';');
  char* attrlist = amata_next_kleene(&ictx->amata, "", 'c', 0);
  logdebug("DA1: %u [%s]", val, attrlist);
  if(ictx->initdata){
    int foundsixel = 0;
    unsigned curattr = 0;
    for(const char* a = attrlist ; *a ; ++a){
      if(isdigit(*a)){
        curattr *= 10;
        curattr += *a - '0';
      }else if(*a == ';'){
        if(curattr == 4){
          foundsixel = 1;
          if(ictx->initdata->color_registers <= 0){
            ictx->initdata->color_registers = 256;
          }
        }else if(curattr == 28){
          ictx->initdata->rectangular_edits = true;
        }
        curattr = 0;
      }
    }
    if(curattr == 4){
      foundsixel = 1;
      if(ictx->initdata->color_registers <= 0){
        ictx->initdata->color_registers = 256;
      }
    }else if(curattr == 28){
      ictx->initdata->rectangular_edits = true;
    }
    if(!foundsixel){
      scrub_sixel_responses(ictx->initdata);
    }
    handoff_initial_responses_early(ictx);
  }
  free(attrlist);
  return 1;
}

// GNU screen primarily identifies itself via an "83" as the first parameter
// of its DA2 reply. the version is the second parameter.
static int
da2_screen_cb(inputctx* ictx){
  if(ictx->initdata == NULL){
    return 2;
  }
  if(ictx->initdata->qterm != TERMINAL_UNKNOWN){
    logwarn("already identified term (%d)", ictx->initdata->qterm);
    return 2;
  }
  unsigned ver = amata_next_numeric(&ictx->amata, "\x1b[>83;", ';');
  if(ver < 10000){
    logwarn("version %u doesn't look like GNU screen", ver);
    return 2;
  }
  char verstr[9]; // three two-digit components plus two delims
  int s = snprintf(verstr, sizeof(verstr), "%u.%02u.%02u",
                   ver / 10000, ver / 100 % 100, ver % 100);
  if(s < 0 || (unsigned)s >= sizeof(verstr)){
    logwarn("bad screen version %u", ver);
    return 2;
  }
  ictx->initdata->version = strdup(verstr);
  ictx->initdata->qterm = TERMINAL_GNUSCREEN;
  return 2;
}

// we use secondary device attributes to recognize the alacritty crate
// version, and to ascertain the version of old, pre-XTVERSION XTerm.
static int
da2_cb(inputctx* ictx){
  loginfo("read secondary device attributes");
  if(ictx->initdata == NULL){
    return 2;
  }
  amata_next_numeric(&ictx->amata, "\x1b[>", ';');
  unsigned pv = amata_next_numeric(&ictx->amata, "", ';');
  int maj, min, patch;
  if(pv == 0){
    return 2;
  }
  // modern XTerm replies to XTVERSION, but older versions require extracting
  // the version from secondary DA
  if(ictx->initdata->qterm == TERMINAL_XTERM){
    if(ictx->initdata->version == NULL){
      char ver[8];
      int s = snprintf(ver, sizeof(ver), "%u", pv);
      if(s < 0 || (unsigned)s >= sizeof(ver)){
        logerror("bad version: %u", pv);
      }else{
        ictx->initdata->version = strdup(ver);
      }
      return 2;
    }
  }
  // SDA yields up Alacritty's crate version, but it doesn't unambiguously
  // identify Alacritty. If we've got any other version information, don't
  // use this. use the second parameter (Pv).
  if(ictx->initdata->qterm != TERMINAL_UNKNOWN || ictx->initdata->version){
    loginfo("termtype was %d %s, not alacritty", ictx->initdata->qterm,
            ictx->initdata->version);
    return 2;
  }
  // if a termname was manually supplied in setup, it was written to the env
  const char* termname = getenv("TERM");
  if(termname == NULL || strstr(termname, "alacritty") == NULL){
    loginfo("termname was [%s], probably not alacritty",
            termname ? termname : "unset");
    return 2;
  }
  maj = pv / 10000;
  min = (pv % 10000) / 100;
  patch = pv % 100;
  if(maj >= 100 || min >= 100 || patch >= 100){
    return 2;
  }
  // 3x components (two digits max each), 2x '.', NUL would suggest 9 bytes,
  // but older gcc __builtin___sprintf_chk insists on 13. fuck it. FIXME.
  char* buf = malloc(13);
  if(buf){
    sprintf(buf, "%d.%d.%d", maj, min, patch);
    loginfo("might be alacritty %s", buf);
    ictx->initdata->version = buf;
    ictx->initdata->qterm = TERMINAL_ALACRITTY;
  }
  return 2;
}

// weird form of Ternary Device Attributes used only by WezTerm
static int
wezterm_tda_cb(inputctx* ictx){
  if(ictx->initdata){
    loginfo("read ternary device attributes");
  }
  return 2;
}

static int
kittygraph_cb(inputctx* ictx){
  loginfo("kitty graphics message");
  if(ictx->initdata){
    ictx->initdata->kitty_graphics = 1;
  }
  return 2;
}

static int
decrpm_pixelmice(inputctx* ictx){
  unsigned ps = amata_next_numeric(&ictx->amata, "\x1b[?1016;", '$');
  loginfo("received decrpm 1016 %u", ps);
  if(ps == 2){
    if(ictx->initdata){
      ictx->initdata->pixelmice = 1;
    }
  }
  return 2;
}

static int
decrpm_asu_cb(inputctx* ictx){
  unsigned ps = amata_next_numeric(&ictx->amata, "\x1b[?2026;", '$');
  loginfo("received decrpm 2026 %u", ps);
  if(ps == 2){
    if(ictx->initdata){
      ictx->initdata->appsync_supported = 1;
    }
  }
  return 2;
}

static int
get_default_color(const char* str, uint32_t* color){
  int r, g, b;
  if(sscanf(str, "%02x/%02x/%02x", &r, &g, &b) == 3){
    // great! =]
  }else if(sscanf(str, "%04x/%04x/%04x", &r, &g, &b) == 3){
    r /= 256;
    g /= 256;
    b /= 256;
  }else{
    logerror("couldn't extract rgb from %s", str);
    return -1;
  }
  if(r < 0 || g < 0 || b < 0){
    logerror("invalid colors %d %d %d", r, g, b);
    return -1;
  }
  *color = (r << 16u) | (g << 8u) | b;
  return 0;
}

static int
bgdef_cb(inputctx* ictx){
  if(ictx->initdata){
    char* str = amata_next_string(&ictx->amata, "\x1b]11;rgb:");
    if(str == NULL){
      logerror("empty bg string");
    }else{
      if(get_default_color(str, &ictx->initdata->bg) == 0){
        ictx->initdata->got_bg = true;
        loginfo("default background 0x%06x", ictx->initdata->bg);
      }
      free(str);
    }
  }
  return 2;
}

static int
fgdef_cb(inputctx* ictx){
  if(ictx->initdata){
    char* str = amata_next_string(&ictx->amata, "\x1b]10;rgb:");
    if(str == NULL){
      logerror("empty fg string");
    }else{
      if(get_default_color(str, &ictx->initdata->fg) == 0){
        ictx->initdata->got_fg = true;
        loginfo("default foreground 0x%06x", ictx->initdata->fg);
      }
      free(str);
    }
  }
  return 2;
}

static int
palette_cb(inputctx* ictx){
  if(ictx->initdata){
    unsigned idx = amata_next_numeric(&ictx->amata, "\x1b]4;", ';');
    char* str = amata_next_string(&ictx->amata, "rgb:");
    if(idx > sizeof(ictx->initdata->palette.chans) / sizeof(*ictx->initdata->palette.chans)){
      logerror("invalid index %u", idx);
    }else if(str == NULL){
      logerror("empty palette string");
    }else{
      if(get_default_color(str, &ictx->initdata->palette.chans[idx]) == 0){
        if((int)idx > ictx->initdata->maxpaletteread){
          ictx->initdata->maxpaletteread = idx;
        }
        logverbose("index %u 0x%06x", idx, ictx->initdata->palette.chans[idx]);
      }
      free(str);
    }
  }
  return 2;
}

static int
bracket_start_cb(inputctx* ictx)
{
    loginfo("bracket start");
    ictx->in_bracketed_paste = true;
    return 2;
}

static int
bracket_end_cb(inputctx* ictx)
{
    loginfo("bracket end");
    ictx->in_bracketed_paste = false;

    fbuf_putc(&ictx->paste_buffer, '\0');
    ncinput pni = {
        .id = NCKEY_PASTE,
        .evtype = NCTYPE_UNKNOWN,
        .paste_content = ictx->paste_buffer.buf,
    };
    ictx->paste_buffer.buf = 0;
    ictx->paste_buffer.size = 0;
    ictx->paste_buffer.used = 0;
    fbuf_init_small(&ictx->paste_buffer);
    load_ncinput(ictx, &pni);

    return 2;
}

static int
extract_xtversion(inputctx* ictx, const char* str, char suffix){
  size_t slen = strlen(str);
  if(slen == 0){
    logwarn("empty version in xtversion");
    return -1;
  }
  if(suffix){
    if(str[slen - 1] != suffix){
      return -1;
    }
    --slen;
  }
  if(slen == 0){
    logwarn("empty version in xtversion");
    return -1;
  }
  ictx->initdata->version = strndup(str, slen);
  return 0;
}

static int
xtversion_cb(inputctx* ictx){
  if(ictx->initdata == NULL){
    return 2;
  }
  char* xtversion = amata_next_string(&ictx->amata, "\x1bP>|");
  if(xtversion == NULL){
    logwarn("empty xtversion");
    return 2; // don't replay as input
  }
  static const struct {
    const char* prefix;
    char suffix;
    queried_terminals_e term;
  } xtvers[] = {
    { .prefix = "XTerm(", .suffix = ')', .term = TERMINAL_XTERM, },
    { .prefix = "WezTerm ", .suffix = 0, .term = TERMINAL_WEZTERM, },
    { .prefix = "contour ", .suffix = 0, .term = TERMINAL_CONTOUR, },
    { .prefix = "kitty(", .suffix = ')', .term = TERMINAL_KITTY, },
    { .prefix = "foot(", .suffix = ')', .term = TERMINAL_FOOT, },
    { .prefix = "ghostty ", .suffix = 0, .term = TERMINAL_GHOSTTY, },
    { .prefix = "mlterm(", .suffix = ')', .term = TERMINAL_MLTERM, },
    { .prefix = "tmux ", .suffix = 0, .term = TERMINAL_TMUX, },
    { .prefix = "iTerm2 ", .suffix = 0, .term = TERMINAL_ITERM, },
    { .prefix = "mintty ", .suffix = 0, .term = TERMINAL_MINTTY, },
    { .prefix = "terminology ", .suffix = 0, .term = TERMINAL_TERMINOLOGY, },
    { .prefix = NULL, .suffix = 0, .term = TERMINAL_UNKNOWN, },
  }, *xtv;
  for(xtv = xtvers ; xtv->prefix ; ++xtv){
    if(strncmp(xtversion, xtv->prefix, strlen(xtv->prefix)) == 0){
      if(extract_xtversion(ictx, xtversion + strlen(xtv->prefix), xtv->suffix) == 0){
        loginfo("found terminal type %d version %s", xtv->term, ictx->initdata->version);
        ictx->initdata->qterm = xtv->term;
      }else{
        free(xtversion);
        return 2;
      }
      break;
    }
  }
  if(xtv->prefix == NULL){
    logwarn("unknown xtversion [%s]", xtversion);
  }
  free(xtversion);
  return 2;
}

// precondition: s starts with two hex digits, the first of which is not
// greater than 7.
static inline char
toxdigit(const char* s){
  char c = isalpha(*s) ? tolower(*s) - 'a' + 10 : *s - '0';
  c *= 16;
  ++s;
  c += isalpha(*s) ? tolower(*s) - 'a' + 10 : *s - '0';
  return c;
}

// on success, the subsequent character is returned, and |key| and |val| have
// heap-allocated, decoded, nul-terminated copies of the appropriate input.
static const char*
gettcap(const char* s, char** key, char** val){
  const char* equals = s;
  // we don't want anything bigger than 7 in the first nibble
  unsigned firstnibble = true;
  while(*equals != '='){
    if(!isxdigit(*equals)){ // rejects a NUL byte
      logerror("bad key in %s", s);
      return NULL;
    }
    if(firstnibble && (!isdigit(*equals) || *equals - '0' >= 8)){
      logerror("bad key in %s", s);
      return NULL;
    }
    firstnibble = !firstnibble;
    ++equals;
  }
  if(equals - s == 0 || !firstnibble){
    logerror("bad key in %s", s);
    return NULL;
  }
  if((*key = malloc((equals - s) / 2 + 1)) == NULL){
    return NULL;
  }
  char* keytarg = *key;
  do{
    *keytarg = toxdigit(s);
    s += 2;
    ++keytarg;
  }while(*s != '=');
  *keytarg = '\0';
  ++equals; // now one past the equal sign
  const char *end = equals;
  firstnibble = true;
  while(*end != ';' && *end){
    if(!isxdigit(*end)){
      logerror("bad value in %s", s);
      goto valerr;
    }
    if(firstnibble && (!isdigit(*end) || *end - '0' >= 8)){
      logerror("bad value in %s", s);
      goto valerr;
    }
    firstnibble = !firstnibble;
    ++end;
  }
  if(end - equals == 0 || !firstnibble){
    logerror("bad value in %s", s);
    goto valerr;
  }
  if((*val = malloc((end - equals) / 2 + 1)) == NULL){
    goto valerr;
  }
  char* valtarg = *val;
  ++s;
  do{
    *valtarg = toxdigit(s);
    s += 2;
    ++valtarg;
  }while(s != end);
  *valtarg = '\0';
  loginfo("key: %s val: %s", *key, *val);
  return end;

valerr:
  free(*key);
  *key = NULL;
  return NULL;
}

// replace \E with actual 0x1b for use as a terminfo-like format string,
// writing in-place (and updating the nul terminator, if necessary).
// returns its input.
static inline char*
determinfo(char* old){
  bool escaped = false;
  char* targo = old;
  for(char* o = old ; *o ; ++o){
    if(escaped){
      if(*o == 'E'){
        *targo = 0x1b;
        ++targo;
      }else{
        *targo = '\\';
        ++targo;
        *targo = *o;
        ++targo;
      }
      escaped = false;
    }else if(*o == '\\'){
      escaped = true;
    }else{
      *targo = *o;
      ++targo;
    }
  }
  *targo = '\0';
  return old;
}

// XTGETTCAP responses are delimited by semicolons
static int
tcap_cb(inputctx* ictx){
  char* str = amata_next_string(&ictx->amata, "\x1bP1+r");
  if(str == NULL){
    return 2;
  }
  loginfo("xtgettcap [%s]", str);
  if(ictx->initdata == NULL){
    free(str);
    return 2;
  }
  const char* s = str;
  char* key;
  char* val;
  // answers are delimited with semicolons, hex-encoded, key=value
  while(*s && (s = gettcap(s, &val, &key)) ){
    if(strcmp(val, "TN") == 0){
      if(ictx->initdata->qterm == TERMINAL_UNKNOWN){
        if(strcmp(key, "xterm") == 0){
          ictx->initdata->qterm = TERMINAL_XTERM;
        }else if(strcmp(key, "mlterm") == 0){
          ictx->initdata->qterm = TERMINAL_MLTERM;
        }else if(strcmp(key, "xterm-kitty") == 0){
          ictx->initdata->qterm = TERMINAL_KITTY;
        }else if(strcmp(key, "xterm-ghostty") == 0){
          ictx->initdata->qterm = TERMINAL_GHOSTTY;
        }else if(strcmp(key, "xterm-256color") == 0){
          ictx->initdata->qterm = TERMINAL_XTERM;
        }else{
          logwarn("unknown terminal name %s", key);
        }
      }
    }else if(strcmp(val, "RGB") == 0){
      loginfo("got rgb (%s)", key);
      ictx->initdata->rgb = true;
    }else if(strcmp(val, "hpa") == 0){
      loginfo("got hpa (%s)", key);
      ictx->initdata->hpa = determinfo(key);
      key = NULL;
    }else{
      logwarn("unknown capability: %s", str);
    }
    free(val);
    free(key);
    if(*s == ';'){
      ++s;
    }
  }
  if(!s || *s){
    free(str);
    return -1;
  }
  free(str);
  return 2;
}

static int
tda_cb(inputctx* ictx){
  char* str = amata_next_string(&ictx->amata, "\x1bP!|");
  if(str == NULL){
    logwarn("empty ternary device attribute");
    return 2; // don't replay
  }
  if(ictx->initdata && ictx->initdata->qterm == TERMINAL_UNKNOWN){
    if(strcmp(str, "7E565445") == 0){ // "~VTE"
      ictx->initdata->qterm = TERMINAL_VTE;
    }else if(strcmp(str, "7E7E5459") == 0){ // "~~TY"
      ictx->initdata->qterm = TERMINAL_TERMINOLOGY;
    }else if(strcmp(str, "464F4F54") == 0){ // "FOOT"
      ictx->initdata->qterm = TERMINAL_FOOT;
    }else if(strcmp(str, "7E4B4445") == 0){
      ictx->initdata->qterm = TERMINAL_KONSOLE;
    }
    loginfo("got TDA: %s, terminal type %d", str, ictx->initdata->qterm);
  }
  free(str);
  return 2;
}

static int
build_cflow_automaton(inputctx* ictx){
  // syntax: literals are matched. \N is a numeric. \D is a drain (Kleene
  // closure). \S is a ST-terminated string. this working is very dependent on
  // order, and very delicate! hands off!
  const struct {
    const char* cflow;
    triefunc fxn;
  } csis[] = {
    // CSI (\e[)
    { "[E", simple_cb_begin, },
    { "[<\\N;\\N;\\NM", mouse_press_cb, },
    { "[<\\N;\\N;\\Nm", mouse_release_cb, },
    // technically these must begin with "4" or "8"; enforce in callbacks
    { "[\\N;\\N;\\Nt", geom_cb, },
    { "[\\Nu", kitty_cb_simple, },
    { "[\\N;\\N~", wezterm_cb, },
    { "[\\N;\\Nu", kitty_cb, },
    { "[\\N;\\N;\\Nu", kitty_cb_atxt1, },
    { "[\\N;\\N;\\N;\\Nu", kitty_cb_atxt2, },
    { "[\\N;\\N;\\N;\\N;\\Nu", kitty_cb_atxt3, },
    { "[\\N;\\N;\\N;\\N;\\N;\\Nu", kitty_cb_atxt4, },
    { "[\\N;\\N:\\Nu", kitty_cb_complex, },
    { "[\\N;\\N:\\N;\\Nu", kitty_cb_complex_atxt1, },
    { "[\\N;\\N:\\N;\\N;\\Nu", kitty_cb_complex_atxt2, },
    { "[\\N;\\N:\\N;\\N;\\N;\\Nu", kitty_cb_complex_atxt3, },
    { "[\\N;\\N:\\N;\\N;\\N;\\N;\\Nu", kitty_cb_complex_atxt4, },
    { "[\\N;\\N;\\N~", xtmodkey_cb, },
    { "[\\N;\\N:\\N~", kitty_cb_functional, },
    { "[1;\\NP", legacy_cb_f1, },
    { "[1;\\NQ", legacy_cb_f2, },
    { "[1;\\NS", legacy_cb_f4, },
    { "[1;\\ND", legacy_cb_left, },
    { "[1;\\NC", legacy_cb_right, },
    { "[1;\\NB", legacy_cb_down, },
    { "[1;\\NA", legacy_cb_up, },
    { "[1;\\NE", legacy_cb_begin, },
    { "[1;\\NF", legacy_cb_end, },
    { "[1;\\NH", legacy_cb_home, },
    { "[1;\\N:\\NP", kitty_cb_f1, },
    { "[1;\\N:\\NQ", kitty_cb_f2, },
    { "[1;\\N:\\NR", kitty_cb_f3, },
    { "[1;\\N:\\NS", kitty_cb_f4, },
    { "[1;\\N:\\ND", kitty_cb_left, },
    { "[1;\\N:\\NC", kitty_cb_right, },
    { "[1;\\N:\\NB", kitty_cb_down, },
    { "[1;\\N:\\NA", kitty_cb_up, },
    { "[1;\\N:\\NE", kitty_cb_begin, },
    { "[1;\\N:\\NF", kitty_cb_end, },
    { "[1;\\N:\\NH", kitty_cb_home, },
    {"[200~", bracket_start_cb, },
    {"[201~", bracket_end_cb, },
    { "[?\\Nu", kitty_keyboard_cb, },
    { "[?1016;\\N$y", decrpm_pixelmice, },
    { "[?2026;\\N$y", decrpm_asu_cb, },
    { "[\\N;\\NR", cursor_location_cb, },
    { "[?1;1S", NULL, }, // negative cregs XTSMGRAPHICS
    { "[?1;2S", NULL, }, // negative cregs XTSMGRAPHICS
    { "[?1;3S", NULL, }, // negative cregs XTSMGRAPHICS
    { "[?1;3;S", NULL, }, // iterm2 negative cregs XTSMGRAPHICS
    { "[?1;3;0S", NULL, }, // negative cregs XTSMGRAPHICS
    { "[?2;1S", NULL, }, // negative pixels XTSMGRAPHICS
    { "[?2;2S", NULL, }, // negative pixels XTSMGRAPHICS
    { "[?2;3S", NULL, }, // negative pixels XTSMGRAPHICS
    { "[?2;3;S", NULL, }, // iterm2 negative pixels XTSMGRAPHICS
    { "[?2;3;0S", NULL, }, // negative pixels XTSMGRAPHICS
    { "[?6c", da1_vt102_cb, },   // CSI ? 6 c ("VT102")
    { "[?7c", da1_cb, },   // CSI ? 7 c ("VT131")
    { "[?1;0c", da1_cb, }, // CSI ? 1 ; 0 c ("VT101 with No Options")
    { "[?1;2c", da1_cb, }, // CSI ? 1 ; 2 c ("VT100 with Advanced Video Option")
    { "[?4;6c", da1_cb, }, // CSI ? 4 ; 6 c ("VT132 with Advanced Video and Graphics")
    // CSI ? 1 2 ; Ps c ("VT125")
    // CSI ? 6 0 ; Ps c (kmscon)
    // CSI ? 6 2 ; Ps c ("VT220")
    // CSI ? 6 3 ; Ps c ("VT320")
    // CSI ? 6 4 ; Ps c ("VT420")
    // CSI ? 6 5 ; Ps c (WezTerm, VT5xx?)
    { "[?\\N;\\Dc", da1_attrs_cb, },
    { "[?1;0;\\NS", xtsmgraphics_cregs_cb, },
    { "[?2;0;\\N;\\NS", xtsmgraphics_sixel_cb, },
    { "[>83;\\N;0c", da2_screen_cb, },
    { "[>\\N;\\N;\\Nc", da2_cb, },
    { "[=\\Sc", wezterm_tda_cb, }, // CSI da3 form as issued by WezTerm
    // DCS (\eP...ST)
    { "P0+\\S", NULL, }, // negative XTGETTCAP
    { "P1+r\\S", tcap_cb, }, // positive XTGETTCAP
    { "P!|\\S", tda_cb, }, // DCS da3 form used by XTerm
    { "P>|\\S", xtversion_cb, },
    // OSC (\e_...ST)
    { "_G\\S", kittygraph_cb, },
    // a mystery to everyone!
    { "]10;rgb:\\S", fgdef_cb, },
    { "]11;rgb:\\S", bgdef_cb, },
    { NULL, NULL, },
  }, *csi;
  for(csi = csis ; csi->cflow ; ++csi){
    if(inputctx_add_cflow(&ictx->amata, csi->cflow, csi->fxn)){
      logerror("failed adding %p via %s", csi->fxn, csi->cflow);
      return -1;
    }
    loginfo("added %p via %s", csi->fxn, csi->cflow);
  }
  if(ictx->ti->qterm == TERMINAL_RXVT){
    if(inputctx_add_cflow(&ictx->amata, "]4;\\N;rgb:\\R", palette_cb)){
      logerror("failed adding palette_cb");
      return -1;
    }
  }else{
    if(inputctx_add_cflow(&ictx->amata, "]4;\\N;rgb:\\S", palette_cb)){
      logerror("failed adding palette_cb");
      return -1;
    }
    // handle old-style contour responses, though we can't make use of them
    if(inputctx_add_cflow(&ictx->amata, "]4;rgb:\\S", palette_cb)){
      logerror("failed adding palette_cb");
      return -1;
    }
  }
  return 0;
}

static void
closepipe(ipipe p){
#ifndef __MINGW32__
  if(p >= 0){
    close(p);
  }
#else
  if(p){
    CloseHandle(p);
  }
#endif
}

static void
endpipes(ipipe pipes[static 2]){
  closepipe(pipes[0]);
  closepipe(pipes[1]);
}

// only linux and freebsd13+ have eventfd(), so we'll fall back to pipes sigh.
static int
getpipes(ipipe pipes[static 2]){
#ifndef __MINGW32__
#ifndef __APPLE__
  if(pipe2(pipes, O_CLOEXEC | O_NONBLOCK)){
    logerror("couldn't get pipes (%s)", strerror(errno));
    return -1;
  }
#else
  if(pipe(pipes)){
    logerror("couldn't get pipes (%s)", strerror(errno));
    return -1;
  }
  if(set_fd_cloexec(pipes[0], 1, NULL) || set_fd_nonblocking(pipes[0], 1, NULL)){
    logerror("couldn't prep pipe[0] (%s)", strerror(errno));
    endpipes(pipes);
    return -1;
  }
  if(set_fd_cloexec(pipes[1], 1, NULL) || set_fd_nonblocking(pipes[1], 1, NULL)){
    logerror("couldn't prep pipe[1] (%s)", strerror(errno));
    endpipes(pipes);
    return -1;
  }
#endif
#else // windows
  if(!CreatePipe(&pipes[0], &pipes[1], NULL, BUFSIZ)){
    logerror("couldn't get pipes");
    return -1;
  }
#endif
  return 0;
}

static inline inputctx*
create_inputctx(tinfo* ti, FILE* infp, int lmargin, int tmargin, int rmargin,
                int bmargin, ncsharedstats* stats, unsigned drain,
                int linesigs_enabled){
  bool sent_queries = (ti->ttyfd >= 0) ? true : false;
  inputctx* i = malloc(sizeof(*i));
  if(i){
      i->looping = true;
    i->csize = 64;
    if( (i->csrs = malloc(sizeof(*i->csrs) * i->csize)) ){
      i->isize = BUFSIZ;
      if( (i->inputs = malloc(sizeof(*i->inputs) * i->isize)) ){
        if(pthread_mutex_init(&i->ilock, NULL) == 0){
          if(pthread_condmonotonic_init(&i->icond) == 0){
            if(pthread_mutex_init(&i->clock, NULL) == 0){
              if(pthread_condmonotonic_init(&i->ccond) == 0){
                if((i->stdinfd = fileno(infp)) >= 0){
                  if( (i->initdata = malloc(sizeof(*i->initdata))) ){
                    if(getpipes(i->readypipes) == 0){
                      if(getpipes(i->ipipes) == 0){
                        memset(&i->amata, 0, sizeof(i->amata));
                        if(prep_special_keys(i) == 0){
                          if(set_fd_nonblocking(i->stdinfd, 1, &ti->stdio_blocking_save) == 0){
                              logdebug("tty_check(%d) = %d",
                                  i->stdinfd, tty_check(i->stdinfd));
                            i->termfd = tty_check(i->stdinfd) ? -1 : get_tty_fd(infp);
                            memset(i->initdata, 0, sizeof(*i->initdata));
                            if(sent_queries){
                              i->coutstanding = 1; // one in initial request set
                              i->initdata->qterm = ti->qterm;
                              i->initdata->cursory = -1;
                              i->initdata->cursorx = -1;
                              i->initdata->maxpaletteread = -1;
                              i->initdata->kbdlevel = UINT_MAX;
                            }else{
                              free(i->initdata);
                              i->initdata = NULL;
                              i->coutstanding = 0;
                            }
                            i->kittykbd = 0;
                            i->iread = i->iwrite = i->ivalid = 0;
                            i->cread = i->cwrite = i->cvalid = 0;
                            i->initdata_complete = NULL;
                            i->stats = stats;
                            i->ti = ti;
                            i->stdineof = 0;
#ifdef __MINGW32__
                            i->stdinhandle = ti->inhandle;
#endif
                            i->ibufvalid = 0;
                            i->linesigs = linesigs_enabled;
                            i->tbufvalid = 0;
                            i->midescape = 0;
                            i->lmargin = lmargin;
                            i->tmargin = tmargin;
                            i->rmargin = rmargin;
                            i->bmargin = bmargin;
                            i->drain = drain;
                            i->failed = false;
                              i->bracked_paste_enabled = false;
                              i->in_bracketed_paste = false;
                              fbuf_init_small(&i->paste_buffer);
                            logdebug("input descriptors: %d/%d", i->stdinfd, i->termfd);
                            return i;
                          }
                        }
                        input_free_esctrie(&i->amata);
                      }
                      endpipes(i->ipipes);
                    }
                    endpipes(i->readypipes);
                  }
                  free(i->initdata);
                }
                pthread_cond_destroy(&i->ccond);
              }
              pthread_mutex_destroy(&i->clock);
            }
            pthread_cond_destroy(&i->icond);
          }
          pthread_mutex_destroy(&i->ilock);
        }
        free(i->inputs);
      }
      free(i->csrs);
    }
    free(i);
  }
  return NULL;
}

static inline void
free_inputctx(inputctx* i){
  if(i){
    // we *do not* own stdinfd; don't close() it! we do own termfd.
    if(i->termfd >= 0){
      close(i->termfd);
    }
    pthread_mutex_destroy(&i->ilock);
    pthread_cond_destroy(&i->icond);
    pthread_mutex_destroy(&i->clock);
    pthread_cond_destroy(&i->ccond);
    input_free_esctrie(&i->amata);
    // do not kill the thread here, either.
    if(i->initdata){
      free(i->initdata->version);
      free(i->initdata);
    }else if(i->initdata_complete){
      free(i->initdata_complete->version);
      free(i->initdata_complete);
    }
    endpipes(i->readypipes);
    endpipes(i->ipipes);
    free(i->inputs);
    free(i->csrs);
      fbuf_free(&i->paste_buffer);
    free(i);
  }
}

// https://sw.kovidgoyal.net/kitty/keyboard-protocol/#functional-key-definitions
static int
prep_kitty_special_keys(inputctx* ictx){
  // we do not list here those already handled by prep_windows_special_keys()
  static const struct {
    const char* esc;
    uint32_t key;
    unsigned modifiers;
  } keys[] = {
    { .esc = "\x1b[P", .key = NCKEY_F01, },
    { .esc = "\x1b[Q", .key = NCKEY_F02, },
    { .esc = "\x1b[R", .key = NCKEY_F03, },
    { .esc = "\x1b[S", .key = NCKEY_F04, },
    { .esc = "\x1b[127;2u", .key = NCKEY_BACKSPACE,
      .modifiers = NCKEY_MOD_SHIFT, },
    { .esc = "\x1b[127;3u", .key = NCKEY_BACKSPACE,
      .modifiers = NCKEY_MOD_ALT, },
    { .esc = "\x1b[127;5u", .key = NCKEY_BACKSPACE,
      .modifiers = NCKEY_MOD_CTRL, },
    { .esc = NULL, .key = 0, },
  }, *k;
  for(k = keys ; k->esc ; ++k){
    if(inputctx_add_input_escape(&ictx->amata, k->esc, k->key, k->modifiers)){
      return -1;
    }
  }
  loginfo("added all kitty special keys");
  return 0;
}

// add the hardcoded windows input sequences to ti->input. should only
// be called after verifying that this is TERMINAL_MSTERMINAL.
static int
prep_windows_special_keys(inputctx* ictx){
  // here, lacking terminfo, we hardcode the sequences. they can be found at
  // https://docs.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences
  // under the "Input Sequences" heading.
  static const struct {
    const char* esc;
    uint32_t key;
    unsigned modifiers;
  } keys[] = {
    { .esc = "\x1b[A", .key = NCKEY_UP, },
    { .esc = "\x1b[B", .key = NCKEY_DOWN, },
    { .esc = "\x1b[C", .key = NCKEY_RIGHT, },
    { .esc = "\x1b[D", .key = NCKEY_LEFT, },
    { .esc = "\x1b[1;5A", .key = NCKEY_UP,
      .modifiers = NCKEY_MOD_CTRL, },
    { .esc = "\x1b[1;5B", .key = NCKEY_DOWN,
      .modifiers = NCKEY_MOD_CTRL, },
    { .esc = "\x1b[1;5C", .key = NCKEY_RIGHT,
      .modifiers = NCKEY_MOD_CTRL, },
    { .esc = "\x1b[1;5D", .key = NCKEY_LEFT,
      .modifiers = NCKEY_MOD_CTRL, },
    { .esc = "\x1b[H", .key = NCKEY_HOME, },
    { .esc = "\x1b[F", .key = NCKEY_END, },
    { .esc = "\x1b[2~", .key = NCKEY_INS, },
    { .esc = "\x1b[3~", .key = NCKEY_DEL, },
    { .esc = "\x1b[5~", .key = NCKEY_PGUP, },
    { .esc = "\x1b[6~", .key = NCKEY_PGDOWN, },
    { .esc = "\x1bOP", .key = NCKEY_F01, },
    { .esc = "\x1b[11~", .key = NCKEY_F01, },
    { .esc = "\x1bOQ", .key = NCKEY_F02, },
    { .esc = "\x1b[12~", .key = NCKEY_F02, },
    { .esc = "\x1bOR", .key = NCKEY_F03, },
    { .esc = "\x1b[13~", .key = NCKEY_F03, },
    { .esc = "\x1bOS", .key = NCKEY_F04, },
    { .esc = "\x1b[14~", .key = NCKEY_F04, },
    { .esc = "\x1b[15~", .key = NCKEY_F05, },
    { .esc = "\x1b[17~", .key = NCKEY_F06, },
    { .esc = "\x1b[18~", .key = NCKEY_F07, },
    { .esc = "\x1b[19~", .key = NCKEY_F08, },
    { .esc = "\x1b[20~", .key = NCKEY_F09, },
    { .esc = "\x1b[21~", .key = NCKEY_F10, },
    { .esc = "\x1b[23~", .key = NCKEY_F11, },
    { .esc = "\x1b[24~", .key = NCKEY_F12, },
    { .esc = NULL, .key = 0, },
  }, *k;
  for(k = keys ; k->esc ; ++k){
    if(inputctx_add_input_escape(&ictx->amata, k->esc, k->key, k->modifiers)){
      return -1;
    }
    logdebug("added %s %u", k->esc, k->key);
  }
  loginfo("added all windows special keys");
  return 0;
}

static int
prep_all_keys(inputctx* ictx){
  if(prep_windows_special_keys(ictx)){
    return -1;
  }
  if(prep_kitty_special_keys(ictx)){
    return -1;
  }
  if(prep_xtmodkeys(ictx)){
    return -1;
  }
  return 0;
}

// populate |buf| with any new data from the specified file descriptor |fd|.
static void
read_input_nblock(int fd, unsigned char* buf, size_t buflen, int *bufused,
                  unsigned* goteof){
  if(fd < 0){
    return;
  }
  size_t space = buflen - *bufused;
  if(space == 0){
    return;
  }
  ssize_t r = read(fd, buf + *bufused, space);
  if(r <= 0){
    if(r < 0 && (errno != EAGAIN && errno != EBUSY && errno == EWOULDBLOCK)){
      logwarn("couldn't read from %d (%s)", fd, strerror(errno));
    }else{
      if(r < 0){
        logerror("error reading from %d (%s)", fd, strerror(errno));
      }else{
        logwarn("got EOF on %d", fd);
      }
      if(goteof){
        *goteof = 1;
      }
    }
    return;
  }
  *bufused += r;
  space -= r;
  loginfo("read %" PRIdPTR "B from %d (%" PRIuPTR "B left)", r, fd, space);
}

// are terminal and stdin distinct for this inputctx?
static inline bool
ictx_independent_p(const inputctx* ictx){
  return ictx->termfd >= 0;
}

// try to lex a single control sequence off of buf. return the number of bytes
// consumed if we do so. otherwise, return the negative number of bytes
// examined. set ictx->midescape if we're uncertain. we preserve a->used,
// a->state, etc. across runs to avoid reprocessing. buf is almost certainly
// *not* NUL-terminated.
//
// our rule is: an escape must arrive as a single unit to be interpreted as
// an escape. this is most relevant for Alt+keypress (Esc followed by the
// character), which is ambiguous with regards to pressing 'Escape' followed
// by some other character. if they arrive together, we consider it to be
// the escape. we might need to allow more than one process_escape call,
// however, in case the escape ended the previous read buffer.
//  precondition: buflen >= 1. precondition: buf[0] == 0x1b.
static int
process_escape(inputctx* ictx, const unsigned char* buf, int buflen){
  assert(ictx->amata.used <= buflen);
  while(ictx->amata.used < buflen){
    unsigned char candidate = buf[ictx->amata.used++];
    unsigned used = ictx->amata.used;
    if(candidate >= 0x80){
      ictx->amata.used = 0;
      return -(used - 1);
    }
    // an escape always resets the trie (unless we're in the middle of an
    // ST-terminated string), as does a NULL transition.
    if(candidate == NCKEY_ESC && !ictx->amata.instring){
      ictx->amata.matchstart = buf + ictx->amata.used - 1;
      ictx->amata.state = ictx->amata.escapes;
      logtrace("initialized automaton to %u", ictx->amata.state);
      ictx->amata.used = 1;
      if(used > 1){ // we got reset; replay as input
        return -(used - 1);
      }
      // validated first byte as escape! keep going. otherwise, check trie.
      // we can safely check trie[candidate] above because we are either coming
      // off the initial node, which definitely has a valid ->trie, or we're
      // coming from a transition, where ictx->triepos->trie is checked below.
    }else{
      ncinput ni = {0};
      logdebug("walk_auto %u (%c)", candidate, isprint(candidate) ? candidate : ' ');
      int w = walk_automaton(&ictx->amata, ictx, candidate, &ni);
      logdebug("walk result on %u (%c): %d %u", candidate,
               isprint(candidate) ? candidate : ' ', w, ictx->amata.state);
      if(w > 0){
        if(ni.id){
          load_ncinput(ictx, &ni);
        }
        ictx->amata.used = 0;
        return used;
      }else if(w < 0){
        // all inspected characters are invalid; return full negative "used"
        ictx->amata.used = 0;
        return -used;
      }
    }
  }
  logdebug("midescape %d", -ictx->amata.used);
  // we exhausted input without knowing whether or not this is a valid control
  // sequence; we're still on-trie, and need more (immediate) input.
  ictx->midescape = 1;
  return -ictx->amata.used;
}

// process as many control sequences from |buf|, having |bufused| bytes,
// as we can. this text needn't be valid UTF-8. this is always called on
// tbuf; if we find bulk data here, we need replay it into ibuf (assuming
// that there's room).
static void
process_escapes(inputctx* ictx, unsigned char* buf, int* bufused){
  int offset = 0;
  while(*bufused){
    int consumed = process_escape(ictx, buf + offset, *bufused);
    // negative |consumed| means either that we're not sure whether it's an
    // escape, or it definitely is not.
    if(consumed < 0){
      // if midescape is not set, the negative return means invalid escape.
      // replay it to the bulk input buffer; our automaton will have been reset.
      if(!ictx->midescape){
        consumed = -consumed;
        int available = sizeof(ictx->ibuf) - ictx->ibufvalid;
        if(available){
          if(available > consumed){
            available = consumed;
          }
          logwarn("replaying %dB of %dB to ibuf", available, consumed);
          memcpy(ictx->ibuf + ictx->ibufvalid, buf + offset, available);
          ictx->ibufvalid += available;
        }
        offset += consumed;
        ictx->midescape = 0;
        *bufused -= consumed;
        assert(0 <= *bufused);
      }else{
        break;
      }
    }
    *bufused -= consumed;
    offset += consumed;
    assert(0 <= *bufused);
  }
  // move any leftovers to the front; only happens if we fill output queue,
  // or ran out of input data mid-escape
  if(*bufused){
    ictx->amata.matchstart = buf;
    memmove(buf, buf + offset, *bufused);
  }
}

// precondition: buflen >= 1. attempts to consume UTF8 input from buf. the
// expected length of a UTF8 character can be determined from its first byte.
// if we don't have that much data, return 0 and read more. if we determine
// an error, return -1 to consume 1 byte, restarting the UTF8 lex on the next
// byte. on a valid UTF8 character, set up the ncinput and return its length.
static int
process_input(const unsigned char* buf, int buflen, ncinput* ni){
  assert(1 <= buflen);
  memset(ni, 0, sizeof(*ni));
  const int cpointlen = utf8_codepoint_length(*buf);
  if(cpointlen <= 0){
    logwarn("invalid UTF8 initiator on input (0x%02x)", *buf);
    return -1;
  }else if(cpointlen == 1){ // pure ascii can't show up mid-utf8-character
    ni->id = buf[0];
    return 1;
  }
  if(cpointlen > buflen){
    logwarn("utf8 character (%dB) broken across read", cpointlen);
    return 0; // need read more data; we don't have the complete character
  }
  wchar_t w;
  mbstate_t mbstate = {0};
//fprintf(stderr, "CANDIDATE: %d cpointlen: %zu cpoint: %d\n", candidate, cpointlen, cpoint[cpointlen]);
  // FIXME how the hell does this work with 16-bit wchar_t?
  size_t r = mbrtowc(&w, (const char*)buf, cpointlen, &mbstate);
  if(r == (size_t)-1 || r == (size_t)-2){
    logerror("invalid utf8 prefix (%dB) on input", cpointlen);
    return -1;
  }
  ni->id = w;
  return cpointlen;
}

// precondition: buflen >= 1. gets an ncinput prepared by process_input, and
// sticks that into the bulk queue.
static int
process_ncinput(inputctx* ictx, const unsigned char* buf, int buflen){
  ncinput ni;
  int r = process_input(buf, buflen, &ni);
  if(r > 0){
    load_ncinput(ictx, &ni);
  }else if(r < 0){
    inc_input_errors(ictx);
    r = 1; // we want to consume a single byte, upstairs
  }
  return r;
}

// handle redirected input (i.e. not from our connected terminal). process as
// much bulk UTF-8 input as we can, knowing it to be free of control sequences.
// anything not a valid UTF-8 character is dropped.
static void
process_bulk(inputctx* ictx, unsigned char* buf, int* bufused){
  int offset = 0;
  while(*bufused){
    bool noroom = false;
    pthread_mutex_lock(&ictx->ilock);
    if(ictx->ivalid == ictx->isize){
      noroom = true;
    }
    pthread_mutex_unlock(&ictx->ilock);
    if(noroom){
      break;
    }
    int consumed = process_ncinput(ictx, buf + offset, *bufused);
    if(consumed <= 0){
      break;
    }
    *bufused -= consumed;
    offset += consumed;
  }
  // move any leftovers to the front
  if(*bufused){
    memmove(buf, buf + offset, *bufused);
  }
}

// process as much mixed input as we can. we might find UTF-8 bulk input and
// control sequences mixed (though each individual character/sequence ought be
// contiguous). known control sequences are removed for internal processing.
// everything else will be handed up to the client (assuming it to be valid
// UTF-8).
static void
process_melange(inputctx* ictx, const unsigned char* buf, int* bufused){
  int offset = 0;
  int origlen = *bufused;
  while(*bufused){
    logdebug("input %d (%u)/%d [0x%02x] (%c)", offset, ictx->amata.used,
             *bufused, buf[offset], isprint(buf[offset]) ? buf[offset] : ' ');
    int consumed = 0;
    if(buf[offset] == '\x1b'){
      consumed = process_escape(ictx, buf + offset, *bufused);
      if(consumed < 0){
        if(ictx->midescape){
          if(*bufused != -consumed || consumed == -1){
            logdebug("not midescape bufused=%d origlen=%d", *bufused, origlen);
            // not at the end; treat it as input. no need to move between
            // buffers; simply ensure we process it as input, and don't mark
            // anything as consumed.
            ictx->midescape = 0;
          }
        }
      }
    }
    // don't process as input only if we just read a valid control character,
    // or if we need to read more to determine what it is.
    if(consumed <= 0 && !ictx->midescape){
        if (ictx->bracked_paste_enabled && ictx->in_bracketed_paste) {
            const unsigned char* esc = memchr(buf + offset, '\x1b', *bufused);
            consumed = *bufused;
            if (esc) {
                consumed = esc - (buf + offset);
            }
            fbuf_putn(&ictx->paste_buffer, (const char*) buf + offset, consumed);
            loginfo("consumed for paste %d; total=%llu/%llu", consumed, ictx->paste_buffer.used, ictx->paste_buffer.size);
        } else {
            consumed = process_ncinput(ictx, buf + offset, *bufused);
        }
    }
    if(consumed < 0){
      logdebug("consumed < 0; break");
      break;
    }
    *bufused -= consumed;
    offset += consumed;
  }
  handoff_initial_responses_late(ictx);
}

// walk the matching automaton from wherever we were.
static void
process_ibuf(inputctx* ictx){
  if(resize_seen){
    ncinput tni = {
      .id = NCKEY_RESIZE,
    };
    load_ncinput(ictx, &tni);
    resize_seen = 0;
  }
  if(cont_seen){
    ncinput tni = {
      .id = NCKEY_SIGNAL,
    };
    load_ncinput(ictx, &tni);
    cont_seen = 0;
  }
  if(ictx->tbufvalid){
    // we could theoretically do this in parallel with process_bulk, but it
    // hardly seems worthwhile without breaking apart the fetches of input.
    process_escapes(ictx, ictx->tbuf, &ictx->tbufvalid);
    handoff_initial_responses_late(ictx);
  }
  if(ictx->ibufvalid){
    if(ictx_independent_p(ictx)){
      process_bulk(ictx, ictx->ibuf, &ictx->ibufvalid);
    }else{
      int valid = ictx->ibufvalid;
      process_melange(ictx, ictx->ibuf, &ictx->ibufvalid);
      // move any leftovers to the front
      if(ictx->ibufvalid){
        memmove(ictx->ibuf, ictx->ibuf + valid - ictx->ibufvalid, ictx->ibufvalid);
        if(ictx->amata.matchstart){
          ictx->amata.matchstart = ictx->ibuf;
        }
      }
    }
  }
}

int ncinput_shovel(inputctx* ictx, const void* buf, int len){
  process_melange(ictx, buf, &len);
  if(len){
    logwarn("dropping %d byte%s", len, len == 1 ? "" : "s");
    inc_input_errors(ictx);
  }
  return 0;
}

// here, we always block for an arbitrarily long time, or not at all,
// doing the latter only when ictx->midescape is set. |rtfd| and/or |rifd|
// are set high iff they are ready for reading, and otherwise cleared.
static int
block_on_input(inputctx* ictx, unsigned* rtfd, unsigned* rifd){
  logtrace("blocking on input availability");
  *rtfd = *rifd = 0;
  unsigned nonblock = ictx->midescape;
  if(nonblock){
    loginfo("nonblocking read to check for completion");
    ictx->midescape = 0;
  }
#ifdef __MINGW32__
  int timeoutms = nonblock ? 0 : -1;
  DWORD ncount = 0;
  HANDLE handles[2];
  if(!ictx->stdineof){
    if(ictx->ibufvalid != sizeof(ictx->ibuf)){
      handles[ncount++] = ictx->stdinhandle;
    }
  }
  if(ncount == 0){
    handles[ncount++] = ictx->ipipes[0];
  }
  DWORD d = WaitForMultipleObjects(ncount, handles, false, timeoutms);
  if(d == WAIT_TIMEOUT){
    return 0;
  }else if(d == WAIT_FAILED){
    return -1;
  }else if(d - WAIT_OBJECT_0 == 0){
    *rifd = 1;
    return 1;
  }
  return -1;
#else
  int inevents = POLLIN;
#ifdef POLLRDHUP
  inevents |= POLLRDHUP;
#endif
      fd_set rfds;
      FD_ZERO(&rfds);
      int maxfd = 0;
  struct pollfd pfds[2];
  int pfdcount = 0;
  if(!ictx->stdineof){
    if(ictx->ibufvalid != sizeof(ictx->ibuf)){
        loginfo("reading from stdin %d", ictx->stdinfd);
        FD_SET(ictx->stdinfd, &rfds);
        if (ictx->stdinfd > maxfd) {
            maxfd = ictx->stdinfd;
        }
      pfds[pfdcount].fd = ictx->stdinfd;
      pfds[pfdcount].events = inevents;
      pfds[pfdcount].revents = 0;

      ++pfdcount;
    }
  }
  if(pfdcount == 0){
    loginfo("output queues full; blocking on ipipes");
  }
        FD_SET(ictx->ipipes[0], &rfds);
      if (ictx->ipipes[0] > maxfd) {
          maxfd = ictx->ipipes[0];
      }
    pfds[pfdcount].fd = ictx->ipipes[0];
    pfds[pfdcount].events = inevents;
    pfds[pfdcount].revents = 0;
    ++pfdcount;
  if(ictx->termfd >= 0){
      FD_SET(ictx->termfd, &rfds);
      if (ictx->termfd > maxfd) {
          maxfd = ictx->termfd;
      }
    pfds[pfdcount].fd = ictx->termfd;
    pfds[pfdcount].events = inevents;
    pfds[pfdcount].revents = 0;
    ++pfdcount;
  }
  logtrace("waiting on %d fds (ibuf: %u/%"PRIuPTR")", pfdcount, ictx->ibufvalid, sizeof(ictx->ibuf));
  sigset_t smask;
  sigfillset(&smask);
  sigdelset(&smask, SIGCONT);
  sigdelset(&smask, SIGWINCH);
#ifdef SIGTHR
  // freebsd uses SIGTHR for thread cancellation; need this to ensure wakeup
  // on exit (in cancel_and_join()).
  sigdelset(&smask, SIGTHR);
#endif
  int events;
#if defined(__APPLE__)
      loginfo("select maxfd %d", maxfd);
      struct timeval ts = {1, 0};
      while ((events = select(maxfd + 1, &rfds, NULL, NULL, &ts)) <0) {
#    elif defined(__MINGW32__)
  int timeoutms = nonblock ? 0 : -1;
  while((events = poll(pfds, pfdcount, timeoutms)) < 0){ // FIXME smask?
#else
  struct timespec ts = { .tv_sec = 1, .tv_nsec = 0, };
  struct timespec* pts = nonblock ? &ts : NULL;
  while((events = ppoll(pfds, pfdcount, pts, &smask)) < 0){
#endif
    if(errno == EINTR){
      loginfo("interrupted by signal");
      return resize_seen;
    }else if(errno != EAGAIN && errno != EBUSY && errno != EWOULDBLOCK){
      logerror("error polling (%s)", strerror(errno));
      return -1;
    } else {
        loginfo("poll spin");
    }
  }
  loginfo("poll returned %d", events);
#if defined(__APPLE__)
      if (nonblock || FD_ISSET(ictx->stdinfd, &rfds)) {
          *rifd = 1;
      }
      if (ictx->termfd >= 0 && FD_ISSET(ictx->termfd, &rfds)) {
          *rtfd = 1;
      }
      if (FD_ISSET(ictx->ipipes[0], &rfds)) {
          loginfo("drain ipipe");
          char c;
          while(read(ictx->ipipes[0], &c, sizeof(c)) == 1){
              // FIXME accelerate?
          }
      }
      events = 0;
#endif
  pfdcount = 0;
  while(events){
    if(pfds[pfdcount].revents){
      if(pfds[pfdcount].fd == ictx->stdinfd){
        *rifd = 1;
      }else if(pfds[pfdcount].fd == ictx->termfd){
        *rtfd = 1;
      }else if(pfds[pfdcount].fd == ictx->ipipes[0]){
        char c;
        while(read(ictx->ipipes[0], &c, sizeof(c)) == 1){
          // FIXME accelerate?
        }
      }
      --events;
    }
    ++pfdcount;
  }
  loginfo("got events: %c%c", *rtfd ? 'T' : 't', *rifd ? 'I' : 'i');
  return events;
#endif
}

// populate the ibuf with any new data, up through its size, but do not block.
// don't loop around this call without some kind of readiness notification.
static void
read_inputs_nblock(inputctx* ictx){
  unsigned rtfd, rifd;
  block_on_input(ictx, &rtfd, &rifd);
  // first we read from the terminal, if that's a distinct source.
  if(rtfd){
    read_input_nblock(ictx->termfd, ictx->tbuf, sizeof(ictx->tbuf),
                      &ictx->tbufvalid, NULL);
  }
  // now read bulk, possibly with term escapes intermingled within (if there
  // was not a distinct terminal source).
  if(rifd){
    unsigned eof = ictx->stdineof;
    read_input_nblock(ictx->stdinfd, ictx->ibuf, sizeof(ictx->ibuf),
                      &ictx->ibufvalid, &ictx->stdineof);
    // did we switch from non-EOF state to EOF? if so, mark us ready
    if(!eof && ictx->stdineof){
      // we hit EOF; write an event to the readiness fd
      mark_pipe_ready(ictx->readypipes);
      pthread_cond_broadcast(&ictx->icond);
    }
  }
}

static void*
input_thread(void* vmarshall){
  setup_alt_sig_stack();
  inputctx* ictx = vmarshall;
  if(prep_all_keys(ictx) || build_cflow_automaton(ictx)){
    ictx->failed = true;
    handoff_initial_responses_early(ictx);
    handoff_initial_responses_late(ictx);
  }
  while(ictx->looping){
    read_inputs_nblock(ictx);
    // process anything we've read
    process_ibuf(ictx);
  }
  return NULL;
}

int init_inputlayer(tinfo* ti, FILE* infp, int lmargin, int tmargin,
                    int rmargin, int bmargin, ncsharedstats* stats,
                    unsigned drain, int linesigs_enabled){
  inputctx* ictx = create_inputctx(ti, infp, lmargin, tmargin, rmargin,
                                   bmargin, stats, drain, linesigs_enabled);
  if(ictx == NULL){
    return -1;
  }
  if(pthread_create(&ictx->tid, NULL, input_thread, ictx)){
    free_inputctx(ictx);
    return -1;
  }
  ti->ictx = ictx;
  loginfo("spun up input thread");
  return 0;
}

int stop_inputlayer(tinfo* ti){
  int ret = 0;
  if(ti){
    // FIXME cancellation on shutdown does not yet work on windows #2192
#ifndef __MINGW32__
    if(ti->ictx){
        void *res;
      loginfo("tearing down input thread");
        ti->ictx->looping = false;
        mark_pipe_ready(ti->ictx->ipipes);
        if(pthread_join(ti->ictx->tid, &res)){
            logerror("error joining input thread");
            return -1;
        }
      ret |= set_fd_nonblocking(ti->ictx->stdinfd, ti->stdio_blocking_save, NULL);
      free_inputctx(ti->ictx);
      ti->ictx = NULL;
    }
#endif
  }
  return ret;
}

int inputready_fd(const inputctx* ictx){
#ifndef __MINGW32__
  return ictx->readypipes[0];
#else
  (void)ictx;
  logerror("readiness descriptor unavailable on windows");
  return -1;
#endif
}

static inline uint32_t
internal_get(inputctx* ictx, const struct timespec* ts, ncinput* ni){
  uint32_t id;
  if(ictx->drain){
    logerror("input is being drained");
    if(ni){
      memset(ni, 0, sizeof(*ni));
      ni->id = (uint32_t)-1;
    }
    return (uint32_t)-1;
  }
  pthread_mutex_lock(&ictx->ilock);
  while(!ictx->ivalid){
    if(ictx->stdineof){
      pthread_mutex_unlock(&ictx->ilock);
      logwarn("read eof on stdin");
      if(ni){
        memset(ni, 0, sizeof(*ni));
        ni->id = NCKEY_EOF;
      }
      return NCKEY_EOF;
    }
    if(ts == NULL){
      pthread_cond_wait(&ictx->icond, &ictx->ilock);
    }else{
      int r = pthread_cond_timedwait(&ictx->icond, &ictx->ilock, ts);
      if(r == ETIMEDOUT){
        pthread_mutex_unlock(&ictx->ilock);
        if(ni){
          memset(ni, 0, sizeof(*ni));
        }
        return 0;
      }else if(r < 0){
        inc_input_errors(ictx);
        if(ni){
          memset(ni, 0, sizeof(*ni));
          ni->id = (uint32_t)-1;
        }
        return (uint32_t)-1;
      }
    }
  }
  id = ictx->inputs[ictx->iread].id;
  if(ni){
    memcpy(ni, &ictx->inputs[ictx->iread], sizeof(*ni));

      if (ncinput_ctrl_p(ni) && ni->id < 127) {
          ni->utf8[0] = ni->id & 0x1f;
          ni->utf8[1] = '\0';
          ni->eff_text[0] = ni->id & 0x1f;
          ni->eff_text[1] = '\0';
      }
    else if(notcurses_ucs32_to_utf8(&ni->id, 1, (unsigned char*)ni->utf8, sizeof(ni->utf8)) < 0){
      ni->utf8[0] = 0;
    }
    if (ni->eff_text[0]==0) {
        ni->eff_text[0]=ni->id;
    }
  }
  if(++ictx->iread == ictx->isize){
    ictx->iread = 0;
  }
  bool sendsignal = false;
  if(ictx->ivalid-- == ictx->isize){
    sendsignal = true;
  }else{
    logtrace("draining event readiness pipe %d", ictx->ivalid);
#ifndef __MINGW32__
    char c;
    while(read(ictx->readypipes[0], &c, sizeof(c)) == 1){
      // FIXME accelerate?
    }
#else
    // we ought be draining this, but it breaks everything, as we can't easily
    // do nonblocking input from a pipe in windows, augh...
    // Ne pleure pas, Alfred! J'ai besoin de tout mon courage pour mourir a vingt ans!
    /*while(ReadFile(ictx->readypipes[0], &c, sizeof(c), NULL, NULL)){
      // FIXME accelerate?
    }*/
#endif
  }
  pthread_mutex_unlock(&ictx->ilock);
  if(sendsignal){
    mark_pipe_ready(ictx->ipipes);
  }
  return id;
}

int
notcurses_bracketed_paste_enable(struct notcurses* nc)
{
    const char* be = get_escape(&nc->tcache, ESCAPE_BE);
    if (be) {
        if (!tty_emit(be, nc->tcache.ttyfd)) {
            loginfo("enabled bracketed paste mode");
            nc->tcache.ictx->bracked_paste_enabled = true;
            return 0;
        }
    }

    return -1;
}

int
notcurses_bracketed_paste_disable(struct notcurses* nc)
{
    if (!nc->tcache.ictx->bracked_paste_enabled) {
        return 0;
    }

    const char* bd = get_escape(&nc->tcache, ESCAPE_BD);
    if (bd) {
        if (!tty_emit(bd, nc->tcache.ttyfd)) {
            loginfo("disabled bracketed paste mode");
            nc->tcache.ictx->bracked_paste_enabled = false;
            return 0;
        }
    }

    return -1;
}

void
ncinput_free_paste_content(ncinput* n)
{
    if (n->id == NCKEY_PASTE) {
        fbuf small_f = {0};

        small_f.buf = (char *) n->paste_content;
        fbuf_free(&small_f);
        n->paste_content = NULL;
    }
}


// infp has already been set non-blocking
uint32_t notcurses_get(notcurses* nc, const struct timespec* absdl, ncinput* ni){
  uint32_t ret = internal_get(nc->tcache.ictx, absdl, ni);
  return ret;
}

// FIXME better performance if we move this within the locked area
int notcurses_getvec(notcurses* n, const struct timespec* absdl,
                     ncinput* ni, int vcount){
  for(int v = 0 ; v < vcount ; ++v){
    uint32_t u = notcurses_get(n, absdl, &ni[v]);
    if(u == (uint32_t)-1){
      if(v == 0){
        return -1;
      }
      return v;
    }else if(u == 0){
      return v;
    }
  }
  return vcount;
}

uint32_t ncdirect_get(ncdirect* n, const struct timespec* absdl, ncinput* ni){
  if(n->eof){
    logerror("already got EOF");
    return -1;
  }
  uint32_t r = internal_get(n->tcache.ictx, absdl, ni);
  if(r == NCKEY_EOF){
    n->eof = 1;
  }
  return r;
}

int get_cursor_location(inputctx* ictx, const char* u7, unsigned* y, unsigned* x){
  pthread_mutex_lock(&ictx->clock);
  while(ictx->cvalid == 0){
    if(ictx->coutstanding == 0){
      if(tty_emit(u7, ictx->ti->ttyfd)){
        pthread_mutex_unlock(&ictx->clock);
        return -1;
      }
      ++ictx->coutstanding;
    }
    pthread_cond_wait(&ictx->ccond, &ictx->clock);
  }
  const cursorloc* cloc = &ictx->csrs[ictx->cread];
  if(++ictx->cread == ictx->csize){
    ictx->cread = 0;
  }
  --ictx->cvalid;
  if(y){
    *y = cloc->y;
  }
  if(x){
    *x = cloc->x;
  }
  pthread_mutex_unlock(&ictx->clock);
  return 0;
}

// Disable signals originating from the terminal's line discipline, i.e.
// SIGINT (^C), SIGQUIT (^\), and SIGTSTP (^Z). They are enabled by default.
static int
linesigs_disable(tinfo* ti){
  if(!ti->ictx->linesigs){
    logwarn("linedisc signals already disabled");
  }
#ifndef __MINGW32__
  if(ti->ttyfd < 0){
    return 0;
  }
  struct termios tios;
  if(tcgetattr(ti->ttyfd, &tios)){
    logerror("Couldn't preserve terminal state for %d (%s)", ti->ttyfd, strerror(errno));
    return -1;
  }
  tios.c_lflag &= ~ISIG;
  if(tcsetattr(ti->ttyfd, TCSANOW, &tios)){
    logerror("Error disabling signals on %d (%s)", ti->ttyfd, strerror(errno));
    return -1;
  }
#else
  DWORD mode;
  if(!GetConsoleMode(ti->inhandle, &mode)){
    logerror("error acquiring input mode");
    return -1;
  }
  mode &= ~ENABLE_PROCESSED_INPUT;
  if(!SetConsoleMode(ti->inhandle, mode)){
    logerror("error setting input mode");
    return -1;
  }
#endif
  ti->ictx->linesigs = 0;
  loginfo("disabled line discipline signals");
  return 0;
}

int notcurses_linesigs_disable(notcurses* nc){
  return linesigs_disable(&nc->tcache);
}

static int
linesigs_enable(tinfo* ti){
  if(ti->ictx->linesigs){
    logwarn("linedisc signals already enabled");
  }
#ifndef __MINGW32__
  if(ti->ttyfd < 0){
    return 0;
  }
  struct termios tios;
  if(tcgetattr(ti->ttyfd, &tios)){
    logerror("couldn't preserve terminal state for %d (%s)", ti->ttyfd, strerror(errno));
    return -1;
  }
  tios.c_lflag |= ISIG;
  if(tcsetattr(ti->ttyfd, TCSANOW, &tios)){
    logerror("error disabling signals on %d (%s)", ti->ttyfd, strerror(errno));
    return -1;
  }
#else
  DWORD mode;
  if(!GetConsoleMode(ti->inhandle, &mode)){
    logerror("error acquiring input mode");
    return -1;
  }
  mode |= ENABLE_PROCESSED_INPUT;
  if(!SetConsoleMode(ti->inhandle, mode)){
    logerror("error setting input mode");
    return -1;
  }
#endif
  ti->ictx->linesigs = 1;
  loginfo("enabled line discipline signals");
  return 0;
}

// Restore signals originating from the terminal's line discipline, i.e.
// SIGINT (^C), SIGQUIT (^\), and SIGTSTP (^Z), if disabled.
int notcurses_linesigs_enable(notcurses* n){
  return linesigs_enable(&n->tcache);
}

struct initial_responses* inputlayer_get_responses(inputctx* ictx){
  struct initial_responses* iresp;
      loginfo("inputlayer_get_resp wait");
  pthread_mutex_lock(&ictx->ilock);
  while(ictx->initdata || !ictx->initdata_complete){
    pthread_cond_wait(&ictx->icond, &ictx->ilock);
  }
  iresp = ictx->initdata_complete;
  ictx->initdata_complete = NULL;
      loginfo("inputlayer_get_resp got %p", iresp);
  pthread_mutex_unlock(&ictx->ilock);
  if(ictx->failed){
    logpanic("aborting after automaton construction failure");
    free(iresp);
    return NULL;
  }
  return iresp;
}
