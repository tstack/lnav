#include "internal.h"

void ncplane_greyscale(ncplane *n){
  for(unsigned y = 0 ; y < n->leny ; ++y){
    for(unsigned x = 0 ; x < n->lenx ; ++x){
      nccell* c = &n->fb[nfbcellidx(n, y, x)];
      unsigned r, g, b;
      nccell_fg_rgb8(c, &r, &g, &b);
      int gy = rgb_greyscale(r, g, b);
      nccell_set_fg_rgb8(c, gy, gy, gy);
      nccell_bg_rgb8(c, &r, &g, &b);
      gy = rgb_greyscale(r, g, b);
      nccell_set_bg_rgb8(c, gy, gy, gy);
    }
  }
}

// if this is not polyfillable cell, we return 0. if it is, we attempt to fill
// it, then recurse out. return -1 on error, or number of cells filled on
// success. so a return of 0 means there's no work to be done here, and N means
// we did some work here, filling everything we could reach. out-of-plane is 0.
static int
ncplane_polyfill_inner(ncplane* n, unsigned y, unsigned x, const nccell* c, const char* filltarg){
  struct topolyfill* stack = NULL;
  if(create_polyfill_op(y, x, &stack) == NULL){
    return -1;
  }
  int ret = 0;
  struct topolyfill* s;
  do{
    s = stack;
    stack = stack->next;
    y = s->y;
    x = s->x;
    nccell* cur = &n->fb[nfbcellidx(n, y, x)];
    const char* glust = nccell_extended_gcluster(n, cur);
//fprintf(stderr, "checking %d/%d (%s) for [%s]\n", y, x, glust, filltarg);
    if(strcmp(glust, filltarg) == 0){
      ++ret;
      if(nccell_duplicate(n, cur, c) < 0){
        goto err;
      }
//fprintf(stderr, "blooming from %d/%d ret: %d\n", y, x, ret);
      if(y){
        if(create_polyfill_op(y - 1, x, &stack) == NULL){
          goto err;
        }
      }
      if(y + 1 < n->leny){
        if(create_polyfill_op(y + 1, x, &stack) == NULL){
          goto err;
        }
      }
      if(x){
        if(create_polyfill_op(y, x - 1, &stack) == NULL){
          goto err;
        }
      }
      if(x + 1 < n->lenx){
        if(create_polyfill_op(y, x + 1, &stack) == NULL){
          goto err;
        }
      }
    }
    free(s);
  }while(stack);
  return ret;

err:
  free(s);
  while(stack){
    struct topolyfill* tmp = stack->next;
    free(stack);
    stack = tmp;
  }
  return -1;
}

// at the initial step only, invalid ystart, xstart is an error, so explicitly check.
int ncplane_polyfill_yx(ncplane* n, int ystart, int xstart, const nccell* c){
  if(ystart < 0){
    if(ystart != -1){
      logerror("invalid y: %d", ystart);
      return -1;
    }
    ystart = n->y;
  }
  unsigned y = ystart;
  if(xstart < 0){
    if(xstart != -1){
      logerror("invalid x: %d", xstart);
      return -1;
    }
    xstart = n->x;
  }
  unsigned x = xstart;
  if(y >= n->leny || x >= n->lenx){
    logerror("invalid start: %u/%u (%u/%u)", y, x, n->leny, n->lenx);
    return -1;
  }
  const nccell* cur = &n->fb[nfbcellidx(n, y, x)];
  const char* targ = nccell_extended_gcluster(n, cur);
  const char* fillegc = nccell_extended_gcluster(n, c);
//fprintf(stderr, "checking %d/%d (%s) for [%s]\n", y, x, targ, fillegc);
  if(strcmp(fillegc, targ) == 0){
    return 0;
  }
  int ret = -1;
  // we need an external copy of this, since we'll be writing to it on
  // the first call into ncplane_polyfill_inner()
  char* targcopy = strdup(targ);
  if(targcopy){
    ret = ncplane_polyfill_inner(n, y, x, c, targcopy);
    free(targcopy);
  }
  return ret;
}

static bool
check_gradient_channel_args(uint32_t ul, uint32_t ur, uint32_t bl, uint32_t br){
  if(ncchannel_default_p(ul) || ncchannel_default_p(ur) ||
     ncchannel_default_p(bl) || ncchannel_default_p(br)){
    if(!(ncchannel_default_p(ul) && ncchannel_default_p(ur) &&
         ncchannel_default_p(bl) && ncchannel_default_p(br))){
      logerror("some (not all) channels were defaults");
      return true;
    }
  }
  if(ncchannel_alpha(ul) != ncchannel_alpha(ur) ||
     ncchannel_alpha(ur) != ncchannel_alpha(bl) ||
     ncchannel_alpha(bl) != ncchannel_alpha(br)){
    logerror("channel alphas didn't match");
    return true;
  }
  if(ncchannel_palindex_p(ul) || ncchannel_palindex_p(bl) ||
     ncchannel_palindex_p(br) || ncchannel_palindex_p(ur)){
    logerror("can't blend palette-indexed color");
    return true;
  }
  return false;
}

// Given the four channels arguments, verify that:
//
// - if any is default foreground, all are default foreground
// - if any is default background, all are default background
// - all foregrounds must have the same alpha
// - all backgrounds must have the same alpha
// - palette-indexed color must not be used
bool check_gradient_args(uint64_t ul, uint64_t ur, uint64_t bl, uint64_t br){
  if(check_gradient_channel_args(ncchannels_fchannel(ul), ncchannels_fchannel(ur),
                                 ncchannels_fchannel(bl), ncchannels_fchannel(br))){
    return true;
  }
  if(check_gradient_channel_args(ncchannels_bchannel(ul), ncchannels_bchannel(ur),
                                 ncchannels_bchannel(bl), ncchannels_bchannel(br))){
    return true;
  }
  return false;
}

// calculate both channels of a gradient at a particular point, knowing that
// we're using double halfblocks, into `c`->channels.
static inline void
calc_highgradient(nccell* c, uint32_t ul, uint32_t ur, uint32_t ll,
                  uint32_t lr, unsigned y, unsigned x,
                  unsigned ylen, unsigned xlen){
  if(!ncchannel_default_p(ul)){
    cell_set_fchannel(c, calc_gradient_channel(ul, ur, ll, lr,
                                               y * 2, x, ylen, xlen));
    cell_set_bchannel(c, calc_gradient_channel(ul, ur, ll, lr,
                                               y * 2 + 1, x, ylen, xlen));
  }else{
    nccell_set_fg_default(c);
    nccell_set_bg_default(c);
  }
}

int ncplane_gradient2x1(ncplane* n, int y, int x, unsigned ylen, unsigned xlen,
                        uint32_t ul, uint32_t ur, uint32_t ll, uint32_t lr){
  if(!notcurses_canutf8(ncplane_notcurses(n))){
    logerror("highdef gradients require utf8");
    return -1;
  }
  if(check_gradient_channel_args(ul, ur, ll, lr)){
    return -1;
  }
  unsigned ystart, xstart;
  if(check_geometry_args(n, y, x, &ylen, &xlen, &ystart, &xstart)){
    return -1;
  }
  if(xlen == 1){
    if(ul != ur || ll != lr){
      logerror("horizontal channel variation in single column");
      return -1;
    }
  }
  int total = 0;
  for(unsigned yy = ystart ; yy < ystart + ylen ; ++yy){
    for(unsigned xx = xstart ; xx < xstart + xlen ; ++xx){
      nccell* targc = ncplane_cell_ref_yx(n, yy, xx);
      targc->channels = 0;
      if(pool_blit_direct(&n->pool, targc, "▀", strlen("▀"), 1) <= 0){
        return -1;
      }
      calc_highgradient(targc, ul, ur, ll, lr, yy - ystart, xx - xstart, ylen * 2, xlen);
      ++total;
    }
  }
  return total;
}

// FIXME remove in abi3
int ncplane_highgradient_sized(ncplane* n, uint32_t ul, uint32_t ur,
                               uint32_t ll, uint32_t lr, int ylen, int xlen){
  if(ylen < 1 || xlen < 1){
    return -1;
  }
  if(!notcurses_canutf8(ncplane_notcurses_const(n))){
    // this works because the uin32_ts we pass in will be promoted to uint64_ts
    // via extension, and the space will employ the background. mwahh!
    return ncplane_gradient(n, -1, -1, ylen, xlen, " ", 0, ul, ur, ll, lr);
  }
  return ncplane_gradient2x1(n, -1, -1, ylen, xlen, ul, ur, ll, lr);
}

int ncplane_gradient(ncplane* n, int y, int x, unsigned ylen, unsigned xlen,
                     const char* egc, uint16_t stylemask,
                     uint64_t ul, uint64_t ur, uint64_t bl, uint64_t br){
  if(check_gradient_args(ul, ur, bl, br)){
    return -1;
  }
  unsigned ystart, xstart;
  if(check_geometry_args(n, y, x, &ylen, &xlen, &ystart, &xstart)){
    return -1;
  }
  if(ylen == 1){
    if(xlen == 1){
      if(ul != ur || ur != br || br != bl){
        logerror("channel variation in 1x1 area");
        return -1;
      }
    }else{
      if(ul != bl || ur != br){
        logerror("vertical channel variation in single row");
        return -1;
      }
    }
  }else if(xlen == 1){
    if(ul != ur || bl != br){
      logerror("horizontal channel variation in single column");
      return -1;
    }
  }
  int total = 0;
  for(unsigned yy = ystart ; yy < ystart + ylen ; ++yy){
    for(unsigned xx = xstart ; xx < xstart + xlen ; ++xx){
      nccell* targc = ncplane_cell_ref_yx(n, yy, xx);
      targc->channels = 0;
      if(nccell_load(n, targc, egc) < 0){
        return -1;
      }
      targc->stylemask = stylemask;
      calc_gradient_channels(&targc->channels, ul, ur, bl, br,
                             yy - ystart, xx - xstart, ylen, xlen);
      ++total;
    }
  }
  return total;
}

int ncplane_stain(ncplane* n, int y, int x, unsigned ylen, unsigned xlen,
                  uint64_t tl, uint64_t tr, uint64_t bl, uint64_t br){
  // Can't use default or palette-indexed colors in a gradient
  if(check_gradient_args(tl, tr, bl, br)){
    return -1;
  }
  unsigned ystart, xstart;
  if(check_geometry_args(n, y, x, &ylen, &xlen, &ystart, &xstart)){
    return -1;
  }
  int total = 0;
  for(unsigned yy = ystart ; yy < ystart + ylen ; ++yy){
    for(unsigned xx = xstart ; xx < xstart + xlen ; ++xx){
      nccell* targc = ncplane_cell_ref_yx(n, yy, xx);
      if(targc->gcluster){
        calc_gradient_channels(&targc->channels, tl, tr, bl, br,
                               yy - ystart, xx - xstart, ylen, xlen);
      }
      ++total;
    }
  }
  return total;
}

int ncplane_format(ncplane* n, int y, int x, unsigned ylen,
                   unsigned xlen, uint16_t stylemask){
  unsigned ystart, xstart;
  if(check_geometry_args(n, y, x, &ylen, &xlen, &ystart, &xstart)){
    return -1;
  }
  int total = 0;
  for(unsigned yy = ystart ; yy < ystart + ylen ; ++yy){
    for(unsigned xx = xstart ; xx < xstart + xlen ; ++xx){
      nccell* targc = ncplane_cell_ref_yx(n, yy, xx);
      targc->stylemask = stylemask;
      ++total;
    }
  }
  return total;
}

// if we're a half block, reverse the channels. if we're a space, set both to
// the background. if we're a full block, set both to the foreground.
static int
rotate_channels(ncplane* src, const nccell* c, uint32_t* fchan, uint32_t* bchan){
  const char* egc = nccell_extended_gcluster(src, c);
  if(egc[0] == ' ' || egc[0] == 0){
    *fchan = *bchan;
    return 0;
  }else if(strcmp(egc, "▄") == 0 || strcmp(egc, "▀") == 0){
    uint32_t tmp = *fchan;
    *fchan = *bchan;
    *bchan = tmp;
    return 0;
  }else if(strcmp(egc, "█") == 0){
    *bchan = *fchan;
    return 0;
  }
  logerror("invalid EGC for rotation [%s]", egc);
  return -1;
}

static int
rotate_output(ncplane* dst, uint32_t tchan, uint32_t bchan){
  dst->channels = ncchannels_combine(tchan, bchan);
  if(tchan != bchan){
    return ncplane_putegc(dst, "▀", NULL);
  }
  if(ncchannel_default_p(tchan) && ncchannel_default_p(bchan)){
    return ncplane_putegc(dst, "", NULL);
  }else if(ncchannel_default_p(tchan)){
    return ncplane_putegc(dst, " ", NULL);
  }
  return ncplane_putegc(dst, "█", NULL);
}

// rotation works at two levels:
//  1) each 1x2 block is rotated into a 1x2 block ala
//      ab   cw    ca   ccw   ab   ccw   bd  ccw   dc  ccw   ca  ccw  ab
//      cd   -->   db   -->   cd   -->   ac  -->   ba  -->   db  -->  cd
//  2) each 1x2 block is rotated into its new location
//
// Characters which can be rotated must be RGB, to differentiate full blocks,
// spaces, and nuls. For clockwise rotations:
//
//  nul: converts to two half defaults
//  space: converts to two half backgrounds
//  full: converts to two half foregrounds
//  upper: converts to half background + half foreground
//  lower: converts to half foreground + half background
//
// Fore/background carry full channel, including transparency.
//
// Ideally, rotation through 360 degrees will restore the original 2x1 squre.
// Unfortunately, the case where a half block occupies a cell having the same
// fore- and background will see it rotated into a single full block. In
// addition, lower blocks eventually become upper blocks with their channels
// reversed. In general:
//
//  if a "row" (the bottom or top halves) are the same forechannel, merge to a
//    single full block of that color (what is its background?).
//  if a "row" is two different channels, they become a upper block (why not
//   lower?) having the two channels as fore- and background.
static int
rotate_2x1_cw(ncplane* src, ncplane* dst, int srcy, int srcx, int dsty, int dstx){
  nccell c1 = NCCELL_TRIVIAL_INITIALIZER;
  nccell c2 = NCCELL_TRIVIAL_INITIALIZER;
  if(ncplane_at_yx_cell(src, srcy, srcx, &c1) < 0){
    return -1;
  }
  if(ncplane_at_yx_cell(src, srcy, srcx + 1, &c2) < 0){
    nccell_release(src, &c1);
    return -1;
  }
  // there can be at most 4 colors and 4 transparencies:
  //  - c1fg, c1bg, c2fg, c2bg, c1ftrans, c2ftrans, c1btrans, c2btrans
  // but not all are necessarily used:
  //  - topleft gets lowerleft. if lowerleft is foreground, c1fg c1ftrans.
  //     otherwise, c1bg c1btrans
  //  - topright gets upperleft. if upperleft is foreground, c1fg c1ftrans.
  //     otherwise, c1bg c1btrans
  //  - botleft get botright. if botright is foreground, c2fg c2ftrans.
  //     otherwise, c2bg c2btrans
  //  - botright gets topright. if topright is foreground, c2fg c2ftrans.
  //     otherwise, c2bg c2btrans
  uint32_t c1b = cell_bchannel_common(&c1);
  uint32_t c2b = cell_bchannel_common(&c2);
  uint32_t c1t = cell_fchannel_common(&c1);
  uint32_t c2t = cell_fchannel_common(&c2);
  int ret = 0;
  ret |= rotate_channels(src, &c1, &c1t, &c1b);
  ret |= rotate_channels(src, &c2, &c2t, &c2b);
  // right char comes from two tops. left char comes from two bottoms. if
  // they're the same channel, they become a:
  //
  //  nul if the channel is default
  //  space if the fore is default
  //  full if the back is default
  ncplane_cursor_move_yx(dst, dsty, dstx);
  rotate_output(dst, c1b, c2b);
  rotate_output(dst, c1t, c2t);
  return ret;
}

static int
rotate_2x1_ccw(ncplane* src, ncplane* dst, int srcy, int srcx, int dsty, int dstx){
  nccell c1 = NCCELL_TRIVIAL_INITIALIZER;
  nccell c2 = NCCELL_TRIVIAL_INITIALIZER;
  if(ncplane_at_yx_cell(src, srcy, srcx, &c1) < 0){
    return -1;
  }
  if(ncplane_at_yx_cell(src, srcy, srcx + 1, &c2) < 0){
    nccell_release(src, &c1);
    return -1;
  }
  uint32_t c1b = cell_bchannel_common(&c1);
  unsigned c2b = cell_bchannel_common(&c2);
  unsigned c1t = cell_fchannel_common(&c1);
  unsigned c2t = cell_fchannel_common(&c2);
  int ret = 0;
  ret |= rotate_channels(src, &c1, &c1t, &c1b);
  ret |= rotate_channels(src, &c2, &c2t, &c2b);
  ncplane_cursor_move_yx(dst, dsty, dstx);
  rotate_output(dst, c1t, c2t);
  rotate_output(dst, c1b, c2b);
  return ret;
}

// copy 'newp' into 'n' after resizing 'n' to match 'newp'
static int
rotate_merge(ncplane* n, ncplane* newp){
  unsigned dimy, dimx;
  ncplane_dim_yx(newp, &dimy, &dimx);
  int ret = ncplane_resize(n, 0, 0, 0, 0, 0, 0, dimy, dimx);
  if(ret == 0){
    for(unsigned y = 0 ; y < dimy ; ++y){
      for(unsigned x = 0 ; x < dimx ; ++x){
        const nccell* src = &newp->fb[fbcellidx(y, dimx, x)];
        nccell* targ = &n->fb[fbcellidx(y, dimx, x)];
        if(cell_duplicate_far(&n->pool, targ, newp, src) < 0){
          return -1;
        }
      }
    }
  }
  return ret;
}

// generate a temporary plane that can hold the contents of n, rotated 90°
static ncplane*
rotate_plane(ncplane* n){
  int absy, absx;
  ncplane_yx(n, &absy, &absx);
  unsigned dimy, dimx;
  ncplane_dim_yx(n, &dimy, &dimx);
  if(dimx % 2 != 0){
    return NULL;
  }
  const int newy = dimx / 2;
  const int newx = dimy * 2;
  struct ncplane_options nopts = {
    .y = absy,
    .x = absx,
    .rows = newy,
    .cols = newx,
    .userptr = n->userptr,
    .name = "copy",
  };
  ncplane* newp = ncplane_create(n, &nopts);
  return newp;
}

int ncplane_rotate_cw(ncplane* n){
  ncplane* newp = rotate_plane(n);
  if(newp == NULL){
    return -1;
  }
  unsigned dimy, dimx;
  ncplane_dim_yx(n, &dimy, &dimx);
  int centy, centx;
  ncplane_center_abs(n, &centy, &centx);
  // the topmost row consists of the leftmost two columns. the rightmost column
  // of the topmost row consists of the top half of the top two leftmost cells.
  // the penultimate column of the topmost row consists of the bottom half of
  // the top two leftmost cells. work from the bottom up on the source, so we
  // can copy to the top row from the left to the right.
  int targx, targy = 0;
  for(unsigned x = 0 ; x < dimx ; x += 2){
    targx = 0;
    for(int y = (int)dimy - 1 ; y >= 0 ; --y){
      if(rotate_2x1_cw(n, newp, y, x, targy, targx)){
        ncplane_destroy(newp);
        return -1;
      }
      targx += 2;
    }
    ++targy;
  }
  int ret = rotate_merge(n, newp);
  ret |= ncplane_destroy(newp);
  return ret;
}

int ncplane_rotate_ccw(ncplane* n){
  ncplane* newp = rotate_plane(n);
  if(newp == NULL){
    return -1;
  }
  unsigned dimy, dimx, targdimy, targdimx;
  ncplane_dim_yx(n, &dimy, &dimx);
  ncplane_dim_yx(newp, &targdimy, &targdimx);
  int x = (int)dimx - 2;
  int y;
  // Each row of the target plane is taken from a column of the source plane.
  // As the target row grows (down), the source column shrinks (moves left).
  for(unsigned targy = 0 ; targy < targdimy ; ++targy){
    y = 0;
    for(unsigned targx = 0 ; targx < targdimx ; targx += 2){
      if(rotate_2x1_ccw(n, newp, y, x, targy, targx)){
        ncplane_destroy(newp);
        return -1;
      }
      ++y;
    }
    x -= 2;
  }
  int ret = rotate_merge(n, newp);
  ret |= ncplane_destroy(newp);
  return ret;
}

#ifdef USE_QRCODEGEN
#include <qrcodegen/qrcodegen.h>
#define QR_BASE_SIZE 17
#define PER_QR_VERSION 4

static inline unsigned
qrcode_rows(unsigned version){
  return QR_BASE_SIZE + (version * PER_QR_VERSION);
}

static inline unsigned
qrcode_cols(unsigned version){
  return QR_BASE_SIZE + (version * PER_QR_VERSION);
}

int ncplane_qrcode(ncplane* n, unsigned* ymax, unsigned* xmax, const void* data, size_t len){
  const ncblitter_e blitfxn = NCBLIT_2x1;
  const int MAX_QR_VERSION = 40; // QR library only supports up to 40
  if(*ymax <= 0 || *xmax <= 0){
    return -1;
  }
  if(len == 0){
    return -1;
  }
  const int starty = n->y;
  const int startx = n->x;
  if(*xmax > n->lenx - startx){
    return -1;
  }
  if(*ymax > n->leny - starty){
    return -1;
  }
  if(*ymax * 2 < qrcode_rows(1)){
    return -1;
  }
  if(*xmax < qrcode_cols(1)){
    return -1;
  }
  const int availsquare = *ymax * 2 < *xmax ? *ymax * 2 : *xmax;
  int roomforver = (availsquare - QR_BASE_SIZE) / PER_QR_VERSION;
  if(roomforver > MAX_QR_VERSION){
    roomforver = MAX_QR_VERSION;
  }
  const size_t bsize = qrcodegen_BUFFER_LEN_FOR_VERSION(roomforver);
  if(bsize < len){
    return -1;
  }
  uint8_t* src = malloc(bsize);
  uint8_t* dst = malloc(bsize);
  if(src == NULL || dst == NULL){
    free(src);
    free(dst);
    return -1;
  }
  unsigned r, g, b;
  // FIXME default might not be all-white
  if(ncplane_fg_default_p(n)){
    r = g = b = 0xff;
  }else{
    ncplane_fg_rgb8(n, &r, &g, &b);
  }
  memcpy(src, data, len);
  int ret = -1;
  int yscale, xscale;
  if(qrcodegen_encodeBinary(src, len, dst, qrcodegen_Ecc_HIGH, 1, roomforver, qrcodegen_Mask_AUTO, true)){
    const int square = qrcodegen_getSize(dst);
    uint32_t* rgba = malloc(square * square * sizeof(uint32_t));
    if(rgba){
      for(int y = starty ; y < starty + square ; ++y){
        for(int x = startx ; x < startx + square ; ++x){
          const bool pixel = qrcodegen_getModule(dst, x, y);
          ncpixel_set_a(&rgba[y * square + x], 0xff);
          ncpixel_set_rgb8(&rgba[y * square + x], r * pixel, g * pixel, b * pixel);
        }
      }
      struct ncvisual* ncv = ncvisual_from_rgba(rgba, square, square * sizeof(uint32_t), square);
      free(rgba);
      if(ncv){
        ret = square;
        // we don't allow degredation because 2x1 is the only blitter which
        // can generate a qrcode with proper aspect ratio. ascii is thus out.
        struct ncvisual_options vopts = {
          .n = n,
          .blitter = blitfxn,
          .flags = NCVISUAL_OPTION_NODEGRADE,
        };
        if(ncvisual_blit(ncplane_notcurses(n), ncv, &vopts) == n){
          ret = square;
        }
        ncvgeom geom;
        ncvisual_geom(ncplane_notcurses(n), NULL, &vopts, &geom);
        yscale = geom.scaley;
        xscale = geom.scalex;
      }
      ncvisual_destroy(ncv);
    }
  }
  free(src);
  free(dst);
  if(ret > 0){
    ret = (ret - QR_BASE_SIZE) / PER_QR_VERSION;
    *ymax = qrcode_rows(ret) / yscale;
    *xmax = qrcode_cols(ret) / xscale;
    return ret;
  }
  return -1;
}
#else
int ncplane_qrcode(ncplane* n, unsigned* ymax, unsigned* xmax, const void* data, size_t len){
  (void)n;
  (void)ymax;
  (void)xmax;
  (void)data;
  (void)len;
  return -1;
}
#endif
