#include "internal.h"

// print the first 'bytes' bytes of 'text' to 'n', using alignment 'align'
// and requiring 'cols' columns, relative to the current cursor position.
// it is an error to call ncplane_putline() with more data than can be printed
// on the current row.
static inline int
ncplane_putline(ncplane* n, ncalign_e align, int cols, const char* text, size_t bytes){
  const int avail = ncplane_dim_x(n) - n->x - 1;
  const int offset = (align == NCALIGN_UNALIGNED ? 0 :
                      notcurses_align(avail, align, cols));
  return ncplane_putnstr_yx(n, -1, n->x + offset, bytes, text);
}

static int
puttext_advance_line(ncplane* n, unsigned truebreak){
//fprintf(stderr, "ADVANCING LINE FROM %d/%d\n", n->y, n->x);
  if(n->scrolling || n->autogrow){
    if(truebreak){
      if(ncplane_putchar(n, '\n') < 1){
        return -1;
      }
    }else{
      scroll_down(n);
    }
    return 0;
  }
  // will fail on last line in the absence of scrolling, which is proper
  return ncplane_cursor_move_yx(n, n->y + 1, 0);
}

// put up to a line of text down at the current cursor position. returns the
// number of columns consumed, or -1 on error. the number of bytes consumed is
// added to '*bytes', if 'bytes' is not NULL. any alignment is done relative to
// the current cursor position. any line-breaking character will immediately
// end the output, and move the cursor to the beginning of the next row. on an
// error, '*bytes' is not updated, and nothing is printed.
//
// an input with C columns available on the row can be one of a few things:
//  * text wholly within C columns -- print it, advance x
//  * text + newline within C columns -- print through newline, ++y, x = 0
//  * text + wordbreak at C columns -- print through C, ++y, x = 0
//  * text + text at C columns:
//    * breaker (some text followed by whitespace): print through breaker, ++y, x = 0
//    * no breaker (all one word, with possible leading whitespace):
//      * leading whitespace? dump it, ++y, x = 0
//      * C == dimx: print through C, ++y, x = 0
//      * C < dimx: ++y, x = 0
static int
puttext_line(ncplane* n, ncalign_e align, const char* text, size_t* bytes){
  unsigned cursx; // current cursor location
  ncplane_cursor_yx(n, NULL, &cursx);
  const int dimx = ncplane_dim_x(n);
  const int avail = dimx - cursx - 1;
//fprintf(stderr, "LINE %d starts at %d, len %d, avail %d\n", n->y, cursx, dimx, avail);
  int bytes_leading_ws;    // bytes thus far of leading whitespace
  int cols_leading_ws;     // cols thus far of leading whitespace
  int bytes_leading_break; // bytes through last wordbreaker, 0 for no break yet
  int cols_leading_break;  // cols through last wordbreaker, 0 for no break yet
  int cols = 0;            // columns consumed thus far, cols > cols_leading_ws -> got_glyph
  int b = 0;               // bytes consumed thus far
  bytes_leading_ws = cols_leading_ws = 0;
  bytes_leading_break = cols_leading_break = 0;
  while(cols <= avail){    // we can print everything we've read, if desired
    mbstate_t mbstate = {0};
    wchar_t w;
    const size_t consumed = mbrtowc(&w, text + b, MB_CUR_MAX, &mbstate);
    if(consumed == (size_t)-2 || consumed == (size_t)-1){
      logerror("invalid UTF-8 after %d bytes", b);
      return -1;
    }
//fprintf(stderr, "converted [%s] -> %lc\n", text + b, w);
    if(consumed == 0){ // text was wholly within destination row, print it
      if(ncplane_putline(n, align, cols, text, b) < 0){
        return -1;
      }
      if(bytes){
        *bytes = b;
      }
      return cols;
    }
    // if w is a linebreaker, print what we have, advance, and return
    if(islinebreak(w)){
//fprintf(stderr, "LINEBREAK at %d/%d\n", n->y, n->x);
      if(b){
        if(ncplane_putline(n, align, cols, text, b) < 0){
          return -1;
        }
      }
      if(puttext_advance_line(n, true)){
        return -1;
      }
      if(bytes){
        *bytes += b + consumed;
      }
      return cols;
    }
    b += consumed;
    int width = uc_width(w, "UTF-8");
    if(width < 0){
      width = 0; // FIXME
    }
    cols += width;
    if(iswordbreak(w)){
      if(cols > cols_leading_ws){
        bytes_leading_break = b;
        cols_leading_break = cols;
      }else{
        bytes_leading_ws = b;
        cols_leading_ws = cols;
      }
    }
//fprintf(stderr, "%d approved [%lc] (tbytes: %d tcols: %d)\n", n->y, w, b, cols);
  }
  int colsreturn = 0;
  if(bytes_leading_break){
    if(ncplane_putline(n, align, cols, text, bytes_leading_break) < 0){
      return -1;
    }
    if(bytes){
      *bytes += bytes_leading_break;
    }
    colsreturn = cols_leading_break;
  }else if(bytes_leading_ws){
    if(ncplane_putline(n, align, cols, text, bytes_leading_ws) < 0){
      return -1;
    }
    if(bytes){
      *bytes += bytes_leading_ws;
    }
    colsreturn = cols_leading_ws;
  }else if(cols == dimx){
    if(ncplane_putline(n, align, cols, text, b) < 0){
      return -1;
    }
    if(bytes){
      *bytes = b;
    }
    colsreturn = cols;
  }
//fprintf(stderr, "FELL OFF line %d after %d cols %dB returning %d\n", n->y, cols, b, colsreturn);
  if(puttext_advance_line(n, false)){
    return -1;
  }
  return colsreturn;
}

// FIXME probably best to use u8_wordbreaks() and get all wordbreaks at once...
int ncplane_puttext(ncplane* n, int y, ncalign_e align, const char* text, size_t* bytes){
  if(bytes){
    *bytes = 0;
  }
  int totalcols = 0;
  // text points to the text we have *not* yet output. at each step, we see
  // how much space we have available, and begin iterating from text. remember
  // the most recent linebreaker that we see. when we exhaust our line, print
  // through the linebreaker, and advance text.
  // if we're using NCALIGN_LEFT, we'll be printing with x==-1, i.e. wherever
  // the cursor is. if there's insufficient room to print anything, we need to
  // try moving to the next line first. FIXME this ought actually apply to all
  // alignments, which ought be taken relative to n->x. no change for
  // NCALIGN_RIGHT, but NCALIGN_CENTER needs explicitly handle it...
  do{
    if(y != -1){
      if(ncplane_cursor_move_yx(n, y, -1)){
        return -1;
      }
    }
    size_t linebytes = 0;
    int cols = puttext_line(n, align, text, &linebytes);
    if(cols < 0){
      return -1;
    }
    totalcols += cols;
    if(bytes){
      *bytes += linebytes;
    }
    text += linebytes;
//fprintf(stderr, "new cursor: %d/%d consumed: %zu\n", n->y, n->x, linebytes);
    y = n->y;
  }while(*text);
  return totalcols;
}

