#ifndef NOTCURSES_NOTCURSES
#define NOTCURSES_NOTCURSES

#include <time.h>
#include <ctype.h>
#include <wchar.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <stdbool.h>
#include <notcurses/ncport.h>
#include <notcurses/nckeys.h>
#include <notcurses/ncseqs.h>

#ifdef __cplusplus
extern "C" {
#define RESTRICT
#define _Static_assert(...)
#else
#define RESTRICT restrict
#endif

#ifdef NOTCURSES_FFI
#define static API
#endif

#ifndef __MINGW32__
#define API __attribute__((visibility("default")))
#else
#define API __declspec(dllexport)
#endif
#define ALLOC __attribute__((malloc)) __attribute__((warn_unused_result))

// Get a human-readable string describing the running Notcurses version.
API const char* notcurses_version(void);
// Cannot be inline, as we want to get the versions of the actual Notcurses
// library we loaded, not what we compile against.
API void notcurses_version_components(int* major, int* minor, int* patch, int* tweak);

API int ncwidth(uint32_t ch, const char* encoding);

struct notcurses; // Notcurses state for a given terminal, composed of ncplanes
struct ncplane;   // a drawable Notcurses surface, composed of cells
struct ncvisual;  // a visual bit of multimedia opened with LibAV|OIIO
struct ncuplot;   // uint64_t histogram
struct ncdplot;   // double histogram
struct ncprogbar; // progress bar
struct ncfdplane; // i/o wrapper to dump file descriptor to plane
struct ncsubproc; // ncfdplane wrapper with subprocess management
struct ncselector;// widget supporting selecting 1 from a list of options
struct ncmultiselector; // widget supporting selecting 0..n from n options
struct ncreader;  // widget supporting free string input ala readline
struct ncfadectx; // context for a palette fade operation
struct nctablet;  // grouped item within an ncreel
struct ncreel;    // hierarchical block-based data browser
struct nctab;     // grouped item within an nctabbed
struct nctabbed;  // widget with one tab visible at a time
struct ncdirect;  // direct mode context

// we never blit full blocks, but instead spaces (more efficient) with the
// background set to the desired foreground. these need be kept in the same
// order as the blitters[] definition in lib/blit.c.
typedef enum {
  NCBLIT_DEFAULT, // let the ncvisual pick
  NCBLIT_1x1,     // space, compatible with ASCII
  NCBLIT_2x1,     // halves + 1x1 (space)     ▄▀
  NCBLIT_2x2,     // quadrants + 2x1          ▗▐ ▖▀▟▌▙
  NCBLIT_3x2,     // sextants (*NOT* 2x2)     🬀🬁🬂🬃🬄🬅🬆🬇🬈🬉🬊🬋🬌🬍🬎🬏🬐🬑🬒🬓🬔🬕🬖🬗🬘🬙🬚🬛🬜🬝🬞
  NCBLIT_BRAILLE, // 4 rows, 2 cols (braille) ⡀⡄⡆⡇⢀⣀⣄⣆⣇⢠⣠⣤⣦⣧⢰⣰⣴⣶⣷⢸⣸⣼⣾⣿
  NCBLIT_PIXEL,   // pixel graphics
  // these blitters are suitable only for plots, not general media
  NCBLIT_4x1,     // four vertical levels     █▆▄▂
  NCBLIT_8x1,     // eight vertical levels    █▇▆▅▄▃▂▁
} ncblitter_e;

// Alignment within a plane or terminal. Left/right-justified, or centered.
typedef enum {
  NCALIGN_UNALIGNED,
  NCALIGN_LEFT,
  NCALIGN_CENTER,
  NCALIGN_RIGHT,
} ncalign_e;

#define NCALIGN_TOP NCALIGN_LEFT
#define NCALIGN_BOTTOM NCALIGN_RIGHT

#define NCACS_ULCORNER "l"
#define NCACS_LLCORNER "m"
#define NCACS_URCORNER "k"
#define NCACS_LRCORNER "j"
#define NCACS_LTEE "t"
#define NCACS_RTEE "u"
#define NCACS_BTEE "v"
#define NCACS_TTEE "w"
#define NCACS_HLINE "q"
#define NCACS_VLINE "x"
#define NCACS_PLUS "n"
#define NCACS_DIAMOND "`"
#define NCACS_LARROW ","
#define NCACS_RARROW "+"
#define NCACS_DARROW "."
#define NCACS_UARROW "-"

// How to scale an ncvisual during rendering. NCSCALE_NONE will apply no
// scaling. NCSCALE_SCALE scales a visual to the plane's size, maintaining
// aspect ratio. NCSCALE_STRETCH stretches and scales the image in an attempt
// to fill the entirety of the plane. NCSCALE_NONE_HIRES and
// NCSCALE_SCALE_HIRES behave like their counterparts, but admit blitters
// which don't preserve aspect ratio.
typedef enum {
  NCSCALE_NONE,
  NCSCALE_SCALE,
  NCSCALE_STRETCH,
  NCSCALE_NONE_HIRES,
  NCSCALE_SCALE_HIRES,
} ncscale_e;

// background cannot be highcontrast, only foreground
#define NCALPHA_HIGHCONTRAST    0x30000000ull
#define NCALPHA_TRANSPARENT     0x20000000ull
#define NCALPHA_BLEND           0x10000000ull
#define NCALPHA_OPAQUE          0x00000000ull

// we support palette-indexed color up to 8 bits.
#define NCPALETTESIZE 256

// Does this glyph completely obscure the background? If so, there's no need
// to emit a background when rasterizing, a small optimization. These are
// also used to track regions into which we must not cellblit.
#define NC_NOBACKGROUND_MASK  0x8700000000000000ull
// if this bit is set, we are *not* using the default background color
#define NC_BGDEFAULT_MASK     0x0000000040000000ull
// extract these bits to get the background RGB value
#define NC_BG_RGB_MASK        0x0000000000ffffffull
// if this bit *and* NC_BGDEFAULT_MASK are set, we're using a
// palette-indexed background color
#define NC_BG_PALETTE         0x0000000008000000ull
// extract these bits to get the background alpha mask
#define NC_BG_ALPHA_MASK      0x30000000ull

// initialize a 32-bit channel pair with specified RGB
#define NCCHANNEL_INITIALIZER(r, g, b) \
  (((uint32_t)(r) << 16u) + ((uint32_t)(g) << 8u) + (b) + NC_BGDEFAULT_MASK)

// initialize a 64-bit channel pair with specified RGB fg/bg
#define NCCHANNELS_INITIALIZER(fr, fg, fb, br, bg, bb) \
  ((NCCHANNEL_INITIALIZER((fr), (fg), (fb)) << 32ull) + \
   (NCCHANNEL_INITIALIZER((br), (bg), (bb))))

// These lowest-level functions directly manipulate a channel. Users will
// typically manipulate ncplanes' and nccells' channels through their APIs,
// rather than calling these explicitly.

// Extract the 2-bit alpha component from a 32-bit channel. It is not
// shifted down, and can be directly compared to NCALPHA_* values.
static inline uint32_t
ncchannel_alpha(uint32_t channel){
  return channel & NC_BG_ALPHA_MASK;
}

// Set the 2-bit alpha component of the 32-bit channel. Background channels
// must not be set to NCALPHA_HIGHCONTRAST. It is an error if alpha contains
// any bits other than NCALPHA_*.
static inline int
ncchannel_set_alpha(uint32_t* channel, unsigned alpha){
  if(alpha & ~NC_BG_ALPHA_MASK){
    return -1;
  }
  *channel = (uint32_t)alpha | (*channel & (uint32_t)~NC_BG_ALPHA_MASK);
  if(alpha != NCALPHA_OPAQUE){
    *channel |= NC_BGDEFAULT_MASK;
  }
  return 0;
}

// Is this channel using the "default color" rather than RGB/palette-indexed?
static inline bool
ncchannel_default_p(uint32_t channel){
  return !(channel & NC_BGDEFAULT_MASK);
}

// Mark the channel as using its default color. Alpha is set opaque.
static inline uint32_t
ncchannel_set_default(uint32_t* channel){
  *channel &= (uint32_t)~NC_BGDEFAULT_MASK; // turn off not-default bit
  ncchannel_set_alpha(channel, NCALPHA_OPAQUE);
  return *channel;
}

// Is this channel using palette-indexed color?
static inline bool
ncchannel_palindex_p(uint32_t channel){
  return !ncchannel_default_p(channel) && (channel & NC_BG_PALETTE);
}

// Extract the palette index from a channel. Only valid if
// ncchannel_palindex_p() would return true for the channel.
static inline unsigned
ncchannel_palindex(uint32_t channel){
  return channel & 0xff;
}

// Mark the channel as using the specified palette color. It is an error if
// the index is greater than NCPALETTESIZE. Alpha is set opaque.
static inline int
ncchannel_set_palindex(uint32_t* channel, unsigned idx){
  if(idx >= NCPALETTESIZE){
    return -1;
  }
  ncchannel_set_alpha(channel, NCALPHA_OPAQUE);
  *channel &= 0xff000000ull;
  *channel |= NC_BGDEFAULT_MASK | NC_BG_PALETTE | idx;
  return 0;
}

// Is this channel using RGB color?
static inline bool
ncchannel_rgb_p(uint32_t channel){
  return !(ncchannel_default_p(channel) || ncchannel_palindex_p(channel));
}

// Extract the 8-bit red component from a 32-bit channel. Only valid if
// ncchannel_rgb_p() would return true for the channel.
static inline unsigned
ncchannel_r(uint32_t channel){
  return (channel & 0xff0000u) >> 16u;
}

// Extract the 8-bit green component from a 32-bit channel. Only valid if
// ncchannel_rgb_p() would return true for the channel.
static inline unsigned
ncchannel_g(uint32_t channel){
  return (channel & 0x00ff00u) >> 8u;
}

// Extract the 8-bit blue component from a 32-bit channel. Only valid if
// ncchannel_rgb_p() would return true for the channel.
static inline unsigned
ncchannel_b(uint32_t channel){
  return (channel & 0x0000ffu);
}

// Extract the 24-bit RGB value from a 32-bit channel.
// Only valid if ncchannel_rgb_p() would return true for the channel.
static inline uint32_t
ncchannel_rgb(uint32_t channel){
  return channel & NC_BG_RGB_MASK;
}

// Extract the three 8-bit R/G/B components from a 32-bit channel.
// Only valid if ncchannel_rgb_p() would return true for the channel.
static inline uint32_t
ncchannel_rgb8(uint32_t channel, unsigned* RESTRICT r, unsigned* RESTRICT g,
               unsigned* RESTRICT b){
  *r = ncchannel_r(channel);
  *g = ncchannel_g(channel);
  *b = ncchannel_b(channel);
  return channel;
}

// Set the three 8-bit components of a 32-bit channel, and mark it as not using
// the default color. Retain the other bits unchanged. Any value greater than
// 255 will result in a return of -1 and no change to the channel.
static inline int
ncchannel_set_rgb8(uint32_t* channel, unsigned r, unsigned g, unsigned b){
  if(r >= 256 || g >= 256 || b >= 256){
    return -1;
  }
  uint32_t c = (r << 16u) | (g << 8u) | b;
  // clear the existing rgb bits, clear the palette index indicator, set
  // the not-default bit, and or in the new rgb.
  *channel = (uint32_t)((*channel & ~(NC_BG_RGB_MASK | NC_BG_PALETTE)) | NC_BGDEFAULT_MASK | c);
  return 0;
}

// Same, but provide an assembled, packed 24 bits of rgb.
static inline int
ncchannel_set(uint32_t* channel, uint32_t rgb){
  if(rgb > 0xffffffu){
    return -1;
  }
  *channel = (uint32_t)((*channel & ~(NC_BG_RGB_MASK | NC_BG_PALETTE)) | NC_BGDEFAULT_MASK | rgb);
  return 0;
}

// Set the three 8-bit components of a 32-bit channel, and mark it as not using
// the default color. Retain the other bits unchanged. r, g, and b will be
// clipped to the range [0..255].
static inline void
ncchannel_set_rgb8_clipped(uint32_t* channel, int r, int g, int b){
  if(r >= 256){
    r = 255;
  }
  if(g >= 256){
    g = 255;
  }
  if(b >= 256){
    b = 255;
  }
  if(r <= -1){
    r = 0;
  }
  if(g <= -1){
    g = 0;
  }
  if(b <= -1){
    b = 0;
  }
  uint32_t c = (uint32_t)((r << 16u) | (g << 8u) | b);
  *channel = (uint32_t)((*channel & ~(NC_BG_RGB_MASK | NC_BG_PALETTE)) | NC_BGDEFAULT_MASK | c);
}

// Extract the background alpha and coloring bits from a 64-bit channel
// pair as a single 32-bit value.
static inline uint32_t
ncchannels_bchannel(uint64_t channels){
  return channels & (NC_BG_RGB_MASK | NC_BG_PALETTE |
                     NC_BGDEFAULT_MASK | NC_BG_ALPHA_MASK);
}

// Extract the foreground alpha and coloring bits from a 64-bit channel
// pair as a single 32-bit value.
static inline uint32_t
ncchannels_fchannel(uint64_t channels){
  return ncchannels_bchannel(channels >> 32u);
}

// Extract the background alpha and coloring bits from a 64-bit channel pair.
static inline uint64_t
ncchannels_channels(uint64_t channels){
  return ncchannels_bchannel(channels) |
         ((uint64_t)ncchannels_fchannel(channels) << 32u);
}

static inline bool
ncchannels_bg_rgb_p(uint64_t channels){
  return ncchannel_rgb_p(ncchannels_bchannel(channels));
}

static inline bool
ncchannels_fg_rgb_p(uint64_t channels){
  return ncchannel_rgb_p(ncchannels_fchannel(channels));
}

// Extract 2 bits of background alpha from 'channels', shifted to LSBs.
static inline unsigned
ncchannels_bg_alpha(uint64_t channels){
  return ncchannel_alpha(ncchannels_bchannel(channels));
}

// Set the background alpha and coloring bits of the 64-bit channel pair
// from a single 32-bit value.
static inline uint64_t
ncchannels_set_bchannel(uint64_t* channels, uint32_t channel){
  // drop the background color and alpha bit
  *channels &= ((0xffffffffllu << 32u) | NC_NOBACKGROUND_MASK);
  *channels |= (uint32_t)(channel & ~NC_NOBACKGROUND_MASK);
  return *channels;
}

// Set the foreground alpha and coloring bits of the 64-bit channel pair
// from a single 32-bit value.
static inline uint64_t
ncchannels_set_fchannel(uint64_t* channels, uint32_t channel){
  // drop the foreground color and alpha bit
  *channels &= (0xffffffffllu | ((uint64_t)NC_NOBACKGROUND_MASK << 32u));
  *channels |= (uint64_t)(channel & ~NC_NOBACKGROUND_MASK) << 32u;
  return *channels;
}

// Set the alpha and coloring bits of a channel pair from another channel pair.
static inline uint64_t
ncchannels_set_channels(uint64_t* dst, uint64_t channels){
  ncchannels_set_bchannel(dst, channels & 0xffffffffull);
  ncchannels_set_fchannel(dst, (uint32_t)((channels >> 32u) & 0xffffffffull));
  return *dst;
}

// Set the 2-bit alpha component of the background channel.
static inline int
ncchannels_set_bg_alpha(uint64_t* channels, unsigned alpha){
  if(alpha == NCALPHA_HIGHCONTRAST){ // forbidden for background alpha
    return -1;
  }
  uint32_t channel = ncchannels_bchannel(*channels);
  if(ncchannel_set_alpha(&channel, alpha) < 0){
    return -1;
  }
  ncchannels_set_bchannel(channels, channel);
  return 0;
}

// Extract 2 bits of foreground alpha from 'channels', shifted to LSBs.
static inline unsigned
ncchannels_fg_alpha(uint64_t channels){
  return ncchannel_alpha(ncchannels_fchannel(channels));
}

// Set the 2-bit alpha component of the foreground channel.
static inline int
ncchannels_set_fg_alpha(uint64_t* channels, unsigned alpha){
  uint32_t channel = ncchannels_fchannel(*channels);
  if(ncchannel_set_alpha(&channel, alpha) < 0){
    return -1;
  }
  *channels = ((uint64_t)channel << 32llu) | (*channels & 0xffffffffllu);
  return 0;
}

// Returns the channels with the fore- and background's color information
// swapped, but without touching housekeeping bits. Alpha is retained unless
// it would lead to an illegal state: HIGHCONTRAST, TRANSPARENT, and BLEND
// are taken to OPAQUE unless the new value is RGB.
static inline uint64_t
ncchannels_reverse(uint64_t channels){
  const uint64_t raw = ((uint64_t)ncchannels_bchannel(channels) << 32u) +
                       ncchannels_fchannel(channels);
  const uint64_t statemask = ((NC_NOBACKGROUND_MASK | NC_BG_ALPHA_MASK) << 32u) |
                             NC_NOBACKGROUND_MASK | NC_BG_ALPHA_MASK;
  uint64_t ret = raw & ~statemask;
  ret |= channels & statemask;
  if(ncchannels_bg_alpha(ret) != NCALPHA_OPAQUE){
    if(!ncchannels_bg_rgb_p(ret)){
      ncchannels_set_bg_alpha(&ret, NCALPHA_OPAQUE);
    }
  }
  if(ncchannels_fg_alpha(ret) != NCALPHA_OPAQUE){
    if(!ncchannels_fg_rgb_p(ret)){
      ncchannels_set_fg_alpha(&ret, NCALPHA_OPAQUE);
    }
  }
  return ret;
}

// Creates a new channel pair using 'fchan' as the foreground channel
// and 'bchan' as the background channel.
static inline uint64_t
ncchannels_combine(uint32_t fchan, uint32_t bchan){
  uint64_t channels = 0;
  ncchannels_set_fchannel(&channels, fchan);
  ncchannels_set_bchannel(&channels, bchan);
  return channels;
}

static inline unsigned
ncchannels_fg_palindex(uint64_t channels){
  return ncchannel_palindex(ncchannels_fchannel(channels));
}

static inline unsigned
ncchannels_bg_palindex(uint64_t channels){
  return ncchannel_palindex(ncchannels_bchannel(channels));
}

// Extract 24 bits of foreground RGB from 'channels', shifted to LSBs.
static inline uint32_t
ncchannels_fg_rgb(uint64_t channels){
  return ncchannel_rgb(ncchannels_fchannel(channels));
}

// Extract 24 bits of background RGB from 'channels', shifted to LSBs.
static inline uint32_t
ncchannels_bg_rgb(uint64_t channels){
  return ncchannel_rgb(ncchannels_bchannel(channels));
}

// Extract 24 bits of foreground RGB from 'channels', split into subchannels.
static inline uint32_t
ncchannels_fg_rgb8(uint64_t channels, unsigned* r, unsigned* g, unsigned* b){
  return ncchannel_rgb8(ncchannels_fchannel(channels), r, g, b);
}

// Extract 24 bits of background RGB from 'channels', split into subchannels.
static inline uint32_t
ncchannels_bg_rgb8(uint64_t channels, unsigned* r, unsigned* g, unsigned* b){
  return ncchannel_rgb8(ncchannels_bchannel(channels), r, g, b);
}

// Set the r, g, and b channels for the foreground component of this 64-bit
// 'channels' variable, and mark it as not using the default color.
static inline int
ncchannels_set_fg_rgb8(uint64_t* channels, unsigned r, unsigned g, unsigned b){
  uint32_t channel = ncchannels_fchannel(*channels);
  if(ncchannel_set_rgb8(&channel, r, g, b) < 0){
    return -1;
  }
  *channels = ((uint64_t)channel << 32llu) | (*channels & 0xffffffffllu);
  return 0;
}

// Same, but clips to [0..255].
static inline void
ncchannels_set_fg_rgb8_clipped(uint64_t* channels, int r, int g, int b){
  uint32_t channel = ncchannels_fchannel(*channels);
  ncchannel_set_rgb8_clipped(&channel, r, g, b);
  *channels = ((uint64_t)channel << 32llu) | (*channels & 0xffffffffllu);
}

static inline int
ncchannels_set_fg_palindex(uint64_t* channels, unsigned idx){
  uint32_t channel = ncchannels_fchannel(*channels);
  if(ncchannel_set_palindex(&channel, idx) < 0){
    return -1;
  }
  *channels = ((uint64_t)channel << 32llu) | (*channels & 0xffffffffllu);
  return 0;
}

// Same, but set an assembled 24 bit channel at once.
static inline int
ncchannels_set_fg_rgb(uint64_t* channels, unsigned rgb){
  uint32_t channel = ncchannels_fchannel(*channels);
  if(ncchannel_set(&channel, rgb) < 0){
    return -1;
  }
  *channels = ((uint64_t)channel << 32llu) | (*channels & 0xffffffffllu);
  return 0;
}

// Set the r, g, and b channels for the background component of this 64-bit
// 'channels' variable, and mark it as not using the default color.
static inline int
ncchannels_set_bg_rgb8(uint64_t* channels, unsigned r, unsigned g, unsigned b){
  uint32_t channel = ncchannels_bchannel(*channels);
  if(ncchannel_set_rgb8(&channel, r, g, b) < 0){
    return -1;
  }
  ncchannels_set_bchannel(channels, channel);
  return 0;
}

// Same, but clips to [0..255].
static inline void
ncchannels_set_bg_rgb8_clipped(uint64_t* channels, int r, int g, int b){
  uint32_t channel = ncchannels_bchannel(*channels);
  ncchannel_set_rgb8_clipped(&channel, r, g, b);
  ncchannels_set_bchannel(channels, channel);
}

// Set the cell's background palette index, set the background palette index
// bit, set it background-opaque, and clear the background default color bit.
static inline int
ncchannels_set_bg_palindex(uint64_t* channels, unsigned idx){
  uint32_t channel = ncchannels_bchannel(*channels);
  if(ncchannel_set_palindex(&channel, idx) < 0){
    return -1;
  }
  ncchannels_set_bchannel(channels, channel);
  return 0;
}

// Same, but set an assembled 24 bit channel at once.
static inline int
ncchannels_set_bg_rgb(uint64_t* channels, unsigned rgb){
  uint32_t channel = ncchannels_bchannel(*channels);
  if(ncchannel_set(&channel, rgb) < 0){
    return -1;
  }
  ncchannels_set_bchannel(channels, channel);
  return 0;
}

// Is the foreground using the "default foreground color"?
static inline bool
ncchannels_fg_default_p(uint64_t channels){
  return ncchannel_default_p(ncchannels_fchannel(channels));
}

// Is the foreground using indexed palette color?
static inline bool
ncchannels_fg_palindex_p(uint64_t channels){
  return ncchannel_palindex_p(ncchannels_fchannel(channels));
}

// Is the background using the "default background color"? The "default
// background color" must generally be used to take advantage of
// terminal-effected transparency.
static inline bool
ncchannels_bg_default_p(uint64_t channels){
  return ncchannel_default_p(ncchannels_bchannel(channels));
}

// Is the background using indexed palette color?
static inline bool
ncchannels_bg_palindex_p(uint64_t channels){
  return ncchannel_palindex_p(ncchannels_bchannel(channels));
}

// Mark the foreground channel as using its default color.
static inline uint64_t
ncchannels_set_fg_default(uint64_t* channels){
  uint32_t channel = ncchannels_fchannel(*channels);
  ncchannel_set_default(&channel);
  ncchannels_set_fchannel(channels, channel);
  return *channels;
}

// Mark the background channel as using its default color.
static inline uint64_t
ncchannels_set_bg_default(uint64_t* channels){
  uint32_t channel = ncchannels_bchannel(*channels);
  ncchannel_set_default(&channel);
  ncchannels_set_bchannel(channels, channel);
  return *channels;
}

// 0x0--0x10ffff can be UTF-8-encoded with only 4 bytes
#define WCHAR_MAX_UTF8BYTES 4

// Returns the number of columns occupied by the longest valid prefix of a
// multibyte (UTF-8) string. If an invalid character is encountered, -1 will be
// returned, and the number of valid bytes and columns will be written into
// *|validbytes| and *|validwidth| (assuming them non-NULL). If the entire
// string is valid, *|validbytes| and *|validwidth| reflect the entire string.
API int ncstrwidth(const char* egcs, int* validbytes, int* validwidth)
  __attribute__ ((nonnull (1)));

// input functions like notcurses_get() return ucs32-encoded uint32_t. convert
// a series of uint32_t to utf8. result must be at least 4 bytes per input
// uint32_t (6 bytes per uint32_t will future-proof against Unicode expansion).
// the number of bytes used is returned, or -1 if passed illegal ucs32, or too
// small of a buffer.
API int notcurses_ucs32_to_utf8(const uint32_t* ucs32, unsigned ucs32count,
                                unsigned char* resultbuf, size_t buflen)
  __attribute__ ((nonnull (1, 3)));

// An nccell corresponds to a single character cell on some plane, which can be
// occupied by a single grapheme cluster (some root spacing glyph, along with
// possible combining characters, which might span multiple columns). At any
// cell, we can have a theoretically arbitrarily long UTF-8 EGC, a foreground
// color, a background color, and an attribute set. Valid grapheme cluster
// contents include:
//
//  * A NUL terminator,
//  * A single control character, followed by a NUL terminator,
//  * At most one spacing character, followed by zero or more nonspacing
//    characters, followed by a NUL terminator.
//
// Multi-column characters can only have a single style/color throughout.
// Existence is suffering, and thus wcwidth() is not reliable. It's just
// quoting whether or not the EGC contains a "Wide Asian" double-width
// character. This is set for some things, like most emoji, and not set for
// other things, like cuneiform. True display width is a *function of the
// font and terminal*. Among the longest Unicode codepoints is
//
//    U+FDFD ARABIC LIGATURE BISMILLAH AR-RAHMAN AR-RAHEEM ﷽
//
// wcwidth() rather optimistically claims this most exalted glyph to occupy
// a single column. BiDi text is too complicated for me to even get into here.
// Be assured there are no easy answers; ours is indeed a disturbing Universe.
//
// Each nccell occupies 16 static bytes (128 bits). The surface is thus ~1.6MB
// for a (pretty large) 500x200 terminal. At 80x43, it's less than 64KB.
// Dynamic requirements (the egcpool) can add up to 16MB to an ncplane, but
// such large pools are unlikely in common use.
//
// We implement some small alpha compositing. Foreground and background both
// have two bits of inverted alpha. The actual grapheme written to a cell is
// the topmost non-zero grapheme. If its alpha is 00, its foreground color is
// used unchanged. If its alpha is 10, its foreground color is derived entirely
// from cells underneath it. Otherwise, the result will be a composite.
// Likewise for the background. If the bottom of a coordinate's zbuffer is
// reached with a cumulative alpha of zero, the default is used. In this way,
// a terminal configured with transparent background can be supported through
// multiple occluding ncplanes. A foreground alpha of 11 requests high-contrast
// text (relative to the computed background). A background alpha of 11 is
// currently forbidden.
//
// Default color takes precedence over palette or RGB, and cannot be used with
// transparency. Indexed palette takes precedence over RGB. It cannot
// meaningfully set transparency, but it can be mixed into a cascading color.
// RGB is used if neither default terminal colors nor palette indexing are in
// play, and fully supports all transparency options.
//
// This structure is exposed only so that most functions can be inlined. Do not
// directly modify or access the fields of this structure; use the API.
typedef struct nccell {
  // These 32 bits, together with the associated plane's associated egcpool,
  // completely define this cell's EGC. Unless the EGC requires more than four
  // bytes to encode as UTF-8, it will be inlined here. If more than four bytes
  // are required, it will be spilled into the egcpool. In either case, there's
  // a NUL-terminated string available without copying, because (1) the egcpool
  // is all NUL-terminated sequences and (2) the fifth byte of this struct (the
  // gcluster_backstop field, see below) is guaranteed to be zero, as are any
  // unused bytes in gcluster.
  //
  // The gcluster + gcluster_backstop thus form a valid C string of between 0
  // and 4 non-NUL bytes. Interpreting them in this fashion requires that
  // gcluster be stored as a little-endian number (strings have no byte order).
  // This gives rise to three simple rules:
  //
  //  * when storing to gcluster from a numeric, always use htole()
  //  * when loading from gcluster for numeric use, always use htole()
  //  * when referencing gcluster as a string, always use a pointer cast
  //
  // Uses of gcluster ought thus always have exactly one htole() or pointer
  // cast associated with them, and we otherwise always work as host-endian.
  //
  // A spilled EGC is indicated by the value 0x01XXXXXX. This cannot alias a
  // true supra-ASCII EGC, because UTF-8 only encodes bytes <= 0x80 when they
  // are single-byte ASCII-derived values. The XXXXXX is interpreted as a 24-bit
  // index into the egcpool. These pools may thus be up to 16MB.
  //
  // The cost of this scheme is that the character 0x01 (SOH) cannot be encoded
  // in a nccell, which we want anyway. It must not be allowed through the API,
  // or havoc will result.
  uint32_t gcluster;          // 4B → 4B little endian EGC
  uint8_t gcluster_backstop;  // 1B → 5B (8 bits of zero)
  // we store the column width in this field. for a multicolumn EGC of N
  // columns, there will be N nccells, and each has a width of N...for now.
  // eventually, such an EGC will set more than one subsequent cell to
  // WIDE_RIGHT, and this won't be necessary. it can then be used as a
  // bytecount. see #1203. FIXME iff width >= 2, the cell is part of a
  // multicolumn glyph. whether a cell is the left or right side of the glyph
  // can be determined by checking whether ->gcluster is zero.
  uint8_t width;              // 1B → 6B (8 bits of EGC column width)
  uint16_t stylemask;         // 2B → 8B (16 bits of NCSTYLE_* attributes)
  // (channels & 0x8000000000000000ull): blitted to upper-left quadrant
  // (channels & 0x4000000000000000ull): foreground is *not* "default color"
  // (channels & 0x3000000000000000ull): foreground alpha (2 bits)
  // (channels & 0x0800000000000000ull): foreground uses palette index
  // (channels & 0x0400000000000000ull): blitted to upper-right quadrant
  // (channels & 0x0200000000000000ull): blitted to lower-left quadrant
  // (channels & 0x0100000000000000ull): blitted to lower-right quadrant
  // (channels & 0x00ffffff00000000ull): foreground in 3x8 RGB (rrggbb) / pindex
  // (channels & 0x0000000080000000ull): reserved, must be 0
  // (channels & 0x0000000040000000ull): background is *not* "default color"
  // (channels & 0x0000000030000000ull): background alpha (2 bits)
  // (channels & 0x0000000008000000ull): background uses palette index
  // (channels & 0x0000000007000000ull): reserved, must be 0
  // (channels & 0x0000000000ffffffull): background in 3x8 RGB (rrggbb) / pindex
  // At render time, these 24-bit values are quantized down to terminal
  // capabilities, if necessary. There's a clear path to 10-bit support should
  // we one day need it, but keep things cagey for now. "default color" is
  // best explained by color(3NCURSES). ours is the same concept. until the
  // "not default color" bit is set, any color you load will be ignored.
  uint64_t channels;          // + 8B == 16B
} nccell;

// do *not* load invalid EGCs using these macros! there is no way for us to
// protect against such misuse here. problems *will* ensue. similarly, do not
// set channel flags other than colors/alpha. we assign non-printing glyphs
// a width of 1 to match utf8_egc_len()'s behavior for whitespace/NUL.
// FIXME can we enforce this with static_assert?
#define NCCELL_INITIALIZER(c, s, chan) { .gcluster = (htole(c)), .gcluster_backstop = 0,\
  .width = (uint8_t)((ncwidth(c, "UTF-8") < 0 || !c) ? 1 : ncwidth(c, "UTF-8")), .stylemask = (s), .channels = (chan), }
// python fails on #define CELL_CHAR_INITIALIZER(c) CELL_INITIALIZER(c, 0, 0)
#define NCCELL_CHAR_INITIALIZER(c) { .gcluster = (htole(c)), .gcluster_backstop = 0,\
  .width = (uint8_t)((ncwidth(c, "UTF-8") < 0 || !c) ? 1 : ncwidth(c, "UTF-8")), .stylemask = 0, .channels = 0, }
// python fails on #define CELL_TRIVIAL_INITIALIZER CELL_CHAR_INITIALIZER(0)
#define NCCELL_TRIVIAL_INITIALIZER { .gcluster = 0, .gcluster_backstop = 0,\
                                     .width = 1, .stylemask = 0, .channels = 0, }

static inline void
nccell_init(nccell* c){
  memset(c, 0, sizeof(*c));
}

// Breaks the UTF-8 string in 'gcluster' down, setting up the nccell 'c'.
// Returns the number of bytes copied out of 'gcluster', or -1 on failure. The
// styling of the cell is left untouched, but any resources are released.
API int nccell_load(struct ncplane* n, nccell* c, const char* gcluster);

// nccell_load(), plus blast the styling with 'attr' and 'channels'.
static inline int
nccell_prime(struct ncplane* n, nccell* c, const char* gcluster,
             uint16_t stylemask, uint64_t channels){
  c->stylemask = stylemask;
  c->channels = channels;
  int ret = nccell_load(n, c, gcluster);
  return ret;
}

// Duplicate 'c' into 'targ'; both must be/will be bound to 'n'. Returns -1 on
// failure, and 0 on success.
API int nccell_duplicate(struct ncplane* n, nccell* targ, const nccell* c);

// Release resources held by the nccell 'c'.
API void nccell_release(struct ncplane* n, nccell* c);

// if you want reverse video, try ncchannels_reverse(). if you want blink, try
// ncplane_pulse(). if you want protection, put things on a different plane.
#define NCSTYLE_MASK      0xffffu
#define NCSTYLE_BLINK 0x0040u
#define NCSTYLE_ALTCHARSET 0x0020u
#define NCSTYLE_ITALIC    0x0010u
#define NCSTYLE_UNDERLINE 0x0008u
#define NCSTYLE_UNDERCURL 0x0004u
#define NCSTYLE_BOLD      0x0002u
#define NCSTYLE_STRUCK    0x0001u
#define NCSTYLE_NONE      0

// Set the specified style bits for the nccell 'c', whether they're actively
// supported or not. Only the lower 16 bits are meaningful.
static inline void
nccell_set_styles(nccell* c, unsigned stylebits){
  c->stylemask = stylebits & NCSTYLE_MASK;
}

// Extract the style bits from the nccell.
static inline uint16_t
nccell_styles(const nccell* c){
  return c->stylemask;
}

// Add the specified styles (in the LSBs) to the nccell's existing spec,
// whether they're actively supported or not.
static inline void
nccell_on_styles(nccell* c, unsigned stylebits){
  c->stylemask |= (uint16_t)(stylebits & NCSTYLE_MASK);
}

// Remove the specified styles (in the LSBs) from the nccell's existing spec.
static inline void
nccell_off_styles(nccell* c, unsigned stylebits){
  c->stylemask &= (uint16_t)~(stylebits & NCSTYLE_MASK);
}

// Use the default color for the foreground.
static inline void
nccell_set_fg_default(nccell* c){
  ncchannels_set_fg_default(&c->channels);
}

// Use the default color for the background.
static inline void
nccell_set_bg_default(nccell* c){
  ncchannels_set_bg_default(&c->channels);
}

static inline int
nccell_set_fg_alpha(nccell* c, unsigned alpha){
  return ncchannels_set_fg_alpha(&c->channels, alpha);
}

static inline int
nccell_set_bg_alpha(nccell* c, unsigned alpha){
  return ncchannels_set_bg_alpha(&c->channels, alpha);
}

static inline uint64_t
nccell_set_bchannel(nccell* c, uint32_t channel){
  return ncchannels_set_bchannel(&c->channels, channel);
}

static inline uint64_t
nccell_set_fchannel(nccell* c, uint32_t channel){
  return ncchannels_set_fchannel(&c->channels, channel);
}

static inline uint64_t
nccell_set_channels(nccell* c, uint64_t channels){
  return ncchannels_set_channels(&c->channels, channels);
}

// Is the cell part of a multicolumn element?
static inline bool
nccell_double_wide_p(const nccell* c){
  return (c->width >= 2);
}

// Is this the right half of a wide character?
static inline bool
nccell_wide_right_p(const nccell* c){
  return nccell_double_wide_p(c) && c->gcluster == 0;
}

// Is this the left half of a wide character?
static inline bool
nccell_wide_left_p(const nccell* c){
  return nccell_double_wide_p(c) && c->gcluster;
}

// return a pointer to the NUL-terminated EGC referenced by 'c'. this pointer
// can be invalidated by any further operation on the plane 'n', so...watch out!
API __attribute__ ((returns_nonnull)) const char*
nccell_extended_gcluster(const struct ncplane* n, const nccell* c);

static inline uint64_t
nccell_channels(const nccell* c){
  return ncchannels_channels(c->channels);
}

// Extract the background alpha and coloring bits from a cell's channels
// as a single 32-bit value.
static inline uint32_t
nccell_bchannel(const nccell* cl){
  return ncchannels_bchannel(cl->channels);
}

// Extract the foreground alpha and coloring bits from a cell's channels
// as a single 32-bit value.
static inline uint32_t
nccell_fchannel(const nccell* cl){
  return ncchannels_fchannel(cl->channels);
}

// return the number of columns occupied by 'c'. see ncstrwidth() for an
// equivalent for multiple EGCs.
static inline unsigned
nccell_cols(const nccell* c){
  return c->width ? c->width : 1;
}

// copy the UTF8-encoded EGC out of the nccell. the result is not tied to any
// ncplane, and persists across erases / destruction.
ALLOC static inline char*
nccell_strdup(const struct ncplane* n, const nccell* c){
  return strdup(nccell_extended_gcluster(n, c));
}

// Extract the three elements of a nccell.
static inline char*
nccell_extract(const struct ncplane* n, const nccell* c,
               uint16_t* stylemask, uint64_t* channels){
  if(stylemask){
    *stylemask = c->stylemask;
  }
  if(channels){
    *channels = c->channels;
  }
  return nccell_strdup(n, c);
}

// Returns true if the two nccells are distinct EGCs, attributes, or channels.
// The actual egcpool index needn't be the same--indeed, the planes needn't even
// be the same. Only the expanded EGC must be equal. The EGC must be bit-equal;
// it would probably be better to test whether they're Unicode-equal FIXME.
// probably needs be fixed up for sprixels FIXME.
static inline bool
nccellcmp(const struct ncplane* n1, const nccell* RESTRICT c1,
          const struct ncplane* n2, const nccell* RESTRICT c2){
  if(c1->stylemask != c2->stylemask){
    return true;
  }
  if(c1->channels != c2->channels){
    return true;
  }
  return strcmp(nccell_extended_gcluster(n1, c1), nccell_extended_gcluster(n2, c2));
}

// Load a 7-bit char 'ch' into the nccell 'c'. Returns the number of bytes
// used, or -1 on error.
static inline int
nccell_load_char(struct ncplane* n, nccell* c, char ch){
  char gcluster[2];
  gcluster[0] = ch;
  gcluster[1] = '\0';
  return nccell_load(n, c, gcluster);
}

// Load a UTF-8 encoded EGC of up to 4 bytes into the nccell 'c'. Returns the
// number of bytes used, or -1 on error.
static inline int
nccell_load_egc32(struct ncplane* n, nccell* c, uint32_t egc){
  char gcluster[sizeof(egc) + 1];
  egc = htole(egc);
  memcpy(gcluster, &egc, sizeof(egc));
  gcluster[4] = '\0';
  return nccell_load(n, c, gcluster);
}

// Load a UCS-32 codepoint into the nccell 'c'. Returns the number of bytes
// used, or -1 on error.
static inline int
nccell_load_ucs32(struct ncplane* n, nccell* c, uint32_t u){
  unsigned char utf8[WCHAR_MAX_UTF8BYTES];
  if(notcurses_ucs32_to_utf8(&u, 1, utf8, sizeof(utf8)) < 0){
    return -1;
  }
  uint32_t utf8asegc;
  _Static_assert(WCHAR_MAX_UTF8BYTES == sizeof(utf8asegc),
                 "WCHAR_MAX_UTF8BYTES didn't equal sizeof(uint32_t)");
  memcpy(&utf8asegc, utf8, sizeof(utf8));
  return nccell_load_egc32(n, c, utf8asegc);
}

// These log levels consciously map cleanly to those of libav; Notcurses itself
// does not use this full granularity. The log level does not affect the opening
// and closing banners, which can be disabled via the notcurses_option struct's
// 'suppress_banner'. Note that if stderr is connected to the same terminal on
// which we're rendering, any kind of logging will disrupt the output (which is
// undesirable). The "default" zero value is NCLOGLEVEL_PANIC.
typedef enum {
  NCLOGLEVEL_SILENT = -1,// default. print nothing once fullscreen service begins
  NCLOGLEVEL_PANIC = 0,  // print diagnostics related to catastrophic failure
  NCLOGLEVEL_FATAL = 1,  // we're hanging around, but we've had a horrible fault
  NCLOGLEVEL_ERROR = 2,  // we can't keep doing this, but we can do other things
  NCLOGLEVEL_WARNING = 3,// you probably don't want what's happening to happen
  NCLOGLEVEL_INFO = 4,   // "standard information"
  NCLOGLEVEL_VERBOSE = 5,// "detailed information"
  NCLOGLEVEL_DEBUG = 6,  // this is honestly a bit much
  NCLOGLEVEL_TRACE = 7,  // there's probably a better way to do what you want
} ncloglevel_e;

// Bits for notcurses_options->flags.

// notcurses_init() will call setlocale() to inspect the current locale. If
// that locale is "C" or "POSIX", it will call setlocale(LC_ALL, "") to set
// the locale according to the LANG environment variable. Ideally, this will
// result in UTF8 being enabled, even if the client app didn't call
// setlocale() itself. Unless you're certain that you're invoking setlocale()
// prior to notcurses_init(), you should not set this bit. Even if you are
// invoking setlocale(), this behavior shouldn't be an issue unless you're
// doing something weird (setting a locale not based on LANG).
#define NCOPTION_INHIBIT_SETLOCALE   0x0001ull

// We typically try to clear any preexisting bitmaps. If we ought *not* try
// to do this, pass NCOPTION_NO_CLEAR_BITMAPS. Note that they might still
// get cleared even if this is set, and they might not get cleared even if
// this is not set. It's a tough world out there.
#define NCOPTION_NO_CLEAR_BITMAPS    0x0002ull

// We typically install a signal handler for SIGWINCH that generates a resize
// event in the notcurses_get() queue. Set to inhibit this handler.
#define NCOPTION_NO_WINCH_SIGHANDLER 0x0004ull

// We typically install a signal handler for SIG{INT, ILL, SEGV, ABRT, TERM,
// QUIT} that restores the screen, and then calls the old signal handler. Set
// to inhibit registration of these signal handlers.
#define NCOPTION_NO_QUIT_SIGHANDLERS 0x0008ull

// Initialize the standard plane's virtual cursor to match the physical cursor
// at context creation time. Together with NCOPTION_NO_ALTERNATE_SCREEN and a
// scrolling standard plane, this facilitates easy scrolling-style programs in
// rendered mode.
#define NCOPTION_PRESERVE_CURSOR     0x0010ull

// Notcurses typically prints version info in notcurses_init() and performance
// info in notcurses_stop(). This inhibits that output.
#define NCOPTION_SUPPRESS_BANNERS    0x0020ull

// If smcup/rmcup capabilities are indicated, Notcurses defaults to making use
// of the "alternate screen". This flag inhibits use of smcup/rmcup.
#define NCOPTION_NO_ALTERNATE_SCREEN 0x0040ull

// Do not modify the font. Notcurses might attempt to change the font slightly,
// to support certain glyphs (especially on the Linux console). If this is set,
// no such modifications will be made. Note that font changes will not affect
// anything but the virtual console/terminal in which Notcurses is running.
#define NCOPTION_NO_FONT_CHANGES     0x0080ull

// Input may be freely dropped. This ought be provided when the program does not
// intend to handle input. Otherwise, input can accumulate in internal buffers,
// eventually preventing Notcurses from processing terminal messages.
#define NCOPTION_DRAIN_INPUT         0x0100ull

// Prepare the standard plane in scrolling mode, useful for CLIs. This is
// equivalent to calling ncplane_set_scrolling(notcurses_stdplane(nc), true).
#define NCOPTION_SCROLLING           0x0200ull

// "CLI mode" is just setting these four options.
#define NCOPTION_CLI_MODE (NCOPTION_NO_ALTERNATE_SCREEN \
                           |NCOPTION_NO_CLEAR_BITMAPS \
                           |NCOPTION_PRESERVE_CURSOR \
                           |NCOPTION_SCROLLING)

// Configuration for notcurses_init().
typedef struct notcurses_options {
  // The name of the terminfo database entry describing this terminal. If NULL,
  // the environment variable TERM is used. Failure to open the terminal
  // definition will result in failure to initialize notcurses.
  const char* termtype;
  // Progressively higher log levels result in more logging to stderr. By
  // default, nothing is printed to stderr once fullscreen service begins.
  ncloglevel_e loglevel;
  // Desirable margins. If all are 0 (default), we will render to the entirety
  // of the screen. If the screen is too small, we do what we can--this is
  // strictly best-effort. Absolute coordinates are relative to the rendering
  // area ((0, 0) is always the origin of the rendering area).
  unsigned margin_t, margin_r, margin_b, margin_l;
  // General flags; see NCOPTION_*. This is expressed as a bitfield so that
  // future options can be added without reshaping the struct. Undefined bits
  // must be set to 0.
  uint64_t flags;
} notcurses_options;

// Lex a margin argument according to the standard Notcurses definition. There
// can be either a single number, which will define all margins equally, or
// there can be four numbers separated by commas.
API int notcurses_lex_margins(const char* op, notcurses_options* opts)
  __attribute__ ((nonnull (1)));

// Lex a blitter.
API int notcurses_lex_blitter(const char* op, ncblitter_e* blitter)
  __attribute__ ((nonnull (1)));

// Get the name of a blitter.
API const char* notcurses_str_blitter(ncblitter_e blitter);

// Lex a scaling mode (one of "none", "stretch", "scale", "hires",
// "scalehi", or "inflate").
API int notcurses_lex_scalemode(const char* op, ncscale_e* scalemode)
  __attribute__ ((nonnull (1)));

// Get the name of a scaling mode.
API const char* notcurses_str_scalemode(ncscale_e scalemode);

// Initialize a Notcurses context on the connected terminal at 'fp'. 'fp' must
// be a tty. You'll usually want stdout. NULL can be supplied for 'fp', in
// which case /dev/tty will be opened. Returns NULL on error, including any
// failure initializing terminfo.
API ALLOC struct notcurses* notcurses_init(const notcurses_options* opts, FILE* fp);

// The same as notcurses_init(), but without any multimedia functionality,
// allowing for a svelter binary. Link with notcurses-core if this is used.
API ALLOC struct notcurses* notcurses_core_init(const notcurses_options* opts, FILE* fp);

// Destroy a Notcurses context. A NULL 'nc' is a no-op.
API int notcurses_stop(struct notcurses* nc);

// Shift to the alternate screen, if available. If already using the alternate
// screen, this returns 0 immediately. If the alternate screen is not
// available, this returns -1 immediately. Entering the alternate screen turns
// off scrolling for the standard plane.
API int notcurses_enter_alternate_screen(struct notcurses* nc)
  __attribute__ ((nonnull (1)));

// Exit the alternate screen. Immediately returns 0 if not currently using the
// alternate screen.
API int notcurses_leave_alternate_screen(struct notcurses* nc)
  __attribute__ ((nonnull (1)));

// Get a reference to the standard plane (one matching our current idea of the
// terminal size) for this terminal. The standard plane always exists, and its
// origin is always at the uppermost, leftmost cell of the terminal.
API struct ncplane* notcurses_stdplane(struct notcurses* nc)
  __attribute__ ((nonnull (1)));
API const struct ncplane* notcurses_stdplane_const(const struct notcurses* nc)
  __attribute__ ((nonnull (1)));

// Return the topmost plane of the pile containing 'n'.
API struct ncplane* ncpile_top(struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Return the bottommost plane of the pile containing 'n'.
API struct ncplane* ncpile_bottom(struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Return the topmost plane of the standard pile.
static inline struct ncplane*
notcurses_top(struct notcurses* n){
  return ncpile_top(notcurses_stdplane(n));
}

// Return the bottommost plane of the standard pile.
static inline struct ncplane*
notcurses_bottom(struct notcurses* n){
  return ncpile_bottom(notcurses_stdplane(n));
}

// Renders the pile of which 'n' is a part. Rendering this pile again will blow
// away the render. To actually write out the render, call ncpile_rasterize().
API int ncpile_render(struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Make the physical screen match the last rendered frame from the pile of
// which 'n' is a part. This is a blocking call. Don't call this before the
// pile has been rendered (doing so will likely result in a blank screen).
API int ncpile_rasterize(struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Renders and rasterizes the standard pile in one shot. Blocking call.
static inline int
notcurses_render(struct notcurses* nc){
  struct ncplane* stdn = notcurses_stdplane(nc);
  if(ncpile_render(stdn)){
    return -1;
  }
  return ncpile_rasterize(stdn);
}

// Perform the rendering and rasterization portion of ncpile_render() and
// ncpile_rasterize(), but do not write the resulting buffer out to the
// terminal. Using this function, the user can control the writeout process.
// The returned buffer must be freed by the caller.
API int ncpile_render_to_buffer(struct ncplane* p, char** buf, size_t* buflen)
  __attribute__ ((nonnull (1, 2, 3)));

// Write the last rendered frame, in its entirety, to 'fp'. If a frame has
// not yet been rendered, nothing will be written.
API int ncpile_render_to_file(struct ncplane* p, FILE* fp)
  __attribute__ ((nonnull (1, 2)));

// Destroy all ncplanes other than the stdplane.
API void notcurses_drop_planes(struct notcurses* nc)
  __attribute__ ((nonnull (1)));

// All input is taken from stdin. We attempt to read a single UTF8-encoded
// Unicode codepoint, *not* an entire Extended Grapheme Cluster. It is also
// possible that we will read a special keypress, i.e. anything that doesn't
// correspond to a Unicode codepoint (e.g. arrow keys, function keys, screen
// resize events, etc.). These are mapped into a Unicode's area beyond the
// 17 65536-entry Planes, starting at U+1115000. See <notcurses/nckeys.h>.
//
// notcurses_get_nblock() is nonblocking. notcurses_get_blocking() blocks
// until a codepoint or special key is read, or until interrupted by a signal.
// notcurses_get() allows an optional timeout to be controlled.
//
// In the case of a valid read, a 32-bit Unicode codepoint is returned. 0 is
// returned to indicate that no input was available. Otherwise (including on
// EOF) (uint32_t)-1 is returned.

// Is the event a synthesized mouse event?
static inline bool
nckey_mouse_p(uint32_t r){
  return r >= NCKEY_MOTION && r <= NCKEY_BUTTON11;
}

typedef enum {
  NCTYPE_UNKNOWN,
  NCTYPE_PRESS,
  NCTYPE_REPEAT,
  NCTYPE_RELEASE,
} ncintype_e;

// Note: changing this also means adding kitty_cb_atxt functions in
// in.c otherwise extra codepoints won't be picked up.
#define NCINPUT_MAX_EFF_TEXT_CODEPOINTS 4

// An input event. Cell coordinates are currently defined only for mouse
// events. It is not guaranteed that we can set the modifiers for a given
// ncinput. We encompass single Unicode codepoints, not complete EGCs.
// FIXME for abi4, combine the bools into |modifiers|
typedef struct ncinput {
  uint32_t id;       // Unicode codepoint or synthesized NCKEY event
  int y, x;          // y/x cell coordinate of event, -1 for undefined
  char utf8[5];      // utf8 representation, if one exists
  // DEPRECATED do not use! going away in 4.0
  bool alt;          // was alt held?
  bool shift;        // was shift held?
  bool ctrl;         // was ctrl held?
  // END DEPRECATION
  ncintype_e evtype;
  unsigned modifiers;// bitmask over NCKEY_MOD_*
  int ypx, xpx;      // pixel offsets within cell, -1 for undefined
  uint32_t eff_text[NCINPUT_MAX_EFF_TEXT_CODEPOINTS];  // Effective 
                     // utf32 representation, taking modifier 
                     // keys into account. This can be multiple
                     // codepoints. Array is zero-terminated.
  const char* paste_content;
} ncinput;

static inline bool
ncinput_shift_p(const ncinput* n){
  return (n->modifiers & NCKEY_MOD_SHIFT);
}

static inline bool
ncinput_ctrl_p(const ncinput* n){
  return (n->modifiers & NCKEY_MOD_CTRL);
}

static inline bool
ncinput_alt_p(const ncinput* n){
  return (n->modifiers & NCKEY_MOD_ALT);
}

static inline bool
ncinput_meta_p(const ncinput* n){
  return (n->modifiers & NCKEY_MOD_META);
}

static inline bool
ncinput_super_p(const ncinput* n){
  return (n->modifiers & NCKEY_MOD_SUPER);
}

static inline bool
ncinput_hyper_p(const ncinput* n){
  return (n->modifiers & NCKEY_MOD_HYPER);
}

static inline bool
ncinput_capslock_p(const ncinput* n){
  return (n->modifiers & NCKEY_MOD_CAPSLOCK);
}

static inline bool
ncinput_numlock_p(const ncinput* n){
  return (n->modifiers & NCKEY_MOD_NUMLOCK);
}

static inline bool
ncinput_mouse_p(const ncinput* n)
{
  return (NCKEY_MOTION <= n->id && n->id <= NCKEY_BUTTON11);
}

static inline bool
ncinput_lock_p(const ncinput* n)
{
  return NCKEY_CAPS_LOCK <= n->id && n->id <= NCKEY_NUM_LOCK;
}

static inline bool
ncinput_modifier_p(const ncinput* n)
{
  return NCKEY_LSHIFT <= n->id && n->id <= NCKEY_L5SHIFT;
}

// compare two ncinput structs for data equality. NCTYPE_PRESS and
// NCTYPE_UNKNOWN are considered to be equivalent. NCKEY_MOD_CAPSLOCK
// and NCKEY_MOD_NUMLOCK are not considered relevant.
static inline bool
ncinput_equal_p(const ncinput* n1, const ncinput* n2){
  // don't need to check ->utf8; it's derived from id
  if(n1->id != n2->id){
    return false;
  }
  if(n1->y != n2->y || n1->x != n2->x){
    return false;
  }
  // don't need to check deprecated alt, ctrl, shift
  if((n1->modifiers & ~(unsigned)(NCKEY_MOD_CAPSLOCK | NCKEY_MOD_NUMLOCK))
      != (n2->modifiers & ~(unsigned)(NCKEY_MOD_CAPSLOCK | NCKEY_MOD_NUMLOCK))){
    return false;
  }
  if(n1->evtype != n2->evtype){
    if((n1->evtype != NCTYPE_UNKNOWN && n1->evtype != NCTYPE_PRESS) ||
       (n2->evtype != NCTYPE_UNKNOWN && n2->evtype != NCTYPE_PRESS)){
      return false;
    }
  }
  if(n1->ypx != n2->ypx || n1->xpx != n2->xpx){
    return false;
  }
  return true;
}

void
ncinput_free_paste_content(ncinput* n);

// Read a UTF-32-encoded Unicode codepoint from input. This might only be part
// of a larger EGC. Provide a NULL 'ts' to block at length, and otherwise a
// timespec specifying an absolute deadline calculated using CLOCK_MONOTONIC.
// Returns a single Unicode code point, or a synthesized special key constant,
// or (uint32_t)-1 on error. Returns 0 on a timeout. If an event is processed,
// the return value is the 'id' field from that event. 'ni' may be NULL.
API uint32_t notcurses_get(struct notcurses* n, const struct timespec* ts,
                           ncinput* ni)
  __attribute__ ((nonnull (1)));

// Acquire up to 'vcount' ncinputs at the vector 'ni'. The number read will be
// returned, or -1 on error without any reads, 0 on timeout.
API int notcurses_getvec(struct notcurses* n, const struct timespec* ts,
                         ncinput* ni, int vcount)
  __attribute__ ((nonnull (1, 3)));

// Get a file descriptor suitable for input event poll()ing. When this
// descriptor becomes available, you can call notcurses_get_nblock(),
// and input ought be ready. This file descriptor is *not* necessarily
// the file descriptor associated with stdin (but it might be!).
API int notcurses_inputready_fd(struct notcurses* n)
  __attribute__ ((nonnull (1)));

// 'ni' may be NULL if the caller is uninterested in event details. If no event
// is immediately ready, returns 0.
static inline uint32_t
notcurses_get_nblock(struct notcurses* n, ncinput* ni){
  struct timespec ts = { .tv_sec = 0, .tv_nsec = 0 };
  return notcurses_get(n, &ts, ni);
}

// 'ni' may be NULL if the caller is uninterested in event details. Blocks
// until an event is processed or a signal is received (including resize events).
static inline uint32_t
notcurses_get_blocking(struct notcurses* n, ncinput* ni){
  return notcurses_get(n, NULL, ni);
}

// Was 'ni' free of modifiers?
static inline bool
ncinput_nomod_p(const ncinput* ni){
  return !(ni->modifiers);
}

#define NCMICE_NO_EVENTS     0
#define NCMICE_MOVE_EVENT    0x1
#define NCMICE_BUTTON_EVENT  0x2
#define NCMICE_DRAG_EVENT    0x4
#define NCMICE_ALL_EVENTS    0x7

// Enable mice events according to 'eventmask'; an eventmask of 0 will disable
// all mice tracking. On failure, -1 is returned. On success, 0 is returned, and
// mouse events will be published to notcurses_get().
API int notcurses_mice_enable(struct notcurses* n, unsigned eventmask)
  __attribute__ ((nonnull (1)));

// Disable mouse events. Any events in the input queue can still be delivered.
__attribute__ ((nonnull (1))) static inline int
notcurses_mice_disable(struct notcurses* n){
  return notcurses_mice_enable(n, NCMICE_NO_EVENTS);
}

// Disable signals originating from the terminal's line discipline, i.e.
// SIGINT (^C), SIGQUIT (^\), and SIGTSTP (^Z). They are enabled by default.
API int notcurses_linesigs_disable(struct notcurses* n)
  __attribute__ ((nonnull (1)));

// Restore signals originating from the terminal's line discipline, i.e.
// SIGINT (^C), SIGQUIT (^\), and SIGTSTP (^Z), if disabled.
API int notcurses_linesigs_enable(struct notcurses* n)
  __attribute__ ((nonnull (1)));

// Refresh the physical screen to match what was last rendered (i.e., without
// reflecting any changes since the last call to notcurses_render()). This is
// primarily useful if the screen is externally corrupted, or if an
// NCKEY_RESIZE event has been read and you're not yet ready to render. The
// current screen geometry is returned in 'y' and 'x', if they are not NULL.
API int notcurses_refresh(struct notcurses* n, unsigned* RESTRICT y, unsigned* RESTRICT x)
  __attribute__ ((nonnull (1)));

// Extract the Notcurses context to which this plane is attached.
API struct notcurses* ncplane_notcurses(const struct ncplane* n)
  __attribute__ ((nonnull (1)));

API const struct notcurses* ncplane_notcurses_const(const struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Return the dimensions of this ncplane. y or x may be NULL.
API void ncplane_dim_yx(const struct ncplane* n, unsigned* RESTRICT y, unsigned* RESTRICT x)
  __attribute__ ((nonnull (1)));

// notcurses_stdplane(), plus free bonus dimensions written to non-NULL y/x!
static inline struct ncplane*
notcurses_stddim_yx(struct notcurses* nc, unsigned* RESTRICT y, unsigned* RESTRICT x){
  struct ncplane* s = notcurses_stdplane(nc); // can't fail
  ncplane_dim_yx(s, y, x); // accepts NULL
  return s;
}

static inline const struct ncplane*
notcurses_stddim_yx_const(const struct notcurses* nc, unsigned* RESTRICT y, unsigned* RESTRICT x){
  const struct ncplane* s = notcurses_stdplane_const(nc); // can't fail
  ncplane_dim_yx(s, y, x); // accepts NULL
  return s;
}

static inline unsigned
ncplane_dim_y(const struct ncplane* n){
  unsigned dimy;
  ncplane_dim_yx(n, &dimy, NULL);
  return dimy;
}

static inline unsigned
ncplane_dim_x(const struct ncplane* n){
  unsigned dimx;
  ncplane_dim_yx(n, NULL, &dimx);
  return dimx;
}

// Retrieve pixel geometry for the display region ('pxy', 'pxx'), each cell
// ('celldimy', 'celldimx'), and the maximum displayable bitmap ('maxbmapy',
// 'maxbmapx'). If bitmaps are not supported, or if there is no artificial
// limit on bitmap size, 'maxbmapy' and 'maxbmapx' will be 0. Any of the
// geometry arguments may be NULL.
API void ncplane_pixel_geom(const struct ncplane* n,
                           unsigned* RESTRICT pxy, unsigned* RESTRICT pxx,
                           unsigned* RESTRICT celldimy, unsigned* RESTRICT celldimx,
                           unsigned* RESTRICT maxbmapy, unsigned* RESTRICT maxbmapx)
  __attribute__ ((nonnull (1)));

// Return our current idea of the terminal dimensions in rows and cols.
static inline void
notcurses_term_dim_yx(const struct notcurses* n, unsigned* RESTRICT rows, unsigned* RESTRICT cols){
  ncplane_dim_yx(notcurses_stdplane_const(n), rows, cols);
}

// Retrieve the contents of the specified cell as last rendered. Returns the EGC
// or NULL on error. This EGC must be free()d by the caller. The stylemask and
// channels are written to 'stylemask' and 'channels', respectively.
API char* notcurses_at_yx(struct notcurses* nc, unsigned yoff, unsigned xoff,
                          uint16_t* stylemask, uint64_t* channels)
  __attribute__ ((nonnull (1)));

// Horizontal alignment relative to the parent plane. Use ncalign_e for 'x'.
#define NCPLANE_OPTION_HORALIGNED   0x0001ull
// Vertical alignment relative to the parent plane. Use ncalign_e for 'y'.
#define NCPLANE_OPTION_VERALIGNED   0x0002ull
// Maximize relative to the parent plane, modulo the provided margins. The
// margins are best-effort; the plane will always be at least 1 column by
// 1 row. If the margins can be effected, the plane will be sized to all
// remaining space. 'y' and 'x' are overloaded as the top and left margins
// when this flag is used. 'rows' and 'cols' must be 0 when this flag is
// used. This flag is exclusive with both of the alignment flags.
#define NCPLANE_OPTION_MARGINALIZED 0x0004ull
// If this plane is bound to a scrolling plane, it ought *not* scroll along
// with the parent (it will still move with the parent, maintaining its
// relative position, if the parent is moved to a new location).
#define NCPLANE_OPTION_FIXED        0x0008ull
// Enable automatic growth of the plane to accommodate output. Creating a
// plane with this flag is equivalent to immediately calling
// ncplane_set_autogrow(p, true) following plane creation.
#define NCPLANE_OPTION_AUTOGROW     0x0010ull
// Enable vertical scrolling of the plane to accommodate output. Creating a
// plane with this flag is equivalent to immediately calling
// ncplane_set_scrolling(p, true) following plane creation.
#define NCPLANE_OPTION_VSCROLL      0x0020ull

typedef struct ncplane_options {
  int y;            // vertical placement relative to parent plane
  int x;            // horizontal placement relative to parent plane
  unsigned rows;    // rows, must be >0 unless NCPLANE_OPTION_MARGINALIZED
  unsigned cols;    // columns, must be >0 unless NCPLANE_OPTION_MARGINALIZED
  void* userptr;    // user curry, may be NULL
  const char* name; // name (used only for debugging), may be NULL
  int (*resizecb)(struct ncplane*); // callback when parent is resized
  uint64_t flags;   // closure over NCPLANE_OPTION_*
  unsigned margin_b, margin_r; // margins (require NCPLANE_OPTION_MARGINALIZED)
} ncplane_options;

// Create a new ncplane bound to plane 'n', at the offset 'y'x'x' (relative to
// the origin of 'n') and the specified size. The number of 'rows' and 'cols'
// must both be positive. This plane is initially at the top of the z-buffer,
// as if ncplane_move_top() had been called on it. The void* 'userptr' can be
// retrieved (and reset) later. A 'name' can be set, used in debugging.
API ALLOC struct ncplane* ncplane_create(struct ncplane* n, const ncplane_options* nopts)
  __attribute__ ((nonnull (1, 2)));

// Same as ncplane_create(), but creates a new pile. The returned plane will
// be the top, bottom, and root of this new pile.
API ALLOC struct ncplane* ncpile_create(struct notcurses* nc, const ncplane_options* nopts)
  __attribute__ ((nonnull (1, 2)));

// Utility resize callbacks. When a parent plane is resized, it invokes each
// child's resize callback. Any logic can be run in a resize callback, but
// these are some generically useful ones.

// resize the plane to the visual region's size (used for the standard plane).
API int ncplane_resize_maximize(struct ncplane* n);

// resize the plane to its parent's size, attempting to enforce the margins
// supplied along with NCPLANE_OPTION_MARGINALIZED.
API int ncplane_resize_marginalized(struct ncplane* n);

// realign the plane 'n' against its parent, using the alignments specified
// with NCPLANE_OPTION_HORALIGNED and/or NCPLANE_OPTION_VERALIGNED.
API int ncplane_resize_realign(struct ncplane* n);

// move the plane such that it is entirely within its parent, if possible.
// no resizing is performed.
API int ncplane_resize_placewithin(struct ncplane* n);

// Replace the ncplane's existing resizecb with 'resizecb' (which may be NULL).
// The standard plane's resizecb may not be changed.
API void ncplane_set_resizecb(struct ncplane* n, int(*resizecb)(struct ncplane*));

// Returns the ncplane's current resize callback.
API int (*ncplane_resizecb(const struct ncplane* n))(struct ncplane*);

// Set the plane's name (may be NULL), replacing any current name.
API int ncplane_set_name(struct ncplane* n, const char* name)
  __attribute__ ((nonnull (1)));

// Return a heap-allocated copy of the plane's name, or NULL if it has none.
API ALLOC char* ncplane_name(const struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Plane 'n' will be unbound from its parent plane, and will be made a bound
// child of 'newparent'. It is an error if 'n' or 'newparent' are NULL. If
// 'newparent' is equal to 'n', 'n' becomes the root of a new pile, unless 'n'
// is already the root of a pile, in which case this is a no-op. Returns 'n'.
// The standard plane cannot be reparented. Any planes bound to 'n' are
// reparented to the previous parent of 'n'.
API struct ncplane* ncplane_reparent(struct ncplane* n, struct ncplane* newparent)
  __attribute__ ((nonnull (1, 2)));

// The same as ncplane_reparent(), except any planes bound to 'n' come along
// with it to its new destination. Their z-order is maintained. If 'newparent'
// is an ancestor of 'n', NULL is returned, and no changes are made.
API struct ncplane* ncplane_reparent_family(struct ncplane* n, struct ncplane* newparent)
  __attribute__ ((nonnull (1, 2)));

// Duplicate an existing ncplane. The new plane will have the same geometry,
// will duplicate all content, and will start with the same rendering state.
// The new plane will be immediately above the old one on the z axis, and will
// be bound to the same parent (unless 'n' is a root plane, in which case the
// new plane will be bound to it). Bound planes are *not* duplicated; the new
// plane is bound to the parent of 'n', but has no bound planes.
API ALLOC struct ncplane* ncplane_dup(const struct ncplane* n, void* opaque)
  __attribute__ ((nonnull (1)));

// provided a coordinate relative to the origin of 'src', map it to the same
// absolute coordinate relative to the origin of 'dst'. either or both of 'y'
// and 'x' may be NULL. if 'dst' is NULL, it is taken to be the standard plane.
API void ncplane_translate(const struct ncplane* src, const struct ncplane* dst,
                           int* RESTRICT y, int* RESTRICT x)
  __attribute__ ((nonnull (1)));

// Fed absolute 'y'/'x' coordinates, determine whether that coordinate is
// within the ncplane 'n'. If not, return false. If so, return true. Either
// way, translate the absolute coordinates relative to 'n'. If the point is not
// within 'n', these coordinates will not be within the dimensions of the plane.
API bool ncplane_translate_abs(const struct ncplane* n, int* RESTRICT y, int* RESTRICT x)
  __attribute__ ((nonnull (1)));

// All planes are created with scrolling disabled. Scrolling can be dynamically
// controlled with ncplane_set_scrolling(). Returns true if scrolling was
// previously enabled, or false if it was disabled.
API bool ncplane_set_scrolling(struct ncplane* n, unsigned scrollp)
  __attribute__ ((nonnull (1)));

API bool ncplane_scrolling_p(const struct ncplane* n)
  __attribute__ ((nonnull (1)));

// By default, planes are created with autogrow disabled. Autogrow can be
// dynamically controlled with ncplane_set_autogrow(). Returns true if
// autogrow was previously enabled, or false if it was disabled.
API bool ncplane_set_autogrow(struct ncplane* n, unsigned growp)
  __attribute__ ((nonnull (1)));

API bool ncplane_autogrow_p(const struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Palette API. Some terminals only support 256 colors, but allow the full
// palette to be specified with arbitrary RGB colors. In all cases, it's more
// performant to use indexed colors, since it's much less data to write to the
// terminal. If you can limit yourself to 256 colors, that's probably best.

typedef struct ncpalette {
  uint32_t chans[NCPALETTESIZE]; // RGB values as regular ol' channels
} ncpalette;

// Create a new palette store. It will be initialized with notcurses' best
// knowledge of the currently configured palette.
API ALLOC ncpalette* ncpalette_new(struct notcurses* nc)
  __attribute__ ((nonnull (1)));

// Attempt to configure the terminal with the provided palette 'p'. Does not
// transfer ownership of 'p'; ncpalette_free() can (ought) still be called.
API int ncpalette_use(struct notcurses* nc, const ncpalette* p)
  __attribute__ ((nonnull (1, 2)));

// Manipulate entries in the palette store 'p'. These are *not* locked.
static inline int
ncpalette_set_rgb8(ncpalette* p, int idx, unsigned r, unsigned g, unsigned b){
  if(idx < 0 || (size_t)idx > sizeof(p->chans) / sizeof(*p->chans)){
    return -1;
  }
  return ncchannel_set_rgb8(&p->chans[idx], r, g, b);
}

static inline int
ncpalette_set(ncpalette* p, int idx, unsigned rgb){
  if(idx < 0 || (size_t)idx > sizeof(p->chans) / sizeof(*p->chans)){
    return -1;
  }
  return ncchannel_set(&p->chans[idx], rgb);
}

static inline int
ncpalette_get(const ncpalette* p, int idx, uint32_t* palent){
  if(idx < 0 || (size_t)idx > sizeof(p->chans) / sizeof(*p->chans)){
    return -1;
  }
  *palent = ncchannel_rgb(p->chans[idx]);
  return 0;
}

static inline int
ncpalette_get_rgb8(const ncpalette* p, int idx, unsigned* RESTRICT r, unsigned* RESTRICT g, unsigned* RESTRICT b){
  if(idx < 0 || (size_t)idx > sizeof(p->chans) / sizeof(*p->chans)){
    return -1;
  }
  return (int)ncchannel_rgb8(p->chans[idx], r, g, b);
}

// Free the palette store 'p'.
API void ncpalette_free(ncpalette* p);

// Capabilities, derived from terminfo, environment variables, and queries
typedef struct nccapabilities {
  unsigned colors;        // size of palette for indexed colors
  bool utf8;              // are we using utf-8 encoding? from nl_langinfo(3)
  bool rgb;               // 24bit color? COLORTERM/heuristics/terminfo 'rgb'
  bool can_change_colors; // can we change the palette? terminfo 'ccc'
  // these are assigned wholly through TERM- and query-based heuristics
  bool halfblocks;// we assume halfblocks, but some are known to lack them
  bool quadrants; // do we have (good, vetted) Unicode 1 quadrant support?
  bool sextants;  // do we have (good, vetted) Unicode 13 sextant support?
  bool braille;   // do we have Braille support? (linux console does not)
} nccapabilities;

// Returns a 16-bit bitmask of supported curses-style attributes
// (NCSTYLE_UNDERLINE, NCSTYLE_BOLD, etc.) The attribute is only
// indicated as supported if the terminal can support it together with color.
// For more information, see the "ncv" capability in terminfo(5).
API uint16_t notcurses_supported_styles(const struct notcurses* nc)
  __attribute__ ((nonnull (1))) __attribute__ ((pure));

// Returns the number of simultaneous colors claimed to be supported, or 1 if
// there is no color support. Note that several terminal emulators advertise
// more colors than they actually support, downsampling internally.
API unsigned notcurses_palette_size(const struct notcurses* nc)
  __attribute__ ((nonnull (1))) __attribute__ ((pure));

// Returns the name (and sometimes version) of the terminal, as Notcurses
// has been best able to determine.
ALLOC API char* notcurses_detected_terminal(const struct notcurses* nc)
  __attribute__ ((nonnull (1)));

API const nccapabilities* notcurses_capabilities(const struct notcurses* n)
  __attribute__ ((nonnull (1)));

// pixel blitting implementations. informative only; don't special-case
// based off any of this information!
typedef enum {
  NCPIXEL_NONE = 0,
  NCPIXEL_SIXEL,           // sixel
  NCPIXEL_LINUXFB,         // linux framebuffer
  NCPIXEL_ITERM2,          // iTerm2
  // C=1 (disabling scrolling) was only introduced in 0.20.0, at the same
  // time as animation. prior to this, graphics had to be entirely redrawn
  // on any change, and it wasn't possible to use the bottom line.
  NCPIXEL_KITTY_STATIC,
  // until 0.22.0's introduction of 'a=c' for self-referential composition, we
  // had to keep a complete copy of the RGBA data, in case a wiped cell needed
  // to be rebuilt. we'd otherwise have to unpack the glyph and store it into
  // the auxvec on the fly.
  NCPIXEL_KITTY_ANIMATED,
  // with 0.22.0, we only ever write transparent cells after writing the
  // original image (which we now deflate, since we needn't unpack it later).
  // the only data we need keep is the auxvecs.
  NCPIXEL_KITTY_SELFREF,
} ncpixelimpl_e;

// Can we blit pixel-accurate bitmaps?
API ncpixelimpl_e notcurses_check_pixel_support(const struct notcurses* nc)
  __attribute__ ((nonnull (1))) __attribute__ ((pure));

// Can we set the "hardware" palette? Requires the "ccc" terminfo capability,
// and that the number of colors supported is at least the size of our
// ncpalette structure.
__attribute__ ((nonnull (1))) __attribute__ ((pure)) static inline bool
nccapability_canchangecolor(const nccapabilities* caps){
  if(!caps->can_change_colors){
    return false;
  }
  ncpalette* p;
  if(caps->colors < sizeof(p->chans) / sizeof(*p->chans)){
    return false;
  }
  return true;
}

// Can we emit 24-bit, three-channel RGB foregrounds and backgrounds?
__attribute__ ((nonnull (1))) __attribute__ ((pure)) static inline bool
notcurses_cantruecolor(const struct notcurses* nc){
  return notcurses_capabilities(nc)->rgb;
}

// Can we directly specify RGB values per cell, or only use palettes?
__attribute__ ((nonnull (1))) __attribute__ ((pure)) static inline bool
notcurses_canchangecolor(const struct notcurses* nc){
  return nccapability_canchangecolor(notcurses_capabilities(nc));
}

// Can we fade? Fading requires either the "rgb" or "ccc" terminfo capability.
__attribute__ ((nonnull (1))) __attribute__ ((pure)) static inline bool
notcurses_canfade(const struct notcurses* n){
  return notcurses_canchangecolor(n) || notcurses_cantruecolor(n);
}

// Can we load images? This requires being built against FFmpeg/OIIO.
API bool notcurses_canopen_images(const struct notcurses* nc)
  __attribute__ ((pure));

// Can we load videos? This requires being built against FFmpeg.
API bool notcurses_canopen_videos(const struct notcurses* nc)
  __attribute__ ((pure));

// Is our encoding UTF-8? Requires LANG being set to a UTF8 locale.
__attribute__ ((nonnull (1))) __attribute__ ((pure)) static inline bool
notcurses_canutf8(const struct notcurses* nc){
  return notcurses_capabilities(nc)->utf8;
}

// Can we reliably use Unicode halfblocks? Any Unicode implementation can.
__attribute__ ((nonnull (1))) __attribute__ ((pure)) static inline bool
notcurses_canhalfblock(const struct notcurses* nc){
  return notcurses_canutf8(nc);
}

// Can we reliably use Unicode quadrants?
__attribute__ ((nonnull (1))) __attribute__ ((pure)) static inline bool
notcurses_canquadrant(const struct notcurses* nc){
  return notcurses_canutf8(nc) && notcurses_capabilities(nc)->quadrants;
}

// Can we reliably use Unicode 13 sextants?
__attribute__ ((nonnull (1))) __attribute__ ((pure)) static inline bool
notcurses_cansextant(const struct notcurses* nc){
  return notcurses_canutf8(nc) && notcurses_capabilities(nc)->sextants;
}

// Can we reliably use Unicode Braille?
__attribute__ ((nonnull (1))) __attribute__ ((pure)) static inline bool
notcurses_canbraille(const struct notcurses* nc){
  return notcurses_canutf8(nc) && notcurses_capabilities(nc)->braille;
}

// Can we blit pixel-accurate bitmaps?
__attribute__ ((nonnull (1))) __attribute__ ((pure)) static inline bool
notcurses_canpixel(const struct notcurses* nc){
  return notcurses_check_pixel_support(nc) != NCPIXEL_NONE;
}

// whenever a new field is added here, ensure we add the proper rule to
// notcurses_stats_reset(), so that values are preserved in the stash stats.
typedef struct ncstats {
  // purely increasing stats
  uint64_t renders;          // successful ncpile_render() runs
  uint64_t writeouts;        // successful ncpile_rasterize() runs
  uint64_t failed_renders;   // aborted renders, should be 0
  uint64_t failed_writeouts; // aborted writes
  uint64_t raster_bytes;     // bytes emitted to ttyfp
  int64_t raster_max_bytes;  // max bytes emitted for a frame
  int64_t raster_min_bytes;  // min bytes emitted for a frame
  uint64_t render_ns;        // nanoseconds spent rendering
  int64_t render_max_ns;     // max ns spent in render for a frame
  int64_t render_min_ns;     // min ns spent in render for a frame
  uint64_t raster_ns;        // nanoseconds spent rasterizing
  int64_t raster_max_ns;     // max ns spent in raster for a frame
  int64_t raster_min_ns;     // min ns spent in raster for a frame
  uint64_t writeout_ns;      // nanoseconds spent writing frames to terminal
  int64_t writeout_max_ns;   // max ns spent writing out a frame
  int64_t writeout_min_ns;   // min ns spent writing out a frame
  uint64_t cellelisions;     // cells we elided entirely thanks to damage maps
  uint64_t cellemissions;    // total number of cells emitted to terminal
  uint64_t fgelisions;       // RGB fg elision count
  uint64_t fgemissions;      // RGB fg emissions
  uint64_t bgelisions;       // RGB bg elision count
  uint64_t bgemissions;      // RGB bg emissions
  uint64_t defaultelisions;  // default color was emitted
  uint64_t defaultemissions; // default color was elided
  uint64_t refreshes;        // refresh requests (non-optimized redraw)
  uint64_t sprixelemissions; // sprixel draw count
  uint64_t sprixelelisions;  // sprixel elision count
  uint64_t sprixelbytes;     // sprixel bytes emitted
  uint64_t appsync_updates;  // how many application-synchronized updates?
  uint64_t input_errors;     // errors processing control sequences/utf8
  uint64_t input_events;     // characters returned to userspace
  uint64_t hpa_gratuitous;   // unnecessary hpas issued
  uint64_t cell_geo_changes; // cell geometry changes (resizes)
  uint64_t pixel_geo_changes;// pixel geometry changes (font resize)

  // current state -- these can decrease
  uint64_t fbbytes;          // total bytes devoted to all active framebuffers
  unsigned planes;           // number of planes currently in existence
} ncstats;

// Allocate an ncstats object. Use this rather than allocating your own, since
// future versions of Notcurses might enlarge this structure.
API ALLOC ncstats* notcurses_stats_alloc(const struct notcurses* nc
                                         __attribute__ ((unused)))
  __attribute__ ((nonnull (1)));

// Acquire an atomic snapshot of the Notcurses object's stats.
API void notcurses_stats(struct notcurses* nc, ncstats* stats)
  __attribute__ ((nonnull (1, 2)));

// Reset all cumulative stats (immediate ones, such as fbbytes, are not reset),
// first copying them into |*stats| (if |stats| is not NULL).
API void notcurses_stats_reset(struct notcurses* nc, ncstats* stats)
  __attribute__ ((nonnull (1)));

// Resize the specified ncplane. The four parameters 'keepy', 'keepx',
// 'keepleny', and 'keeplenx' define a subset of the ncplane to keep,
// unchanged. This may be a region of size 0, though none of these four
// parameters may be negative. 'keepx' and 'keepy' are relative to the ncplane.
// They must specify a coordinate within the ncplane's totality. 'yoff' and
// 'xoff' are relative to 'keepy' and 'keepx', and place the upper-left corner
// of the resized ncplane. Finally, 'ylen' and 'xlen' are the dimensions of the
// ncplane after resizing. 'ylen' must be greater than or equal to 'keepleny',
// and 'xlen' must be greater than or equal to 'keeplenx'. It is an error to
// attempt to resize the standard plane. If either of 'keepleny' or 'keeplenx'
// is non-zero, both must be non-zero.
//
// Essentially, the kept material does not move. It serves to anchor the
// resized plane. If there is no kept material, the plane can move freely.
API int ncplane_resize(struct ncplane* n, int keepy, int keepx,
                       unsigned keepleny, unsigned keeplenx,
                       int yoff, int xoff,
                       unsigned ylen, unsigned xlen);

// Resize the plane, retaining what data we can (everything, unless we're
// shrinking in some dimension). Keep the origin where it is.
static inline int
ncplane_resize_simple(struct ncplane* n, unsigned ylen, unsigned xlen){
  unsigned oldy, oldx;
  ncplane_dim_yx(n, &oldy, &oldx); // current dimensions of 'n'
  unsigned keepleny = oldy > ylen ? ylen : oldy;
  unsigned keeplenx = oldx > xlen ? xlen : oldx;
  return ncplane_resize(n, 0, 0, keepleny, keeplenx, 0, 0, ylen, xlen);
}

// Destroy the specified ncplane. None of its contents will be visible after
// the next call to notcurses_render(). It is an error to attempt to destroy
// the standard plane.
API int ncplane_destroy(struct ncplane* n);

// Set the ncplane's base nccell to 'c'. The base cell is used for purposes of
// rendering anywhere that the ncplane's gcluster is 0. Note that the base cell
// is not affected by ncplane_erase(). 'c' must not be a secondary cell from a
// multicolumn EGC.
API int ncplane_set_base_cell(struct ncplane* n, const nccell* c);

// Set the ncplane's base nccell. It will be used for purposes of rendering
// anywhere that the ncplane's gcluster is 0. Note that the base cell is not
// affected by ncplane_erase(). 'egc' must be an extended grapheme cluster.
// Returns the number of bytes copied out of 'gcluster', or -1 on failure.
API int ncplane_set_base(struct ncplane* n, const char* egc,
                         uint16_t stylemask, uint64_t channels);

// Extract the ncplane's base nccell into 'c'. The reference is invalidated if
// 'ncp' is destroyed.
API int ncplane_base(struct ncplane* n, nccell* c);

// Get the origin of plane 'n' relative to its bound plane, or pile (if 'n' is
// a root plane). To get absolute coordinates, use ncplane_abs_yx().
API void ncplane_yx(const struct ncplane* n, int* RESTRICT y, int* RESTRICT x)
  __attribute__ ((nonnull (1)));
API int ncplane_y(const struct ncplane* n) __attribute__ ((pure));
API int ncplane_x(const struct ncplane* n) __attribute__ ((pure));

// Move this plane relative to the standard plane, or the plane to which it is
// bound (if it is bound to a plane). It is an error to attempt to move the
// standard plane.
API int ncplane_move_yx(struct ncplane* n, int y, int x);

// Move this plane relative to its current location. Negative values move up
// and left, respectively. Pass 0 to hold an axis constant.
__attribute__ ((nonnull (1))) static inline int
ncplane_move_rel(struct ncplane* n, int y, int x){
  int oy, ox;
  ncplane_yx(n, &oy, &ox);
  return ncplane_move_yx(n, oy + y, ox + x);
}

// Get the origin of plane 'n' relative to its pile. Either or both of 'x' and
// 'y' may be NULL.
API void ncplane_abs_yx(const struct ncplane* n, int* RESTRICT y, int* RESTRICT x)
  __attribute__ ((nonnull (1)));
API int ncplane_abs_y(const struct ncplane* n) __attribute__ ((pure));
API int ncplane_abs_x(const struct ncplane* n) __attribute__ ((pure));

// Get the plane to which the plane 'n' is bound, if any.
API struct ncplane* ncplane_parent(struct ncplane* n)
  __attribute__ ((nonnull (1)));
API const struct ncplane* ncplane_parent_const(const struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Return non-zero iff 'n' is a proper descendent of 'ancestor'.
static inline int
ncplane_descendant_p(const struct ncplane* n, const struct ncplane* ancestor){
  for(const struct ncplane* parent = ncplane_parent_const(n) ; parent != ancestor ; parent = ncplane_parent_const(parent)){
    if(ncplane_parent_const(parent) == parent){ // reached a root plane
      return 0;
    }
  }
  return 1;
}

// Splice ncplane 'n' out of the z-buffer, and reinsert it above 'above'.
// Returns non-zero if 'n' is already in the desired location. 'n' and
// 'above' must not be the same plane. If 'above' is NULL, 'n' is moved
// to the bottom of its pile.
API int ncplane_move_above(struct ncplane* RESTRICT n,
                           struct ncplane* RESTRICT above)
  __attribute__ ((nonnull (1)));

// Splice ncplane 'n' out of the z-buffer, and reinsert it below 'below'.
// Returns non-zero if 'n' is already in the desired location. 'n' and
// 'below' must not be the same plane. If 'below' is NULL, 'n' is moved to
// the top of its pile.
API int ncplane_move_below(struct ncplane* RESTRICT n,
                           struct ncplane* RESTRICT below)
  __attribute__ ((nonnull (1)));

// Splice ncplane 'n' out of the z-buffer; reinsert it at the top or bottom.
__attribute__ ((nonnull (1)))
static inline void
ncplane_move_top(struct ncplane* n){
  ncplane_move_below(n, NULL);
}

__attribute__ ((nonnull (1)))
static inline void
ncplane_move_bottom(struct ncplane* n){
  ncplane_move_above(n, NULL);
}

// Splice ncplane 'n' and its bound planes out of the z-buffer, and reinsert
// them above or below 'targ'. Relative order will be maintained between the
// reinserted planes. For a plane E bound to C, with z-ordering A B C D E,
// moving the C family to the top results in C E A B D, while moving it to
// the bottom results in A B D C E.
API int ncplane_move_family_above(struct ncplane* n, struct ncplane* targ)
  __attribute__ ((nonnull (1)));

API int ncplane_move_family_below(struct ncplane* n, struct ncplane* targ)
  __attribute__ ((nonnull (1)));

__attribute__ ((nonnull (1)))
static inline void
ncplane_move_family_top(struct ncplane* n){
  ncplane_move_family_below(n, NULL);
}

__attribute__ ((nonnull (1)))
static inline void
ncplane_move_family_bottom(struct ncplane* n){
  ncplane_move_family_above(n, NULL);
}

// Return the plane below this one, or NULL if this is at the bottom.
API struct ncplane* ncplane_below(struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Return the plane above this one, or NULL if this is at the top.
API struct ncplane* ncplane_above(struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Effect |r| scroll events on the plane |n|. Returns an error if |n| is not
// a scrolling plane, and otherwise returns the number of lines scrolled.
API int ncplane_scrollup(struct ncplane* n, int r)
  __attribute__ ((nonnull (1)));

// Scroll |n| up until |child| is no longer hidden beneath it. Returns an
// error if |child| is not a child of |n|, or |n| is not scrolling, or |child|
// is fixed. Returns the number of scrolling events otherwise (might be 0).
// If the child plane is not fixed, it will likely scroll as well.
API int ncplane_scrollup_child(struct ncplane* n, const struct ncplane* child)
  __attribute__ ((nonnull (1, 2)));

// Rotate the plane π/2 radians clockwise or counterclockwise. This cannot
// be performed on arbitrary planes, because glyphs cannot be arbitrarily
// rotated. The glyphs which can be rotated are limited: line-drawing
// characters, spaces, half blocks, and full blocks. The plane must have
// an even number of columns. Use the ncvisual rotation for a more
// flexible approach.
API int ncplane_rotate_cw(struct ncplane* n)
  __attribute__ ((nonnull (1)));
API int ncplane_rotate_ccw(struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Retrieve the current contents of the cell under the cursor. The EGC is
// returned, or NULL on error. This EGC must be free()d by the caller. The
// stylemask and channels are written to 'stylemask' and 'channels', respectively.
API char* ncplane_at_cursor(const struct ncplane* n, uint16_t* stylemask, uint64_t* channels)
  __attribute__ ((nonnull (1)));

// Retrieve the current contents of the cell under the cursor into 'c'. This
// cell is invalidated if the associated plane is destroyed. Returns the number
// of bytes in the EGC, or -1 on error.
API int ncplane_at_cursor_cell(struct ncplane* n, nccell* c)
  __attribute__ ((nonnull (1, 2)));

// Retrieve the current contents of the specified cell. The EGC is returned, or
// NULL on error. This EGC must be free()d by the caller. The stylemask and
// channels are written to 'stylemask' and 'channels', respectively. The return
// represents how the cell will be used during rendering, and thus integrates
// any base cell where appropriate. If called upon the secondary columns of a
// wide glyph, the EGC will be returned (i.e. this function does not distinguish
// between the primary and secondary columns of a wide glyph). If called on a
// sprixel plane, its control sequence is returned for all valid locations.
API char* ncplane_at_yx(const struct ncplane* n, int y, int x,
                        uint16_t* stylemask, uint64_t* channels)
  __attribute__ ((nonnull (1)));

// Retrieve the current contents of the specified cell into 'c'. This cell is
// invalidated if the associated plane is destroyed. Returns the number of
// bytes in the EGC, or -1 on error. Unlike ncplane_at_yx(), when called upon
// the secondary columns of a wide glyph, the return can be distinguished from
// the primary column (nccell_wide_right_p(c) will return true). It is an
// error to call this on a sprixel plane (unlike ncplane_at_yx()).
API int ncplane_at_yx_cell(struct ncplane* n, int y, int x, nccell* c)
  __attribute__ ((nonnull (1, 4)));

// Create a flat string from the EGCs of the selected region of the ncplane
// 'n'. Start at the plane's 'begy'x'begx' coordinate (which must lie on the
// plane), continuing for 'leny'x'lenx' cells. Either or both of 'leny' and
// 'lenx' can be specified as 0 to go through the boundary of the plane.
// -1 can be specified for 'begx'/'begy' to use the current cursor location.
API char* ncplane_contents(struct ncplane* n, int begy, int begx,
                           unsigned leny, unsigned lenx)
  __attribute__ ((nonnull (1)));

// Manipulate the opaque user pointer associated with this plane.
// ncplane_set_userptr() returns the previous userptr after replacing
// it with 'opaque'. the others simply return the userptr.
API void* ncplane_set_userptr(struct ncplane* n, void* opaque)
  __attribute__ ((nonnull (1)));
API void* ncplane_userptr(struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Find the center coordinate of a plane, preferring the top/left in the
// case of an even number of rows/columns (in such a case, there will be one
// more cell to the bottom/right of the center than the top/left). The
// center is then modified relative to the plane's origin.
API void ncplane_center_abs(const struct ncplane* n, int* RESTRICT y,
                            int* RESTRICT x)
  __attribute__ ((nonnull (1)));

// Create an RGBA flat array from the selected region of the ncplane 'nc'.
// Start at the plane's 'begy'x'begx' coordinate (which must lie on the
// plane), continuing for 'leny'x'lenx' cells. Either or both of 'leny' and
// 'lenx' can be specified as 0 to go through the boundary of the plane.
// Only glyphs from the specified ncblitset may be present. If 'pxdimy' and/or
// 'pxdimx' are non-NULL, they will be filled in with the total pixel geometry.
API ALLOC uint32_t* ncplane_as_rgba(const struct ncplane* n, ncblitter_e blit,
                                    int begy, int begx,
                                    unsigned leny, unsigned lenx,
                                    unsigned* pxdimy, unsigned* pxdimx)
  __attribute__ ((nonnull (1)));

// Return the offset into 'availu' at which 'u' ought be output given the
// requirements of 'align'. Return -INT_MAX on invalid 'align'. Undefined
// behavior on negative 'availu' or 'u'.
static inline int
notcurses_align(int availu, ncalign_e align, int u){
  if(align == NCALIGN_LEFT || align == NCALIGN_TOP){
    return 0;
  }
  if(align == NCALIGN_CENTER){
    return (availu - u) / 2;
  }
  if(align == NCALIGN_RIGHT || align == NCALIGN_BOTTOM){
    return availu - u;
  }
  return -INT_MAX; // invalid |align|
}

// Return the column at which 'c' cols ought start in order to be aligned
// according to 'align' within ncplane 'n'. Return -INT_MAX on invalid
// 'align'. Undefined behavior on negative 'c'.
static inline int
ncplane_halign(const struct ncplane* n, ncalign_e align, int c){
  return notcurses_align((int)ncplane_dim_x(n), align, c);
}

// Return the row at which 'r' rows ought start in order to be aligned
// according to 'align' within ncplane 'n'. Return -INT_MAX on invalid
// 'align'. Undefined behavior on negative 'r'.
static inline int
ncplane_valign(const struct ncplane* n, ncalign_e align, int r){
  return notcurses_align((int)ncplane_dim_y(n), align, r);
}

// Move the cursor to the specified position (the cursor needn't be visible).
// Pass -1 as either coordinate to hold that axis constant. Returns -1 if the
// move would place the cursor outside the plane.
API int ncplane_cursor_move_yx(struct ncplane* n, int y, int x)
  __attribute__ ((nonnull (1)));

// Move the cursor relative to the current cursor position (the cursor needn't
// be visible). Returns -1 on error, including target position exceeding the
// plane's dimensions.
API int ncplane_cursor_move_rel(struct ncplane* n, int y, int x)
  __attribute__ ((nonnull (1)));

// Move the cursor to 0, 0. Can't fail.
API void ncplane_home(struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Get the current position of the cursor within n. y and/or x may be NULL.
API void ncplane_cursor_yx(const struct ncplane* n, unsigned* RESTRICT y, unsigned* RESTRICT x)
  __attribute__ ((nonnull (1)));

static inline unsigned
ncplane_cursor_y(const struct ncplane* n){
  unsigned y;
  ncplane_cursor_yx(n, &y, NULL);
  return y;
}

static inline unsigned
ncplane_cursor_x(const struct ncplane* n){
  unsigned x;
  ncplane_cursor_yx(n, NULL, &x);
  return x;
}

// Get the current colors and alpha values for ncplane 'n'.
API uint64_t ncplane_channels(const struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Get the current styling for the ncplane 'n'.
API uint16_t ncplane_styles(const struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Replace the cell at the specified coordinates with the provided cell 'c',
// and advance the cursor by the width of the cell (but not past the end of the
// plane). On success, returns the number of columns the cursor was advanced.
// 'c' must already be associated with 'n'. On failure, -1 is returned.
API int ncplane_putc_yx(struct ncplane* n, int y, int x, const nccell* c)
  __attribute__ ((nonnull (1, 4)));

// Call ncplane_putc_yx() for the current cursor location.
static inline int
ncplane_putc(struct ncplane* n, const nccell* c){
  return ncplane_putc_yx(n, -1, -1, c);
}

// Replace the cell at the specified coordinates with the provided 7-bit char
// 'c'. Advance the cursor by 1. On success, returns the number of columns the
// cursor was advanced. On failure, returns -1. This works whether the
// underlying char is signed or unsigned.
static inline int
ncplane_putchar_yx(struct ncplane* n, int y, int x, char c){
  nccell ce = NCCELL_INITIALIZER((uint32_t)c, ncplane_styles(n), ncplane_channels(n));
  return ncplane_putc_yx(n, y, x, &ce);
}

// Call ncplane_putchar_yx() at the current cursor location.
static inline int
ncplane_putchar(struct ncplane* n, char c){
  return ncplane_putchar_yx(n, -1, -1, c);
}

// Replace the EGC underneath us, but retain the styling. The current styling
// of the plane will not be changed.
API int ncplane_putchar_stained(struct ncplane* n, char c)
  __attribute__ ((nonnull (1)));

// Replace the cell at the specified coordinates with the provided EGC, and
// advance the cursor by the width of the cluster (but not past the end of the
// plane). On success, returns the number of columns the cursor was advanced.
// On failure, -1 is returned. The number of bytes converted from gclust is
// written to 'sbytes' if non-NULL.
API int ncplane_putegc_yx(struct ncplane* n, int y, int x, const char* gclust,
                          size_t* sbytes)
  __attribute__ ((nonnull (1, 4)));

// Call ncplane_putegc_yx() at the current cursor location.
static inline int
ncplane_putegc(struct ncplane* n, const char* gclust, size_t* sbytes){
  return ncplane_putegc_yx(n, -1, -1, gclust, sbytes);
}

// Replace the EGC underneath us, but retain the styling. The current styling
// of the plane will not be changed.
API int ncplane_putegc_stained(struct ncplane* n, const char* gclust, size_t* sbytes)
  __attribute__ ((nonnull (1, 2)));

// generate a heap-allocated UTF-8 encoding of the wide string 'src'.
ALLOC static inline char*
ncwcsrtombs(const wchar_t* src){
  mbstate_t ps;
  memset(&ps, 0, sizeof(ps));
  size_t mbytes = wcsrtombs(NULL, &src, 0, &ps);
  if(mbytes == (size_t)-1){
    return NULL;
  }
  ++mbytes;
  char* mbstr = (char*)malloc(mbytes); // need cast for c++ callers
  if(mbstr == NULL){
    return NULL;
  }
  size_t s = wcsrtombs(mbstr, &src, mbytes, &ps);
  if(s == (size_t)-1){
    free(mbstr);
    return NULL;
  }
  return mbstr;
}

// ncplane_putegc(), but following a conversion from wchar_t to UTF-8 multibyte.
static inline int
ncplane_putwegc(struct ncplane* n, const wchar_t* gclust, size_t* sbytes){
  char* mbstr = ncwcsrtombs(gclust);
  if(mbstr == NULL){
    return -1;
  }
  int ret = ncplane_putegc(n, mbstr, sbytes);
  free(mbstr);
  return ret;
}

// Call ncplane_putwegc() after successfully moving to y, x.
static inline int
ncplane_putwegc_yx(struct ncplane* n, int y, int x, const wchar_t* gclust,
                   size_t* sbytes){
  if(ncplane_cursor_move_yx(n, y, x)){
    return -1;
  }
  return ncplane_putwegc(n, gclust, sbytes);
}

// Replace the EGC underneath us, but retain the styling. The current styling
// of the plane will not be changed.
API int ncplane_putwegc_stained(struct ncplane* n, const wchar_t* gclust, size_t* sbytes)
  __attribute__ ((nonnull (1, 2)));

// Write a series of EGCs to the current location, using the current style.
// They will be interpreted as a series of columns (according to the definition
// of ncplane_putc()). Advances the cursor by some positive number of columns
// (though not beyond the end of the plane); this number is returned on success.
// On error, a non-positive number is returned, indicating the number of columns
// which were written before the error.
static inline int
ncplane_putstr_yx(struct ncplane* n, int y, int x, const char* gclusters){
  int ret = 0;
  while(*gclusters){
    size_t wcs;
    int cols = ncplane_putegc_yx(n, y, x, gclusters, &wcs);
//fprintf(stderr, "wrote %.*s %d cols %zu bytes\n", (int)wcs, gclusters, cols, wcs);
    if(cols < 0){
      return -ret;
    }
    if(wcs == 0){
      break;
    }
    // after the first iteration, just let the cursor code control where we
    // print, so that scrolling is taken into account
    y = -1;
    x = -1;
    gclusters += wcs;
    ret += cols;
  }
  return ret;
}

static inline int
ncplane_putstr(struct ncplane* n, const char* gclustarr){
  return ncplane_putstr_yx(n, -1, -1, gclustarr);
}

static inline int
ncplane_putstr_aligned(struct ncplane* n, int y, ncalign_e align, const char* s){
  int validbytes, validwidth;
  // we'll want to do the partial write if there's an error somewhere within
  ncstrwidth(s, &validbytes, &validwidth);
  int xpos = ncplane_halign(n, align, validwidth);
  if(xpos < 0){
    xpos = 0;
  }
  return ncplane_putstr_yx(n, y, xpos, s);
}

// Replace a string's worth of glyphs at the current cursor location, but
// retain the styling. The current styling of the plane will not be changed.
static inline int
ncplane_putstr_stained(struct ncplane* n, const char* gclusters){
  int ret = 0;
  while(*gclusters){
    size_t wcs;
    int cols = ncplane_putegc_stained(n, gclusters, &wcs);
    if(cols < 0){
      return -ret;
    }
    if(wcs == 0){
      break;
    }
    gclusters += wcs;
    ret += cols;
  }
  return ret;
}

API int ncplane_putnstr_aligned(struct ncplane* n, int y, ncalign_e align, size_t s, const char* str)
  __attribute__ ((nonnull (1, 5)));

// Write a series of EGCs to the current location, using the current style.
// They will be interpreted as a series of columns (according to the definition
// of ncplane_putc()). Advances the cursor by some positive number of columns
// (though not beyond the end of the plane); this number is returned on success.
// On error, a non-positive number is returned, indicating the number of columns
// which were written before the error. No more than 's' bytes will be written.
static inline int
ncplane_putnstr_yx(struct ncplane* n, int y, int x, size_t s, const char* gclusters){
  int ret = 0;
  size_t offset = 0;
//fprintf(stderr, "PUT %zu at %d/%d [%.*s]\n", s, y, x, (int)s, gclusters);
  while(offset < s && gclusters[offset]){
    size_t wcs;
    int cols = ncplane_putegc_yx(n, y, x, gclusters + offset, &wcs);
    if(cols < 0){
      return -ret;
    }
    if(wcs == 0){
      break;
    }
    // after the first iteration, just let the cursor code control where we
    // print, so that scrolling is taken into account
    y = -1;
    x = -1;
    offset += wcs;
    ret += cols;
  }
  return ret;
}

static inline int
ncplane_putnstr(struct ncplane* n, size_t s, const char* gclustarr){
  return ncplane_putnstr_yx(n, -1, -1, s, gclustarr);
}

// ncplane_putstr(), but following a conversion from wchar_t to UTF-8 multibyte.
// FIXME do this as a loop over ncplane_putegc_yx and save the big allocation+copy
static inline int
ncplane_putwstr_yx(struct ncplane* n, int y, int x, const wchar_t* gclustarr){
  // maximum of six UTF8-encoded bytes per wchar_t
  const size_t mbytes = (wcslen(gclustarr) * WCHAR_MAX_UTF8BYTES) + 1;
  char* mbstr = (char*)malloc(mbytes); // need cast for c++ callers
  if(mbstr == NULL){
    return -1;
  }
  mbstate_t ps;
  memset(&ps, 0, sizeof(ps));
  const wchar_t** gend = &gclustarr;
  size_t s = wcsrtombs(mbstr, gend, mbytes, &ps);
  if(s == (size_t)-1){
    free(mbstr);
    return -1;
  }
  int ret = ncplane_putstr_yx(n, y, x, mbstr);
  free(mbstr);
  return ret;
}

static inline int
ncplane_putwstr_aligned(struct ncplane* n, int y, ncalign_e align,
                        const wchar_t* gclustarr){
  int width = wcswidth(gclustarr, INT_MAX);
  int xpos = ncplane_halign(n, align, width);
  if(xpos < 0){
    xpos = 0;
  }
  return ncplane_putwstr_yx(n, y, xpos, gclustarr);
}

API int ncplane_putwstr_stained(struct ncplane* n, const wchar_t* gclustarr)
  __attribute__ ((nonnull (1, 2)));

static inline int
ncplane_putwstr(struct ncplane* n, const wchar_t* gclustarr){
  return ncplane_putwstr_yx(n, -1, -1, gclustarr);
}

// Replace the cell at the specified coordinates with the provided UTF-32
// 'u'. Advance the cursor by the character's width as reported by wcwidth().
// On success, returns the number of columns written. On failure, returns -1.
static inline int
ncplane_pututf32_yx(struct ncplane* n, int y, int x, uint32_t u){
  if(u > WCHAR_MAX){
    return -1;
  }
  // we use MB_LEN_MAX (and potentially "waste" a few stack bytes to avoid
  // the greater sin of a VLA (and to be locale-independent).
  char utf8c[MB_LEN_MAX + 1];
  mbstate_t ps;
  memset(&ps, 0, sizeof(ps));
  // this isn't going to be valid for reconstructued surrogate pairs...
  // we need our own, or to use unistring or something.
  size_t s = wcrtomb(utf8c, (wchar_t)u, &ps);
  if(s == (size_t)-1){
    return -1;
  }
  utf8c[s] = '\0';
  return ncplane_putegc_yx(n, y, x, utf8c, NULL);
}

static inline int
ncplane_putwc_yx(struct ncplane* n, int y, int x, wchar_t w){
  return ncplane_pututf32_yx(n, y, x, (uint32_t)w);
}

// Write 'w' at the current cursor position, using the plane's current styling.
static inline int
ncplane_putwc(struct ncplane* n, wchar_t w){
  return ncplane_putwc_yx(n, -1, -1, w);
}

// Write the first Unicode character from 'w' at the current cursor position,
// using the plane's current styling. In environments where wchar_t is only
// 16 bits (Windows, essentially), a single Unicode might require two wchar_t
// values forming a surrogate pair. On environments with 32-bit wchar_t, this
// should not happen. If w[0] is a surrogate, it is decoded together with
// w[1], and passed as a single reconstructed UTF-32 character to
// ncplane_pututf32(); 'wchars' will get a value of 2 in this case. 'wchars'
// otherwise gets a value of 1. A surrogate followed by an invalid pairing
// will set 'wchars' to 2, but return -1 immediately.
static inline int
ncplane_putwc_utf32(struct ncplane* n, const wchar_t* w, unsigned* wchars){
  uint32_t utf32;
  if(*w >= 0xd000 && *w <= 0xdbff){
    *wchars = 2;
    if(w[1] < 0xdc00 || w[1] > 0xdfff){
      return -1; // invalid surrogate pairing
    }
    utf32 = (w[0] & 0x3fflu) << 10lu;
    utf32 += (w[1] & 0x3fflu);
  }else{
    *wchars = 1;
    utf32 = (uint32_t)*w;
  }
  return ncplane_pututf32_yx(n, -1, -1, utf32);
}

// Write 'w' at the current cursor position, using any preexisting styling
// at that cell.
static inline int
ncplane_putwc_stained(struct ncplane* n, wchar_t w){
  wchar_t warr[2] = { w, L'\0' };
  return ncplane_putwstr_stained(n, warr);
}

// The ncplane equivalents of printf(3) and vprintf(3).
API int ncplane_vprintf_aligned(struct ncplane* n, int y, ncalign_e align,
                                const char* format, va_list ap)
  __attribute__ ((nonnull (1, 4)))
  __attribute__ ((format (printf, 4, 0)));

API int ncplane_vprintf_yx(struct ncplane* n, int y, int x,
                           const char* format, va_list ap)
  __attribute__ ((nonnull (1, 4)))
  __attribute__ ((format (printf, 4, 0)));

static inline int
ncplane_vprintf(struct ncplane* n, const char* format, va_list ap){
  return ncplane_vprintf_yx(n, -1, -1, format, ap);
}

API int ncplane_vprintf_stained(struct ncplane* n, const char* format, va_list ap)
  __attribute__ ((nonnull (1, 2)))
  __attribute__ ((format (printf, 2, 0)));

static inline int
ncplane_printf(struct ncplane* n, const char* format, ...)
  __attribute__ ((nonnull (1, 2)))
  __attribute__ ((format (printf, 2, 3)));

static inline int
ncplane_printf(struct ncplane* n, const char* format, ...){
  va_list va;
  va_start(va, format);
  int ret = ncplane_vprintf(n, format, va);
  va_end(va);
  return ret;
}

static inline int
ncplane_printf_yx(struct ncplane* n, int y, int x, const char* format, ...)
  __attribute__ ((nonnull (1, 4))) __attribute__ ((format (printf, 4, 5)));

static inline int
ncplane_printf_yx(struct ncplane* n, int y, int x, const char* format, ...){
  va_list va;
  va_start(va, format);
  int ret = ncplane_vprintf_yx(n, y, x, format, va);
  va_end(va);
  return ret;
}

static inline int
ncplane_printf_aligned(struct ncplane* n, int y, ncalign_e align,
                       const char* format, ...)
  __attribute__ ((nonnull (1, 4))) __attribute__ ((format (printf, 4, 5)));

static inline int
ncplane_printf_aligned(struct ncplane* n, int y, ncalign_e align, const char* format, ...){
  va_list va;
  va_start(va, format);
  int ret = ncplane_vprintf_aligned(n, y, align, format, va);
  va_end(va);
  return ret;
}

static inline int
ncplane_printf_stained(struct ncplane* n, const char* format, ...)
  __attribute__ ((nonnull (1, 2))) __attribute__ ((format (printf, 2, 3)));

static inline int
ncplane_printf_stained(struct ncplane* n, const char* format, ...){
  va_list va;
  va_start(va, format);
  int ret = ncplane_vprintf_stained(n, format, va);
  va_end(va);
  return ret;
}

// Write the specified text to the plane, breaking lines sensibly, beginning at
// the specified line. Returns the number of columns written. When breaking a
// line, the line will be cleared to the end of the plane (the last line will
// *not* be so cleared). The number of bytes written from the input is written
// to '*bytes' if it is not NULL. Cleared columns are included in the return
// value, but *not* included in the number of bytes written. Leaves the cursor
// at the end of output. A partial write will be accomplished as far as it can;
// determine whether the write completed by inspecting '*bytes'. Can output to
// multiple rows even in the absence of scrolling, but not more rows than are
// available. With scrolling enabled, arbitrary amounts of data can be emitted.
// All provided whitespace is preserved -- ncplane_puttext() followed by an
// appropriate ncplane_contents() will read back the original output.
//
// If 'y' is -1, the first row of output is taken relative to the current
// cursor: it will be left-, right-, or center-aligned in whatever remains
// of the row. On subsequent rows -- or if 'y' is not -1 -- the entire row can
// be used, and alignment works normally.
//
// A newline at any point will move the cursor to the next row.
API int ncplane_puttext(struct ncplane* n, int y, ncalign_e align,
                        const char* text, size_t* bytes)
  __attribute__ ((nonnull (1, 4)));

// Draw horizontal or vertical lines using the specified cell, starting at the
// current cursor position. The cursor will end at the cell following the last
// cell output (even, perhaps counter-intuitively, when drawing vertical
// lines), just as if ncplane_putc() was called at that spot. Return the
// number of cells drawn on success. On error, return the negative number of
// cells drawn. A length of 0 is an error, resulting in a return of -1.
API int ncplane_hline_interp(struct ncplane* n, const nccell* c,
                             unsigned len, uint64_t c1, uint64_t c2)
  __attribute__ ((nonnull (1, 2)));

__attribute__ ((nonnull (1, 2))) static inline int
ncplane_hline(struct ncplane* n, const nccell* c, unsigned len){
  return ncplane_hline_interp(n, c, len, c->channels, c->channels);
}

API int ncplane_vline_interp(struct ncplane* n, const nccell* c,
                             unsigned len, uint64_t c1, uint64_t c2)
  __attribute__ ((nonnull (1, 2)));

__attribute__ ((nonnull (1, 2))) static inline int
ncplane_vline(struct ncplane* n, const nccell* c, unsigned len){
  return ncplane_vline_interp(n, c, len, c->channels, c->channels);
}

#define NCBOXMASK_TOP    0x0001
#define NCBOXMASK_RIGHT  0x0002
#define NCBOXMASK_BOTTOM 0x0004
#define NCBOXMASK_LEFT   0x0008
#define NCBOXGRAD_TOP    0x0010
#define NCBOXGRAD_RIGHT  0x0020
#define NCBOXGRAD_BOTTOM 0x0040
#define NCBOXGRAD_LEFT   0x0080
#define NCBOXCORNER_MASK 0x0300
#define NCBOXCORNER_SHIFT 8u

// Draw a box with its upper-left corner at the current cursor position, and its
// lower-right corner at 'ystop'x'xstop'. The 6 cells provided are used to draw the
// upper-left, ur, ll, and lr corners, then the horizontal and vertical lines.
// 'ctlword' is defined in the least significant byte, where bits [7, 4] are a
// gradient mask, and [3, 0] are a border mask:
//  * 7, 3: top
//  * 6, 2: right
//  * 5, 1: bottom
//  * 4, 0: left
// If the gradient bit is not set, the styling from the hl/vl cells is used for
// the horizontal and vertical lines, respectively. If the gradient bit is set,
// the color is linearly interpolated between the two relevant corner cells.
//
// By default, vertexes are drawn whether their connecting edges are drawn or
// not. The value of the bits corresponding to NCBOXCORNER_MASK control this,
// and are interpreted as the number of connecting edges necessary to draw a
// given corner. At 0 (the default), corners are always drawn. At 3, corners
// are never drawn (since at most 2 edges can touch a box's corner).
API int ncplane_box(struct ncplane* n, const nccell* ul, const nccell* ur,
                    const nccell* ll, const nccell* lr, const nccell* hline,
                    const nccell* vline, unsigned ystop, unsigned xstop,
                    unsigned ctlword);

// Draw a box with its upper-left corner at the current cursor position, having
// dimensions 'ylen'x'xlen'. See ncplane_box() for more information. The
// minimum box size is 2x2, and it cannot be drawn off-plane.
static inline int
ncplane_box_sized(struct ncplane* n, const nccell* ul, const nccell* ur,
                  const nccell* ll, const nccell* lr, const nccell* hline,
                  const nccell* vline, unsigned ystop, unsigned xstop,
                  unsigned ctlword){
  unsigned y, x;
  ncplane_cursor_yx(n, &y, &x);
  return ncplane_box(n, ul, ur, ll, lr, hline, vline, y + ystop - 1,
                     x + xstop - 1, ctlword);
}

static inline int
ncplane_perimeter(struct ncplane* n, const nccell* ul, const nccell* ur,
                  const nccell* ll, const nccell* lr, const nccell* hline,
                  const nccell* vline, unsigned ctlword){
  if(ncplane_cursor_move_yx(n, 0, 0)){
    return -1;
  }
  unsigned dimy, dimx;
  ncplane_dim_yx(n, &dimy, &dimx);
  return ncplane_box_sized(n, ul, ur, ll, lr, hline, vline, dimy, dimx, ctlword);
}

// Starting at the specified coordinate, if its glyph is different from that of
// 'c', 'c' is copied into it, and the original glyph is considered the fill
// target. We do the same to all cardinally-connected cells having this same
// fill target. Returns the number of cells polyfilled. An invalid initial y, x
// is an error. Returns the number of cells filled, or -1 on error.
API int ncplane_polyfill_yx(struct ncplane* n, int y, int x, const nccell* c)
  __attribute__ ((nonnull (1, 4)));

// Draw a gradient with its upper-left corner at the position specified by 'y'/'x',
// where -1 means the current cursor position in that dimension. The area is
// specified by 'ylen'/'xlen', where 0 means "everything remaining below or
// to the right, respectively." The glyph composed of 'egc' and 'styles' is
// used for all cells. The channels specified by 'ul', 'ur', 'll', and 'lr'
// are composed into foreground and background gradients. To do a vertical
// gradient, 'ul' ought equal 'ur' and 'll' ought equal 'lr'. To do a
// horizontal gradient, 'ul' ought equal 'll' and 'ur' ought equal 'ul'. To
// color everything the same, all four channels should be equivalent. The
// resulting alpha values are equal to incoming alpha values. Returns the
// number of cells filled on success, or -1 on failure.
// Palette-indexed color is not supported.
//
// Preconditions for gradient operations (error otherwise):
//
//  all: only RGB colors, unless all four channels match as default
//  all: all alpha values must be the same
//  1x1: all four colors must be the same
//  1xN: both top and both bottom colors must be the same (vertical gradient)
//  Nx1: both left and both right colors must be the same (horizontal gradient)
API int ncplane_gradient(struct ncplane* n, int y, int x, unsigned ylen,
                         unsigned xlen, const char* egc, uint16_t styles,
                         uint64_t ul, uint64_t ur, uint64_t ll, uint64_t lr)
  __attribute__ ((nonnull (1, 6)));

// Do a high-resolution gradient using upper blocks and synced backgrounds.
// This doubles the number of vertical gradations, but restricts you to
// half blocks (appearing to be full blocks). Returns the number of cells
// filled on success, or -1 on error.
API int ncplane_gradient2x1(struct ncplane* n, int y, int x, unsigned ylen,
                            unsigned xlen, uint32_t ul, uint32_t ur,
                            uint32_t ll, uint32_t lr)
  __attribute__ ((nonnull (1)));

// Set the given style throughout the specified region, keeping content and
// channels unchanged. The upper left corner is at 'y', 'x', and -1 may be
// specified to indicate the cursor's position in that dimension. The area
// is specified by 'ylen', 'xlen', and 0 may be specified to indicate everything
// remaining to the right and below, respectively. It is an error for any
// coordinate to be outside the plane. Returns the number of cells set,
// or -1 on failure.
API int ncplane_format(struct ncplane* n, int y, int x, unsigned ylen,
                       unsigned xlen, uint16_t stylemask)
  __attribute__ ((nonnull (1)));

// Set the given channels throughout the specified region, keeping content and
// channels unchanged. The upper left corner is at 'y', 'x', and -1 may be
// specified to indicate the cursor's position in that dimension. The area
// is specified by 'ylen', 'xlen', and 0 may be specified to indicate everything
// remaining to the right and below, respectively. It is an error for any
// coordinate to be outside the plane. Returns the number of cells set,
// or -1 on failure.
API int ncplane_stain(struct ncplane* n, int y, int x, unsigned ylen,
                      unsigned xlen, uint64_t ul, uint64_t ur,
                      uint64_t ll, uint64_t lr)
  __attribute__ ((nonnull (1)));

// Merge the entirety of 'src' down onto the ncplane 'dst'. If 'src' does not
// intersect with 'dst', 'dst' will not be changed, but it is not an error.
API int ncplane_mergedown_simple(struct ncplane* RESTRICT src,
                                 struct ncplane* RESTRICT dst)
  __attribute__ ((nonnull (1, 2)));

// Merge the ncplane 'src' down onto the ncplane 'dst'. This is most rigorously
// defined as "write to 'dst' the frame that would be rendered were the entire
// stack made up only of the specified subregion of 'src' and, below it, the
// subregion of 'dst' having the specified origin. Supply -1 to indicate the
// current cursor position in the relevant dimension. Merging is independent of
// the position of 'src' viz 'dst' on the z-axis. It is an error to define a
// subregion that is not entirely contained within 'src'. It is an error to
// define a target origin such that the projected subregion is not entirely
// contained within 'dst'.  Behavior is undefined if 'src' and 'dst' are
// equivalent. 'dst' is modified, but 'src' remains unchanged. Neither 'src'
// nor 'dst' may have sprixels. Lengths of 0 mean "everything left".
API int ncplane_mergedown(struct ncplane* RESTRICT src,
                          struct ncplane* RESTRICT dst,
                          int begsrcy, int begsrcx,
                          unsigned leny, unsigned lenx,
                          int dsty, int dstx)
  __attribute__ ((nonnull (1, 2)));

// Erase every cell in the ncplane (each cell is initialized to the null glyph
// and the default channels/styles). All cells associated with this ncplane are
// invalidated, and must not be used after the call, *excluding* the base cell.
// The cursor is homed. The plane's active attributes are unaffected.
API void ncplane_erase(struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Erase every cell in the region starting at {ystart, xstart} and having size
// {|ylen|x|xlen|} for non-zero lengths. If ystart and/or xstart are -1, the current
// cursor position along that axis is used; other negative values are an error. A
// negative ylen means to move up from the origin, and a negative xlen means to move
// left from the origin. A positive ylen moves down, and a positive xlen moves right.
// A value of 0 for the length erases everything along that dimension. It is an error
// if the starting coordinate is not in the plane, but the ending coordinate may be
// outside the plane.
//
// For example, on a plane of 20 rows and 10 columns, with the cursor at row 10 and
// column 5, the following would hold:
//
//  (-1, -1, 0, 1): clears the column to the right of the cursor (column 6)
//  (-1, -1, 0, -1): clears the column to the left of the cursor (column 4)
//  (-1, -1, INT_MAX, 0): clears all rows with or below the cursor (rows 10--19)
//  (-1, -1, -INT_MAX, 0): clears all rows with or above the cursor (rows 0--10)
//  (-1, 4, 3, 3): clears from row 5, column 4 through row 7, column 6
//  (-1, 4, -3, -3): clears from row 5, column 4 through row 3, column 2
//  (4, -1, 0, 3): clears columns 5, 6, and 7
//  (-1, -1, 0, 0): clears the plane *if the cursor is in a legal position*
//  (0, 0, 0, 0): clears the plane in all cases
API int ncplane_erase_region(struct ncplane* n, int ystart, int xstart,
                             int ylen, int xlen)
  __attribute__ ((nonnull (1)));

// Extract 24 bits of foreground RGB from 'cl', shifted to LSBs.
static inline uint32_t
nccell_fg_rgb(const nccell* cl){
  return ncchannels_fg_rgb(cl->channels);
}

// Extract 24 bits of background RGB from 'cl', shifted to LSBs.
static inline uint32_t
nccell_bg_rgb(const nccell* cl){
  return ncchannels_bg_rgb(cl->channels);
}

// Extract 2 bits of foreground alpha from 'cl', shifted to LSBs.
static inline uint32_t
nccell_fg_alpha(const nccell* cl){
  return ncchannels_fg_alpha(cl->channels);
}

// Extract 2 bits of background alpha from 'cl', shifted to LSBs.
static inline uint32_t
nccell_bg_alpha(const nccell* cl){
  return ncchannels_bg_alpha(cl->channels);
}

// Extract 24 bits of foreground RGB from 'cl', split into components.
static inline uint32_t
nccell_fg_rgb8(const nccell* cl, unsigned* r, unsigned* g, unsigned* b){
  return ncchannels_fg_rgb8(cl->channels, r, g, b);
}

// Extract 24 bits of background RGB from 'cl', split into components.
static inline uint32_t
nccell_bg_rgb8(const nccell* cl, unsigned* r, unsigned* g, unsigned* b){
  return ncchannels_bg_rgb8(cl->channels, r, g, b);
}

// Set the r, g, and b cell for the foreground component of this 64-bit
// 'cl' variable, and mark it as not using the default color.
static inline int
nccell_set_fg_rgb8(nccell* cl, unsigned r, unsigned g, unsigned b){
  return ncchannels_set_fg_rgb8(&cl->channels, r, g, b);
}

// Same, but clipped to [0..255].
static inline void
nccell_set_fg_rgb8_clipped(nccell* cl, int r, int g, int b){
  ncchannels_set_fg_rgb8_clipped(&cl->channels, r, g, b);
}

// Same, but with an assembled 24-bit RGB value.
static inline int
nccell_set_fg_rgb(nccell* c, uint32_t channel){
  return ncchannels_set_fg_rgb(&c->channels, channel);
}

// Set the cell's foreground palette index, set the foreground palette index
// bit, set it foreground-opaque, and clear the foreground default color bit.
static inline int
nccell_set_fg_palindex(nccell* cl, unsigned idx){
  return ncchannels_set_fg_palindex(&cl->channels, idx);
}

static inline uint32_t
nccell_fg_palindex(const nccell* cl){
  return ncchannels_fg_palindex(cl->channels);
}

// Set the r, g, and b cell for the background component of this 64-bit
// 'cl' variable, and mark it as not using the default color.
static inline int
nccell_set_bg_rgb8(nccell* cl, unsigned r, unsigned g, unsigned b){
  return ncchannels_set_bg_rgb8(&cl->channels, r, g, b);
}

// Same, but clipped to [0..255].
static inline void
nccell_set_bg_rgb8_clipped(nccell* cl, int r, int g, int b){
  ncchannels_set_bg_rgb8_clipped(&cl->channels, r, g, b);
}

// Same, but with an assembled 24-bit RGB value. A value over 0xffffff
// will be rejected, with a non-zero return value.
static inline int
nccell_set_bg_rgb(nccell* c, uint32_t channel){
  return ncchannels_set_bg_rgb(&c->channels, channel);
}

// Set the cell's background palette index, set the background palette index
// bit, set it background-opaque, and clear the background default color bit.
static inline int
nccell_set_bg_palindex(nccell* cl, unsigned idx){
  return ncchannels_set_bg_palindex(&cl->channels, idx);
}

static inline uint32_t
nccell_bg_palindex(const nccell* cl){
  return ncchannels_bg_palindex(cl->channels);
}

// Is the foreground using the "default foreground color"?
static inline bool
nccell_fg_default_p(const nccell* cl){
  return ncchannels_fg_default_p(cl->channels);
}

static inline bool
nccell_fg_palindex_p(const nccell* cl){
  return ncchannels_fg_palindex_p(cl->channels);
}

// Is the background using the "default background color"? The "default
// background color" must generally be used to take advantage of
// terminal-effected transparency.
static inline bool
nccell_bg_default_p(const nccell* cl){
  return ncchannels_bg_default_p(cl->channels);
}

static inline bool
nccell_bg_palindex_p(const nccell* cl){
  return ncchannels_bg_palindex_p(cl->channels);
}

// Extract the background alpha and coloring bits from a 64-bit channel
// pair as a single 32-bit value.
static inline uint32_t
ncplane_bchannel(const struct ncplane* n){
  return ncchannels_bchannel(ncplane_channels(n));
}

// Extract the foreground alpha and coloring bits from a 64-bit channel
// pair as a single 32-bit value.
static inline uint32_t
ncplane_fchannel(const struct ncplane* n){
  return ncchannels_fchannel(ncplane_channels(n));
}

// Set the alpha and coloring bits of the plane's current channels from a
// 64-bit pair of channels.
API void ncplane_set_channels(struct ncplane* n, uint64_t channels)
  __attribute__ ((nonnull (1)));

// Set the background alpha and coloring bits of the plane's current
// channels from a single 32-bit value.
API uint64_t ncplane_set_bchannel(struct ncplane* n, uint32_t channel)
  __attribute__ ((nonnull (1)));

// Set the foreground alpha and coloring bits of the plane's current
// channels from a single 32-bit value.
API uint64_t ncplane_set_fchannel(struct ncplane* n, uint32_t channel)
  __attribute__ ((nonnull (1)));

// Set the specified style bits for the ncplane 'n', whether they're actively
// supported or not.
API void ncplane_set_styles(struct ncplane* n, unsigned stylebits)
  __attribute__ ((nonnull (1)));

API void ncplane_set_cell_yx(struct ncplane* n, int y, int x, unsigned stylebits, uint64_t channel)
  __attribute__ ((nonnull (1)));

// Add the specified styles to the ncplane's existing spec.
API void ncplane_on_styles(struct ncplane* n, unsigned stylebits)
  __attribute__ ((nonnull (1)));

// Remove the specified styles from the ncplane's existing spec.
API void ncplane_off_styles(struct ncplane* n, unsigned stylebits)
  __attribute__ ((nonnull (1)));

API void ncplane_on_styles_yx(struct ncplane* n, int y, int x, unsigned stylebits)
  __attribute__ ((nonnull (1)));

// Extract 24 bits of working foreground RGB from an ncplane, shifted to LSBs.
static inline uint32_t
ncplane_fg_rgb(const struct ncplane* n){
  return ncchannels_fg_rgb(ncplane_channels(n));
}

// Extract 24 bits of working background RGB from an ncplane, shifted to LSBs.
static inline uint32_t
ncplane_bg_rgb(const struct ncplane* n){
  return ncchannels_bg_rgb(ncplane_channels(n));
}

// Extract 2 bits of foreground alpha from 'struct ncplane', shifted to LSBs.
static inline uint32_t
ncplane_fg_alpha(const struct ncplane* n){
  return ncchannels_fg_alpha(ncplane_channels(n));
}

// Is the plane's foreground using the "default foreground color"?
static inline bool
ncplane_fg_default_p(const struct ncplane* n){
  return ncchannels_fg_default_p(ncplane_channels(n));
}

// Extract 2 bits of background alpha from 'struct ncplane', shifted to LSBs.
static inline uint32_t
ncplane_bg_alpha(const struct ncplane* n){
  return ncchannels_bg_alpha(ncplane_channels(n));
}

// Is the plane's background using the "default background color"?
static inline bool
ncplane_bg_default_p(const struct ncplane* n){
  return ncchannels_bg_default_p(ncplane_channels(n));
}

// Extract 24 bits of foreground RGB from 'n', split into components.
static inline uint32_t
ncplane_fg_rgb8(const struct ncplane* n, unsigned* r, unsigned* g, unsigned* b){
  return ncchannels_fg_rgb8(ncplane_channels(n), r, g, b);
}

// Extract 24 bits of background RGB from 'n', split into components.
static inline uint32_t
ncplane_bg_rgb8(const struct ncplane* n, unsigned* r, unsigned* g, unsigned* b){
  return ncchannels_bg_rgb8(ncplane_channels(n), r, g, b);
}

// Set the current fore/background color using RGB specifications. If the
// terminal does not support directly-specified 3x8b cells (24-bit "TrueColor",
// indicated by the "RGB" terminfo capability), the provided values will be
// interpreted in some lossy fashion. None of r, g, or b may exceed 255.
// "HP-like" terminals require setting foreground and background at the same
// time using "color pairs"; Notcurses will manage color pairs transparently.
API int ncplane_set_fg_rgb8(struct ncplane* n, unsigned r, unsigned g, unsigned b);
API int ncplane_set_bg_rgb8(struct ncplane* n, unsigned r, unsigned g, unsigned b);

// Same, but clipped to [0..255].
API void ncplane_set_bg_rgb8_clipped(struct ncplane* n, int r, int g, int b);
API void ncplane_set_fg_rgb8_clipped(struct ncplane* n, int r, int g, int b);

// Same, but with rgb assembled into a channel (i.e. lower 24 bits).
API int ncplane_set_fg_rgb(struct ncplane* n, uint32_t channel);
API int ncplane_set_bg_rgb(struct ncplane* n, uint32_t channel);

// Use the default color for the foreground/background.
API void ncplane_set_fg_default(struct ncplane* n);
API void ncplane_set_bg_default(struct ncplane* n);

// Set the ncplane's foreground palette index, set the foreground palette index
// bit, set it foreground-opaque, and clear the foreground default color bit.
API int ncplane_set_fg_palindex(struct ncplane* n, unsigned idx);
API int ncplane_set_bg_palindex(struct ncplane* n, unsigned idx);

// Set the alpha parameters for ncplane 'n'.
API int ncplane_set_fg_alpha(struct ncplane* n, int alpha);
API int ncplane_set_bg_alpha(struct ncplane* n, int alpha);

// Called for each fade iteration on 'ncp'. If anything but 0 is returned,
// the fading operation ceases immediately, and that value is propagated out.
// The recommended absolute display time target is passed in 'tspec'.
typedef int (*fadecb)(struct notcurses* nc, struct ncplane* n,
                      const struct timespec*, void* curry);

// Fade the ncplane out over the provided time, calling 'fader' at each
// iteration. Requires a terminal which supports truecolor, or at least palette
// modification (if the terminal uses a palette, our ability to fade planes is
// limited, and affected by the complexity of the rest of the screen).
API int ncplane_fadeout(struct ncplane* n, const struct timespec* ts,
                        fadecb fader, void* curry)
  __attribute__ ((nonnull (1)));

// Fade the ncplane in over the specified time. Load the ncplane with the
// target cells without rendering, then call this function. When it's done, the
// ncplane will have reached the target levels, starting from zeroes.
API int ncplane_fadein(struct ncplane* n, const struct timespec* ts,
                       fadecb fader, void* curry)
  __attribute__ ((nonnull (1)));

// Rather than the simple ncplane_fade{in/out}(), ncfadectx_setup() can be
// paired with a loop over ncplane_fade{in/out}_iteration() + ncfadectx_free().
API ALLOC struct ncfadectx* ncfadectx_setup(struct ncplane* n)
  __attribute__ ((nonnull (1)));

// Return the number of iterations through which 'nctx' will fade.
API int ncfadectx_iterations(const struct ncfadectx* nctx)
  __attribute__ ((nonnull (1)));

// Fade out through 'iter' iterations, where
// 'iter' < 'ncfadectx_iterations(nctx)'.
API int ncplane_fadeout_iteration(struct ncplane* n, struct ncfadectx* nctx,
                                  int iter, fadecb fader, void* curry)
  __attribute__ ((nonnull (1, 2)));

// Fade in through 'iter' iterations, where
// 'iter' < 'ncfadectx_iterations(nctx)'.
API int ncplane_fadein_iteration(struct ncplane* n, struct ncfadectx* nctx,
                                  int iter, fadecb fader, void* curry)
  __attribute__ ((nonnull (1, 2)));

// Pulse the plane in and out until the callback returns non-zero, relying on
// the callback 'fader' to initiate rendering. 'ts' defines the half-period
// (i.e. the transition from black to full brightness, or back again). Proper
// use involves preparing (but not rendering) an ncplane, then calling
// ncplane_pulse(), which will fade in from black to the specified colors.
API int ncplane_pulse(struct ncplane* n, const struct timespec* ts, fadecb fader, void* curry)
  __attribute__ ((nonnull (1)));

// Release the resources associated with 'nctx'.
API void ncfadectx_free(struct ncfadectx* nctx);

// load up six cells with the EGCs necessary to draw a box. returns 0 on
// success, -1 on error. on error, any cells this function might
// have loaded before the error are nccell_release()d. There must be at least
// six EGCs in gcluster.
static inline int
nccells_load_box(struct ncplane* n, uint16_t styles, uint64_t channels,
                 nccell* ul, nccell* ur, nccell* ll, nccell* lr,
                 nccell* hl, nccell* vl, const char* gclusters){
  int ulen;
  if((ulen = nccell_prime(n, ul, gclusters, styles, channels)) > 0){
    if((ulen = nccell_prime(n, ur, gclusters += ulen, styles, channels)) > 0){
      if((ulen = nccell_prime(n, ll, gclusters += ulen, styles, channels)) > 0){
        if((ulen = nccell_prime(n, lr, gclusters += ulen, styles, channels)) > 0){
          if((ulen = nccell_prime(n, hl, gclusters += ulen, styles, channels)) > 0){
            if(nccell_prime(n, vl, gclusters + ulen, styles, channels) > 0){
              return 0;
            }
            nccell_release(n, hl);
          }
          nccell_release(n, lr);
        }
        nccell_release(n, ll);
      }
      nccell_release(n, ur);
    }
    nccell_release(n, ul);
  }
  return -1;
}

static inline int
nccells_ascii_box(struct ncplane* n, uint16_t attr, uint64_t channels,
                  nccell* ul, nccell* ur, nccell* ll, nccell* lr, nccell* hl, nccell* vl){
  return nccells_load_box(n, attr, channels, ul, ur, ll, lr, hl, vl, NCBOXASCII);
}

static inline int
nccells_double_box(struct ncplane* n, uint16_t attr, uint64_t channels,
                   nccell* ul, nccell* ur, nccell* ll, nccell* lr, nccell* hl, nccell* vl){
  if(notcurses_canutf8(ncplane_notcurses(n))){
    return nccells_load_box(n, attr, channels, ul, ur, ll, lr, hl, vl, NCBOXDOUBLE);
  }
  return nccells_ascii_box(n, attr, channels, ul, ur, ll, lr, hl, vl);
}

static inline int
nccells_rounded_box(struct ncplane* n, uint16_t attr, uint64_t channels,
                    nccell* ul, nccell* ur, nccell* ll, nccell* lr, nccell* hl, nccell* vl){
  if(notcurses_canutf8(ncplane_notcurses(n))){
    return nccells_load_box(n, attr, channels, ul, ur, ll, lr, hl, vl, NCBOXROUND);
  }
  return nccells_ascii_box(n, attr, channels, ul, ur, ll, lr, hl, vl);
}

static inline int
nccells_light_box(struct ncplane* n, uint16_t attr, uint64_t channels,
                  nccell* ul, nccell* ur, nccell* ll, nccell* lr, nccell* hl, nccell* vl){
  if(notcurses_canutf8(ncplane_notcurses(n))){
    return nccells_load_box(n, attr, channels, ul, ur, ll, lr, hl, vl, NCBOXLIGHT);
  }
  return nccells_ascii_box(n, attr, channels, ul, ur, ll, lr, hl, vl);
}

static inline int
nccells_heavy_box(struct ncplane* n, uint16_t attr, uint64_t channels,
                  nccell* ul, nccell* ur, nccell* ll, nccell* lr, nccell* hl, nccell* vl){
  if(notcurses_canutf8(ncplane_notcurses(n))){
    return nccells_load_box(n, attr, channels, ul, ur, ll, lr, hl, vl, NCBOXHEAVY);
  }
  return nccells_ascii_box(n, attr, channels, ul, ur, ll, lr, hl, vl);
}

static inline int
ncplane_rounded_box(struct ncplane* n, uint16_t styles, uint64_t channels,
                    unsigned ystop, unsigned xstop, unsigned ctlword){
  int ret = 0;
  nccell ul = NCCELL_TRIVIAL_INITIALIZER, ur = NCCELL_TRIVIAL_INITIALIZER;
  nccell ll = NCCELL_TRIVIAL_INITIALIZER, lr = NCCELL_TRIVIAL_INITIALIZER;
  nccell hl = NCCELL_TRIVIAL_INITIALIZER, vl = NCCELL_TRIVIAL_INITIALIZER;
  if((ret = nccells_rounded_box(n, styles, channels, &ul, &ur, &ll, &lr, &hl, &vl)) == 0){
    ret = ncplane_box(n, &ul, &ur, &ll, &lr, &hl, &vl, ystop, xstop, ctlword);
  }
  nccell_release(n, &ul); nccell_release(n, &ur);
  nccell_release(n, &ll); nccell_release(n, &lr);
  nccell_release(n, &hl); nccell_release(n, &vl);
  return ret;
}

static inline int
ncplane_perimeter_rounded(struct ncplane* n, uint16_t stylemask,
                          uint64_t channels, unsigned ctlword){
  if(ncplane_cursor_move_yx(n, 0, 0)){
    return -1;
  }
  unsigned dimy, dimx;
  ncplane_dim_yx(n, &dimy, &dimx);
  nccell ul = NCCELL_TRIVIAL_INITIALIZER;
  nccell ur = NCCELL_TRIVIAL_INITIALIZER;
  nccell ll = NCCELL_TRIVIAL_INITIALIZER;
  nccell lr = NCCELL_TRIVIAL_INITIALIZER;
  nccell vl = NCCELL_TRIVIAL_INITIALIZER;
  nccell hl = NCCELL_TRIVIAL_INITIALIZER;
  if(nccells_rounded_box(n, stylemask, channels, &ul, &ur, &ll, &lr, &hl, &vl)){
    return -1;
  }
  int r = ncplane_box_sized(n, &ul, &ur, &ll, &lr, &hl, &vl, dimy, dimx, ctlword);
  nccell_release(n, &ul); nccell_release(n, &ur);
  nccell_release(n, &ll); nccell_release(n, &lr);
  nccell_release(n, &hl); nccell_release(n, &vl);
  return r;
}

static inline int
ncplane_rounded_box_sized(struct ncplane* n, uint16_t styles, uint64_t channels,
                          unsigned ylen, unsigned xlen, unsigned ctlword){
  unsigned y, x;
  ncplane_cursor_yx(n, &y, &x);
  return ncplane_rounded_box(n, styles, channels, y + ylen - 1,
                             x + xlen - 1, ctlword);
}

static inline int
ncplane_double_box(struct ncplane* n, uint16_t styles, uint64_t channels,
                   unsigned ylen, unsigned xlen, unsigned ctlword){
  int ret = 0;
  nccell ul = NCCELL_TRIVIAL_INITIALIZER, ur = NCCELL_TRIVIAL_INITIALIZER;
  nccell ll = NCCELL_TRIVIAL_INITIALIZER, lr = NCCELL_TRIVIAL_INITIALIZER;
  nccell hl = NCCELL_TRIVIAL_INITIALIZER, vl = NCCELL_TRIVIAL_INITIALIZER;
  if((ret = nccells_double_box(n, styles, channels, &ul, &ur, &ll, &lr, &hl, &vl)) == 0){
    ret = ncplane_box(n, &ul, &ur, &ll, &lr, &hl, &vl, ylen, xlen, ctlword);
  }
  nccell_release(n, &ul); nccell_release(n, &ur);
  nccell_release(n, &ll); nccell_release(n, &lr);
  nccell_release(n, &hl); nccell_release(n, &vl);
  return ret;
}

static inline int
ncplane_ascii_box(struct ncplane* n, uint16_t styles, uint64_t channels,
                  unsigned ylen, unsigned xlen, unsigned ctlword){
  int ret = 0;
  nccell ul = NCCELL_TRIVIAL_INITIALIZER, ur = NCCELL_TRIVIAL_INITIALIZER;
  nccell ll = NCCELL_TRIVIAL_INITIALIZER, lr = NCCELL_TRIVIAL_INITIALIZER;
  nccell hl = NCCELL_TRIVIAL_INITIALIZER, vl = NCCELL_TRIVIAL_INITIALIZER;
  if((ret = nccells_ascii_box(n, styles, channels, &ul, &ur, &ll, &lr, &hl, &vl)) == 0){
    ret = ncplane_box(n, &ul, &ur, &ll, &lr, &hl, &vl, ylen, xlen, ctlword);
  }
  nccell_release(n, &ul); nccell_release(n, &ur);
  nccell_release(n, &ll); nccell_release(n, &lr);
  nccell_release(n, &hl); nccell_release(n, &vl);
  return ret;
}

static inline int
ncplane_perimeter_double(struct ncplane* n, uint16_t stylemask,
                         uint64_t channels, unsigned ctlword){
  if(ncplane_cursor_move_yx(n, 0, 0)){
    return -1;
  }
  unsigned dimy, dimx;
  ncplane_dim_yx(n, &dimy, &dimx);
  nccell ul = NCCELL_TRIVIAL_INITIALIZER;
  nccell ur = NCCELL_TRIVIAL_INITIALIZER;
  nccell ll = NCCELL_TRIVIAL_INITIALIZER;
  nccell lr = NCCELL_TRIVIAL_INITIALIZER;
  nccell vl = NCCELL_TRIVIAL_INITIALIZER;
  nccell hl = NCCELL_TRIVIAL_INITIALIZER;
  if(nccells_double_box(n, stylemask, channels, &ul, &ur, &ll, &lr, &hl, &vl)){
    return -1;
  }
  int r = ncplane_box_sized(n, &ul, &ur, &ll, &lr, &hl, &vl, dimy, dimx, ctlword);
  nccell_release(n, &ul); nccell_release(n, &ur);
  nccell_release(n, &ll); nccell_release(n, &lr);
  nccell_release(n, &hl); nccell_release(n, &vl);
  return r;
}

static inline int
ncplane_double_box_sized(struct ncplane* n, uint16_t styles, uint64_t channels,
                         unsigned ylen, unsigned xlen, unsigned ctlword){
  unsigned y, x;
  ncplane_cursor_yx(n, &y, &x);
  return ncplane_double_box(n, styles, channels, y + ylen - 1,
                            x + xlen - 1, ctlword);
}

// Open a visual at 'file', extract a codec and parameters, decode the first
// image to memory.
API ALLOC struct ncvisual* ncvisual_from_file(const char* file)
  __attribute__ ((nonnull (1)));

// Prepare an ncvisual, and its underlying plane, based off RGBA content in
// memory at 'rgba'. 'rgba' is laid out as 'rows' lines, each of which is
// 'rowstride' bytes in length. Each line has 'cols' 32-bit 8bpc RGBA pixels
// followed by possible padding (there will be 'rowstride' - 'cols' * 4 bytes
// of padding). The total size of 'rgba' is thus ('rows' * 'rowstride') bytes,
// of which ('rows' * 'cols' * 4) bytes are actual non-padding data. It is an
// error if any argument is not positive, if 'rowstride' is not a multiple of
// 4, or if 'rowstride' is less than 'cols' * 4.
API ALLOC struct ncvisual* ncvisual_from_rgba(const void* rgba, int rows,
                                              int rowstride, int cols)
  __attribute__ ((nonnull (1)));

// ncvisual_from_rgba(), but the pixels are 3-byte RGB. A is filled in
// throughout using 'alpha'. It is an error if 'rows', 'rowstride', or 'cols'
// is not positive, if 'rowstride' is not a multiple of 3, or if 'rowstride'
// is less than 'cols' * 3.
API ALLOC struct ncvisual* ncvisual_from_rgb_packed(const void* rgba, int rows,
                                                    int rowstride, int cols,
                                                    int alpha)
  __attribute__ ((nonnull (1)));

// ncvisual_from_rgba(), but the pixels are 4-byte RGBx. A is filled in
// throughout using 'alpha'. It is an error if 'rows', 'cols', or 'rowstride'
// are not positive, if 'rowstride' is not a multiple of 4, or if 'rowstride'
// is less than 'cols' * 4.
API ALLOC struct ncvisual* ncvisual_from_rgb_loose(const void* rgba, int rows,
                                                   int rowstride, int cols,
                                                   int alpha)
  __attribute__ ((nonnull (1)));

// ncvisual_from_rgba(), but 'bgra' is arranged as BGRA. note that this is a
// byte-oriented layout, despite being bunched in 32-bit pixels; the lowest
// memory address ought be B, and A is reached by adding 3 to that address.
// It is an error if 'rows', 'cols', or 'rowstride' are not positive, if
// 'rowstride' is not a multiple of 4, or if 'rowstride' is less than 'cols' * 4.
API ALLOC struct ncvisual* ncvisual_from_bgra(const void* bgra, int rows,
                                              int rowstride, int cols)
  __attribute__ ((nonnull (1)));

// ncvisual_from_rgba(), but 'data' is 'pstride'-byte palette-indexed pixels,
// arranged in 'rows' lines of 'rowstride' bytes each, composed of 'cols'
// pixels. 'palette' is an array of at least 'palsize' ncchannels.
// It is an error if 'rows', 'cols', 'rowstride', or 'pstride' are not
// positive, if 'rowstride' is not a multiple of 'pstride', or if 'rowstride'
// is less than 'cols' * 'pstride'.
API ALLOC struct ncvisual* ncvisual_from_palidx(const void* data, int rows,
                                                int rowstride, int cols,
                                                int palsize, int pstride,
                                                const uint32_t* palette)
  __attribute__ ((nonnull (1, 7)));

// Promote an ncplane 'n' to an ncvisual. The plane may contain only spaces,
// half blocks, and full blocks. The latter will be checked, and any other
// glyph will result in a NULL being returned. This function exists so that
// planes can be subjected to ncvisual transformations. If possible, it's
// better to create the ncvisual from memory using ncvisual_from_rgba().
// Lengths of 0 are interpreted to mean "all available remaining area".
API ALLOC struct ncvisual* ncvisual_from_plane(const struct ncplane* n,
                                               ncblitter_e blit,
                                               int begy, int begx,
                                               unsigned leny, unsigned lenx)
  __attribute__ ((nonnull (1)));

// Construct an ncvisual from a nul-terminated Sixel control sequence.
API ALLOC struct ncvisual* ncvisual_from_sixel(const char* s, unsigned leny, unsigned lenx)
  __attribute__ ((nonnull (1)));

#define NCVISUAL_OPTION_NODEGRADE     0x0001ull // fail rather than degrade
#define NCVISUAL_OPTION_BLEND         0x0002ull // use NCALPHA_BLEND with visual
#define NCVISUAL_OPTION_HORALIGNED    0x0004ull // x is an alignment, not absolute
#define NCVISUAL_OPTION_VERALIGNED    0x0008ull // y is an alignment, not absolute
#define NCVISUAL_OPTION_ADDALPHA      0x0010ull // transcolor is in effect
#define NCVISUAL_OPTION_CHILDPLANE    0x0020ull // interpret n as parent
#define NCVISUAL_OPTION_NOINTERPOLATE 0x0040ull // non-interpolative scaling

struct ncvisual_options {
  // if no ncplane is provided, one will be created using the exact size
  // necessary to render the source with perfect fidelity (this might be
  // smaller or larger than the rendering area). if NCVISUAL_OPTION_CHILDPLANE
  // is provided, this must be non-NULL, and will be interpreted as the parent.
  struct ncplane* n;
  // the scaling is ignored if no ncplane is provided (it ought be NCSCALE_NONE
  // in this case). otherwise, the source is stretched/scaled relative to the
  // provided ncplane.
  ncscale_e scaling;
  // if an ncplane is provided, y and x specify where the visual will be
  // rendered on that plane. otherwise, they specify where the created ncplane
  // will be placed relative to the standard plane's origin. x is an ncalign_e
  // value if NCVISUAL_OPTION_HORALIGNED is provided. y is an ncalign_e if
  // NCVISUAL_OPTION_VERALIGNED is provided.
  int y, x;
  // the region of the visual that ought be rendered. for the entire visual,
  // pass an origin of 0, 0 and a size of 0, 0 (or the true height and width).
  // these numbers are all in terms of ncvisual pixels. negative values are
  // prohibited.
  unsigned begy, begx; // origin of rendered region in pixels
  unsigned leny, lenx; // size of rendered region in pixels
  // use NCBLIT_DEFAULT if you don't care, an appropriate blitter will be
  // chosen for your terminal, given your scaling. NCBLIT_PIXEL is never
  // chosen for NCBLIT_DEFAULT.
  ncblitter_e blitter; // glyph set to use (maps input to output cells)
  uint64_t flags; // bitmask over NCVISUAL_OPTION_*
  uint32_t transcolor; // treat this color as transparent under NCVISUAL_OPTION_ADDALPHA
  // pixel offsets within the cell. if NCBLIT_PIXEL is used, the bitmap will
  // be drawn offset from the upper-left cell's origin by these amounts. it is
  // an error if either number exceeds the cell-pixel geometry in its
  // dimension. if NCBLIT_PIXEL is not used, these fields are ignored.
  // this functionality can be used for smooth bitmap movement.
  unsigned pxoffy, pxoffx;
};

// describes all geometries of an ncvisual: those which are inherent, and those
// dependent upon a given rendering regime. pixy and pixx are the true internal
// pixel geometry, taken directly from the load (and updated by
// ncvisual_resize()). cdimy/cdimx are the cell-pixel geometry *at the time
// of this call* (it can change with a font change, in which case all values
// other than pixy/pixx are invalidated). rpixy/rpixx are the pixel geometry as
// handed to the blitter, following any scaling. scaley/scalex are the number
// of input pixels drawn to a single cell; when using NCBLIT_PIXEL, they are
// equivalent to cdimy/cdimx. rcelly/rcellx are the cell geometry as written by
// the blitter, following any padding (there is padding whenever rpix{y, x} is
// not evenly divided by scale{y, x}, and also sometimes for Sixel).
// maxpixely/maxpixelx are defined only when NCBLIT_PIXEL is used, and specify
// the largest bitmap that the terminal is willing to accept. blitter is the
// blitter which will be used, a function of the requested blitter and the
// blitters actually supported by this environment. if no ncvisual was
// supplied, only cdimy/cdimx are filled in.
typedef struct ncvgeom {
  unsigned pixy, pixx;     // true pixel geometry of ncvisual data
  unsigned cdimy, cdimx;   // terminal cell geometry when this was calculated
  unsigned rpixy, rpixx;   // rendered pixel geometry (per visual_options)
  unsigned rcelly, rcellx; // rendered cell geometry (per visual_options)
  unsigned scaley, scalex; // source pixels per filled cell
  unsigned begy, begx;     // upper-left corner of used region
  unsigned leny, lenx;     // geometry of used region
  unsigned maxpixely, maxpixelx; // only defined for NCBLIT_PIXEL
  ncblitter_e blitter;     // blitter that will be used
} ncvgeom;

// all-purpose ncvisual geometry solver. one or both of 'nc' and 'n' must be
// non-NULL. if 'nc' is NULL, only pixy/pixx will be filled in, with the true
// pixel geometry of 'n'. if 'n' is NULL, only cdimy/cdimx, blitter,
// scaley/scalex, and maxpixely/maxpixelx are filled in. cdimy/cdimx and
// maxpixely/maxpixelx are only ever filled in if we know them.
API int ncvisual_geom(const struct notcurses* nc, const struct ncvisual* n,
                      const struct ncvisual_options* vopts, ncvgeom* geom)
  __attribute__ ((nonnull (4)));

// Destroy an ncvisual. Rendered elements will not be disrupted, but the visual
// can be neither decoded nor rendered any further.
API void ncvisual_destroy(struct ncvisual* ncv);

// extract the next frame from an ncvisual. returns 1 on end of file, 0 on
// success, and -1 on failure.
API int ncvisual_decode(struct ncvisual* nc)
  __attribute__ ((nonnull (1)));

// decode the next frame ala ncvisual_decode(), but if we have reached the end,
// rewind to the first frame of the ncvisual. a subsequent 'ncvisual_blit()'
// will render the first frame, as if the ncvisual had been closed and reopened.
// the return values remain the same as those of ncvisual_decode().
API int ncvisual_decode_loop(struct ncvisual* nc)
  __attribute__ ((nonnull (1)));

// Rotate the visual 'rads' radians. Only M_PI/2 and -M_PI/2 are supported at
// the moment, but this might change in the future.
API int ncvisual_rotate(struct ncvisual* n, double rads)
  __attribute__ ((nonnull (1)));

// Scale the visual to 'rows' X 'columns' pixels, using the best scheme
// available. This is a lossy transformation, unless the size is unchanged.
API int ncvisual_resize(struct ncvisual* n, int rows, int cols)
  __attribute__ ((nonnull (1)));

// Scale the visual to 'rows' X 'columns' pixels, using non-interpolative
// (naive) scaling. No new colors will be introduced as a result.
API int ncvisual_resize_noninterpolative(struct ncvisual* n, int rows, int cols)
  __attribute__ ((nonnull (1)));

// Polyfill at the specified location within the ncvisual 'n', using 'rgba'.
API int ncvisual_polyfill_yx(struct ncvisual* n, unsigned y, unsigned x, uint32_t rgba)
  __attribute__ ((nonnull (1)));

// Get the specified pixel from the specified ncvisual.
API int ncvisual_at_yx(const struct ncvisual* n, unsigned y, unsigned x,
                       uint32_t* pixel)
  __attribute__ ((nonnull (1, 4)));

// Set the specified pixel in the specified ncvisual.
API int ncvisual_set_yx(const struct ncvisual* n, unsigned y, unsigned x,
                        uint32_t pixel)
  __attribute__ ((nonnull (1)));

// Render the decoded frame according to the provided options (which may be
// NULL). The plane used for rendering depends on vopts->n and vopts->flags.
// If NCVISUAL_OPTION_CHILDPLANE is set, vopts->n must not be NULL, and the
// plane will always be created as a child of vopts->n. If this flag is not
// set, and vopts->n is NULL, a new plane is created as root of a new pile.
// If the flag is not set and vopts->n is not NULL, we render to vopts->n.
// A subregion of the visual can be rendered using 'begx', 'begy', 'lenx', and
// 'leny'. Negative values for any of these are an error. It is an error to
// specify any region beyond the boundaries of the frame. Returns the (possibly
// newly-created) plane to which we drew. Pixels may not be blitted to the
// standard plane.
API struct ncplane* ncvisual_blit(struct notcurses* nc, struct ncvisual* ncv,
                                  const struct ncvisual_options* vopts)
  __attribute__ ((nonnull (2)));

// Create a new plane as prescribed in opts, either as a child of 'vopts->n',
// or the root of a new pile if 'vopts->n' is NULL (or 'vopts' itself is NULL).
// Blit 'ncv' to the created plane according to 'vopts'. If 'vopts->n' is
// non-NULL, NCVISUAL_OPTION_CHILDPLANE must be supplied.
__attribute__ ((nonnull (1, 2, 3))) static inline struct ncplane*
ncvisualplane_create(struct notcurses* nc, const struct ncplane_options* opts,
                     struct ncvisual* ncv, struct ncvisual_options* vopts){
  struct ncplane* newn;
  if(vopts && vopts->n){
    if(vopts->flags & NCVISUAL_OPTION_CHILDPLANE){
      return NULL; // the whole point is to create a new plane
    }
    newn = ncplane_create(vopts->n, opts);
  }else{
    newn = ncpile_create(nc, opts);
  }
  if(newn == NULL){
    return NULL;
  }
  struct ncvisual_options v;
  if(!vopts){
    vopts = &v;
    memset(vopts, 0, sizeof(*vopts));
  }
  vopts->n = newn;
  if(ncvisual_blit(nc, ncv, vopts) == NULL){
    ncplane_destroy(newn);
    vopts->n = NULL;
    return NULL;
  }
  return newn;
}

// If a subtitle ought be displayed at this time, return a new plane (bound
// to 'parent' containing the subtitle, which might be text or graphics
// (depending on the input format).
API ALLOC struct ncplane* ncvisual_subtitle_plane(struct ncplane* parent,
                                                  const struct ncvisual* ncv)
  __attribute__ ((nonnull (1, 2)));

// Get the default *media* (not plot) blitter for this environment when using
// the specified scaling method. Currently, this means:
//  - if lacking UTF-8, NCBLIT_1x1
//  - otherwise, if not NCSCALE_STRETCH, NCBLIT_2x1
//  - otherwise, if sextants are not known to be good, NCBLIT_2x2
//  - otherwise NCBLIT_3x2
// NCBLIT_2x2 and NCBLIT_3x2 both distort the original aspect ratio, thus
// NCBLIT_2x1 is used outside of NCSCALE_STRETCH.
API ncblitter_e ncvisual_media_defblitter(const struct notcurses* nc, ncscale_e scale)
  __attribute__ ((nonnull (1)));

// Called for each frame rendered from 'ncv'. If anything but 0 is returned,
// the streaming operation ceases immediately, and that value is propagated out.
// The recommended absolute display time target is passed in 'tspec'.
typedef int (*ncstreamcb)(struct ncvisual*, struct ncvisual_options*,
                          const struct timespec*, void*);

// Shut up and display my frames! Provide as an argument to ncvisual_stream().
// If you'd like subtitles to be decoded, provide an ncplane as the curry. If the
// curry is NULL, subtitles will not be displayed.
API int ncvisual_simple_streamer(struct ncvisual* ncv, struct ncvisual_options* vopts,
                                 const struct timespec* tspec, void* curry)
  __attribute__ ((nonnull (1)));

// Stream the entirety of the media, according to its own timing. Blocking,
// obviously. streamer may be NULL; it is otherwise called for each frame, and
// its return value handled as outlined for streamcb. If streamer() returns
// non-zero, the stream is aborted, and that value is returned. By convention,
// return a positive number to indicate intentional abort from within
// streamer(). 'timescale' allows the frame duration time to be scaled. For a
// visual naturally running at 30FPS, a 'timescale' of 0.1 will result in
// 300FPS, and a 'timescale' of 10 will result in 3FPS. It is an error to
// supply 'timescale' less than or equal to 0.
API int ncvisual_stream(struct notcurses* nc, struct ncvisual* ncv,
                        float timescale, ncstreamcb streamer,
                        const struct ncvisual_options* vopts, void* curry)
  __attribute__ ((nonnull (1, 2)));

// Blit a flat array 'data' of RGBA 32-bit values to the ncplane 'vopts->n',
// which mustn't be NULL. the blit begins at 'vopts->y' and 'vopts->x' relative
// to the specified plane. Each source row ought occupy 'linesize' bytes (this
// might be greater than 'vopts->lenx' * 4 due to padding or partial blits). A
// subregion of the input can be specified with the 'begy'x'begx' and
// 'leny'x'lenx' fields from 'vopts'. Returns the number of pixels blitted, or
// -1 on error.
API int ncblit_rgba(const void* data, int linesize,
                    const struct ncvisual_options* vopts)
  __attribute__ ((nonnull (1)));

// Same as ncblit_rgba(), but for BGRx.
API int ncblit_bgrx(const void* data, int linesize,
                    const struct ncvisual_options* vopts)
  __attribute__ ((nonnull (1)));

// Supply an alpha value [0..255] to be applied throughout.
API int ncblit_rgb_packed(const void* data, int linesize,
                          const struct ncvisual_options* vopts, int alpha)
  __attribute__ ((nonnull (1)));

// Supply an alpha value [0..255] to be applied throughout. linesize must be
// a multiple of 4 for this RGBx data.
API int ncblit_rgb_loose(const void* data, int linesize,
                         const struct ncvisual_options* vopts, int alpha)
  __attribute__ ((nonnull (1)));

// The ncpixel API facilitates direct management of the pixels within an
// ncvisual (ncvisuals keep a backing store of 32-bit RGBA pixels, and render
// them down to terminal graphics in ncvisual_blit()).
//
// Per libav, we "store as BGRA on little-endian, and ARGB on big-endian".
// This is an RGBA *byte-order* scheme. libav emits bytes, not words. Those
// bytes are R-G-B-A. When read as words, on little endian this will be ABGR,
// and on big-endian this will be RGBA. force everything to LE ABGR, a no-op
// on (and thus favoring) little-endian. Take that, big-endian mafia!

// Extract the 8-bit alpha component from a pixel
static inline unsigned
ncpixel_a(uint32_t pixel){
  return (htole(pixel) & 0xff000000u) >> 24u;
}

// Extract the 8-bit red component from an ABGR pixel
static inline unsigned
ncpixel_r(uint32_t pixel){
  return (htole(pixel) & 0x000000ffu);
}

// Extract the 8-bit green component from an ABGR pixel
static inline unsigned
ncpixel_g(uint32_t pixel){
  return (htole(pixel) & 0x0000ff00u) >> 8u;
}

// Extract the 8-bit blue component from an ABGR pixel
static inline unsigned
ncpixel_b(uint32_t pixel){
  return (htole(pixel) & 0x00ff0000u) >> 16u;
}

// Set the 8-bit alpha component of an ABGR pixel
static inline int
ncpixel_set_a(uint32_t* pixel, unsigned a){
  if(a > 255){
    return -1;
  }
  *pixel = htole((htole(*pixel) & 0x00ffffffu) | (a << 24u));
  return 0;
}

// Set the 8-bit red component of an ABGR pixel
static inline int
ncpixel_set_r(uint32_t* pixel, unsigned r){
  if(r > 255){
    return -1;
  }
  *pixel = htole((htole(*pixel) & 0xffffff00u) | r);
  return 0;
}

// Set the 8-bit green component of an ABGR pixel
static inline int
ncpixel_set_g(uint32_t* pixel, unsigned g){
  if(g > 255){
    return -1;
  }
  *pixel = htole((htole(*pixel) & 0xffff00ffu) | (g << 8u));
  return 0;
}

// Set the 8-bit blue component of an ABGR pixel
static inline int
ncpixel_set_b(uint32_t* pixel, unsigned b){
  if(b > 255){
    return -1;
  }
  *pixel = htole((htole(*pixel) & 0xff00ffffu) | (b << 16u));
  return 0;
}

// Construct a libav-compatible ABGR pixel, clipping at [0, 255).
static inline uint32_t
ncpixel(unsigned r, unsigned g, unsigned b){
  uint32_t pixel = 0;
  ncpixel_set_a(&pixel, 0xff);
  if(r > 255) r = 255;
  ncpixel_set_r(&pixel, r);
  if(g > 255) g = 255;
  ncpixel_set_g(&pixel, g);
  if(b > 255) b = 255;
  ncpixel_set_b(&pixel, b);
  return pixel;
}

// set the RGB values of an RGB pixel
static inline int
ncpixel_set_rgb8(uint32_t* pixel, unsigned r, unsigned g, unsigned b){
  if(ncpixel_set_r(pixel, r) || ncpixel_set_g(pixel, g) || ncpixel_set_b(pixel, b)){
    return -1;
  }
  return 0;
}

// An ncreel is a Notcurses region devoted to displaying zero or more
// line-oriented, contained tablets between which the user may navigate. If at
// least one tablets exists, there is a "focused tablet". As much of the focused
// tablet as is possible is always displayed. If there is space left over, other
// tablets are included in the display. Tablets can come and go at any time, and
// can grow or shrink at any time.
//
// This structure is amenable to line- and page-based navigation via keystrokes,
// scrolling gestures, trackballs, scrollwheels, touchpads, and verbal commands.

// is scrolling infinite (can one move down or up forever, or is an end
// reached?). if true, 'circular' specifies how to handle the special case of
// an incompletely-filled reel.
#define NCREEL_OPTION_INFINITESCROLL 0x0001ull
// is navigation circular (does moving down from the last tablet move to the
// first, and vice versa)? only meaningful when infinitescroll is true. if
// infinitescroll is false, this must be false.
#define NCREEL_OPTION_CIRCULAR       0x0002ull

typedef struct ncreel_options {
  // Notcurses can draw a border around the ncreel, and also around the
  // component tablets. inhibit borders by setting all valid bits in the masks.
  // partially inhibit borders by setting individual bits in the masks. the
  // appropriate attr and pair values will be used to style the borders.
  // focused and non-focused tablets can have different styles. you can instead
  // draw your own borders, or forgo borders entirely.
  unsigned bordermask; // bitfield; 1s will not be drawn (see bordermaskbits)
  uint64_t borderchan; // attributes used for ncreel border
  unsigned tabletmask; // bitfield; same as bordermask but for tablet borders
  uint64_t tabletchan; // tablet border styling channel
  uint64_t focusedchan;// focused tablet border styling channel
  uint64_t flags;      // bitfield over NCREEL_OPTION_*
} ncreel_options;

// Take over the ncplane 'nc' and use it to draw a reel according to 'popts'.
// The plane will be destroyed by ncreel_destroy(); this transfers ownership.
API ALLOC struct ncreel* ncreel_create(struct ncplane* n, const ncreel_options* popts)
  __attribute__ ((nonnull (1)));

// Returns the ncplane on which this ncreel lives.
API struct ncplane* ncreel_plane(struct ncreel* nr)
  __attribute__ ((nonnull (1)));

// Tablet draw callback, provided a tablet (from which the ncplane and userptr
// may be extracted), and a bool indicating whether output ought be drawn from
// the top (true) or bottom (false). Returns non-negative count of output lines,
// which must be less than or equal to ncplane_dim_y(nctablet_plane(t)).
typedef int (*tabletcb)(struct nctablet* t, bool drawfromtop);

// Add a new nctablet to the provided ncreel 'nr', having the callback object
// 'opaque'. Neither, either, or both of 'after' and 'before' may be specified.
// If neither is specified, the new tablet can be added anywhere on the reel.
// If one or the other is specified, the tablet will be added before or after
// the specified tablet. If both are specified, the tablet will be added to the
// resulting location, assuming it is valid (after->next == before->prev); if
// it is not valid, or there is any other error, NULL will be returned.
API ALLOC struct nctablet* ncreel_add(struct ncreel* nr, struct nctablet* after,
                                      struct nctablet* before, tabletcb cb,
                                      void* opaque)
  __attribute__ ((nonnull (1)));

// Return the number of nctablets in the ncreel 'nr'.
API int ncreel_tabletcount(const struct ncreel* nr)
  __attribute__ ((nonnull (1)));

// Delete the tablet specified by t from the ncreel 'nr'. Returns -1 if the
// tablet cannot be found.
API int ncreel_del(struct ncreel* nr, struct nctablet* t)
  __attribute__ ((nonnull (1)));

// Redraw the ncreel 'nr' in its entirety. The reel will be cleared, and
// tablets will be lain out, using the focused tablet as a fulcrum. Tablet
// drawing callbacks will be invoked for each visible tablet.
API int ncreel_redraw(struct ncreel* nr)
  __attribute__ ((nonnull (1)));

// Offer input 'ni' to the ncreel 'nr'. If it's relevant, this function returns
// true, and the input ought not be processed further. If it's irrelevant to
// the reel, false is returned. Relevant inputs include:
//  * a mouse click on a tablet (focuses tablet)
//  * a mouse scrollwheel event (rolls reel)
//  * up, down, pgup, or pgdown (navigates among items)
API bool ncreel_offer_input(struct ncreel* nr, const ncinput* ni)
  __attribute__ ((nonnull (1, 2)));

// Return the focused tablet, if any tablets are present. This is not a copy;
// be careful to use it only for the duration of a critical section.
API struct nctablet* ncreel_focused(struct ncreel* nr)
  __attribute__ ((nonnull (1)));

// Change focus to the next tablet, if one exists
API struct nctablet* ncreel_next(struct ncreel* nr)
  __attribute__ ((nonnull (1)));

// Change focus to the previous tablet, if one exists
API struct nctablet* ncreel_prev(struct ncreel* nr)
  __attribute__ ((nonnull (1)));

// Destroy an ncreel allocated with ncreel_create().
API void ncreel_destroy(struct ncreel* nr);

// Returns a pointer to a user pointer associated with this nctablet.
API void* nctablet_userptr(struct nctablet* t);

// Access the ncplane associated with nctablet 't', if one exists.
API struct ncplane* nctablet_plane(struct nctablet* t);

// Takes an arbitrarily large number, and prints it into a fixed-size buffer by
// adding the necessary SI suffix. Usually, pass a |NC[IB]?PREFIXSTRLEN+1|-sized
// buffer to generate up to |NC[IB]?PREFIXCOLUMNS| columns' worth of EGCs. The
// characteristic can occupy up through |mult-1| characters (3 for 1000, 4 for
// 1024). The mantissa can occupy either zero or two characters.

// snprintf(3) is used internally, with 's' as its size bound. If the output
// requires more size than is available, NULL will be returned.
//
// Floating-point is never used, because an IEEE758 double can only losslessly
// represent integers through 2^53-1.
//
// 2^64-1 is 18446744073709551615, 18.45E(xa). KMGTPEZY thus suffice to handle
// an 89-bit uintmax_t. Beyond Z(etta) and Y(otta) lie lands unspecified by SI.
// 2^-63 is 0.000000000000000000108, 1.08a(tto).
// val: value to print
// s: maximum output size; see snprintf(3)
// decimal: scaling. '1' if none has taken place.
// buf: buffer in which string will be generated
// omitdec: inhibit printing of all-0 decimal portions
// mult: base of suffix system (almost always 1000 or 1024)
// uprefix: character to print following suffix ('i' for kibibytes basically).
//   only printed if suffix is actually printed (input >= mult).
//
// You are encouraged to consult notcurses_metric(3).
API const char* ncnmetric(uintmax_t val, size_t s, uintmax_t decimal,
                          char* buf, int omitdec, uintmax_t mult, int uprefix)
  __attribute__ ((nonnull (4)));

// The number of columns is one fewer, as the STRLEN expressions must leave
// an extra byte open in case 'µ' (U+00B5, 0xC2 0xB5) shows up. NCPREFIXCOLUMNS
// is the maximum number of columns used by a mult == 1000 (standard)
// ncnmetric() call. NCIPREFIXCOLUMNS is the maximum number of columns used by a
// mult == 1024 (digital information) ncnmetric(). NCBPREFIXSTRLEN is the maximum
// number of columns used by a mult == 1024 call making use of the 'i' suffix.
// This is the true number of columns; to set up a printf()-style maximum
// field width, you should use NC[IB]PREFIXFMT (see below).
#define NCPREFIXCOLUMNS 7
#define NCIPREFIXCOLUMNS 8
#define NCBPREFIXCOLUMNS 9
#define NCPREFIXSTRLEN (NCPREFIXCOLUMNS + 1)  // Does not include a '\0' (xxx.xxU)
#define NCIPREFIXSTRLEN (NCIPREFIXCOLUMNS + 1) //  Does not include a '\0' (xxxx.xxU)
#define NCBPREFIXSTRLEN (NCBPREFIXCOLUMNS + 1) // Does not include a '\0' (xxxx.xxUi), i == prefix
// Used as arguments to a variable field width (i.e. "%*s" -- these are the *).
// We need this convoluted grotesquery to properly handle 'µ'.
#define NCMETRICFWIDTH(x, cols) \
    ((int)(strlen(x) - ncstrwidth(x, NULL, NULL) + (cols)))
#define NCPREFIXFMT(x) NCMETRICFWIDTH((x), NCPREFIXCOLUMNS), (x)
#define NCIPREFIXFMT(x) NCMETRICFWIDTH((x), NCIPREFIXCOLUMNS), (x)
#define NCBPREFIXFMT(x) NCMETRICFWIDTH((x), NCBPREFIXCOLUMNS), (x)

// Mega, kilo, gigafoo. Use PREFIXSTRLEN + 1 and PREFIXCOLUMNS.
static inline const char*
ncqprefix(uintmax_t val, uintmax_t decimal, char* buf, int omitdec){
  return ncnmetric(val, NCPREFIXSTRLEN + 1, decimal, buf, omitdec, 1000, '\0');
}

// Mibi, kebi, gibibytes sans 'i' suffix. Use IPREFIXSTRLEN + 1.
static inline const char*
nciprefix(uintmax_t val, uintmax_t decimal, char* buf, int omitdec){
  return ncnmetric(val, NCIPREFIXSTRLEN + 1, decimal, buf, omitdec, 1024, '\0');
}

// Mibi, kebi, gibibytes. Use BPREFIXSTRLEN + 1 and BPREFIXCOLUMNS.
static inline const char*
ncbprefix(uintmax_t val, uintmax_t decimal, char* buf, int omitdec){
  return ncnmetric(val, NCBPREFIXSTRLEN + 1, decimal, buf, omitdec, 1024, 'i');
}

// Get the default foreground color, if it is known. Returns -1 on error
// (unknown foreground). On success, returns 0, writing the RGB value to
// 'fg' (if non-NULL)
API int notcurses_default_foreground(const struct notcurses* nc, uint32_t* fg)
  __attribute__ ((nonnull (1)));

// Get the default background color, if it is known. Returns -1 on error
// (unknown background). On success, returns 0, writing the RGB value to
// 'bg' (if non-NULL) and setting 'bgtrans' high iff the background color
// is treated as transparent.
API int notcurses_default_background(const struct notcurses* nc, uint32_t* bg)
  __attribute__ ((nonnull (1)));

// Enable or disable the terminal's cursor, if supported, placing it at
// 'y', 'x'. Immediate effect (no need for a call to notcurses_render()).
// It is an error if 'y', 'x' lies outside the standard plane. Can be
// called while already visible to move the cursor.
API int notcurses_cursor_enable(struct notcurses* nc, int y, int x)
  __attribute__ ((nonnull (1)));

// Disable the hardware cursor. It is an error to call this while the
// cursor is already disabled.
API int notcurses_cursor_disable(struct notcurses* nc)
  __attribute__ ((nonnull (1)));

// Get the current location of the terminal's cursor, whether visible or not.
API int notcurses_cursor_yx(const struct notcurses* nc, int* y, int* x)
  __attribute__ ((nonnull (1)));

API int notcurses_bracketed_paste_enable(struct notcurses *nc);

API int notcurses_bracketed_paste_disable(struct notcurses *nc);

// Convert the plane's content to greyscale.
API void ncplane_greyscale(struct ncplane* n)
  __attribute__ ((nonnull (1)));

//                                 ╭──────────────────────────╮
//                                 │This is the primary header│
//   ╭──────────────────────this is the secondary header──────╮
//   │        ↑                                               │
//   │ option1 Long text #1                                   │
//   │ option2 Long text #2                                   │
//   │ option3 Long text #3                                   │
//   │ option4 Long text #4                                   │
//   │ option5 Long text #5                                   │
//   │ option6 Long text #6                                   │
//   │        ↓                                               │
//   ╰────────────────────────────────────here's the footer───╯

// selection widget -- an ncplane with a title header and a body section. the
// body section supports infinite scrolling up and down.
//
// At all times, exactly one item is selected.
struct ncselector_item {
  const char* option;
  const char* desc;
};

typedef struct ncselector_options {
  const char* title; // title may be NULL, inhibiting riser, saving two rows.
  const char* secondary; // secondary may be NULL
  const char* footer; // footer may be NULL
  const struct ncselector_item* items; // initial items and descriptions
  // default item (selected at start), must be < itemcount unless itemcount is
  // 0, in which case 'defidx' must also be 0
  unsigned defidx;
  // maximum number of options to display at once, 0 to use all available space
  unsigned maxdisplay;
  // exhaustive styling options
  uint64_t opchannels;   // option channels
  uint64_t descchannels; // description channels
  uint64_t titlechannels;// title channels
  uint64_t footchannels; // secondary and footer channels
  uint64_t boxchannels;  // border channels
  uint64_t flags;        // bitfield of NCSELECTOR_OPTION_*, currently unused
} ncselector_options;

API ALLOC struct ncselector* ncselector_create(struct ncplane* n, const ncselector_options* opts)
  __attribute__ ((nonnull (1)));

// Dynamically add or delete items. It is usually sufficient to supply a static
// list of items via ncselector_options->items.
API int ncselector_additem(struct ncselector* n, const struct ncselector_item* item);
API int ncselector_delitem(struct ncselector* n, const char* item);

// Return reference to the selected option, or NULL if there are no items.
API const char* ncselector_selected(const struct ncselector* n)
  __attribute__ ((nonnull (1)));

// Return a reference to the ncselector's underlying ncplane.
API struct ncplane* ncselector_plane(struct ncselector* n)
  __attribute__ ((nonnull (1)));

// Move up or down in the list. A reference to the newly-selected item is
// returned, or NULL if there are no items in the list.
API const char* ncselector_previtem(struct ncselector* n)
  __attribute__ ((nonnull (1)));
API const char* ncselector_nextitem(struct ncselector* n)
  __attribute__ ((nonnull (1)));

// Offer the input to the ncselector. If it's relevant, this function returns
// true, and the input ought not be processed further. If it's irrelevant to
// the selector, false is returned. Relevant inputs include:
//  * a mouse click on an item
//  * a mouse scrollwheel event
//  * a mouse click on the scrolling arrows
//  * up, down, pgup, or pgdown on an unrolled menu (navigates among items)
API bool ncselector_offer_input(struct ncselector* n, const ncinput* nc)
  __attribute__ ((nonnull (1, 2)));

// Destroy the ncselector.
API void ncselector_destroy(struct ncselector* n, char** item);

struct ncmselector_item {
  const char* option;
  const char* desc;
  bool selected;
};

//                                                   ╭───────────────────╮
//                                                   │ short round title │
//╭now this secondary is also very, very, very outlandishly long, you see┤
//│  ↑                                                                   │
//│ ☐ Pa231 Protactinium-231 (162kg)                                     │
//│ ☐ U233 Uranium-233 (15kg)                                            │
//│ ☐ U235 Uranium-235 (50kg)                                            │
//│ ☐ Np236 Neptunium-236 (7kg)                                          │
//│ ☐ Np237 Neptunium-237 (60kg)                                         │
//│ ☐ Pu238 Plutonium-238 (10kg)                                         │
//│ ☐ Pu239 Plutonium-239 (10kg)                                         │
//│ ☐ Pu240 Plutonium-240 (40kg)                                         │
//│ ☐ Pu241 Plutonium-241 (13kg)                                         │
//│ ☐ Am241 Americium-241 (100kg)                                        │
//│  ↓                                                                   │
//╰────────────────────────press q to exit (there is sartrev("no exit"))─╯

// multiselection widget -- a selector supporting multiple selections.
//
// Unlike the selector widget, zero to all of the items can be selected, but
// also the widget does not support adding or removing items at runtime.
typedef struct ncmultiselector_options {
  const char* title; // title may be NULL, inhibiting riser, saving two rows.
  const char* secondary; // secondary may be NULL
  const char* footer; // footer may be NULL
  const struct ncmselector_item* items; // initial items, descriptions, and statuses
  // maximum number of options to display at once, 0 to use all available space
  unsigned maxdisplay;
  // exhaustive styling options
  uint64_t opchannels;   // option channels
  uint64_t descchannels; // description channels
  uint64_t titlechannels;// title channels
  uint64_t footchannels; // secondary and footer channels
  uint64_t boxchannels;  // border channels
  uint64_t flags;        // bitfield of NCMULTISELECTOR_OPTION_*, currently unused
} ncmultiselector_options;

API ALLOC struct ncmultiselector* ncmultiselector_create(struct ncplane* n, const ncmultiselector_options* opts)
  __attribute__ ((nonnull (1)));

// Return selected vector. An array of bools must be provided, along with its
// length. If that length doesn't match the itemcount, it is an error.
API int ncmultiselector_selected(struct ncmultiselector* n, bool* selected, unsigned count);

// Return a reference to the ncmultiselector's underlying ncplane.
API struct ncplane* ncmultiselector_plane(struct ncmultiselector* n);

// Offer the input to the ncmultiselector. If it's relevant, this function
// returns true, and the input ought not be processed further. If it's
// irrelevant to the multiselector, false is returned. Relevant inputs include:
//  * a mouse click on an item
//  * a mouse scrollwheel event
//  * a mouse click on the scrolling arrows
//  * up, down, pgup, or pgdown on an unrolled menu (navigates among items)
API bool ncmultiselector_offer_input(struct ncmultiselector* n, const ncinput* nc)
  __attribute__ ((nonnull (1, 2)));

// Destroy the ncmultiselector.
API void ncmultiselector_destroy(struct ncmultiselector* n);

// nctree widget -- a vertical browser supporting line-based hierarchies.
//
// each item can have subitems, and has a curry. there is one callback for the
// entirety of the nctree. visible items have the callback invoked upon their
// curry and an ncplane. the ncplane can be reused across multiple invocations
// of the callback.

// each item has a curry, and zero or more subitems.
struct nctree_item {
  void* curry;
  struct nctree_item* subs;
  unsigned subcount;
};

typedef struct nctree_options {
  const struct nctree_item* items; // top-level nctree_item array
  unsigned count;           // size of |items|
  int (*nctreecb)(struct ncplane*, void*, int); // item callback function
  int indentcols;           // columns to indent per level of hierarchy
  uint64_t flags;           // bitfield of NCTREE_OPTION_*
} nctree_options;

// |opts| may *not* be NULL, since it is necessary to define a callback
// function.
API ALLOC struct nctree* nctree_create(struct ncplane* n, const nctree_options* opts)
  __attribute__ ((nonnull (1, 2)));

// Returns the ncplane on which this nctree lives.
API struct ncplane* nctree_plane(struct nctree* n)
  __attribute__ ((nonnull (1)));

// Redraw the nctree 'n' in its entirety. The tree will be cleared, and items
// will be lain out, using the focused item as a fulcrum. Item-drawing
// callbacks will be invoked for each visible item.
API int nctree_redraw(struct nctree* n)
  __attribute__ ((nonnull (1)));

// Offer input 'ni' to the nctree 'n'. If it's relevant, this function returns
// true, and the input ought not be processed further. If it's irrelevant to
// the tree, false is returned. Relevant inputs include:
//  * a mouse click on an item (focuses item)
//  * a mouse scrollwheel event (srolls tree)
//  * up, down, pgup, or pgdown (navigates among items)
API bool nctree_offer_input(struct nctree* n, const ncinput* ni)
  __attribute__ ((nonnull (1, 2)));

// Return the focused item, if any items are present. This is not a copy;
// be careful to use it only for the duration of a critical section.
API void* nctree_focused(struct nctree* n) __attribute__ ((nonnull (1)));

// Change focus to the next item.
API void* nctree_next(struct nctree* n) __attribute__ ((nonnull (1)));

// Change focus to the previous item.
API void* nctree_prev(struct nctree* n) __attribute__ ((nonnull (1)));

// Go to the item specified by the array |spec| (a spec is a series of unsigned
// values, each identifying a subelement in the hierarchy thus far, terminated
// by UINT_MAX). If the spec is invalid, NULL is returned, and the depth of the
// first invalid spec is written to *|failspec|. Otherwise, the true depth is
// written to *|failspec|, and the curry is returned (|failspec| is necessary
// because the curry could itself be NULL).
API void* nctree_goto(struct nctree* n, const unsigned* spec, int* failspec);

// Insert |add| into the nctree |n| at |spec|. The path up to the last element
// must already exist. If an item already exists at the path, it will be moved
// to make room for |add|.
API int nctree_add(struct nctree* n, const unsigned* spec, const struct nctree_item* add)
  __attribute__ ((nonnull (1, 2, 3)));

// Delete the item at |spec|, including any subitems.
API int nctree_del(struct nctree* n, const unsigned* spec)
  __attribute__ ((nonnull (1, 2)));

// Destroy the nctree.
API void nctree_destroy(struct nctree* n);

// Menus. Horizontal menu bars are supported, on the top and/or bottom rows.
// If the menu bar is longer than the screen, it will be only partially
// visible. Menus may be either visible or invisible by default. In the event of
// a plane resize, menus will be automatically moved/resized. Elements can be
// dynamically enabled or disabled at all levels (menu, section, and item),
struct ncmenu_item {
  const char* desc;     // utf-8 menu item, NULL for horizontal separator
  ncinput shortcut;     // shortcut, all should be distinct
};

struct ncmenu_section {
  const char* name;       // utf-8 c string
  int itemcount;
  struct ncmenu_item* items;
  ncinput shortcut;       // shortcut, will be underlined if present in name
};

#define NCMENU_OPTION_BOTTOM 0x0001ull // bottom row (as opposed to top row)
#define NCMENU_OPTION_HIDING 0x0002ull // hide the menu when not unrolled

typedef struct ncmenu_options {
  struct ncmenu_section* sections; // array of 'sectioncount' menu_sections
  int sectioncount;                // must be positive
  uint64_t headerchannels;         // styling for header
  uint64_t sectionchannels;        // styling for sections
  uint64_t flags;                  // flag word of NCMENU_OPTION_*
} ncmenu_options;

// Create a menu with the specified options, bound to the specified plane.
API ALLOC struct ncmenu* ncmenu_create(struct ncplane* n, const ncmenu_options* opts)
  __attribute__ ((nonnull (1)));

// Unroll the specified menu section, making the menu visible if it was
// invisible, and rolling up any menu section that is already unrolled.
API int ncmenu_unroll(struct ncmenu* n, int sectionidx);

// Roll up any unrolled menu section, and hide the menu if using hiding.
API int ncmenu_rollup(struct ncmenu* n) __attribute__ ((nonnull (1)));

// Unroll the previous/next section (relative to current unrolled). If no
// section is unrolled, the first section will be unrolled.
API int ncmenu_nextsection(struct ncmenu* n) __attribute__ ((nonnull (1)));
API int ncmenu_prevsection(struct ncmenu* n) __attribute__ ((nonnull (1)));

// Move to the previous/next item within the currently unrolled section. If no
// section is unrolled, the first section will be unrolled.
API int ncmenu_nextitem(struct ncmenu* n) __attribute__ ((nonnull (1)));
API int ncmenu_previtem(struct ncmenu* n) __attribute__ ((nonnull (1)));

// Disable or enable a menu item. Returns 0 if the item was found.
API int ncmenu_item_set_status(struct ncmenu* n, const char* section,
                               const char* item, bool enabled);

// Return the selected item description, or NULL if no section is unrolled. If
// 'ni' is not NULL, and the selected item has a shortcut, 'ni' will be filled
// in with that shortcut--this can allow faster matching.
API const char* ncmenu_selected(const struct ncmenu* n, ncinput* ni);

// Return the item description corresponding to the mouse click 'click'. The
// item must be on an actively unrolled section, and the click must be in the
// area of a valid item. If 'ni' is not NULL, and the selected item has a
// shortcut, 'ni' will be filled in with the shortcut.
API const char* ncmenu_mouse_selected(const struct ncmenu* n,
                                      const ncinput* click, ncinput* ni);

// Return the ncplane backing this ncmenu.
API struct ncplane* ncmenu_plane(struct ncmenu* n);

// Offer the input to the ncmenu. If it's relevant, this function returns true,
// and the input ought not be processed further. If it's irrelevant to the
// menu, false is returned. Relevant inputs include:
//  * mouse movement over a hidden menu
//  * a mouse click on a menu section (the section is unrolled)
//  * a mouse click outside of an unrolled menu (the menu is rolled up)
//  * left or right on an unrolled menu (navigates among sections)
//  * up or down on an unrolled menu (navigates among items)
//  * escape on an unrolled menu (the menu is rolled up)
API bool ncmenu_offer_input(struct ncmenu* n, const ncinput* nc)
  __attribute__ ((nonnull (1, 2)));

// Destroy a menu created with ncmenu_create().
API void ncmenu_destroy(struct ncmenu* n);

// Progress bars. They proceed linearly in any of four directions. The entirety
// of the plane will be used -- any border should be provided by the caller on
// another plane. The plane will not be erased; text preloaded into the plane
// will be consumed by the progress indicator. The bar is redrawn for each
// provided progress report (a double between 0 and 1), and can regress with
// lower values. The procession will take place along the longer dimension (at
// the time of each redraw), with the horizontal length scaled by 2 for
// purposes of comparison. I.e. for a plane of 20 rows and 50 columns, the
// progress will be to the right (50 > 40) or left with OPTION_RETROGRADE.

#define NCPROGBAR_OPTION_RETROGRADE        0x0001u // proceed left/down

typedef struct ncprogbar_options {
  uint32_t ulchannel; // upper-left channel. in the context of a progress bar,
  uint32_t urchannel; // "up" is the direction we are progressing towards, and
  uint32_t blchannel; // "bottom" is the direction of origin. for monochromatic
  uint32_t brchannel; // bar, all four channels ought be the same.
  uint64_t flags;
} ncprogbar_options;

// Takes ownership of the ncplane 'n', which will be destroyed by
// ncprogbar_destroy(). The progress bar is initially at 0%.
API ALLOC struct ncprogbar* ncprogbar_create(struct ncplane* n, const ncprogbar_options* opts)
  __attribute__ ((nonnull (1)));

// Return a reference to the ncprogbar's underlying ncplane.
API struct ncplane* ncprogbar_plane(struct ncprogbar* n)
  __attribute__ ((nonnull (1)));

// Set the progress bar's completion, a double 0 <= 'p' <= 1.
API int ncprogbar_set_progress(struct ncprogbar* n, double p)
  __attribute__ ((nonnull (1)));

// Get the progress bar's completion, a double on [0, 1].
API double ncprogbar_progress(const struct ncprogbar* n)
  __attribute__ ((nonnull (1)));

// Destroy the progress bar and its underlying ncplane.
API void ncprogbar_destroy(struct ncprogbar* n);

// Tabbed widgets. The tab list is displayed at the top or at the bottom of the
// plane, and only one tab is visible at a time.

// Display the tab list at the bottom instead of at the top of the plane
#define NCTABBED_OPTION_BOTTOM 0x0001ull

typedef struct nctabbed_options {
  uint64_t selchan; // channel for the selected tab header
  uint64_t hdrchan; // channel for unselected tab headers
  uint64_t sepchan; // channel for the tab separator
  const char* separator;  // separator string (copied by nctabbed_create())
  uint64_t flags;   // bitmask of NCTABBED_OPTION_*
} nctabbed_options;

// Tab content drawing callback. Takes the tab it was associated to, the ncplane
// on which tab content is to be drawn, and the user pointer of the tab.
// It is called during nctabbed_redraw().
typedef void (*tabcb)(struct nctab* t, struct ncplane* ncp, void* curry);

// Creates a new nctabbed widget, associated with the given ncplane 'n', and with
// additional options given in 'opts'. When 'opts' is NULL, it acts as if it were
// called with an all-zero opts. The widget takes ownership of 'n', and destroys
// it when the widget is destroyed. Returns the newly created widget. Returns
// NULL on failure, also destroying 'n'.
API ALLOC struct nctabbed* nctabbed_create(struct ncplane* n, const nctabbed_options* opts)
  __attribute ((nonnull (1)));

// Destroy an nctabbed widget. All memory belonging to 'nt' is deallocated,
// including all tabs and their names. The plane associated with 'nt' is also
// destroyed. Calling this with NULL does nothing.
API void nctabbed_destroy(struct nctabbed* nt);

// Redraw the widget. This calls the tab callback of the currently selected tab
// to draw tab contents, and draws tab headers. The tab content plane is not
// modified by this function, apart from resizing the plane is necessary.
API void nctabbed_redraw(struct nctabbed* nt)
  __attribute__ ((nonnull (1)));

// Make sure the tab header of the currently selected tab is at least partially
// visible. (by rotating tabs until at least one column is displayed)
// Does nothing if there are no tabs.
API void nctabbed_ensure_selected_header_visible(struct nctabbed* nt)
  __attribute__ ((nonnull (1)));

// Returns the currently selected tab, or NULL if there are no tabs.
API struct nctab* nctabbed_selected(struct nctabbed* nt)
  __attribute__ ((nonnull (1)));

// Returns the leftmost tab, or NULL if there are no tabs.
API struct nctab* nctabbed_leftmost(struct nctabbed* nt)
  __attribute__ ((nonnull (1)));

// Returns the number of tabs in the widget.
API int nctabbed_tabcount(struct nctabbed* nt)
  __attribute__ ((nonnull (1)));

// Returns the plane associated to 'nt'.
API struct ncplane* nctabbed_plane(struct nctabbed* nt)
  __attribute__ ((nonnull (1)));

// Returns the tab content plane.
API struct ncplane* nctabbed_content_plane(struct nctabbed* nt)
  __attribute__ ((nonnull (1)));

// Returns the tab callback.
API tabcb nctab_cb(struct nctab* t)
  __attribute__ ((nonnull (1)));

// Returns the tab name. This is not a copy and it should not be stored.
API const char* nctab_name(struct nctab* t)
  __attribute__ ((nonnull (1)));

// Returns the width (in columns) of the tab's name.
API int nctab_name_width(struct nctab* t)
  __attribute__ ((nonnull (1)));

// Returns the tab's user pointer.
API void* nctab_userptr(struct nctab* t)
  __attribute__ ((nonnull (1)));

// Returns the tab to the right of 't'. This does not change which tab is selected.
API struct nctab* nctab_next(struct nctab* t)
  __attribute__ ((nonnull (1)));

// Returns the tab to the left of 't'. This does not change which tab is selected.
API struct nctab* nctab_prev(struct nctab* t)
  __attribute__ ((nonnull (1)));

// Add a new tab to 'nt' with the given tab callback, name, and user pointer.
// If both 'before' and 'after' are NULL, the tab is inserted after the selected
// tab. Otherwise, it gets put after 'after' (if not NULL) and before 'before'
// (if not NULL). If both 'after' and 'before' are given, they must be two
// neighboring tabs (the tab list is circular, so the last tab is immediately
// before the leftmost tab), otherwise the function returns NULL. If 'name' is
// NULL or a string containing illegal characters, the function returns NULL.
// On all other failures the function also returns NULL. If it returns NULL,
// none of the arguments are modified, and the widget state is not altered.
API ALLOC struct nctab* nctabbed_add(struct nctabbed* nt, struct nctab* after,
                                     struct nctab* before, tabcb tcb,
                                     const char* name, void* opaque)
  __attribute__ ((nonnull (1, 5)));

// Remove a tab 't' from 'nt'. Its neighboring tabs become neighbors to each
// other. If 't' if the selected tab, the tab after 't' becomes selected.
// Likewise if 't' is the leftmost tab, the tab after 't' becomes leftmost.
// If 't' is the only tab, there will no more be a selected or leftmost tab,
// until a new tab is added. Returns -1 if 't' is NULL, and 0 otherwise.
API int nctabbed_del(struct nctabbed* nt, struct nctab* t)
  __attribute__ ((nonnull (1)));

// Move 't' after 'after' (if not NULL) and before 'before' (if not NULL).
// If both 'after' and 'before' are NULL, the function returns -1, otherwise
// it returns 0.
API int nctab_move(struct nctabbed* nt, struct nctab* t, struct nctab* after,
                   struct nctab* before)
  __attribute__ ((nonnull (1, 2)));

// Move 't' to the right by one tab, looping around to become leftmost if needed.
API void nctab_move_right(struct nctabbed* nt, struct nctab* t)
  __attribute__ ((nonnull (1, 2)));

// Move 't' to the right by one tab, looping around to become the last tab if needed.
API void nctab_move_left(struct nctabbed* nt, struct nctab* t)
  __attribute__ ((nonnull (1, 2)));

// Rotate the tabs of 'nt' right by 'amt' tabs, or '-amt' tabs left if 'amt' is
// negative. Tabs are rotated only by changing the leftmost tab; the selected tab
// stays the same. If there are no tabs, nothing happens.
API void nctabbed_rotate(struct nctabbed* nt, int amt)
  __attribute__ ((nonnull (1)));

// Select the tab after the currently selected tab, and return the newly selected
// tab. Returns NULL if there are no tabs.
API struct nctab* nctabbed_next(struct nctabbed* nt)
  __attribute__ ((nonnull (1)));

// Select the tab before the currently selected tab, and return the newly selected
// tab. Returns NULL if there are no tabs.
API struct nctab* nctabbed_prev(struct nctabbed* nt)
  __attribute__ ((nonnull (1)));

// Change the selected tab to be 't'. Returns the previously selected tab.
API struct nctab* nctabbed_select(struct nctabbed* nt, struct nctab* t)
  __attribute__ ((nonnull (1, 2)));

// Write the channels for tab headers, the selected tab header, and the separator
// to '*hdrchan', '*selchan', and '*sepchan' respectively.
API void nctabbed_channels(struct nctabbed* nt, uint64_t* RESTRICT hdrchan,
                           uint64_t* RESTRICT selchan, uint64_t* RESTRICT sepchan)
  __attribute__ ((nonnull (1)));

static inline uint64_t
nctabbed_hdrchan(struct nctabbed* nt){
  uint64_t ch;
  nctabbed_channels(nt, &ch, NULL, NULL);
  return ch;
}

static inline uint64_t
nctabbed_selchan(struct nctabbed* nt){
  uint64_t ch;
  nctabbed_channels(nt, NULL, &ch, NULL);
  return ch;
}

static inline uint64_t
nctabbed_sepchan(struct nctabbed* nt){
  uint64_t ch;
  nctabbed_channels(nt, NULL, NULL, &ch);
  return ch;
}

// Returns the tab separator. This is not a copy and it should not be stored.
// This can be NULL, if the separator was set to NULL in ncatbbed_create() or
// nctabbed_set_separator().
API const char* nctabbed_separator(struct nctabbed* nt)
  __attribute__ ((nonnull (1)));

// Returns the tab separator width, or zero if there is no separator.
API int nctabbed_separator_width(struct nctabbed* nt)
  __attribute__ ((nonnull (1)));

// Set the tab headers channel for 'nt'.
API void nctabbed_set_hdrchan(struct nctabbed* nt, uint64_t chan)
  __attribute__ ((nonnull (1)));

// Set the selected tab header channel for 'nt'.
API void nctabbed_set_selchan(struct nctabbed* nt, uint64_t chan)
  __attribute__ ((nonnull (1)));

// Set the tab separator channel for 'nt'.
API void nctabbed_set_sepchan(struct nctabbed* nt, uint64_t chan)
  __attribute__ ((nonnull (1)));

// Set the tab callback function for 't'. Returns the previous tab callback.
API tabcb nctab_set_cb(struct nctab* t, tabcb newcb)
  __attribute__ ((nonnull (1)));

// Change the name of 't'. Returns -1 if 'newname' is NULL, and 0 otherwise.
API int nctab_set_name(struct nctab* t, const char* newname)
  __attribute__ ((nonnull (1, 2)));

// Set the user pointer of 't'. Returns the previous user pointer.
API void* nctab_set_userptr(struct nctab* t, void* newopaque)
  __attribute__ ((nonnull (1)));

// Change the tab separator for 'nt'. Returns -1 if 'separator' is not NULL and
// is not a valid string, and 0 otherwise.
API int nctabbed_set_separator(struct nctabbed* nt, const char* separator)
  __attribute__ ((nonnull (1, 2)));

// Plots. Given a rectilinear area, an ncplot can graph samples along some axis.
// There is some underlying independent variable--this could be e.g. measurement
// sequence number, or measurement time. Samples are tagged with this variable, which
// should never fall, but may grow non-monotonically. The desired range in terms
// of the underlying independent variable is provided at creation time. The
// desired domain can be specified, or can be autosolved. Granularity of the
// dependent variable depends on glyph selection.
//
// For instance, perhaps we're sampling load as a time series. We want to
// display an hour's worth of samples in 40 columns and 5 rows. We define the
// x-axis to be the independent variable, time. We'll stamp at second
// granularity. In this case, there are 60 * 60 == 3600 total elements in the
// range. Each column will thus cover a 90s span. Using vertical blocks (the
// most granular glyph), we have 8 * 5 == 40 levels of domain. If we report the
// following samples, starting at 0, using autosolving, we will observe:
//
// 60   -- 1%       |domain:   1--1, 0: 20 levels
// 120  -- 50%      |domain:  1--50, 0: 0 levels, 1: 40 levels
// 180  -- 50%      |domain:  1--50, 0: 0 levels, 1: 40 levels, 2: 40 levels
// 240  -- 100%     |domain:  1--75, 0: 1, 1: 27, 2: 40
// 271  -- 100%     |domain: 1--100, 0: 0, 1: 20, 2: 30, 3: 40
// 300  -- 25%      |domain:  1--75, 0: 0, 1: 27, 2: 40, 3: 33
//
// At the end, we have data in 4 90s spans: [0--89], [90--179], [180--269], and
// [270--359]. The first two spans have one sample each, while the second two
// have two samples each. Samples within a span are averaged (FIXME we could
// probably do better), so the results are 0, 50, 75, and 62.5. Scaling each of
// these out of 90 and multiplying by 40 gets our resulting levels. The final
// domain is 75 rather than 100 due to the averaging of 100+25/2->62.5 in the
// third span, at which point the maximum span value is once again 75.
//
// The 20 levels at first is a special case. When the domain is only 1 unit,
// and autoscaling is in play, assign 50%.
//
// This options structure works for both the ncuplot (uint64_t) and ncdplot
// (double) types.
#define NCPLOT_OPTION_LABELTICKSD   0x0001u // show labels for dependent axis
#define NCPLOT_OPTION_EXPONENTIALD  0x0002u // exponential dependent axis
#define NCPLOT_OPTION_VERTICALI     0x0004u // independent axis is vertical
#define NCPLOT_OPTION_NODEGRADE     0x0008u // fail rather than degrade blitter
#define NCPLOT_OPTION_DETECTMAXONLY 0x0010u // use domain detection only for max
#define NCPLOT_OPTION_PRINTSAMPLE   0x0020u // print the most recent sample

typedef struct ncplot_options {
  // channels for the maximum and minimum levels. linear or exponential
  // interpolation will be applied across the domain between these two.
  uint64_t maxchannels;
  uint64_t minchannels;
  // styling used for the legend, if NCPLOT_OPTION_LABELTICKSD is set
  uint16_t legendstyle;
  // if you don't care, pass NCBLIT_DEFAULT and get NCBLIT_8x1 (assuming
  // UTF8) or NCBLIT_1x1 (in an ASCII environment)
  ncblitter_e gridtype; // number of "pixels" per row x column
  // independent variable can either be a contiguous range, or a finite set
  // of keys. for a time range, say the previous hour sampled with second
  // resolution, the independent variable would be the range [0..3600): 3600.
  // if rangex is 0, it is dynamically set to the number of columns.
  int rangex;
  const char* title;   // optional, printed by the labels
  uint64_t flags;      // bitfield over NCPLOT_OPTION_*
} ncplot_options;

// Use the provided plane 'n' for plotting according to the options 'opts'. The
// plot will make free use of the entirety of the plane. For domain
// autodiscovery, set miny == maxy == 0. ncuplot holds uint64_ts, while
// ncdplot holds doubles.
API ALLOC struct ncuplot* ncuplot_create(struct ncplane* n, const ncplot_options* opts,
                                         uint64_t miny, uint64_t maxy)
  __attribute__ ((nonnull (1)));

API ALLOC struct ncdplot* ncdplot_create(struct ncplane* n, const ncplot_options* opts,
                                         double miny, double maxy)
  __attribute__ ((nonnull (1)));

// Return a reference to the ncplot's underlying ncplane.
API struct ncplane* ncuplot_plane(struct ncuplot* n)
  __attribute__ ((nonnull (1)));

API struct ncplane* ncdplot_plane(struct ncdplot* n)
  __attribute__ ((nonnull (1)));

// Add to or set the value corresponding to this x. If x is beyond the current
// x window, the x window is advanced to include x, and values passing beyond
// the window are lost. The first call will place the initial window. The plot
// will be redrawn, but notcurses_render() is not called.
API int ncuplot_add_sample(struct ncuplot* n, uint64_t x, uint64_t y)
  __attribute__ ((nonnull (1)));
API int ncdplot_add_sample(struct ncdplot* n, uint64_t x, double y)
  __attribute__ ((nonnull (1)));
API int ncuplot_set_sample(struct ncuplot* n, uint64_t x, uint64_t y)
  __attribute__ ((nonnull (1)));
API int ncdplot_set_sample(struct ncdplot* n, uint64_t x, double y)
  __attribute__ ((nonnull (1)));

API int ncuplot_sample(const struct ncuplot* n, uint64_t x, uint64_t* y)
  __attribute__ ((nonnull (1)));
API int ncdplot_sample(const struct ncdplot* n, uint64_t x, double* y)
  __attribute__ ((nonnull (1)));

API void ncuplot_destroy(struct ncuplot* n);
API void ncdplot_destroy(struct ncdplot* n);

typedef int(*ncfdplane_callback)(struct ncfdplane* n, const void* buf, size_t s, void* curry);
typedef int(*ncfdplane_done_cb)(struct ncfdplane* n, int fderrno, void* curry);

// read from an fd until EOF (or beyond, if follow is set), invoking the user's
// callback each time. runs in its own context. on EOF or error, the finalizer
// callback will be invoked, and the user ought destroy the ncfdplane. the
// data is *not* guaranteed to be nul-terminated, and may contain arbitrary
// zeroes.
typedef struct ncfdplane_options {
  void* curry;    // parameter provided to callbacks
  bool follow;    // keep reading after hitting end? (think tail -f)
  uint64_t flags; // bitfield over NCOPTION_FDPLANE_*
} ncfdplane_options;

// Create an ncfdplane around the fd 'fd'. Consider this function to take
// ownership of the file descriptor, which will be closed in ncfdplane_destroy().
API ALLOC struct ncfdplane* ncfdplane_create(struct ncplane* n, const ncfdplane_options* opts,
                                             int fd, ncfdplane_callback cbfxn, ncfdplane_done_cb donecbfxn)
  __attribute__ ((nonnull (1)));

API struct ncplane* ncfdplane_plane(struct ncfdplane* n)
  __attribute__ ((nonnull (1)));

API int ncfdplane_destroy(struct ncfdplane* n);

typedef struct ncsubproc_options {
  void* curry;
  uint64_t restart_period; // restart this many seconds after an exit (watch)
  uint64_t flags;          // bitfield over NCOPTION_SUBPROC_*
} ncsubproc_options;

// see exec(2). p-types use $PATH. e-type passes environment vars.
API ALLOC struct ncsubproc* ncsubproc_createv(struct ncplane* n, const ncsubproc_options* opts,
                                              const char* bin, const char* const arg[],
                                              ncfdplane_callback cbfxn, ncfdplane_done_cb donecbfxn)
  __attribute__ ((nonnull (1)));

API ALLOC struct ncsubproc* ncsubproc_createvp(struct ncplane* n, const ncsubproc_options* opts,
                                               const char* bin, const char* const arg[],
                                               ncfdplane_callback cbfxn, ncfdplane_done_cb donecbfxn)
  __attribute__ ((nonnull (1)));

API ALLOC struct ncsubproc* ncsubproc_createvpe(struct ncplane* n, const ncsubproc_options* opts,
                                                const char* bin, const char* const arg[],
                                                const char* const env[],
                                                ncfdplane_callback cbfxn, ncfdplane_done_cb donecbfxn)
  __attribute__ ((nonnull (1)));

API struct ncplane* ncsubproc_plane(struct ncsubproc* n)
  __attribute__ ((nonnull (1)));

API int ncsubproc_destroy(struct ncsubproc* n);

// Draw a QR code at the current position on the plane. If there is insufficient
// room to draw the code here, or there is any other error, non-zero will be
// returned. Otherwise, the QR code "version" (size) is returned. The QR code
// is (version * 4 + 17) columns wide, and ⌈version * 4 + 17⌉ rows tall (the
// properly-scaled values are written back to '*ymax' and '*xmax').
// NCBLIT_2x1 is always used, and the call will fail if it is not available,
// as only this blitter can generate a proper aspect ratio.
API int ncplane_qrcode(struct ncplane* n, unsigned* ymax, unsigned* xmax,
                       const void* data, size_t len)
  __attribute__ ((nonnull (1, 4)));

// Enable horizontal scrolling. Virtual lines can then grow arbitrarily long.
#define NCREADER_OPTION_HORSCROLL 0x0001ull
// Enable vertical scrolling. You can then use arbitrarily many virtual lines.
#define NCREADER_OPTION_VERSCROLL 0x0002ull
// Disable all editing shortcuts. By default, emacs-style keys are available.
#define NCREADER_OPTION_NOCMDKEYS 0x0004ull
// Make the terminal cursor visible across the lifetime of the ncreader, and
// have the ncreader manage the cursor's placement.
#define NCREADER_OPTION_CURSOR    0x0008ull

typedef struct ncreader_options {
  uint64_t tchannels; // channels used for input
  uint32_t tattrword; // attributes used for input
  uint64_t flags;     // bitfield of NCREADER_OPTION_*
} ncreader_options;

// ncreaders provide freeform input in a (possibly multiline) region, supporting
// optional readline keybindings. takes ownership of 'n', destroying it on any
// error (ncreader_destroy() otherwise destroys the ncplane).
API ALLOC struct ncreader* ncreader_create(struct ncplane* n, const ncreader_options* opts)
  __attribute__ ((nonnull (1)));

// empty the ncreader of any user input, and home the cursor.
API int ncreader_clear(struct ncreader* n)
  __attribute__ ((nonnull (1)));

API struct ncplane* ncreader_plane(struct ncreader* n)
  __attribute__ ((nonnull (1)));

// Offer the input to the ncreader. If it's relevant, this function returns
// true, and the input ought not be processed further. Almost all inputs
// are relevant to an ncreader, save synthesized ones.
API bool ncreader_offer_input(struct ncreader* n, const ncinput* ni)
  __attribute__ ((nonnull (1, 2)));

// Atttempt to move in the specified direction. Returns 0 if a move was
// successfully executed, -1 otherwise. Scrolling is taken into account.
API int ncreader_move_left(struct ncreader* n)
  __attribute__ ((nonnull (1)));
API int ncreader_move_right(struct ncreader* n)
  __attribute__ ((nonnull (1)));
API int ncreader_move_up(struct ncreader* n)
  __attribute__ ((nonnull (1)));
API int ncreader_move_down(struct ncreader* n)
  __attribute__ ((nonnull (1)));

// Destructively write the provided EGC to the current cursor location. Move
// the cursor as necessary, scrolling if applicable.
API int ncreader_write_egc(struct ncreader* n, const char* egc)
  __attribute__ ((nonnull (1, 2)));

// return a heap-allocated copy of the current (UTF-8) contents.
API char* ncreader_contents(const struct ncreader* n)
  __attribute__ ((nonnull (1)));

// destroy the reader and its bound plane. if 'contents' is not NULL, the
// UTF-8 input will be heap-duplicated and written to 'contents'.
API void ncreader_destroy(struct ncreader* n, char** contents);

// Returns a heap-allocated copy of the user name under which we are running.
API ALLOC char* notcurses_accountname(void);

// Returns a heap-allocated copy of the local host name.
API ALLOC char* notcurses_hostname(void);

#if 0
// Returns a heap-allocated copy of human-readable OS name and version.
API ALLOC char* notcurses_osversion(void);
#endif

// Dump selected Notcurses state to the supplied 'debugfp'. Output is freeform,
// newline-delimited, and subject to change. It includes geometry of all
// planes, from all piles. No line has more than 80 columns' worth of output.
API void notcurses_debug(const struct notcurses* nc, FILE* debugfp)
  __attribute__ ((nonnull (1, 2)));

#undef API
#undef ALLOC

#ifdef __cplusplus
} // extern "C"
#endif

#endif
