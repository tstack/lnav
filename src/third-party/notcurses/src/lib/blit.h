#ifndef NOTCURSES_BLIT
#define NOTCURSES_BLIT

#ifdef __cplusplus
extern "C" {
#endif

struct ncplane;
struct blitterargs;

// scaledy and scaledx are output geometry from scaling; data is output data
// from scaling. we might actually need more pixels due to framing concerns,
// in which case just assume transparent input pixels where needed.
typedef int (*ncblitter)(struct ncplane* n, int linesize, const void* data,
                         int scaledy, int scaledx, const struct blitterargs* bargs);

void set_pixel_blitter(ncblitter blitfxn);

#ifdef __cplusplus
}
#endif

#endif
