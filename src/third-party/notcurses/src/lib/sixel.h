#ifndef NOTCURSES_LIB_SIXEL
#define NOTCURSES_LIB_SIXEL

#ifdef __cplusplus
extern "C" {
#endif

#include <ctype.h>
#include <stdlib.h>
#include "logging.h"

uint32_t* ncsixel_as_rgba(const char *sx, unsigned leny, unsigned lenx){
  if(!leny || !lenx){
    logerror("null sixel geometry");
    return NULL;
  }
#define MAXCOLORS 65535
  // cast is necessary for c++ callers
  uint32_t* rgba = (uint32_t*)calloc(leny * lenx, sizeof(*rgba));
  if(rgba == NULL){
    return NULL;
  }
  // cast is necessary for c++ callers
  uint32_t* colors = (uint32_t*)calloc(MAXCOLORS, sizeof(*colors));
  if(colors == NULL){
    free(rgba);
    return NULL;
  }
  // first we skip the header. it ends in either a color declaration, or a
  // transparent line (the only possible colorless line).
  while(*sx != '#' && *sx != '-'){
    if(!*sx){
      logerror("expected octothorpe/hyphen, got eol");
      return NULL;
    }
    ++sx;
  }
  // now we build the color table (form: #Pc;Pu;Px;Py;Pz). data starts with
  // a color spec lacking a semicolon.
  enum {
    STATE_WANT_HASH,
    STATE_WANT_COLOR,
    STATE_WANT_COLORSEMI,
    STATE_WANT_COLORSPACE,
    STATE_WANT_DATA,
  } state = STATE_WANT_HASH;
  unsigned color = 0;
  unsigned x = 0;
  unsigned y = 0;
  unsigned rle = 1;
  while(*sx){
//fprintf(stderr, "SX: %u 0x%02x %c\n", *sx, *sx, *sx);
    if(*sx == '\e'){
      break;
    }
    if(state == STATE_WANT_HASH){
      if(*sx == '-'){
        x = 0;
        y += 6;
      }else if('#' == *sx){
        state = STATE_WANT_COLOR;
      }else{
        logerror("expected octothorpe, got %u", *sx);
        goto err;
      }
    }else if(state == STATE_WANT_COLOR){
      if(!isdigit(*sx)){
        logerror("expected digit, got %u", *sx);
        goto err;
      }
      color = 0;
      do{
//fprintf(stderr, "SX: %u 0x%02x %c\n", *sx, *sx, *sx);
        color *= 10;
        color += *sx - '0';
        ++sx;
      }while(isdigit(*sx));
//std::cerr << "Got color " << color << std::endl;
      --sx;
      state = STATE_WANT_COLORSEMI;
    }else if(state == STATE_WANT_COLORSEMI){
      // if we get a semicolon, we're a colorspec, otherwise data
      if(*sx == ';'){
        state = STATE_WANT_COLORSPACE;
      }else{
        state = STATE_WANT_DATA;
        rle = 1;
      }
    }else if(state == STATE_WANT_COLORSPACE){
      if('2' != *(sx++)){
        logerror("expected '2', got %u", *sx);
        goto err;
      }
//fprintf(stderr, "SX: %u 0x%02x %c\n", *sx, *sx, *sx);
      if(';' != *(sx++)){
        logerror("expected semicolon, got %u", *sx);
        goto err;
      }
//fprintf(stderr, "SX: %u 0x%02x %c\n", *sx, *sx, *sx);
      int r = 0;
      do{
        r *= 10;
        r += *sx - '0';
        ++sx;
      }while(isdigit(*sx));
      if(';' != *(sx++)){
        logerror("expected semicolon, got %u", *sx);
        goto err;
      }
//fprintf(stderr, "SX: %u 0x%02x %c\n", *sx, *sx, *sx);
      r = r * 255 / 100;
      int g = 0;
      do{
        g *= 10;
        g += *sx - '0';
        ++sx;
      }while(isdigit(*sx));
      if(';' != *(sx++)){
        logerror("expected semicolon, got %u", *sx);
        goto err;
      }
//fprintf(stderr, "SX: %u 0x%02x %c\n", *sx, *sx, *sx);
      g = g * 255 / 100;
      int b = 0;
      do{
        b *= 10;
        b += *sx - '0';
        ++sx;
      }while(isdigit(*sx));
      b = b * 255 / 100;
      ncpixel_set_a(&colors[color], 0xff);
      ncpixel_set_rgb8(&colors[color], r, g, b);
//fprintf(stderr, "Got color %d: 0x%08x %u %u %u\n", color, colors[color], r, g, b);
      if(color >= MAXCOLORS){
        goto err;
      }
      state = STATE_WANT_HASH;
      --sx;
    }
    // read until we hit next colorspec
    if(state == STATE_WANT_DATA){
//fprintf(stderr, "Character %c\n", *sx);
      if(*sx == '#'){
        state = STATE_WANT_HASH;
        --sx;
      }else if(*sx == '!'){ // RLE
        ++sx;
        rle = 0;
        do{
          rle *= 10;
          rle += *sx - '0';
          ++sx;
        }while(isdigit(*sx));
        if(0 == rle){
          rle = 1;
        }
        --sx;
      }else if(*sx == '$'){
        x = 0;
        state = STATE_WANT_DATA;
      }else if(*sx == '-'){
        x = 0;
        y += 6;
        state = STATE_WANT_DATA;
      }else{
//fprintf(stderr, "RLE: %d pos: %d x %d\n", rle, y, x);
        if(y + 6 > (leny + 5) / 6 * 6){
          logerror("too many rows %d + 6 > %d", y, (leny + 5) / 6 * 6);
          goto err;
        }
        if(x + rle > lenx){
          logerror("invalid rle %d + %d > %d", x, rle, lenx);
          goto err;
        }
        for(unsigned xpos = x ; xpos < x + rle ; ++xpos){
          for(unsigned ypos = y ; ypos < y + 6 ; ++ypos){
            if((*sx - 63) & (1u << (ypos - y))){
              // ought be an empty pixel
//std::cerr << *s << " BMAP[" << ypos << "][" << xpos << "] = " << std::hex << colors[color] << std::dec << std::endl;
              rgba[ypos * lenx + xpos] = colors[color];
            }
          }
        }
        x += rle;
        rle = 1;
      }
    }
    ++sx;
  }
  free(colors);
  return rgba;

err:
  free(rgba);
  free(colors);
  return NULL;
#undef MAXCOLORS
}

#ifdef __cplusplus
}
#endif

#endif
