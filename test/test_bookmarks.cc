
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "bookmarks.hh"

int main(int argc, char *argv[])
{
  int lpc, retval = EXIT_SUCCESS;
  bookmark_vector<vis_line_t> bv, bv_cp;

  bv.insert_once(vis_line_t(1));
  bv.insert_once(vis_line_t(1));
  assert(bv.size() == 1);

  bv.insert_once(vis_line_t(3));
  bv.insert_once(vis_line_t(2));
  assert(bv[0] == 1);
  assert(bv[1] == 2);
  assert(bv[2] == 3);
  
  bv.clear();
  assert(bv.next(vis_line_t(0)) == -1);
  assert(bv.prev(vis_line_t(0)) == -1);
  assert(bv.next(vis_line_t(100)) == -1);
  assert(bv.prev(vis_line_t(100)) == -1);
  
  bv.insert_once(vis_line_t(2));

  assert(bv.next(vis_line_t(0)) == 2);
  assert(bv.next(vis_line_t(2)) == -1);
  assert(bv.next(vis_line_t(3)) == -1);
  
  assert(bv.prev(vis_line_t(3)) == 2);
  assert(bv.prev(vis_line_t(2)) == -1);
  
  bv.insert_once(vis_line_t(4));
  
  assert(bv.next(vis_line_t(0)) == 2);
  assert(bv.next(vis_line_t(2)) == 4);
  assert(bv.next(vis_line_t(3)) == 4);
  assert(bv.next(vis_line_t(4)) == -1);
  
  assert(bv.prev(vis_line_t(10)) == 4);
  assert(bv.prev(vis_line_t(5)) == 4);
  assert(bv.prev(vis_line_t(4)) == 2);
  assert(bv.prev(vis_line_t(2)) == -1);
  
  bv.clear();

  const int LINE_COUNT = 10000;
  
  for (lpc = 0; lpc < 1000; lpc++) {
    bv.insert_once(vis_line_t(random() % LINE_COUNT));
  }
  bv_cp = bv;
  sort(bv_cp.begin(), bv_cp.end());
  assert(equal(bv.begin(), bv.end(), bv_cp.begin()));
  unique(bv_cp.begin(), bv_cp.end());
  assert(equal(bv.begin(), bv.end(), bv_cp.begin()));

  {
    vis_line_t last_line(-1);
    
    for (lpc = 0; lpc != -1; lpc = bv.next(vis_line_t(lpc))) {
      assert(lpc >= 0);
      assert(lpc < LINE_COUNT);
      assert(last_line < lpc);
      
      last_line = vis_line_t(lpc);
    }

    last_line = vis_line_t(10000);
    for (lpc = LINE_COUNT - 1; lpc != -1; lpc = bv.prev(vis_line_t(lpc))) {
      assert(lpc >= 0);
      assert(lpc < LINE_COUNT);
      assert(last_line > lpc);
      
      last_line = vis_line_t(lpc);
    }
  }
  
  return retval;
}
