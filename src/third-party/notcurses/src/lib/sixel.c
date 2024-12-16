#include <math.h>
#include <stdatomic.h>
#include "internal.h"
#include "fbuf.h"

#define RGBSIZE 3

// number of worker threads
// FIXME fit to local machine, but more than 3 never seems to help
#define POPULATION 3

// a worker can have up to three qstates enqueued for work
#define WORKERDEPTH 3

// this palette entry is a sentinel for a transparent pixel (and thus caps
// the palette at 65535 other entries).
#define TRANS_PALETTE_ENTRY 65535

// bytes per element in the auxiliary vector
#define AUXVECELEMSIZE 2

// three scaled sixel [0..100x3] components plus a population count.
typedef struct qsample {
  unsigned char comps[RGBSIZE];
  uint32_t pop;
} qsample;

// lowest samples for each node. first-order nodes track 1000 points in
// sixelspace (10x10x10). there are eight possible second-order nodes from a
// fractured first-order node, covering 125 points each (5x5x5).
typedef struct qnode {
  qsample q;
  // cidx plays two roles. during merge, we select the active set, and extract
  // them (since they'll be sorted, we can't operate directly on the octree).
  // here, we use cidx to map back to the initial octree entry, as we need
  // update them (from the active set) at the end of merging. afterwards, the
  // high bit indicates that it was chosen, and the cidx is a valid index into
  // the final color table. it is otherwise a link to the merged qnode.
  // during initial filtering, qlink determines whether a node has fractured:
  // if qlink is non-zero, it is a one-biased index to an onode.
  // FIXME combine these once more, but for now to keep it easy, we have two.
  // qlink links back into the octree.
  uint16_t qlink;
  uint16_t cidx;
} qnode;

// an octree-style node, used for fractured first-order nodes. the first
// bit is whether we're on the top or bottom of the R, then G, then B.
typedef struct onode {
  qnode* q[8];
} onode;

// we set P2 based on whether there is any transparency in the sixel. if not,
// use SIXEL_P2_ALLOPAQUE (0), for faster drawing in certain terminals.
typedef enum {
  SIXEL_P2_ALLOPAQUE = 0,
  SIXEL_P2_TRANS = 1,
} sixel_p2_e;

// data for a single sixelband. a vector of sixel rows, one for each color
// represented within the band. we initially create a vector for every
// possible (quantized) color, and then collapse it.
typedef struct sixelband {
  int size;     // capacity FIXME if same for all, eliminate this
  char** vecs;  // array of vectors, many of which can be NULL
} sixelband;

// across the life of the sixel, we'll need to wipe and restore cells, without
// recourse to the original RGBA data. this is prohibitively expensive to do on
// the encoded data, since it might require expanding or collapsing sections in
// the middle (we could use a rope, but it would be annoying). instead, we keep
// for each sixelrow (i.e. for every 6 rows) a vector of colors and distinct
// encoded sections (i.e. *not* from some common long single allocation). this
// way, the encoded sections can be easily and cheaply changed (since they're
// small, and quickly indexed by sixelrow * color)). whenever we want to emit
// the sixel, we just gather all these dynamic sections and write them
// successively into the fbuf. this table can be built up in parallel, since
// it's isolated among sixelrows -- the sixelrow is then the natural work unit.
// this sixelmap is kept across the life of the sprixel; any longlived state
// must be here, whereas state necessary only for rendering ought be in qstate.
typedef struct sixelmap {
  int colors;
  int sixelbands;
  sixelband* bands;      // |sixelbands| collections of sixel vectors
  sixel_p2_e p2;         // set to SIXEL_P2_TRANS if we have transparent pixels
} sixelmap;

typedef struct qstate {
  int refcount; // initialized to worker count
  atomic_int bandbuilder; // threads take bands as their work unit
  // we always work in terms of quantized colors (quantization is the first
  // step of rendering), using indexes into the derived palette. the actual
  // palette need only be stored during the initial render, since the sixel
  // header can be preserved, and the palette is unchanged by wipes/restores.
  unsigned char* table;  // |colors| x RGBSIZE components
  qnode* qnodes;
  onode* onodes;
  unsigned dynnodes_free;
  unsigned dynnodes_total;
  unsigned onodes_free;
  unsigned onodes_total;
  const struct blitterargs* bargs;
  const uint32_t* data;
  int linesize;
  sixelmap* smap;
  // these are the leny and lenx passed to sixel_blit(), which are likely
  // different from those reachable through bargs->len{y,x}!
  int leny, lenx;
} qstate;

// a work_queue per worker thread. if used == WORKERDEPTH, this thread is
// backed up, and we cannot enqueue to it. writeto wraps around the array.
typedef struct work_queue {
  qstate* qstates[WORKERDEPTH];
  unsigned writeto;
  unsigned used;
  struct sixel_engine* sengine;
} work_queue;

// we keep a few worker threads (POPULATION) spun up to assist with
// quantization. each has an array of up to WORKERDEPTH qstates to work on.
typedef struct sixel_engine {
  pthread_mutex_t lock;
  pthread_cond_t cond;
  work_queue queues[POPULATION];
  pthread_t tids[POPULATION];
  bool done;
} sixel_engine;

// enqueue |qs| to any workers with available space. the number of workers with
// a reference will be stored in |qs|->refcount.
static void
enqueue_to_workers(sixel_engine* eng, qstate* qs){
  if(eng == NULL){
    return;
  }
  int usecount = 0;
  pthread_mutex_lock(&eng->lock);
  for(int i = 0 ; i < POPULATION ; ++i){
    work_queue* wq = &eng->queues[i];
    if(wq->used < WORKERDEPTH){
      wq->qstates[wq->writeto] = qs;
      ++wq->used;
      ++usecount;
      if(++wq->writeto == WORKERDEPTH){
        wq->writeto = 0;
      }
    }
  }
  qs->refcount = usecount;
  pthread_mutex_unlock(&eng->lock);
  if(usecount){
    pthread_cond_broadcast(&eng->cond);
  }
}

// block until all workers have finished up with |qs|
static void
block_on_workers(sixel_engine* eng, qstate* qs){
  if(eng == NULL){
    return;
  }
  pthread_mutex_lock(&eng->lock);
  while(qs->refcount){
    pthread_cond_wait(&eng->cond, &eng->lock);
  }
  pthread_mutex_unlock(&eng->lock);
}

// returns the number of individual sixels necessary to represent the specified
// pixel geometry. these might encompass more pixel rows than |dimy| would
// suggest, up to the next multiple of 6 (i.e. a single row becomes a 6-row
// bitmap; as do two, three, four, five, or six rows). input is scaled geometry.
static inline int
sixelcount(int dimy, int dimx){
  return (dimy + 5) / 6 * dimx;
}

// returns the number of sixel bands (horizontal series of sixels, aka 6 rows)
// for |dimy| source rows. sixels are encoded as a series of sixel bands.
static inline int
sixelbandcount(int dimy){
  return sixelcount(dimy, 1);
}

// whip up a sixelmap sans data for the specified pixel geometry and color
// register count.
static sixelmap*
sixelmap_create(int dimy){
  sixelmap* ret = malloc(sizeof(*ret));
  if(ret){
    ret->p2 = SIXEL_P2_ALLOPAQUE;
    // they'll be initialized by their workers, possibly in parallel
    ret->sixelbands = sixelbandcount(dimy);
    ret->bands = malloc(sizeof(*ret->bands) * ret->sixelbands);
    if(ret->bands == NULL){
      free(ret);
      return NULL;
    }
    for(int i = 0 ; i < ret->sixelbands ; ++i){
      ret->bands[i].size = 0;
    }
    ret->colors = 0;
  }
  return ret;
}

static inline void
sixelband_free(sixelband* s){
  for(int j = 0 ; j < s->size ; ++j){
    free(s->vecs[j]);
  }
  free(s->vecs);
}

void sixelmap_free(sixelmap *s){
  if(s){
    for(int i = 0 ; i < s->sixelbands ; ++i){
      sixelband_free(&s->bands[i]);
    }
    free(s->bands);
    free(s);
  }
}

// convert rgb [0..255] to sixel [0..99]
static inline unsigned
ss(unsigned c){
  unsigned r = round(c * 100.0 / 255); // use real [0..100] scaling
  return r > 99 ? 99: r;
}

// get the keys for an rgb point. the returned value is on [0..999], and maps
// to a static qnode. the second value is on [0..7], and maps within the
// fractured onode (if necessary).
static inline unsigned
qnode_keys(unsigned r, unsigned g, unsigned b, unsigned *skey){
  unsigned ssr = ss(r);
  unsigned ssg = ss(g);
  unsigned ssb = ss(b);
  unsigned ret = ssr / 10 * 100 + ssg / 10 * 10 + ssb / 10;
  *skey = (((ssr % 10) / 5) << 2u) +
          (((ssg % 10) / 5) << 1u) +
          ((ssb % 10) / 5);
//fprintf(stderr, "0x%02x 0x%02x 0x%02x %02u %02u %02u %u %u\n", r, g, b, ssr, ssg, ssb, ret, *skey);
  return ret;
}

// have we been chosen for the color table?
static inline bool
chosen_p(const qnode* q){
  return q->cidx & 0x8000u;
}

static inline unsigned
make_chosen(unsigned cidx){
  return cidx | 0x8000u;
}

// get the cidx without the chosen bit
static inline unsigned
qidx(const qnode* q){
  return q->cidx & ~0x8000u;
}

#define QNODECOUNT 1000

// create+zorch an array of QNODECOUNT qnodes. this is 1000 entries covering
// 1000 sixel colors each (we pretend 100 doesn't exist, working on [0..99],
// heh). in addition, at the end we allocate |colorregs| qnodes, to be used
// dynamically in "fracturing". the original QNODECOUNT nodes are a static
// octree, flattened into an array; the latter are used as an actual octree.
// we must have 8 dynnodes available for every onode we create, or we can run
// into a situation where we don't have an available dynnode
// (see insert_color()).
static qstate*
alloc_qstate(unsigned colorregs){
  qstate* qs = malloc(sizeof(*qs));
  if(qs){
    qs->dynnodes_free = colorregs;
    qs->dynnodes_total = qs->dynnodes_free;
    if((qs->qnodes = malloc((QNODECOUNT + qs->dynnodes_total) * sizeof(qnode))) == NULL){
      free(qs);
      return NULL;
    }
    qs->onodes_free = qs->dynnodes_total / 8;
    qs->onodes_total = qs->onodes_free;
    if((qs->onodes = malloc(qs->onodes_total * sizeof(*qs->onodes))) == NULL){
      free(qs->qnodes);
      free(qs);
      return NULL;
    }
    // don't technically need to clear the components, as we could
    // check the pop, but it's hidden under the compulsory cache misses.
    // we only initialize the static nodes, not the dynamic ones--we know
    // when we pull a dynamic one that it needs its popcount initialized.
    memset(qs->qnodes, 0, sizeof(qnode) * QNODECOUNT);
    qs->table = NULL;
  }
  return qs;
}

// free internals of qstate object
static void
free_qstate(qstate *qs){
  if(qs){
    loginfo("freeing qstate");
    free(qs->qnodes);
    free(qs->onodes);
    free(qs->table);
    free(qs);
  }
}

// insert a color from the source image into the octree.
static inline int
insert_color(qstate* qs, uint32_t pixel){
  const unsigned r = ncpixel_r(pixel);
  const unsigned g = ncpixel_g(pixel);
  const unsigned b = ncpixel_b(pixel);
  unsigned skey;
  const unsigned key = qnode_keys(r, g, b, &skey);
  assert(key < QNODECOUNT);
  assert(skey < 8);
  qnode* q = &qs->qnodes[key];
  if(q->q.pop == 0 && q->qlink == 0){ // previously-unused node
    q->q.comps[0] = r;
    q->q.comps[1] = g;
    q->q.comps[2] = b;
    q->q.pop = 1;
    ++qs->smap->colors;
    return 0;
  }
  onode* o;
  // it's not a fractured node, but it's been used. check to see if we
  // match the secondary key of what's here.
  if(q->qlink == 0){
    unsigned skeynat;
    qnode_keys(q->q.comps[0], q->q.comps[1], q->q.comps[2], &skeynat);
    if(skey == skeynat){
      ++q->q.pop; // pretty good match
      return 0;
    }
    // we want to fracture. if we have no onodes, though, we can't.
    // we also need at least one dynamic qnode. note that this means we might
    // open an onode just to fail to insert our current lookup; that's fine;
    // it's a symmetry between creation and extension.
    if(qs->dynnodes_free == 0 || qs->onodes_free == 0){
//fprintf(stderr, "NO FREE ONES %u\n", key);
      ++q->q.pop; // not a great match, but we're already scattered
      return 0;
    }
    // get the next free onode and zorch it out
    o = qs->onodes + qs->onodes_total - qs->onodes_free;
//fprintf(stderr, "o: %p obase: %p %u\n", o, qs->onodes, qs->onodes_total - qs->onodes_free);
    memset(o, 0, sizeof(*o));
    // get the next free dynnode and assign it to o, account for dnode
    o->q[skeynat] = &qs->qnodes[QNODECOUNT + qs->dynnodes_total - qs->dynnodes_free];
    --qs->dynnodes_free;
    // copy over our own details
    memcpy(o->q[skeynat], q, sizeof(*q));
    // set qlink to one-biased index of the onode, and account for onode
    q->qlink = qs->onodes_total - qs->onodes_free + 1;
    --qs->onodes_free;
    // reset our own population count
    q->q.pop = 0;
  }else{
    // the node has already been fractured
    o = qs->onodes + (q->qlink - 1);
  }
  if(o->q[skey]){
    // our subnode is already present, huzzah. increase its popcount.
    ++o->q[skey]->q.pop;
    return 0;
  }
  // we try otherwise to insert ourselves into o. this requires a free dynnode.
  if(qs->dynnodes_free == 0){
//fprintf(stderr, "NO DYNFREE %u\n", key);
    // this should never happen, because we always ought have 8 dynnodes for
    // every possible onode.
    return -1;
  }
  // get the next free dynnode and assign it to o, account for dnode
  o->q[skey] = &qs->qnodes[QNODECOUNT + qs->dynnodes_total - qs->dynnodes_free];
  --qs->dynnodes_free;
  o->q[skey]->q.pop = 1;
  o->q[skey]->q.comps[0] = r;
  o->q[skey]->q.comps[1] = g;
  o->q[skey]->q.comps[2] = b;
  o->q[skey]->qlink = 0;
  o->q[skey]->cidx = 0;
  ++qs->smap->colors;
//fprintf(stderr, "INSERTED[%u]: %u %u %u\n", key, q->q.comps[0], q->q.comps[1], q->q.comps[2]);
  return 0;
}

// resolve the input color to a color table index following any postprocessing
// of the octree.
static inline int
find_color(const qstate* qs, uint32_t pixel){
  const unsigned r = ncpixel_r(pixel);
  const unsigned g = ncpixel_g(pixel);
  const unsigned b = ncpixel_b(pixel);
  unsigned skey;
  const unsigned key = qnode_keys(r, g, b, &skey);
  const qnode* q = &qs->qnodes[key];
  if(q->qlink && q->q.pop == 0){
    if(qs->onodes[q->qlink - 1].q[skey]){
      q = qs->onodes[q->qlink - 1].q[skey];
    }else{
      logpanic("internal error: no color for 0x%016x", pixel);
      return -1;
    }
  }
  return qidx(q);
}

// the P2 parameter on a sixel specifies how unspecified pixels are drawn.
// if P2 is 1, unspecified pixels are transparent. otherwise, they're drawn
// as something else. some terminals (e.g. foot) can draw more quickly if
// P2 is 0, so we set that when we have no transparent pixels -- i.e. when
// all TAM entries are 0. P2 is at a fixed location in the sixel header.
// obviously, the sixel must already exist.
static inline void
change_p2(char* sixel, sixel_p2_e value){
  sixel[4] = value + '0';
}

static inline void
write_rle(char* vec, int* voff, int rle, int rep){
  if(rle > 2){
    *voff += sprintf(vec + *voff, "!%d", rle);
  }else if(rle == 2){
    vec[(*voff)++] = rep;
  }
  if(rle){
    vec[(*voff)++] = rep;
  }
  vec[*voff] = '\0';
}

// one for each color in the band we're building. |rle| tracks the number of
// consecutive unwritten instances of the current non-0 rep, which is itself
// tracked in |rep|. |wrote| tracks the number of sixels written out for this
// color. whenever we get a new rep (this only happens for non-zero reps),
// we must write any old rle rep, plus any zero-reps since then.
struct band_extender {
  int length; // current length of the vector
  int rle;    // current rep count of non-zero sixel for this color
  int wrote;  // number of sixels we've written out
  int rep;    // representation, 0..63
};

// add the supplied rle section to the appropriate vector, which might
// need to be created. we are writing out [bes->wrote, curx) (i.e. curx
// ought *not* describe the |bes| element, and ought equal |dimx| when
// finalizing the band). caller must update bes->wrote afterwards!
static inline char*
sixelband_extend(char* vec, struct band_extender* bes, int dimx, int curx){
  assert(dimx >= bes->rle);
  assert(0 <= bes->rle);
  assert(0 <= bes->rep);
  assert(64 > bes->rep);
  if(vec == NULL){
    // FIXME for now we make it as big as it could possibly need to be. ps,
    // don't try to just base it off how far in we are; wipe/restore could
    // change that!
    if((vec = malloc(dimx + 1)) == NULL){
      return NULL;
    }
  }
  // rle will equal 0 if this is our first non-zero rep, at a non-zero x;
  // in that case, rep is guaranteed to be 0; catch it at the bottom.
  write_rle(vec, &bes->length, bes->rle, bes->rep + 63);
  int clearlen = curx - (bes->rle + bes->wrote);
  write_rle(vec, &bes->length, clearlen, '?');
  return vec;
}

// write to this cell's auxvec, backing up the pixels cleared by a wipe. wipes
// are requested at cell granularity, broken down into sixelbands, broken down
// by color, and then finally effected at the sixel RLE level. we're thus in
// any given call handling a horizontal contiguous range of sixels for a single
// color. the x range is wholly within the cell to be wiped, but the y range
// might not be, since cells and bands don't necessarily line up. |y| ought be
// the row of the first pixel of the *band*.
//
// we thus need:
//  - the starting and ending true x positions for the *portion of this sixel
//     contained within the wiped cell*.
//  - the true y position at which the sixel starts.
//  - the previous sixel rep and the masked sixel rep--the difference between
//     the two tells us which rows (offset from y) need be written. they ought
//     be the binary forms, not the presentation forms (i.e. [0..63]).
//  - the cell-pixel geometry, necessary for computing offset into the auxvec.
//  - the color.
//
// precondition: mask is a bitwise proper subset of rep
//
// we find which [1..6] of six rows are affected by examining the difference
// between |rep| and |masked|, the sixel's row within the cell by taking |y|
// modulo |cellpxy|, and the position within the auxvec by multiplying that
// result by |cellpxx| and adding |x| modulo |cellpxx|. we set |len| pixels.
static inline void
write_auxvec(uint8_t* auxvec, uint16_t color, int endy, int y, int x, int len,
             char rep, char masked, int cellpxy, int cellpxx){
  rep -= 63;
  masked -= 63;
  const char diff = rep ^ masked;
//fprintf(stderr, "AUXVEC WRITE[%hu] ey: %d y/x: %d/%d:%d r: 0x%x m: 0x%x d: 0x%x total %d\n", color, endy, y, x, len, rep, masked, diff, cellpxy * cellpxx);
  const int xoff = x % cellpxx;
  const int yoff = y % cellpxy;
  int dy = 0;
  for(char bitselector = 1 ; bitselector < 0x40 ; bitselector <<= 1u, ++dy){
    if((diff & bitselector) == 0){
//if(diff == 0x20)fprintf(stderr, "diff: 0x%x bs: %d\n", diff, bitselector);
      continue;
    }
    if(yoff + dy == endy){ // reached the next cell below
//if(diff == 0x20)fprintf(stderr, "BOUNCING! 0x%x bs: %d %d > %d\n", diff, bitselector, yoff + dy, cellpxy);
      break;
    }
//fprintf(stderr, " writing to auxrow %d (%d)\n", yoff + dy, bitselector);
    const int idx = (((yoff + dy) % cellpxy) * cellpxx + xoff) * AUXVECELEMSIZE;
//fprintf(stderr, " xoff: %d yoff: %d dy: %d ydy: %d idx: %d\n", xoff, yoff, dy, yoff + dy, idx);
    for(int i = 0 ; i < len ; ++i){
      memcpy(&auxvec[idx + i * AUXVECELEMSIZE], &color, AUXVECELEMSIZE);
    }
  }
}

// wipe the color within this band from startx to endx - 1, from starty to
// endy - 1 (0-offset in the band, a cell-sized region), writing out the
// auxvec. mask is the allowable sixel, y-wise. returns a positive number if
// pixels were wiped.
static inline int
wipe_color(sixelband* b, int color, int y, int endy, int startx, int endx,
           char mask, int dimx, uint8_t* auxvec,
           int cellpxy, int cellpxx){
  const char* vec = b->vecs[color];
  if(vec == NULL){
    return 0; // no work to be done here
  }
  int wiped = 0;
  char* newvec = malloc(dimx + 1);
  if(newvec == NULL){
    return -1;
  }
//fprintf(stderr, "color: %d Y: %d-%d X: %d-%d\n", color, starty, endy, startx, endx);
//fprintf(stderr, "s/e: %d/%d mask: %02x WIPE: [%s]\n", starty, endy, mask, vec);
  // we decode the color within the sixelband, and rebuild it without the
  // wiped pixels.
  int rle = 0; // the repetition number for this element
  // the x coordinate through which we've checked this band. if x + rle is
  // less than startx, this element cannot be affected by the wipe.
  // otherwise, starting at startx, it can be affected. once x >= endx, we
  // are done, and can copy any remaining elements blindly.
  int x = 0;
  int voff = 0;
  while(*vec){
    if(isdigit(*vec)){
      rle *= 10;
      rle += (*vec - '0');
    }else if(*vec == '!'){
      rle = 0;
    }else{
      if(rle == 0){
        rle = 1;
      }
      char rep = *vec;
      char masked = ((rep - 63) & mask) + 63;
//fprintf(stderr, "X/RLE/ENDX: %d %d %d\n", x, rle, endx);
      if(x + rle <= startx){ // not wiped material; reproduce as-is
        write_rle(newvec, &voff, rle, rep);
        x += rle;
      }else if(masked == rep){ // not changed by wipe; reproduce as-is
        write_rle(newvec, &voff, rle, rep);
        x += rle;
      }else{ // changed by wipe; might have to break it up
        wiped = 1;
        if(x < startx){
          write_rle(newvec, &voff, startx - x, rep);
          rle -= startx - x;
          x = startx;
        }
        if(x + rle >= endx){
          // FIXME this might equal the prev/next rep, and we ought combine
//fprintf(stderr, "************************* %d %d %d\n", endx - x, x, rle);
          write_rle(newvec, &voff, endx - x, masked);
          write_auxvec(auxvec, color, endy, y, x, endx - x, rep, masked, cellpxy, cellpxx);
          rle -= endx - x;
          x = endx;
        }else{
          write_rle(newvec, &voff, rle, masked);
          write_auxvec(auxvec, color, endy, y, x, rle, rep, masked, cellpxy, cellpxx);
          x += rle;
          rle = 0;
        }
        if(rle){
          write_rle(newvec, &voff, rle, rep);
          x += rle;
        }
      }
      rle = 0;
    }
    ++vec;
    if(x >= endx){
      strcpy(newvec + voff, vec); // there is always room
      break;
    }
  }
//if(strcmp(newvec, b->vecs[color])) fprintf(stderr, "WIPED %d y [%d..%d) x [%d..%d) mask: %d [%s]\n", color, starty, endy, startx, endx, mask, newvec);
  free(b->vecs[color]);
  if(voff == 0){
    // FIXME check for other null vectors; free such, and assign NULL
    free(newvec);
    newvec = NULL;
  }
  b->vecs[color] = newvec;
  return wiped;
}

// wipe the band from startx to endx - 1, from starty to endy - 1. returns the
// number of pixels actually wiped.
static inline int
wipe_band(sixelmap* smap, int band, int startx, int endx,
          int starty, int endy, int dimx, int cellpxy, int cellpxx,
          uint8_t* auxvec){
  int wiped = 0;
  // get 0-offset start and end row bounds for our band.
  const int sy = band * 6 < starty ? starty - band * 6 : 0;
  const int ey = (band + 1) * 6 > endy ? 6 - ((band + 1) * 6 - endy) : 6;
  // we've got a mask that we'll AND with the decoded sixels; set it to
  // 0 wherever we're wiping.
  unsigned char mask = 63;
  // knock out a bit for each row we're wiping within the band
  for(int i = 0 ; i < 6 ; ++i){
    if(i >= sy && i < ey){
      mask &= ~(1u << i);
    }
  }
//fprintf(stderr, "******************** BAND %d MASK 0x%x ********************8\n", band, mask);
  sixelband* b = &smap->bands[band];
  // offset into map->data where our color starts
  for(int i = 0 ; i < b->size ; ++i){
    wiped += wipe_color(b, i, band * 6, endy, startx, endx, mask,
                        dimx, auxvec, cellpxy, cellpxx);
  }
  return wiped;
}

// we return -1 because we're not doing a proper wipe -- that's not possible
// using sixel. we just mark it as partially transparent, so that if it's
// redrawn, it's redrawn using P2=1.
int sixel_wipe(sprixel* s, int ycell, int xcell){
//fprintf(stderr, "WIPING %d/%d\n", ycell, xcell);
  uint8_t* auxvec = sixel_trans_auxvec(ncplane_pile(s->n));
  if(auxvec == NULL){
    return -1;
  }
  const int cellpxy = ncplane_pile(s->n)->cellpxy;
  const int cellpxx = ncplane_pile(s->n)->cellpxx;
  sixelmap* smap = s->smap;
  const int startx = xcell * cellpxx;
  const int starty = ycell * cellpxy;
  int endx = ((xcell + 1) * cellpxx);
  if(endx >= s->pixx){
    endx = s->pixx;
  }
  int endy = ((ycell + 1) * cellpxy);
  if(endy >= s->pixy){
    endy = s->pixy;
  }
  const int startband = starty / 6;
  const int endband = (endy - 1) / 6;
//fprintf(stderr, "y/x: %d/%d bands: %d-%d start: %d/%d end: %d/%d\n", ycell, xcell, startband, endband - 1, starty, startx, endy, endx);
  // walk through each color, and wipe the necessary sixels from each band
  int w = 0;
  for(int b = startband ; b <= endband ; ++b){
    w += wipe_band(smap, b, startx, endx, starty, endy, s->pixx,
                   cellpxy, cellpxx, auxvec);
  }
  if(w){
    s->wipes_outstanding = true;
  }
  change_p2(s->glyph.buf, SIXEL_P2_TRANS);
  assert(NULL == s->n->tam[s->dimx * ycell + xcell].auxvector);
  s->n->tam[s->dimx * ycell + xcell].auxvector = auxvec;
  // FIXME this invalidation ought not be necessary, since we're simply
  // wiping, and thus a glyph is going to be printed over whatever we've
  // just destroyed. in alacritty, however, this isn't sufficient to knock
  // out a graphic; we need repaint with the transparency.
  // see https://github.com/dankamongmen/notcurses/issues/2142
  int absx, absy;
  ncplane_abs_yx(s->n, &absy, &absx);
  sprixel_invalidate(s, absy, absx);
  return 0;
}

// rebuilds the auxiliary vectors, and scrubs the actual pixels, following
// extraction of the palette. doing so allows the new frame's pixels to
// contribute to the solved palette, even if they were wiped in the previous
// frame. pixels ought thus have been set up in sixel_blit(), despite TAM
// entries in the ANNIHILATED state.
static int
scrub_color_table(sprixel* s){
  if(s->n && s->n->tam){
    // we use the sprixel cell geometry rather than the plane's because this
    // is called during our initial blit, before we've resized the plane.
    for(unsigned y = 0 ; y < s->dimy ; ++y){
      for(unsigned x = 0 ; x < s->dimx ; ++x){
        unsigned txyidx = y * s->dimx + x;
        sprixcell_e state = s->n->tam[txyidx].state;
        if(state == SPRIXCELL_ANNIHILATED || state == SPRIXCELL_ANNIHILATED_TRANS){
//fprintf(stderr, "POSTEXTRACT WIPE %d/%d\n", y, x);
          sixel_wipe(s, y, x);
        }
      }
    }
  }
  return 0;
}

// goes through the needs_refresh matrix, and damages cells needing refreshing.
void sixel_refresh(const ncpile* p, sprixel* s){
  if(s->needs_refresh == NULL){
    return;
  }
  int absy, absx;
  ncplane_abs_yx(s->n, &absy, &absx);
  for(unsigned y = 0 ; y < s->dimy ; ++y){
    const unsigned yy = absy + y;
    for(unsigned x = 0 ; x < s->dimx ; ++x){
      unsigned idx = y * s->dimx + x;
      if(s->needs_refresh[idx]){
        const unsigned xx = absx + x;
        if(xx < p->dimx && yy < p->dimy){
          unsigned ridx = yy * p->dimx + xx;
          struct crender *r = &p->crender[ridx];
          r->s.damaged = 1;
        }
      }
    }
  }
  free(s->needs_refresh);
  s->needs_refresh = NULL;
}

// when we first cross into a new cell, we check its old state, and if it
// was transparent, set the rmatrix low. otherwise, set it high. this should
// only be called for the first pixel in each cell.
static inline void
update_rmatrix(unsigned char* rmatrix, int txyidx, const tament* tam){
  if(rmatrix == NULL){
    return;
  }
  sprixcell_e state = tam[txyidx].state;
  if(state == SPRIXCELL_TRANSPARENT || state > SPRIXCELL_ANNIHILATED){
    rmatrix[txyidx] = 0;
  }else{
    rmatrix[txyidx] = 1;
  }
}

static int
qnodecmp(const void* q0, const void* q1){
  const qnode* qa = q0;
  const qnode* qb = q1;
  return qa->q.pop < qb->q.pop ? -1 : qa->q.pop == qb->q.pop ? 0 : 1;
}

// from the initial set of QNODECOUNT qnodes, extract the number of active
// ones -- our initial (reduced) color count -- and sort. heap allocation.
// precondition: colors > 0
static qnode*
get_active_set(qstate* qs, uint32_t colors){
  qnode* act = malloc(sizeof(*act) * colors);
  unsigned targidx = 0;
  // filter the initial qnodes for pop != 0
  unsigned total = QNODECOUNT + (qs->dynnodes_total - qs->dynnodes_free);
//fprintf(stderr, "TOTAL IS %u WITH %u COLORS\n", total, colors);
  for(unsigned z = 0 ; z < total && targidx < colors ; ++z){
//fprintf(stderr, "EXTRACT? [%04u] pop %u\n", z, qs->qnodes[z].q.pop);
    if(qs->qnodes[z].q.pop){
      memcpy(&act[targidx], &qs->qnodes[z], sizeof(*act));
      // link it back to the original node's position in the octree
//fprintf(stderr, "LINKING %u to %u\n", targidx, z);
      act[targidx].qlink = z;
      ++targidx;
    }else if(qs->qnodes[z].qlink){
      const struct onode* o = &qs->onodes[qs->qnodes[z].qlink - 1];
      // FIXME i don't think we need the second conditional? in a perfect world?
      for(unsigned s = 0 ; s < 8 && targidx < colors ; ++s){
//fprintf(stderr, "o: %p qlink: %u\n", o, qs->qnodes[z].qlink - 1);
        if(o->q[s]){
          memcpy(&act[targidx], o->q[s], sizeof(*act));
//fprintf(stderr, "O-LINKING %u to %ld[%u]\n", targidx, o->q[s] - qs->qnodes, s);
          act[targidx].qlink = o->q[s] - qs->qnodes;
          ++targidx;
        }
      }
    }
  }
//fprintf(stderr, "targidx: %u colors: %u\n", targidx, colors);
  assert(targidx == colors);
  qsort(act, colors, sizeof(*act), qnodecmp);
  return act;
}

static inline int
find_next_lowest_chosen(const qstate* qs, int z, int i, const qnode** hq){
//fprintf(stderr, "FIRST CHOSEN: %u %d\n", z, i);
  do{
    const qnode* h = &qs->qnodes[z];
//fprintf(stderr, "LOOKING AT %u POP %u QLINK %u CIDX %u\n", z, h->q.pop, h->qlink, h->cidx);
    if(h->q.pop == 0 && h->qlink){
      const onode* o = &qs->onodes[h->qlink - 1];
      while(i >= 0){
        h = o->q[i];
        if(h && chosen_p(h)){
          *hq = h;
//fprintf(stderr, "NEW HQ: %p RET: %u\n", *hq, z * 8 + i);
          return z * 8 + i;
        }
        if(++i == 8){
          break;
        }
      }
    }else{
      if(chosen_p(h)){
        *hq = h;
//fprintf(stderr, "NEW HQ: %p RET: %u\n", *hq, z * 8);
        return z * 8;
      }
    }
    ++z;
    i = 0;
  }while(z < QNODECOUNT);
//fprintf(stderr, "RETURNING -1\n");
  return -1;
}

static inline void
choose(qstate* qs, qnode* q, int z, int i, int* hi, int* lo,
       const qnode** hq, const qnode** lq){
  if(!chosen_p(q)){
//fprintf(stderr, "NOT CHOSEN: %u %u %u %u\n", z, qs->qnodes[z].qlink, qs->qnodes[z].q.pop, qs->qnodes[z].cidx);
    if(z * 8 > *hi){
      *hi = find_next_lowest_chosen(qs, z, i, hq);
    }
    int cur = z * 8 + (i >= 0 ? i : 4);
    if(*lo == -1){
      q->cidx = qidx(*hq);
    }else if(*hi == -1 || cur - *lo < *hi - cur){
      q->cidx = qidx(*lq);
    }else{
      q->cidx = qidx(*hq);
    }
  }else{
    *lq = q;
    *lo = z * 8;
  }
}

// we must reduce the number of colors until we're using less than or equal
// to the number of color registers.
static inline int
merge_color_table(qstate* qs){
  if(qs->smap->colors == 0){
    return 0;
  }
  qnode* qactive = get_active_set(qs, qs->smap->colors);
  if(qactive == NULL){
    return -1;
  }
  // assign color table entries to the most popular colors. use the lowest
  // color table entries for the most popular ones, as they're the shortest
  // (this is not necessarily an optimizing huristic, but it'll do for now).
  int cidx = 0;
//fprintf(stderr, "colors: %u cregs: %u\n", qs->colors, colorregs);
  for(int z = qs->smap->colors - 1 ; z >= 0 ; --z){
    if(qs->smap->colors >= qs->bargs->u.pixel.colorregs){
      if(cidx == qs->bargs->u.pixel.colorregs){
        break; // we just ran out of color registers
      }
    }
    qs->qnodes[qactive[z].qlink].cidx = make_chosen(cidx);
    ++cidx;
  }
  free(qactive);
  if(qs->smap->colors > qs->bargs->u.pixel.colorregs){
    // tend to those which couldn't get a color table entry. we start with two
    // values, lo and hi, initialized to -1. we iterate over the *static* qnodes,
    // descending into onodes to check their qnodes. we thus iterate over all
    // used qnodes, in order (and also unused static qnodes). if the node is
    // empty, continue. if it is chosen, replace lo. otherwise, if hi is less
    // than z, we need find the next lowest chosen one. if there is no next
    // lowest, hi is reset to -1. otherwise, set hi. once we have the new hi > z,
    // determine which of hi and lo are closer to z, discounting -1 values, and
    // link te closer one to z. a toplevel node is worth 8 in terms of distance;
    // and lowlevel node is worth 1.
    int lo = -1;
    int hi = -1;
    const qnode* lq = NULL;
    const qnode* hq = NULL;
    for(int z = 0 ; z < QNODECOUNT ; ++z){
      if(qs->qnodes[z].q.pop == 0){
        if(qs->qnodes[z].qlink == 0){
          continue; // unused
        }
        // process the onode
        const onode* o = &qs->onodes[qs->qnodes[z].qlink - 1];
        for(int i = 0 ; i < 8 ; ++i){
          if(o->q[i]){
            choose(qs, o->q[i], z, i, &hi, &lo, &hq, &lq);
          }
        }
      }else{
        choose(qs, &qs->qnodes[z], z, -1, &hi, &lo, &hq, &lq);
      }
    }
    qs->smap->colors = qs->bargs->u.pixel.colorregs;
  }
  return 0;
}

static inline void
load_color_table(const qstate* qs){
  int loaded = 0;
  int total = QNODECOUNT + (qs->dynnodes_total - qs->dynnodes_free);
  for(int z = 0 ; z < total && loaded < qs->smap->colors ; ++z){
    const qnode* q = &qs->qnodes[z];
    if(chosen_p(q)){
      qs->table[RGBSIZE * qidx(q) + 0] = ss(q->q.comps[0]);
      qs->table[RGBSIZE * qidx(q) + 1] = ss(q->q.comps[1]);
      qs->table[RGBSIZE * qidx(q) + 2] = ss(q->q.comps[2]);
      ++loaded;
    }
  }
//fprintf(stderr, "loaded: %u colors: %u\n", loaded, qs->colors);
  assert(loaded == qs->smap->colors);
}

// build up a sixel band from (up to) 6 rows of the source RGBA.
static inline int
build_sixel_band(qstate* qs, int bnum){
//fprintf(stderr, "building band %d\n", bnum);
  sixelband* b = &qs->smap->bands[bnum];
  b->size = qs->smap->colors;
  size_t bsize = sizeof(*b->vecs) * b->size;
  size_t mlen = qs->smap->colors * sizeof(struct band_extender);
  struct band_extender* meta = malloc(mlen);
  if(meta == NULL){
    return -1;
  }
  b->vecs = malloc(bsize);
  if(b->vecs == NULL){
    free(meta);
    return -1;
  }
  memset(b->vecs, 0, bsize);
  memset(meta, 0, mlen);
  const int ystart = qs->bargs->begy + bnum * 6;
  const int endy = (bnum + 1 == qs->smap->sixelbands ?
                                 qs->leny - qs->bargs->begy : ystart + 6);
  struct {
    int color; // 0..colormax
    int rep;   // non-zero representation, 1..63
  } active[6];
  // we're going to advance horizontally through the sixelband
  int x;
  // FIXME we could greatly clean this up by tracking, for each color, the active
  // rep and the number of times we've seen it...but only write it out either (a)
  // when the rep changes (b) when we get the color again after a gap or (c) at the
  // end. that way we wouldn't need maintain these prevactive/active sets...
  for(x = qs->bargs->begx ; x < (qs->bargs->begx + qs->lenx) ; ++x){ // pixel column
    // there are at most 6 colors represented in any given sixel. at each
    // sixel, we need to *start tracking* new colors, and colors which changed
    // their representation. we also write out what we previously tracked for
    // this color: possibly a non-zero rep, possibly followed by a zero-rep (we
    // can have zero, either, or both).
    int activepos = 0; // number of active entries used
    for(int y = ystart ; y < endy ; ++y){
      const uint32_t* rgb = (qs->data + (qs->linesize / 4 * y) + x);
      if(rgba_trans_p(*rgb, qs->bargs->transcolor)){
        continue;
      }
      int cidx = find_color(qs, *rgb);
      if(cidx < 0){
        free(meta);
        return -1;
      }
      int act;
      for(act = 0 ; act < activepos ; ++act){
        if(active[act].color == cidx){
          active[act].rep |= (1u << (y - ystart));
          break;
        }
      }
      if(act == activepos){ // didn't find it; create new entry
        active[activepos].color = cidx;
        active[activepos].rep = (1u << (y - ystart));
        ++activepos;
      }
    }
    // we now have the active set. check to see if they extend existing RLEs,
    // and if not, write out whatever came before us.
    for(int i = 0 ; i < activepos ; ++i){
      const int c = active[i].color;
      if(meta[c].rep == active[i].rep && meta[c].rle + meta[c].wrote == x){
        ++meta[c].rle;
      }else{
        b->vecs[c] = sixelband_extend(b->vecs[c], &meta[c], qs->lenx, x);
        if(b->vecs[c] == NULL){
          free(meta);
          return -1;
        }
        meta[c].rle = 1;
        meta[c].wrote = x;
        meta[c].rep = active[i].rep;
      }
    }
  }
  for(int i = 0 ; i < qs->smap->colors ; ++i){
    if(meta[i].rle){ // color was wholly unused iff rle == 0 at end
      b->vecs[i] = sixelband_extend(b->vecs[i], &meta[i], qs->lenx, x);
      if(b->vecs[i] == NULL){
        free(meta);
        return -1;
      }
    }else{
      b->vecs[i] = NULL;
    }
  }
  free(meta);
  return 0;
}

static int
bandworker(qstate* qs){
  int b;
  while((b = qs->bandbuilder++) < qs->smap->sixelbands){
    if(build_sixel_band(qs, b) < 0){
      return -1;
    }
  }
  return 0;
}

// we have converged upon some number of colors. we now run over the pixels
// once again, and get the actual (color-indexed) sixels.
static inline int
build_data_table(sixel_engine* sengine, qstate* qs){
  sixelmap* smap = qs->smap;
  if(smap->sixelbands == 0){
    logerror("no sixels");
    return -1;
  }
  qs->bandbuilder = 0;
  enqueue_to_workers(sengine, qs);
  size_t tsize = RGBSIZE * smap->colors;
  qs->table = malloc(tsize);
  if(qs->table == NULL){
    return -1;
  }
  load_color_table(qs);
  bandworker(qs);
  block_on_workers(sengine, qs);
  return 0;
}

static inline int
extract_cell_color_table(qstate* qs, long cellid){
  const int ccols = qs->bargs->u.pixel.spx->dimx;
  const long x = cellid % ccols;
  const long y = cellid / ccols;
  const int cdimy = qs->bargs->u.pixel.cellpxy;
  const int cdimx = qs->bargs->u.pixel.cellpxx;
  const int begy = qs->bargs->begy;
  const int begx = qs->bargs->begx;
  const int leny = qs->leny;
  const int lenx = qs->lenx;
  const int cstartx = begx + x * cdimx; // starting pixel col for cell
  const int cstarty = begy + y * cdimy; // starting pixel row for cell
  typeof(qs->bargs->u.pixel.spx->needs_refresh) rmatrix = qs->bargs->u.pixel.spx->needs_refresh;
  tament* tam = qs->bargs->u.pixel.spx->n->tam;
  int cendy = cstarty + cdimy;    // one past last pixel row for cell
  if(cendy > begy + leny){
    cendy = begy + leny;
  }
  int cendx = cstartx + cdimx;      // one past last pixel col for cell
  if(cendx > begx + lenx){
    cendx = begx + lenx;
  }
  // we initialize the TAM entry based on the first pixel. if it's transparent,
  // initialize as transparent, and otherwise as opaque. following that, any
  // transparent pixel takes opaque to mixed, and any filled pixel takes
  // transparent to mixed.
  if(cstarty >= cendy){ // we're entirely transparent sixel overhead
    tam[cellid].state = SPRIXCELL_TRANSPARENT;
    qs->smap->p2 = SIXEL_P2_TRANS; // even one forces P2=1
    // FIXME need we set rmatrix?
    return 0;
  }
  const uint32_t* rgb = (qs->data + (qs->linesize / 4 * cstarty) + cstartx);
  if(tam[cellid].state == SPRIXCELL_ANNIHILATED || tam[cellid].state == SPRIXCELL_ANNIHILATED_TRANS){
    if(rgba_trans_p(*rgb, qs->bargs->transcolor)){
      update_rmatrix(rmatrix, cellid, tam);
      tam[cellid].state = SPRIXCELL_ANNIHILATED_TRANS;
      free(tam[cellid].auxvector);
      tam[cellid].auxvector = NULL;
    }else{
      update_rmatrix(rmatrix, cellid, tam);
      free(tam[cellid].auxvector);
      tam[cellid].auxvector = NULL;
    }
  }else{
    if(rgba_trans_p(*rgb, qs->bargs->transcolor)){
      update_rmatrix(rmatrix, cellid, tam);
      tam[cellid].state = SPRIXCELL_TRANSPARENT;
    }else{
      update_rmatrix(rmatrix, cellid, tam);
      tam[cellid].state = SPRIXCELL_OPAQUE_SIXEL;
    }
  }
  for(int visy = cstarty ; visy < cendy ; ++visy){   // current abs pixel row
    for(int visx = cstartx ; visx < cendx ; ++visx){ // current abs pixel col
      rgb = (qs->data + (qs->linesize / 4 * visy) + visx);
      // we do *not* exempt already-wiped pixels from palette creation. once
      // we're done, we'll call sixel_wipe() on these cells. so they remain
      // one of SPRIXCELL_ANNIHILATED or SPRIXCELL_ANNIHILATED_TRANS.
      // intentional bitwise or, to avoid dependency
      if(tam[cellid].state != SPRIXCELL_ANNIHILATED){
        if(tam[cellid].state == SPRIXCELL_ANNIHILATED_TRANS){
          if(!rgba_trans_p(*rgb, qs->bargs->transcolor)){
            tam[cellid].state = SPRIXCELL_ANNIHILATED;
          }
        }else{
          if(rgba_trans_p(*rgb, qs->bargs->transcolor)){
            if(tam[cellid].state == SPRIXCELL_OPAQUE_SIXEL){
              tam[cellid].state = SPRIXCELL_MIXED_SIXEL;
            }
          }else{
            if(tam[cellid].state == SPRIXCELL_TRANSPARENT){
              tam[cellid].state = SPRIXCELL_MIXED_SIXEL;
            }
          }
        }
      }
//fprintf(stderr, "vis: %d/%d\n", visy, visx);
      if(rgba_trans_p(*rgb, qs->bargs->transcolor)){
        continue;
      }
      if(insert_color(qs, *rgb)){
        return -1;
      }
    }
  }
  // if we're opaque, we needn't clear the old cell with a glyph
  if(tam[cellid].state == SPRIXCELL_OPAQUE_SIXEL){
    rmatrix[cellid] = 0;
  }else{
    qs->smap->p2 = SIXEL_P2_TRANS; // even one forces P2=1
  }
  return 0;
}

// we have a 4096-element array that takes the 4-5-3 MSBs from the RGB
// components. once it's complete, we might need to either merge some
// chunks, or expand them, converging towards the available number of
// color registers. |ccols| is cell geometry; |leny| and |lenx| are pixel
// geometry, and *do not* include sixel padding.
static int
extract_color_table(sixel_engine* sengine, qstate* qs){
  const blitterargs* bargs = qs->bargs;
  // use the cell geometry as computed by the visual layer; leny doesn't
  // include any mandatory sixel padding.
  const int crows = bargs->u.pixel.spx->dimy;
  const int ccols = bargs->u.pixel.spx->dimx;
  typeof(bargs->u.pixel.spx->needs_refresh) rmatrix;
  rmatrix = malloc(sizeof(*rmatrix) * crows * ccols);
  if(rmatrix == NULL){
    return -1;
  }
  bargs->u.pixel.spx->needs_refresh = rmatrix;
  long cellid = 0;
  for(int y = 0 ; y < crows ; ++y){ // cell row
    for(int x = 0 ; x < ccols ; ++x){ // cell column
      if(extract_cell_color_table(qs, cellid)){
        return -1;
      }
      ++cellid;
    }
  }
  loginfo("octree got %"PRIu32" entries", qs->smap->colors);
  if(merge_color_table(qs)){
    return -1;
  }
  if(build_data_table(sengine, qs)){
    return -1;
  }
  loginfo("final palette: %u/%u colors", qs->smap->colors, qs->bargs->u.pixel.colorregs);
  return 0;
}

static inline int
write_sixel_intro(fbuf* f, sixel_p2_e p2, int leny, int lenx){
  int rr, r = fbuf_puts(f, "\x1bP0;");
  if(r < 0){
    return -1;
  }
  if((rr = fbuf_putint(f, p2)) < 0){
    return -1;
  }
  r += rr;
  if((rr = fbuf_puts(f, ";0q\"1;1;")) < 0){
    return -1;
  }
  r += rr;
  if((rr = fbuf_putint(f, lenx)) < 0){
    return -1;
  }
  r += rr;
  if(fbuf_putc(f, ';') != 1){
    return -1;
  }
  ++r;
  if((rr = fbuf_putint(f, leny)) < 0){
    return -1;
  }
  r += rr;
  return r;
}

// write a single color register. rc/gc/bc are on [0..100].
static inline int
write_sixel_creg(fbuf* f, int idx, int rc, int gc, int bc){
  int rr, r = 0;
  if(fbuf_putc(f, '#') != 1){
    return -1;
  }
  ++r;
  if((rr = fbuf_putint(f, idx)) < 0){
    return -1;
  }
  r += rr;
  if((rr = fbuf_puts(f, ";2;")) < 0){
    return -1;
  }
  r += rr;
  if((rr = fbuf_putint(f, rc)) < 0){
    return -1;
  }
  r += rr;
  if(fbuf_putc(f, ';') != 1){
    return -1;
  }
  ++r;
  if((rr = fbuf_putint(f, gc)) < 0){
    return -1;
  }
  r += rr;
  if(fbuf_putc(f, ';') != 1){
    return -1;
  }
  ++r;
  if((rr = fbuf_putint(f, bc)) < 0){
    return -1;
  }
  r += rr;
  return r;
}

// write the escape which opens a Sixel, plus the palette table. returns the
// number of bytes written, so that this header can be directly copied in
// future reencodings. |leny| and |lenx| are output pixel geometry.
// returns the number of bytes written, so it can be stored at *parse_start.
static int
write_sixel_header(qstate* qs, fbuf* f, int leny){
  if(leny % 6){
    return -1;
  }
  // Set Raster Attributes - pan/pad=1 (pixel aspect ratio), Ph=qs->lenx, Pv=leny
  int r = write_sixel_intro(f, qs->smap->p2, leny, qs->lenx);
  if(r < 0){
    return -1;
  }
  for(int i = 0 ; i < qs->smap->colors ; ++i){
    const unsigned char* rgb = qs->table + i * RGBSIZE;
    //fprintf(fp, "#%d;2;%u;%u;%u", i, rgb[0], rgb[1], rgb[2]);
    int rr = write_sixel_creg(f, i, rgb[0], rgb[1], rgb[2]);
    if(rr < 0){
      return -1;
    }
    r += rr;
  }
  return r;
}

static int
write_sixel_payload(fbuf* f, const sixelmap* map){
  for(int j = 0 ; j < map->sixelbands ; ++j){
    int needclosure = 0;
    const sixelband* band = &map->bands[j];
    for(int i = 0 ; i < band->size ; ++i){
      if(band->vecs[i]){
        if(needclosure){
          if(fbuf_putc(f, '$') != 1){ // end previous one
            return -1;
          }
        }else{
          needclosure = 1;
        }
        if(fbuf_putc(f, '#') != 1){
          return -1;
        }
        if(fbuf_putint(f, i) < 0){
          return -1;
        }
        if(fbuf_puts(f, band->vecs[i]) < 0){
          return -1;
        }
      }
    }
    if(fbuf_putc(f, '-') != 1){
      return -1;
    }
  }
  if(fbuf_puts(f, "\e\\") < 0){
    return -1;
  }
  return 0;
}

// once per render cycle (if needed), make the actual payload match the TAM. we
// don't do these one at a time due to the complex (expensive) process involved
// in regenerating a sixel (we can't easily do it in-place). anything newly
// ANNIHILATED (state is ANNIHILATED, but no auxvec present) is dropped from
// the payload, and an auxvec is generated. anything newly restored (state is
// OPAQUE_SIXEL or MIXED_SIXEL, but an auxvec is present) is restored to the
// payload, and the auxvec is freed. none of this takes effect until the sixel
// is redrawn, and annihilated sprixcells still require a glyph to be emitted.
static inline int
sixel_reblit(sprixel* s){
  fbuf_chop(&s->glyph, s->parse_start);
  if(write_sixel_payload(&s->glyph, s->smap) < 0){
    return -1;
  }
  change_p2(s->glyph.buf, s->smap->p2);
  return 0;
}

// write out the sixel header after having quantized the palette.
static inline int
sixel_blit_inner(qstate* qs, sixelmap* smap, const blitterargs* bargs, tament* tam){
  fbuf f;
  if(fbuf_init(&f)){
    return -1;
  }
  sprixel* s = bargs->u.pixel.spx;
  const int cellpxy = bargs->u.pixel.cellpxy;
  const int cellpxx = bargs->u.pixel.cellpxx;
  int outy = qs->leny;
  if(outy % 6){
    outy += 6 - (qs->leny % 6);
    smap->p2 = SIXEL_P2_TRANS;
  }
  int parse_start = write_sixel_header(qs, &f, outy);
  if(parse_start < 0){
    fbuf_free(&f);
    return -1;
  }
  // we don't write out the payload yet -- set wipes_outstanding high, and
  // it'll be emitted via sixel_reblit(), taking into account any wipes that
  // occurred before it was displayed. otherwise, such a wipe would require
  // two emissions, one of which would be thrown away.
  scrub_tam_boundaries(tam, outy, qs->lenx, cellpxy, cellpxx);
  // take ownership of buf on success
  if(plane_blit_sixel(s, &f, outy, qs->lenx, parse_start, tam, SPRIXEL_INVALIDATED) < 0){
    fbuf_free(&f);
    return -1;
  }
  s->smap = smap;
  return 1;
}

// |leny| and |lenx| are the scaled output geometry. we take |leny| up to the
// nearest multiple of six greater than or equal to |leny|.
int sixel_blit(ncplane* n, int linesize, const void* data, int leny, int lenx,
               const blitterargs* bargs){
  if(bargs->u.pixel.colorregs >= TRANS_PALETTE_ENTRY){
    logerror("palette too large %d", bargs->u.pixel.colorregs);
    return -1;
  }
  sixelmap* smap = sixelmap_create(leny - bargs->begy);
  if(smap == NULL){
    return -1;
  }
  assert(n->tam);
  qstate* qs;
  if((qs = alloc_qstate(bargs->u.pixel.colorregs)) == NULL){
    logerror("couldn't allocate qstate");
    sixelmap_free(smap);
    return -1;
  }
  qs->bargs = bargs;
  qs->data = data;
  qs->linesize = linesize;
  qs->smap = smap;
  qs->leny = leny;
  qs->lenx = lenx;
  sixel_engine* sengine = ncplane_pile(n) ? ncplane_notcurses(n)->tcache.sixelengine : NULL;
  if(extract_color_table(sengine, qs)){
    free(bargs->u.pixel.spx->needs_refresh);
    bargs->u.pixel.spx->needs_refresh = NULL;
    sixelmap_free(smap);
    free_qstate(qs);
    return -1;
  }
  // takes ownership of sixelmap on success
  int r = sixel_blit_inner(qs, smap, bargs, n->tam);
  free_qstate(qs);
  if(r < 0){
    sixelmap_free(smap);
    // FIXME free refresh table?
  }
  scrub_color_table(bargs->u.pixel.spx);
  // we haven't actually emitted the body of the sixel yet. instead, we'll emit
  // it at sixel_redraw(), thus avoiding a double emission in the case of wipes
  // taking place before it's visible.
  bargs->u.pixel.spx->wipes_outstanding = 1;
  return r;
}

// to destroy a sixel, we damage all cells underneath it. we might not have
// to, though, if we've got a new sixel ready to go where the old sixel was
// (though we'll still need to if the new sprixcell not opaque, and the
// old and new sprixcell are different in any transparent pixel).
int sixel_scrub(const ncpile* p, sprixel* s){
  loginfo("%d state %d at %d/%d (%d/%d)", s->id, s->invalidated, s->movedfromy, s->movedfromx, s->dimy, s->dimx);
  int starty = s->movedfromy;
  int startx = s->movedfromx;
  for(int yy = starty ; yy < starty + (int)s->dimy && yy < (int)p->dimy ; ++yy){
    for(int xx = startx ; xx < startx + (int)s->dimx && xx < (int)p->dimx ; ++xx){
      int ridx = yy * p->dimx + xx;
      struct crender *r = &p->crender[ridx];
      if(!s->n){
        // need this to damage cells underneath a sprixel we're removing
        r->s.damaged = 1;
        continue;
      }
      sprixel* trues = r->sprixel ? r->sprixel : s;
      if(yy >= (int)trues->n->leny || yy - trues->n->absy < 0){
        r->s.damaged = 1;
        continue;
      }
      if(xx >= (int)trues->n->lenx || xx - trues->n->absx < 0){
        r->s.damaged = 1;
        continue;
      }
      sprixcell_e state = sprixel_state(trues, yy, xx);
//fprintf(stderr, "CHECKING %d/%d state: %d %d/%d\n", yy - s->movedfromy - s->n->absy, xx - s->movedfromx - s->n->absx, state, yy, xx);
      if(state == SPRIXCELL_TRANSPARENT || state == SPRIXCELL_MIXED_SIXEL){
        r->s.damaged = 1;
      }else if(s->invalidated == SPRIXEL_MOVED){
        // ideally, we wouldn't damage our annihilated sprixcells, but if
        // we're being annihilated only during this cycle, we need to go
        // ahead and damage it.
        r->s.damaged = 1;
      }
    }
  }
  return 1;
}

// returns the number of bytes written
int sixel_draw(const tinfo* ti, const ncpile* p, sprixel* s, fbuf* f,
               int yoff, int xoff){
  (void)ti;
  // if we've wiped or rebuilt any cells, effect those changes now, or else
  // we'll get flicker when we move to the new location.
  if(s->wipes_outstanding){
    if(sixel_reblit(s)){
      return -1;
    }
    s->wipes_outstanding = false;
  }
  if(p){
    const int targy = s->n->absy + yoff;
    const int targx = s->n->absx + xoff;
    if(goto_location(p->nc, f, targy, targx, NULL)){
      return -1;
    }
    if(s->invalidated == SPRIXEL_MOVED){
      for(int yy = s->movedfromy ; yy < s->movedfromy + (int)s->dimy && yy < (int)p->dimy ; ++yy){
        if(yy < 0){
          continue;
        }
        for(int xx = s->movedfromx ; xx < s->movedfromx + (int)s->dimx && xx < (int)p->dimx ; ++xx){
          if(xx < 0){
            continue;
          }
          struct crender *r = &p->crender[yy * p->dimx + xx];
          if(!r->sprixel || sprixel_state(r->sprixel, yy, xx) != SPRIXCELL_OPAQUE_SIXEL){
            r->s.damaged = 1;
          }
        }
      }
    }
  }
  if(fbuf_putn(f, s->glyph.buf, s->glyph.used) < 0){
    return -1;
  }
  s->invalidated = SPRIXEL_QUIESCENT;
  return s->glyph.used;
}

// a quantization worker.
static void *
sixel_worker(void* v){
  work_queue* wq = v;
  sixel_engine *sengine = wq->sengine;

  qstate* qs = NULL;
  unsigned bufpos = 0; // index into worker queue
  do{
    pthread_mutex_lock(&sengine->lock);
    while(wq->used == 0 && !sengine->done){
      pthread_cond_wait(&sengine->cond, &sengine->lock);
    }
    if(!sengine->done){
      qs = wq->qstates[bufpos];
    }else{
      qs = NULL;
    }
    pthread_mutex_unlock(&sengine->lock);
    if(qs == NULL){
      break;
    }
    bandworker(qs);
    bool sendsignal = false;
    pthread_mutex_lock(&sengine->lock);
    --wq->used;
    if(--qs->refcount == 0){
      sendsignal = true;
    }
    pthread_mutex_unlock(&sengine->lock);
    if(sendsignal){
      pthread_cond_broadcast(&sengine->cond);
    }
    if(++bufpos == WORKERDEPTH){
      bufpos = 0;
    }
  }while(1);
  return NULL;
}

static int
sixel_init_core(tinfo* ti, const char* initstr, int fd){
  if((ti->sixelengine = malloc(sizeof(sixel_engine))) == NULL){
    return -1;
  }
  sixel_engine* sengine = ti->sixelengine;
  pthread_mutex_init(&sengine->lock, NULL);
  pthread_cond_init(&sengine->cond, NULL);
  sengine->done = false;
  const int workers_wanted = sizeof(sengine->tids) / sizeof(*sengine->tids);
  for(int w = 0 ; w < workers_wanted ; ++w){
    sengine->queues[w].sengine = sengine;
    sengine->queues[w].writeto = 0;
    sengine->queues[w].used = 0;
    if(pthread_create(&sengine->tids[w], NULL, sixel_worker, &sengine->queues[w])){
      logerror("couldn't spin up sixel worker %d/%d", w, workers_wanted);
      // FIXME kill any created workers
      return -1;
    }
  }
  return tty_emit(initstr, fd);
}

// private mode 80 (DECSDM) manages "Sixel Scrolling Mode" vs "Sixel Display
// Mode". when 80 is enabled (i.e. DECSDM mode), images are displayed at the
// upper left, and clipped to the window. we don't want either of those things
// to happen, so we explicitly disable DECSDM.
// private mode 8452 places the cursor at the end of a sixel when it's
//  emitted. we don't need this for rendered mode, but we do want it for
//  direct mode. it causes us no problems, so always set it.
int sixel_init_forcesdm(tinfo* ti, int fd){
  return sixel_init_core(ti, "\e[?80l\e[?8452h", fd);
}

int sixel_init_inverted(tinfo* ti, int fd){
  // some terminals, at some versions, invert the sense of DECSDM. for those,
  // we must use 80h rather than the correct 80l. this grows out of a
  // misunderstanding in XTerm through patchlevel 368, which was widely
  // copied into other terminals.
  return sixel_init_core(ti, "\e[?80h\e[?8452h", fd);
}

// if we aren't sure of the semantics of the terminal we're speaking with,
// don't touch DECSDM at all. it's almost certainly set up the way we want.
int sixel_init(tinfo* ti, int fd){
  return sixel_init_core(ti, "\e[?8452h", fd);
}

// restore the |yoff|th bit of the sixel at |xoff| for the specified vec
// FIXME this is a very dopey implementation yuck, use RLE at least
static int
restore_vec(sixelband* b, int color, int bit, int xoff, int dimx){
  if(color >= b->size){
    logpanic("illegal color %d >= %d", color, b->size);
    return -1;
  }
  char* v = NULL;
  const char* vec = b->vecs[color]; // might be NULL
  if(vec == NULL){ // write this sixel, and we're done
    struct band_extender bes = {
      .rle = 1,
      .rep = bit,
    };
    if((v = sixelband_extend(v, &bes, dimx, xoff)) == NULL){
      return -1;
    }
  }else{
    int rle = 0; // the repetition number for this element
    int x = 0;
    int voff = 0;
    if((v = malloc(dimx + 1)) == NULL){
      return -1;
    }
    while(*vec){
      if(isdigit(*vec)){
        rle *= 10;
        rle += (*vec - '0');
      }else if(*vec == '!'){
        rle = 0;
      }else{
        if(rle == 0){
          rle = 1;
        }
        char rep = *vec;
//fprintf(stderr, "X/RLE/ENDX: %d %d %d\n", x, rle, endx);
        if(x + rle <= xoff){ // not wiped material; reproduce as-is
          write_rle(v, &voff, rle, rep);
          x += rle;
        }else if(x > xoff){
          write_rle(v, &voff, rle, rep);
          x += rle;
        }else{
          if(x < xoff){
            write_rle(v, &voff, xoff - x, rep);
            rle -= xoff - x;
            x = xoff;
          }
          write_rle(v, &voff, 1, ((rep - 63) | bit) + 63);
          --rle;
          ++x;
          if(rle){
            write_rle(v, &voff, rle, rep);
            x += rle;
          }
        }
        rle = 0;
      }
      ++vec;
      if(x > xoff){
        strcpy(v + voff, vec); // there is always room
        break;
      }
    }
  }
  free(b->vecs[color]);
  b->vecs[color] = v;
//fprintf(stderr, "SET NEW VEC (%zu) [%s]\n", strlen(v), v);
  return 0;
}

// rebuild the portion of some cell which is within this band, having stored
// the pixels into the auxvec when the cell was wiped (and updated them if we
// loaded another frame). we go through the auxvec to the right and down,
// within the area covered by our band. if the entry is transparent, do
// nothing. otherwise, it is some color; collect other instances of the color,
// marking them transparent as we do so, and update that color's band. in
// the worst case (all pixels different colors), this will be p^2 =\ FIXME.
//
// returns the number of source-transparent pixels (i.e. pixels which weren't
// restored), which will be used to update the TAM state.
static inline int
restore_band(sixelmap* smap, int band, int startx, int endx,
             int starty, int endy, int dimx, int cellpxy, int cellpxx,
             uint8_t* auxvec){
  int restored = 0;
  const int sy = band * 6 < starty ? starty - band * 6 : 0;
  const int ey = (band + 1) * 6 > endy ? 6 - ((band + 1) * 6 - endy) : 6;
  const int width = endx - startx;
  const int height = ey - sy;
  const int totalpixels = width * height;
  sixelband* b = &smap->bands[band];
//fprintf(stderr, "RESTORING band %d (%d->%d (%d->%d), %d->%d) %d pixels\n", band, sy, ey, starty, endy, startx, endx, totalpixels);
  int yoff = ((band * 6) + sy - starty) % cellpxy; // we start off on this row of the auxvec
  int xoff = startx % cellpxx;
  for(int dy = sy ; dy < ey ; ++dy, ++yoff){
    const int idx = (yoff * cellpxx + xoff) * AUXVECELEMSIZE;
    const int bit = 1 << dy;
//fprintf(stderr, " looking at bandline %d (auxvec row %d idx %d, dy %d)\n", dy, yoff, idx, dy);
    for(int dx = 0 ; startx + dx < endx ; ++dx){
      uint16_t color;
      memcpy(&color, &auxvec[idx + dx * AUXVECELEMSIZE], AUXVECELEMSIZE);
//fprintf(stderr, "  idx %d (dx %d x %d): %hu\n", idx, dx, dx + startx, color);
      if(color != TRANS_PALETTE_ENTRY){
        restore_vec(b, color, bit, startx + dx, dimx);
        ++restored;
      }
    }
  }
  (void)smap;
  return totalpixels - restored;
}

// only called for cells in SPRIXCELL_ANNIHILATED[_TRANS]. just post to
// wipes_outstanding, so the Sixel gets regenerated the next render cycle,
// just like wiping. this is necessary due to the complex nature of
// modifying a Sixel -- we want to do them all in one batch.
int sixel_rebuild(sprixel* s, int ycell, int xcell, uint8_t* auxvec){
//fprintf(stderr, "REBUILDING %d/%d\n", ycell, xcell);
  if(auxvec == NULL){
    return -1;
  }
  const int cellpxy = ncplane_pile(s->n)->cellpxy;
  const int cellpxx = ncplane_pile(s->n)->cellpxx;
  sixelmap* smap = s->smap;
  const int startx = xcell * cellpxx;
  const int starty = ycell * cellpxy;
  int endx = ((xcell + 1) * cellpxx);
  if(endx >= s->pixx){
    endx = s->pixx;
  }
  int endy = ((ycell + 1) * cellpxy);
  if(endy >= s->pixy){
    endy = s->pixy;
  }
  const int startband = starty / 6;
  const int endband = (endy - 1) / 6;
//fprintf(stderr, "%d/%d start: %d/%d end: %d/%d bands: %d-%d\n", ycell, xcell, starty, startx, endy, endx, starty / 6, endy / 6);
  // walk through each color, and wipe the necessary sixels from each band
  int w = 0;
  for(int b = startband ; b <= endband ; ++b){
    w += restore_band(smap, b, startx, endx, starty, endy, s->pixx,
                      cellpxy, cellpxx, auxvec);
  }
  s->wipes_outstanding = true;
  sprixcell_e newstate;
  if(w == cellpxx * cellpxy){
    newstate = SPRIXCELL_TRANSPARENT;
  }else if(w){
    newstate = SPRIXCELL_MIXED_SIXEL;
  }else{
    newstate = SPRIXCELL_OPAQUE_SIXEL;
  }
  s->n->tam[s->dimx * ycell + xcell].state = newstate;
  return 1;
}

void sixel_cleanup(tinfo* ti){
  sixel_engine* sengine = ti->sixelengine;
  const unsigned tids = POPULATION;
  pthread_mutex_lock(&sengine->lock);
  sengine->done = 1;
  pthread_mutex_unlock(&sengine->lock);
  pthread_cond_broadcast(&sengine->cond);
  loginfo("joining %u sixel thread%s", tids, tids == 1 ? "" : "s");
  for(unsigned t = 0 ; t < tids ; ++t){
    pthread_join(sengine->tids[t], NULL);
  }
  pthread_mutex_destroy(&sengine->lock);
  pthread_cond_destroy(&sengine->cond);
  free(sengine);
  loginfo("reaped sixel engine");
  ti->sixelengine = NULL;
  // no way to know what the state was before; we ought use XTSAVE/XTRESTORE
}

// create an auxiliary vector suitable for a Sixel sprixcell, and zero it out.
// there are two bytes per pixel in the cell: a palette index of up to 65534,
// or 65535 to indicate transparency.
uint8_t* sixel_trans_auxvec(const ncpile* p){
  const size_t slen = AUXVECELEMSIZE * p->cellpxy * p->cellpxx;
  uint8_t* a = malloc(slen);
  if(a){
    memset(a, 0xff, slen);
  }
  return a;
}
