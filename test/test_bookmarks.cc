/**
 * Copyright (c) 2007-2012, Timothy Stack
 *
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "bookmarks.hh"
#include "textview_curses.hh"

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
