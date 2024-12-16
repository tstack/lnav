#include <fenv.h>
#include <string.h>
#include <locale.h>
#include <pthread.h>
#include <inttypes.h>
#include "notcurses/notcurses.h"
#include "internal.h"

static const wchar_t UTF8_SUBPREFIX[] = L"mµnpfazy"; // 10^24-1
static const wchar_t ASCII_SUBPREFIX[] = L"munpfazy"; // 10^24-1

// we want to use UTF8_SUBPREFIX if we have utf8 available to us. we could
// pull this out of const struct notcurses*, except these ncnmetric() doesn't
// take one, and we don't want to break the API. instead, we call this from
// notcurses_init() when we create a utf8 context. a gross hack =\.
static pthread_once_t utf8_verdict = PTHREAD_ONCE_INIT;
static const wchar_t* SUBPREFIXES = ASCII_SUBPREFIX;

static void
ncmetric_use_utf8_internal(void){
  SUBPREFIXES = UTF8_SUBPREFIX;
}

void ncmetric_use_utf8(void){
  pthread_once(&utf8_verdict, ncmetric_use_utf8_internal);
}

const char* ncnmetric(uintmax_t val, size_t s, uintmax_t decimal,
                      char* buf, int omitdec, uintmax_t mult,
                      int uprefix){
  fesetround(FE_TONEAREST); // FIXME global to the process...ick :/
  // these two must have the same number of elements
  const wchar_t* subprefixes = SUBPREFIXES;
  const wchar_t prefixes[] = L"KMGTPEZY"; // 10^21-1 encompasses 2^64-1
  if(decimal == 0 || mult == 0){
    return NULL;
  }
  if(decimal > UINTMAX_MAX / 10){
    return NULL;
  }
  unsigned consumed = 0;
  uintmax_t dv = mult;
  if(decimal <= val || val == 0){
    // FIXME verify that input < 2^89, wish we had static_assert() :/
    while((val / decimal) >= dv && consumed < sizeof(prefixes) / sizeof(*prefixes)){
      dv *= mult;
      ++consumed;
      if(UINTMAX_MAX / dv < mult){ // near overflow--can't scale dv again
        break;
      }
    }
  }else{
    while(val < decimal && consumed < sizeof(prefixes) / sizeof(*prefixes)){
      val *= mult;
      ++consumed;
      if(UINTMAX_MAX / dv < mult){ // near overflow--can't scale dv again
        break;
      }
    }
  }
  int sprintfed;
  if(dv != mult){ // if consumed == 0, dv must equal mult
    if((val / decimal) / dv > 0){
      ++consumed;
    }else{
      dv /= mult;
    }
    val /= decimal;
    // Remainder is val % dv, but we want a percentage as scaled integer.
    // Ideally we would multiply by 100 and divide the result by dv, for
    // maximum accuracy (dv might not be a multiple of 10--it is not for
    // 1,024). That can overflow with large 64-bit values, but we can first
    // divide both sides by mult, and then scale by 100.
    if(omitdec && (val % dv) == 0){
      sprintfed = snprintf(buf, s, "%" PRIu64 "%lc", (uint64_t)(val / dv),
                          (wint_t)prefixes[consumed - 1]);
    }else{
      sprintfed = snprintf(buf, s, "%.2f%lc", (double)val / dv,
                          (wint_t)prefixes[consumed - 1]);
    }
    if(sprintfed < 0){
      return NULL;
    }
    if(uprefix){
      if((size_t)sprintfed < s){
        buf[sprintfed] = uprefix;
        buf[++sprintfed] = '\0';
      }
    }
    return buf;
  }
  // unscaled output, consumed == 0, dv == mult
  // val / decimal < dv (or we ran out of prefixes)
  if(omitdec && val % decimal == 0){
    if(consumed){
      sprintfed = snprintf(buf, s, "%" PRIu64 "%lc", (uint64_t)(val / decimal),
                          (wint_t)subprefixes[consumed - 1]);
    }else{
      sprintfed = snprintf(buf, s, "%" PRIu64, (uint64_t)(val / decimal));
    }
  }else{
    if(consumed){
      sprintfed = snprintf(buf, s, "%.2f%lc", (double)val / decimal,
                          (wint_t)subprefixes[consumed - 1]);
    }else{
      sprintfed = snprintf(buf, s, "%.2f", (double)val / decimal);
    }
  }
  if(sprintfed < 0){
    return NULL;
  }
  if(consumed && uprefix){
    if((size_t)sprintfed < s){
      buf[sprintfed] = uprefix;
      buf[++sprintfed] = '\0';
    }
  }
  return buf;
}
