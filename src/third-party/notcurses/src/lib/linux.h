#ifndef NOTCURSES_LINUX
#define NOTCURSES_LINUX

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

struct tinfo;

// is this a linux virtual console?
bool is_linux_console(int fd);

// attempt to reprogram the console font, if necessary, to include all the
// quadrant glyphs (which include the halfblocks). *|halfblocks| will be true
// if the halfblocks are available, whether they required a reprogramming or
// not. *|quadrants| will be true if the quadrants are available, whether that
// required a reprogramming or not.
// note that reprogramming the font drops any existing graphics from the
// framebuffer. if ti has mapped the framebuffer, it will be copied and
// unmapped before we reprogram. after reprogramming, it is remapped, and
// the old contents are copied in, then freed. there will be an unavoidable
// flicker while this happens.
int reprogram_console_font(struct tinfo* ti, unsigned no_font_changes,
                           bool* halfblocks, bool* quadrants);

// if is_linux_console() returned true, call this to determine whether it is
// a drawable framebuffer console. do not call if not a verified console!
bool is_linux_framebuffer(struct tinfo* ti);

// call only on an fd where is_linux_framebuffer() returned true. gets the
// pixel geometry for the visual area.
int get_linux_fb_pixelgeom(struct tinfo* ti, unsigned* ypix, unsigned *xpix);

#ifdef __cplusplus
}
#endif

#endif
