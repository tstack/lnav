#include "internal.h"

typedef struct nctabbed_opsint {
  uint64_t selchan; // channel for the selected tab header
  uint64_t hdrchan; // channel for unselected tab headers
  uint64_t sepchan; // channel for the tab separator
  char* separator;  // separator string (copied by nctabbed_create())
  uint64_t flags;   // bitmask of NCTABBED_OPTION_*
} nctabbed_opsint;

typedef struct nctabbed {
  ncplane* ncp;          // widget ncplane
  ncplane* p;            // tab content ncplane
  ncplane* hp;           // tab headers ncplane
  // a doubly-linked circular list of tabs
  nctab* leftmost;       // the tab most to the left
  nctab* selected;       // the currently selected tab
  int tabcount;          // tab separator (can be NULL)
  int sepcols;           // separator with in columns
  nctabbed_opsint opts;  // copied in nctabbed_create()
} nctabbed;

void nctabbed_redraw(nctabbed* nt){
  nctab* t;
  unsigned drawn_cols = 0;
  unsigned rows, cols;
  if(nt->tabcount == 0){
    // no tabs = nothing to draw
    ncplane_erase(nt->hp);
    return;
  }
  // update sizes for planes
  ncplane_dim_yx(nt->ncp, &rows, &cols);
  if(nt->opts.flags & NCTABBED_OPTION_BOTTOM){
    ncplane_resize_simple(nt->hp, -1, cols);
    ncplane_resize_simple(nt->p, rows - 1, cols);
    ncplane_move_yx(nt->hp, rows - 2, 0);
  }else{
    ncplane_resize_simple(nt->hp, -1, cols);
    ncplane_resize_simple(nt->p, rows - 1, cols);
  }
  // the callback draws the tab contents
  if(nt->selected->cb){
    nt->selected->cb(nt->selected, nt->p, nt->selected->curry);
  }
  // now we draw the headers
  t = nt->leftmost;
  ncplane_erase(nt->hp);
  ncplane_set_channels(nt->hp, nt->opts.hdrchan);
  do{
    if(t == nt->selected){
      ncplane_set_channels(nt->hp, nt->opts.selchan);
      drawn_cols += ncplane_putstr(nt->hp, t->name);
      ncplane_set_channels(nt->hp, nt->opts.hdrchan);
    }else{
      drawn_cols += ncplane_putstr(nt->hp, t->name);
    }
    // avoid drawing the separator after the last tab, or when we
    // ran out of space, or when it's not set
    if((t->next != nt->leftmost || drawn_cols >= cols) && nt->opts.separator){
      ncplane_set_channels(nt->hp, nt->opts.sepchan);
      drawn_cols += ncplane_putstr(nt->hp, nt->opts.separator);
      ncplane_set_channels(nt->hp, nt->opts.hdrchan);
    }
    t = t->next;
  }while(t != nt->leftmost && drawn_cols < cols);
}

void nctabbed_ensure_selected_header_visible(nctabbed* nt){
  nctab* t = nt->leftmost;
  int cols = ncplane_dim_x(nt->hp);
  int takencols = 0;
  if(!t){
    return;
  }
//fprintf(stderr, "ensuring selected header visible\n");
  do{
    if(t == nt->selected){
      break;
    }
    takencols += t->namecols + nt->sepcols;
    if(takencols >= cols){
//fprintf(stderr, "not enough space, rotating\n");
      takencols -= nt->leftmost->namecols + nt->sepcols;
      nctabbed_rotate(nt, -1);
    }
    t = t->next;
//fprintf(stderr, "iteration over: takencols = %d, cols = %d\n", takencols, cols);
  }while(t != nt->leftmost);
//fprintf(stderr, "ensuring done\n");
}

static bool
nctabbed_validate_opts(const nctabbed_options* opts){
  if(opts->flags > NCTABBED_OPTION_BOTTOM){
    logwarn("provided unsupported flags 0x%016" PRIx64, opts->flags);
  }
  if(opts->sepchan && !opts->separator){
    logwarn("provided non-zero separator channel when separator is NULL")
  }
  return true;
}

nctab* nctabbed_selected(nctabbed* nt){
  return nt->selected;
}

nctab* nctabbed_leftmost(nctabbed* nt){
  return nt->leftmost;
}

int nctabbed_tabcount(nctabbed* nt){
  return nt->tabcount;
}

ncplane* nctabbed_plane(nctabbed* nt){
  return nt->ncp;
}

ncplane* nctabbed_content_plane(nctabbed* nt){
  return nt->p;
}

tabcb nctab_cb(nctab* t){
  return t->cb;
}

const char* nctab_name(nctab* t){
  return t->name;
}

int nctab_name_width(nctab* t){
  return t->namecols;
}

void* nctab_userptr(nctab* t){
  return t->curry;
}

nctab* nctab_next(nctab* t){
  return t->next;
}

nctab* nctab_prev(nctab* t){
  return t->prev;
}

nctabbed* nctabbed_create(ncplane* n, const nctabbed_options* topts){
  nctabbed_options zeroed = {0};
  ncplane_options nopts = {0};
  unsigned nrows, ncols;
  nctabbed* nt = NULL;
  if(!topts){
    topts = &zeroed;
  }
  if(!nctabbed_validate_opts(topts)){
    goto err;
  }
  if((nt = malloc(sizeof(*nt))) == NULL){
    logerror("Couldn't allocate nctabbed");
    goto err;
  }
  nt->ncp = n;
  nt->leftmost = nt->selected = NULL;
  nt->tabcount = 0;
  nt->sepcols = 0;
  nt->opts.separator = NULL;
  nt->opts.selchan = topts->selchan;
  nt->opts.hdrchan = topts->hdrchan;
  nt->opts.sepchan = topts->sepchan;
  nt->opts.flags = topts->flags;
  if(topts->separator){
    if((nt->sepcols = ncstrwidth(topts->separator, NULL, NULL)) < 0){
      logerror("Separator string contains illegal characters");
      goto err;
    }
    if((nt->opts.separator = strdup(topts->separator)) == NULL){
      logerror("Couldn't allocate nctabbed separator");
      goto err;
    }
  }
  ncplane_dim_yx(n, &nrows, &ncols);
  if(topts->flags & NCTABBED_OPTION_BOTTOM){
    nopts.y = nopts.x = 0;
    nopts.cols = ncols;
    nopts.rows = nrows - 1;
    if((nt->p = ncplane_create(n, &nopts)) == NULL){
      logerror("Couldn't create the tab content plane");
      goto err;
    }
    nopts.y = nrows - 2;
    nopts.rows = 1;
    if((nt->hp = ncplane_create(n, &nopts)) == NULL){
      logerror("Couldn't create the tab headers plane");
      ncplane_destroy(nt->p);
      goto err;
    }
  }else{
    nopts.y = nopts.x = 0;
    nopts.cols = ncols;
    nopts.rows = 1;
    if((nt->hp = ncplane_create(n, &nopts)) == NULL){
      logerror("Couldn't create the tab headers plane");
      goto err;
    }
    nopts.y = 1;
    nopts.rows = nrows - 1;
    if((nt->p = ncplane_create(n, &nopts)) == NULL){
      logerror("Couldn't create the tab content plane");
      ncplane_destroy(nt->hp);
      goto err;
    }
  }
  if(ncplane_set_widget(nt->ncp, nt, (void(*)(void*))nctabbed_destroy)){
    ncplane_destroy(nt->hp);
    ncplane_destroy(nt->p);
    goto err;
  }
  nctabbed_redraw(nt);
  return nt;

err:
  ncplane_destroy_family(n);
  if(nt){
    free(nt->opts.separator);
    free(nt);
  }
  return NULL;
}

nctab* nctabbed_add(nctabbed* nt, nctab* after, nctab* before, tabcb cb,
                    const char* name, void* opaque){
  nctab* t;
  if(after && before){
    if(after->next != before || before->prev != after){
      logerror("bad before (%p) / after (%p) spec", before, after);
      return NULL;
    }
  }else if(!after && !before){
    // add it to the right of the selected tab
    after = nt->selected;
  }
  if((t = malloc(sizeof(*t))) == NULL){
    logerror("Couldn't allocate nctab")
    return NULL;
  }
  if((t->name = strdup(name)) == NULL){
    logerror("Couldn't allocate the tab name");
    free(t);
    return NULL;
  }
  if((t->namecols = ncstrwidth(name, NULL, NULL)) < 0){
    logerror("Tab name contains illegal characters")
    free(t->name);
    free(t);
    return NULL;
  }
  if(after){
    t->next = after->next;
    t->prev = after;
    after->next = t;
    t->next->prev = t;
  }else if(before){
    t->next = before;
    t->prev = before->prev;
    before->prev = t;
    t->prev->next = t;
  }else{
    // the first tab
    t->prev = t->next = t;
    nt->leftmost = nt->selected = t;
  }
  t->nt = nt;
  t->cb = cb;
  t->curry = opaque;
  ++nt->tabcount;
  return t;
}

int nctabbed_del(nctabbed* nt, nctab* t){
  if(!t){
    logerror("Provided NULL nctab");
    return -1;
  }
  if(nt->tabcount == 1){
    nt->leftmost = nt->selected = NULL;
  }else{
    if(nt->selected == t){
      nt->selected = t->next;
    }
    if(nt->leftmost == t){
      nt->leftmost = t->next;
    }
    t->next->prev = t->prev;
    t->prev->next = t->next;
  }
  free(t->name);
  free(t);
  --nt->tabcount;
  return 0;
}

int nctab_move(nctabbed* nt __attribute__ ((unused)), nctab* t, nctab* after, nctab* before){
  if(after && before){
    if(after->prev != before || before->next != after){
      logerror("bad before (%p) / after (%p) spec", before, after);
      return -1;
    }
  }else if(!after && !before){
    logerror("bad before (%p) / after (%p) spec", before, after);
    return -1;
  }
  // bad things would happen
  if(t == after || t == before){
    logerror("Cannot move a tab before or after itself.");
    return -1;
  }
  t->prev->next = t->next;
  t->next->prev = t->prev;
  if(after){
    t->next = after->next;
    t->prev = after;
    after->next = t;
    t->next->prev = t;
  }else{
    t->next = before;
    t->prev = before->prev;
    before->prev = t;
    t->prev->next = t;
  }
  return 0;
}

void nctab_move_right(nctabbed* nt, nctab* t){
  if(t == nt->leftmost->prev){
    nctab_move(nt, t, NULL, nt->leftmost);
    nt->leftmost = t;
    return;
  }else if(t == nt->leftmost){
    nt->leftmost = t->next;
  }
  nctab_move(nt, t, t->next, NULL);
}

void nctab_move_left(nctabbed* nt, nctab* t){
  if(t == nt->leftmost){
    nt->leftmost = t->next;
    nctab_move(nt, t, nt->leftmost->prev, NULL);
    return;
  }else if(t == nt->leftmost->next){
    nt->leftmost = t;
  }
  nctab_move(nt, t, NULL, t->prev);
}

void nctabbed_rotate(nctabbed* nt, int amt){
  if(amt > 0){
    for(int i = 0 ; i < amt ; ++i){
      nt->leftmost = nt->leftmost->prev;
    }
  }else{
    for(int i = 0 ; i < -amt ; ++i){
      nt->leftmost = nt->leftmost->next;
    }
  }
}

nctab* nctabbed_next(nctabbed* nt){
  if(nt->tabcount == 0){
    return NULL;
  }
  nt->selected = nt->selected->next;
  return nt->selected;
}

nctab* nctabbed_prev(nctabbed* nt){
  if(nt->tabcount == 0){
    return NULL;
  }
  nt->selected = nt->selected->prev;
  return nt->selected;
}

nctab* nctabbed_select(nctabbed* nt, nctab* t){
  nctab* prevsel = nt->selected;
  nt->selected = t;
  return prevsel;
}

void nctabbed_channels(nctabbed* nt, uint64_t* RESTRICT hdrchan,
                       uint64_t* RESTRICT selchan, uint64_t* RESTRICT sepchan){
  if(hdrchan){
    memcpy(hdrchan, &nt->opts.hdrchan, sizeof(*hdrchan));
  }
  if(selchan){
    memcpy(selchan, &nt->opts.selchan, sizeof(*selchan));
  }
  if(sepchan){
    memcpy(sepchan, &nt->opts.sepchan, sizeof(*sepchan));
  }
}

const char* nctabbed_separator(nctabbed* nt){
  return nt->opts.separator;
}

int nctabbed_separator_width(nctabbed* nt){
  return nt->sepcols;
}

void nctabbed_destroy(nctabbed* nt){
  if(!nt){
    return;
  }
  if(ncplane_set_widget(nt->ncp, NULL, NULL) == 0){
    nctab* t = nt->leftmost;
    nctab* tmp;
    if(t){
      t->prev->next = NULL;
      if(t->next){
        t->next->prev = NULL;
      }
    }
    while(t){
      tmp = t->next;
      free(t->name);
      free(t);
      t = tmp;
    }
    ncplane_destroy_family(nt->ncp);
    free(nt->opts.separator);
    free(nt);
  }
}

void nctabbed_set_hdrchan(nctabbed* nt, uint64_t chan){
  nt->opts.hdrchan = chan;
}

void nctabbed_set_selchan(nctabbed* nt, uint64_t chan){
  nt->opts.selchan = chan;
}

void nctabbed_set_sepchan(nctabbed* nt, uint64_t chan){
  nt->opts.sepchan = chan;
}

tabcb nctab_set_cb(nctab* t, tabcb newcb){
  tabcb prevcb = t->cb;
  t->cb = newcb;
  return prevcb;
}

int nctab_set_name(nctab* t, const char* newname){
  int newnamecols;
  char* prevname = t->name;
  if((newnamecols = ncstrwidth(newname, NULL, NULL)) < 0){
    logerror("New tab name contains illegal characters");
    return -1;
  }
  if((t->name = strdup(newname)) == NULL){
    logerror("Couldn't allocate new tab name");
    t->name = prevname;
    return -1;
  }
  free(prevname);
  t->namecols = newnamecols;
  return 0;
}

void* nctab_set_userptr(nctab* t, void* newopaque){
  void* prevcurry = t->curry;
  t->curry = newopaque;
  return prevcurry;
}

int nctabbed_set_separator(nctabbed* nt, const char* separator){
  int newsepcols;
  char* prevsep = nt->opts.separator;
  if((newsepcols = ncstrwidth(separator, NULL, NULL)) < 0){
    logerror("New tab separator contains illegal characters");
    return -1;
  }
  if((nt->opts.separator = strdup(separator)) == NULL){
    logerror("Couldn't allocate new tab separator");
    nt->opts.separator = prevsep;
    return -1;
  }
  free(prevsep);
  nt->sepcols = newsepcols;
  return 0;
}
