#include "internal.h"

int mouse_setup(tinfo* ti, unsigned eventmask){
  if(ti->qterm == TERMINAL_LINUX){
    if(eventmask == 0){
      if(ti->gpmfd < 0){
        return 0;
      }
      ti->gpmfd = -1;
      return gpm_close(ti);
    }
    if(ti->gpmfd < 0){
      // FIXME pass in eventmask
      if((ti->gpmfd = gpm_connect(ti)) < 0){
        return -1;
      }
    }
    return 0;
  }
  if(ti->ttyfd < 0){
    logerror("no tty, not emitting mouse control\n");
    return -1;
  }
  // we'll need to fill in 'h' vs 'l' for both, and the event mode
  char command = 'h';
  // we have to choose one event mode, where all > drag > button > none.
  // if user wants *only* move and not button, we'll need filter those FIXME.
  if(eventmask & NCMICE_MOVE_EVENT){
    ti->mouseproto = '3'; // SET_ALL_EVENT_MOUSE
  }else if(eventmask & NCMICE_DRAG_EVENT){
    ti->mouseproto = '2'; // SET_BTN_EVENT_MOUSE
  }else if(eventmask & NCMICE_BUTTON_EVENT){
    ti->mouseproto = '0'; // SET_X11_MOUSE_PROT
  }else if(eventmask == 0){
    if(ti->mouseproto == 0){
      return 0;
    }
    command = 'l';
  }
// Sets the shift-escape option, allowing shift+mouse to override the standard
// mouse protocol (mainly so copy-and-paste can still be performed).
#define XTSHIFTESCAPE "\x1b[>1s"
  char* mousecmd;
  if(ti->pixelmice){
    static char m[] = XTSHIFTESCAPE "\x1b[?100x;" SET_PIXEL_MOUSE_PROT "x";
    mousecmd = m;
  }else{
    static char m[] = XTSHIFTESCAPE "\x1b[?100x;" SET_SGR_MOUSE_PROT "x";
    mousecmd = m;
  }
  mousecmd[11] = ti->mouseproto;
  mousecmd[17] = command;
  if(command == 'l'){
    ti->mouseproto = 0;
  }
  return tty_emit(mousecmd, ti->ttyfd);
#undef XTSHIFTESCAPE
}
