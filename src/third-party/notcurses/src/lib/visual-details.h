#ifndef NOTCURSES_VISUAL_DETAILS
#define NOTCURSES_VISUAL_DETAILS

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "builddef.h"

struct blitset;
struct ncplane;
struct sprixel;
struct ncvisual_details;

// an ncvisual is essentially just an unpacked RGBA bitmap, created by
// reading media from disk, supplying RGBA pixels directly in memory, or
// synthesizing pixels from a plane.
typedef struct ncvisual {
  struct ncvisual_details* details;// implementation-specific details
  uint32_t* data; // (scaled) RGBA image data, rowstride bytes per row
  unsigned pixx, pixy; // pixel geometry, *not* cell geometry
  // lines are sometimes padded. this many true bytes per row in data.
  unsigned rowstride;
  bool owndata; // we own data iff owndata == true
} ncvisual;

static inline void
ncvisual_set_data(ncvisual* ncv, void* data, bool owned){
//fprintf(stderr, "replacing %p with %p (%u -> %u)\n", ncv->data, data, ncv->owndata, owned);
  if(ncv->owndata){
    if(data != ncv->data){
      free(ncv->data);
    }
  }
  ncv->data = (uint32_t*)data;
  ncv->owndata = owned;
}

// shrink one dimension to retrieve the original aspect ratio
static inline void
scale_visual(const ncvisual* ncv, unsigned* disprows, unsigned* dispcols){
  float xratio = (float)(*dispcols) / ncv->pixx;
  if(xratio * ncv->pixy > *disprows){
    xratio = (float)(*disprows) / ncv->pixy;
  }
  *disprows = xratio * (ncv->pixy);
  *dispcols = xratio * (ncv->pixx);
}

#ifdef __cplusplus
}
#endif

#endif
