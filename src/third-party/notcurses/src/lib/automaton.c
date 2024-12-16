#include "automaton.h"
#include "internal.h"

// the input automaton, walked for all escape sequences. an escape sequence is
// everything from an escape through recognized termination of that escape, or
// abort of the sequence via another escape, save the case of DCS sequences
// (those beginning with Escape-P), which are terminated by the ST sequence
// Escape-\. in the case of an aborted sequence, the sequence in its entirety
// is replayed as regular input. regular input is not driven through this
// automaton.
//
// one complication is that the user can just press escape themselves, followed
// by arbitrary other keypresses. when input is redirected from some source
// other than the connected terminal, this is no problem: we know control
// sequences to be coming in from the connected terminal, and everything else
// is bulk input.

// we assumed escapes can only be composed of 7-bit chars
typedef struct esctrie {
  // if non-NULL, this is the next level of radix-128 trie. it is NULL on
  // accepting nodes, since no valid control sequence is a prefix of another
  // valid control sequence. links are 1-biased (0 is NULL).
  unsigned* trie;
  enum {
    NODE_SPECIAL,  // an accepting node, or pure transit (if ni.id == 0)
    NODE_NUMERIC,  // accumulates a number
    NODE_STRING,   // accumulates a string
    NODE_FUNCTION, // invokes a function
  } ntype;
  ncinput ni;      // composed key terminating here
  triefunc fxn;    // function to call on match
  unsigned kleene; // idx of kleene match
} esctrie;

// get node corresponding to 1-biased index
static inline esctrie*
esctrie_from_idx(const automaton* a, unsigned idx){
  if(idx == 0){
    return NULL;
  }
  return a->nodepool + (idx - 1);
}

// return 1-biased index of node in pool
static inline unsigned
esctrie_idx(const automaton* a, const esctrie* e){
  return e - a->nodepool + 1;
}

uint32_t esctrie_id(const esctrie* e){
  return e->ni.id;
}

// returns the idx of the new node, or 0 on failure (idx is 1-biased).
// *invalidates any existing escnode pointers!*
static unsigned
create_esctrie_node(automaton* a, int special){
  if(a->poolused == a->poolsize){
    unsigned newsize = a->poolsize ? a->poolsize * 2 : 512;
    esctrie* tmp = realloc(a->nodepool, sizeof(*a->nodepool) * newsize);
    if(tmp == NULL){
      return 0;
    }
    a->nodepool = tmp;
    a->poolsize = newsize;
  }
  esctrie* e = &a->nodepool[a->poolused++];
  memset(e, 0, sizeof(*e));
  e->ntype = NODE_SPECIAL;
  if((e->ni.id = special) == 0){
    const size_t tsize = sizeof(*e->trie) * 0x80;
    if((e->trie = malloc(tsize)) == NULL){
      --a->poolused;
      return 0;
    }
    memset(e->trie, 0, tsize);
  }
  return esctrie_idx(a, e);
}

void input_free_esctrie(automaton* a){
  a->escapes = 0;
  a->poolsize = 0;
  for(unsigned i = 0 ; i < a->poolused ; ++i){
    free(a->nodepool[i].trie);
  }
  free(a->nodepool);
  a->poolused = 0;
  a->nodepool = NULL;
}

static int
esctrie_make_kleene(automaton* a, esctrie* e, unsigned follow, esctrie* term){
  if(e->ntype != NODE_SPECIAL){
    logerror("can't make node type %d string", e->ntype);
    return -1;
  }
  for(unsigned i = 0 ; i < 0x80 ; ++i){
    if(i == follow){
      e->trie[i] = esctrie_idx(a, term);
    }else if(e->trie[i] == 0){
      e->trie[i] = esctrie_idx(a, e);
    }
  }
  return 0;
}

static int
esctrie_make_function(esctrie* e, triefunc fxn){
  if(e->ntype != NODE_SPECIAL){
    logerror("can't make node type %d function", e->ntype);
    return -1;
  }
  if(e->trie){
    logerror("can't make followed function");
    return -1;
  }
  e->ntype = NODE_FUNCTION;
  e->fxn = fxn;
  return 0;
}

static esctrie*
esctrie_make_string(automaton* a, esctrie* e, unsigned rxvtstyle){
  if(e->ntype == NODE_STRING){
    logerror("repeated string node");
    return NULL;
  }
  if(e->ntype != NODE_SPECIAL){
    logerror("can't make node type %d string", e->ntype);
    return NULL;
  }
  for(int i = 0 ; i < 0x80 ; ++i){
    if(!isprint(i)){
      continue;
    }
    if(e->trie[i]){
      logerror("can't make %c-followed string", i);
      return NULL;
    }
  }
  esctrie* newe = esctrie_from_idx(a, create_esctrie_node(a, 0));
  if(newe == NULL){
    return NULL;
  }
  for(int i = 0 ; i < 0x80 ; ++i){
    if(!isprint(i)){
      continue;
    }
    e->trie[i] = esctrie_idx(a, newe);
  }
  e = newe;
  e->ntype = NODE_STRING;
  for(int i = 0 ; i < 0x80 ; ++i){
    if(!isprint(i)){
      continue;
    }
    e->trie[i] = esctrie_idx(a, newe);
  }
  if(rxvtstyle){ // ends with bare ESC, not BEL/ST
    if((e->trie[0x1b] = create_esctrie_node(a, 0)) == 0){
      return NULL;
    }
    e = esctrie_from_idx(a, e->trie[0x1b]);
    e->ni.id = 0;
    e->ntype = NODE_SPECIAL;
  }else{
    if((e->trie[0x07] = create_esctrie_node(a, NCKEY_INVALID)) == 0){
      return NULL;
    }
    esctrie* term = esctrie_from_idx(a, e->trie[0x07]);
    if((e->trie[0x1b] = create_esctrie_node(a, 0)) == 0){
      return NULL;
    }
    e = esctrie_from_idx(a, e->trie[0x1b]);
    e->trie['\\'] = esctrie_idx(a, term);
    term->ni.id = 0;
    term->ntype = NODE_SPECIAL;
    e = term;
  }
  logdebug("made string: %u", esctrie_idx(a, e));
  return e;
}

static esctrie*
link_kleene(automaton* a, esctrie* e, unsigned follow){
  unsigned eidx = esctrie_idx(a, e);
  if(e->kleene){
    return a->nodepool + e->kleene;
  }
  // invalidates e
  unsigned termidx = create_esctrie_node(a, 0);
  unsigned targidx = create_esctrie_node(a, 0);
  esctrie* term = esctrie_from_idx(a, termidx);
  esctrie* targ = esctrie_from_idx(a, targidx);
  if(targ == NULL){
    return NULL;
  }
  if(term == NULL){
    return NULL;
  }
  if(esctrie_make_kleene(a, targ, follow, term)){
    return NULL;
  }
  e = esctrie_from_idx(a, eidx);
  // fill in all NULL numeric links with the new target
  for(unsigned int i = 0 ; i < 0x80 ; ++i){
    if(i == follow){
      if(e->trie[i]){
        logerror("drain terminator already registered");
        return NULL;
      }
      e->trie[follow] = esctrie_idx(a, term);
    }else if(e->trie[i] == 0){
      e->trie[i] = esctrie_idx(a, targ);
    }
  }
  targ->kleene = esctrie_idx(a, targ);
  return esctrie_from_idx(a, e->trie[follow]);
}

// phase 1 of the numeric algorithm; find a φ node on e. not sure what
// to do if we have non-φ links at every digit...punt for now FIXME.
static unsigned
get_phi_node(automaton* a, esctrie* e){
  // find a linked NODE_NUMERIC, if one exists. we'll want to reuse it.
  int nonphis = 0;
  esctrie* targ;
  for(int i = '0' ; i <= '9' ; ++i){
    if( (targ = esctrie_from_idx(a, e->trie[i])) ){
      if(targ->ntype == NODE_NUMERIC){
        logtrace("found existing phi node %u[%c]->%u", esctrie_idx(a, e), i, esctrie_idx(a, targ));
        break;
      }else{
        ++nonphis;
        targ = NULL;
      }
    }
  }
  // we either have a numeric target, or will make one now. if we create a new
  // one, be sure to mark it numeric, and add all digit links back to itself.
  if(targ == NULL){
    if(nonphis == 10){
      logerror("ten non-phi links from %u", esctrie_idx(a, e));
      return 0;
    }
    if((targ = esctrie_from_idx(a, create_esctrie_node(a, 0))) == 0){
      return 0;
    }
    targ->ntype = NODE_NUMERIC;
    for(int i = '0' ; i <= '9' ; ++i){
      targ->trie[i] = esctrie_idx(a, targ);
    }
  }
  assert(NODE_NUMERIC == targ->ntype);
  return esctrie_idx(a, targ);
}

// phase 2 of the numeric algorithm; find a ή node for |successor| on |phi|.
static unsigned
get_eta_node(automaton* a, esctrie* phi, unsigned successor){
  unsigned phiidx = esctrie_idx(a, phi);
  unsigned etaidx = phi->trie[successor];
  esctrie* eta = esctrie_from_idx(a, etaidx);
  if(eta == NULL){
    // invalidates phi
    if((eta = esctrie_from_idx(a, create_esctrie_node(a, 0))) == NULL){
      return 0;
    }
    phi = esctrie_from_idx(a, phiidx);
    phi->trie[successor] = esctrie_idx(a, eta);
  }
  return esctrie_idx(a, eta);
}

// |e| is a known-standard node reached by our prefix; go ahead and prep both
// phi and eta links from it.
static void
add_phi_and_eta_chain(const automaton *a, esctrie* e, unsigned phi,
                      unsigned follow, unsigned eta){
//logtrace("working with %u phi: %u follow: %u eta: %u", esctrie_idx(a, e), phi, follow, eta);
  for(int i = '0' ; i <= '9' ; ++i){
    esctrie* chain = esctrie_from_idx(a, e->trie[i]);
    if(chain == NULL){
      //logdebug("linking %u[%d] to %u", esctrie_idx(a, e), i, phi);
      e->trie[i] = phi;
    }else if(chain->ntype == NODE_SPECIAL){
//logdebug("propagating along %u[%c]", e->trie[i], i);
      add_phi_and_eta_chain(a, esctrie_from_idx(a, e->trie[i]), phi, follow, eta);
    }
  }
  if(e->trie[follow] == 0){
    //logdebug("linking %u[%u] to %u", esctrie_idx(a, e), follow, eta);
    e->trie[follow] = eta;
  }
}

// phase 3 of the numeric algorithm: walk the automaton, finding all nodes
// which are prefixes of phi (all nodes matching the prefix, and all numeric
// non-phi chains from those nodes) and linking them to phi, and finding all
// nodes which are prefixes of eta (all numeric non-phi chains from the
// previous set) and linking them to eta. |e| is the path thus far.
static void
add_phi_and_eta_recurse(automaton* a, esctrie* e, const char* prefix,
                        int pfxlen, esctrie* phi, unsigned follow,
                        esctrie* eta, unsigned inphi){
//logtrace("working with %u %d prefix [%*.*s]", esctrie_idx(a, e), pfxlen, pfxlen, pfxlen, prefix);
  // if pfxlen == 0, we found a match for our fixed prefix. start adding phi
  // links whereever we can. where we find chained numerics, add an eta link.
  if(pfxlen == 0){
    add_phi_and_eta_chain(a, e, esctrie_idx(a, phi), follow, esctrie_idx(a, eta));
    return;
  }
  // when we hit a \N in the prefix, we must recurse along all digit links
  if(*prefix == '\\'){
    ++prefix;
    --pfxlen;
    if(*prefix != 'N'){
      logerror("illegal wildcard in prefix %c", *prefix);
      return;
    }
    ++prefix;
    --pfxlen;
    // Optimization: get_phi_node will set the trie[i] for i='0'..'9' to the exact 
    // same linked tri index. If that happens, there is no need to to the (expensive)
    // add_phi_and_eta_recurse call ten times, only the first time is enough.
    unsigned linked_tri_seen_last = UINT_MAX;
    for(int i = '0' ; i <= '9' ; ++i){
      if(e->trie[i] == 0){
        //logdebug("linking %u[%d] to %u", esctrie_idx(a, e), i, esctrie_idx(a, phi));
        e->trie[i] = esctrie_idx(a, phi);
      }else{
        if(e->trie[i] != linked_tri_seen_last){
            add_phi_and_eta_recurse(a, esctrie_from_idx(a, e->trie[i]),
                                prefix, pfxlen, phi, follow, eta, 1);
            linked_tri_seen_last = e->trie[i];
        }
      }
    }
  }else{
    if(inphi){
      //same optimization as above
      unsigned linked_tri_seen_last = UINT_MAX;
      for(int i = '0' ; i <= '9' ; ++i){
        if(e->trie[i] == 0){
          //logdebug("linking %u[%d] to %u", esctrie_idx(a, e), i, esctrie_idx(a, phi));
          e->trie[i] = esctrie_idx(a, phi);
        }else if(e->trie[i] != esctrie_idx(a, e) && e->trie[i] != linked_tri_seen_last){
          add_phi_and_eta_recurse(a, esctrie_from_idx(a, e->trie[i]),
                                prefix, pfxlen, phi, follow, eta, 1);
          linked_tri_seen_last = e->trie[i];
        }
      }
    }
    unsigned char p = *prefix;
    if(e->trie[p]){
      add_phi_and_eta_recurse(a, esctrie_from_idx(a, e->trie[p]),
                              prefix + 1, pfxlen - 1, phi, follow, eta, 0);
    }
  }
}

// |prefix| does *not* lead with an escape, and does not include the numeric.
static void
add_phi_and_eta(automaton* a, const char* prefix, size_t pfxlen,
                esctrie* phi, unsigned follow, esctrie* eta){
  esctrie* esc = esctrie_from_idx(a, a->escapes);
  if(esc == NULL){
    return;
  }
  add_phi_and_eta_recurse(a, esc, prefix, pfxlen, phi, follow, eta, 0);
}

// accept any digit and transition to a numeric node. |e| is the culmination of
// the prefix before the numeric. |follow| is the successor of the numeric.
// here's our approach:
//  - find a link to a numeric from e. there can only be one node (though it
//     might have many links), so we can use the first one we find.
//  - if there is no such numeric node linked from e, create one.
//     (FIXME if all ten digits are occupied, what would we do?)
//  - chosen numeric node is φ.
//  - if an appropriate follow node exists linked from φ, choose it as ή.
//  - otherwise, create a new ή and link it from φ.
//  - walk from the top, finding all possible prefixes of φ.
//  - at each, link all unused digits to φ.
//  - from each that is also a possible prefix of ή, link ή.
static esctrie*
link_numeric(automaton* a, const char* prefix, int pfxlen,
             esctrie* e, unsigned char follow){
  logdebug("adding numeric with follow %c following %*.*s", follow, pfxlen, pfxlen, prefix);
  unsigned phiidx = get_phi_node(a, e);
  if(phiidx == 0){
    return NULL;
  }
  esctrie* phi = esctrie_from_idx(a, phiidx);
  // invalidates phi
  unsigned etaidx = get_eta_node(a, phi, follow);
  if(etaidx == 0){
    return NULL;
  }
  phi = esctrie_from_idx(a, phiidx);
  esctrie* eta = esctrie_from_idx(a, etaidx);
  logtrace("phi node: %u->%u", esctrie_idx(a, e), esctrie_idx(a, phi));
  logtrace("eta node: %u philink[%c]: %u", esctrie_idx(a, eta), follow, phi->trie[follow]);
  // eta is now bound to phi, and phi links something at all digits, but no
  // other links are guaranteed. walk the automaton, finding all possible
  // prefixes of φ (and linking to φ) and all possible prefixes of ή (and
  // linking them to ή).
  add_phi_and_eta(a, prefix, pfxlen, phi, follow, eta);
  return eta;
}

static esctrie*
insert_path(automaton* a, const char* seq){
  if(a->escapes == 0){
    if((a->escapes = create_esctrie_node(a, 0)) == 0){
      return NULL;
    }
  }
  esctrie* eptr = esctrie_from_idx(a, a->escapes);
  bool inescape = false;
  const char* seqstart = seq;
  unsigned char c;
  while( (c = *seq++) ){
    if(c == '\\'){
      if(inescape){
        logerror("illegal escape: \\");
        return NULL;
      }
      inescape = true;
    }else if(inescape){
      if(c == 'N'){
        // a numeric must be followed by some terminator
        if(!*seq){
          logerror("illegal numeric terminator");
          return NULL;
        }
        c = *seq++;
        eptr = link_numeric(a, seqstart, seq - 3 - seqstart, eptr, c);
        if(eptr == NULL){
          return NULL;
        }
      }else if(c == 'S' || c == 'R'){
        // strings always end with ST ("\e\\") or at least ("\e")
        if((eptr = esctrie_make_string(a, eptr, c == 'R')) == NULL){
          return NULL;
        }
        return eptr;
      }else if(c == 'D'){ // drain (kleene closure)
        // a kleene must be followed by some terminator
        if(!*seq){
          logerror("illegal kleene terminator");
          return NULL;
        }
        c = *seq++;
        eptr = link_kleene(a, eptr, c);
        if(eptr == NULL){
          return NULL;
        }
      }else{
        logerror("illegal escape: %u", c);
        return NULL;
      }
      inescape = false;
    }else{ // fixed character
      unsigned eidx = esctrie_idx(a, eptr);
      // invalidates eptr
      if(eptr->trie[c] == 0 || eptr->trie[c] == eptr->kleene){
        unsigned tidx = create_esctrie_node(a, 0);
        if(tidx == 0){
          return NULL;
        }
        eptr = esctrie_from_idx(a, eidx);
        eptr->trie[c] = tidx;
      }else if(esctrie_from_idx(a, eptr->trie[c])->ntype == NODE_NUMERIC){
        // punch a hole through the numeric loop. create a new one, and fill
        // it in with the existing target.
        struct esctrie* newe;
        // invalidates eptr
        if((newe = esctrie_from_idx(a, create_esctrie_node(a, 0))) == 0){
          return NULL;
        }
        eptr = esctrie_from_idx(a, eidx);
        for(int i = 0 ; i < 0x80 ; ++i){
          newe->trie[i] = esctrie_from_idx(a, eptr->trie[c])->trie[i];
        }
        eptr->trie[c] = esctrie_idx(a, newe);
      }
      eptr = esctrie_from_idx(a, eidx);
      eptr = esctrie_from_idx(a, eptr->trie[c]);
      logtrace("added fixed %c %u as %u", c, c, esctrie_idx(a, eptr));
    }
  }
  if(inescape){
    logerror("illegal escape at end of line");
    return NULL;
  }
  return eptr;
}

// add a cflow path to the automaton
int inputctx_add_cflow(automaton* a, const char* seq, triefunc fxn){
  esctrie* eptr = insert_path(a, seq);
  if(eptr == NULL){
    return -1;
  }
  free(eptr->trie);
  eptr->trie = NULL;
  return esctrie_make_function(eptr, fxn);
}

// multiple input escapes might map to the same input
int inputctx_add_input_escape(automaton* a, const char* esc, uint32_t special,
                              unsigned modifiers){
  if(esc[0] != NCKEY_ESC || strlen(esc) < 2){ // assume ESC prefix + content
    logerror("not an escape (0x%x)", special);
    return -1;
  }
  esctrie* eptr = insert_path(a, esc + 1);
  if(eptr == NULL){
    return -1;
  }
  // it appears that multiple keys can be mapped to the same escape string. as
  // an example, see "kend" and "kc1" in st ("simple term" from suckless) :/.
  if(eptr->ni.id){ // already had one here!
    if(eptr->ni.id != special){
      logwarn("already added escape (got 0x%x, wanted 0x%x)", eptr->ni.id, special);
    }
  }else{
    eptr->ni.id = special;
    eptr->ni.shift = modifiers & NCKEY_MOD_SHIFT;
    eptr->ni.ctrl = modifiers & NCKEY_MOD_CTRL;
    eptr->ni.alt = modifiers & NCKEY_MOD_ALT;
    eptr->ni.y = 0;
    eptr->ni.x = 0;
    eptr->ni.modifiers = modifiers;
    logdebug("added 0x%08x to %u", special, esctrie_idx(a, eptr));
  }
  return 0;
}

// returns -1 for non-match, 0 for match, 1 for acceptance. if we are in the
// middle of a sequence, and receive an escape, *do not call this*, but
// instead call reset_automaton() after replaying the used characters to the
// bulk input buffer, and *then* call this with the escape.
int walk_automaton(automaton* a, struct inputctx* ictx, unsigned candidate,
                   ncinput* ni){
  if(candidate >= 0x80){
    logerror("eight-bit char %u in control sequence", candidate);
    return -1;
  }
  esctrie* e = esctrie_from_idx(a, a->state);
  // we ought not have been called for an escape with any state!
  if(candidate == 0x1b && !a->instring){
    assert(NULL == e);
    a->state = a->escapes;
    return 0;
  }
  if(e->ntype == NODE_STRING){
    if(candidate == 0x1b || candidate == 0x07){
      a->state = e->trie[candidate];
      a->instring = 0;
    }
    e = esctrie_from_idx(a, a->state);
    if(e->ntype == NODE_FUNCTION){ // for the 0x07s of the world
      if(e->fxn == NULL){
        return 2;
      }
      return e->fxn(ictx);
    }
    return 0;
  }
  if((a->state = e->trie[candidate]) == 0){
    if(esctrie_idx(a, e) == a->escapes){
      memset(ni, 0, sizeof(*ni));
      ni->id = candidate;
      ni->alt = true;
      return 1;
    }
    loginfo("unexpected transition on %u[%u]",
            esctrie_idx(a, e), candidate);
    return -1;
  }
  e = esctrie_from_idx(a, a->state);
  // initialize any node we've just stepped into
  switch(e->ntype){
    case NODE_NUMERIC:
      break;
    case NODE_STRING:
      a->instring = 1;
      break;
    case NODE_SPECIAL:
      if(e->ni.id){
        memcpy(ni, &e->ni, sizeof(*ni));
        return 1;
      }
      break;
    case NODE_FUNCTION:
      if(e->fxn == NULL){
        return 2;
      }
      return e->fxn(ictx);
      break;
  }
  return 0;
}
