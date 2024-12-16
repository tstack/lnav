#ifndef NOTCURSES_LOGGING
#define NOTCURSES_LOGGING

#ifdef __cplusplus
extern "C" {
#endif

// logging
extern ncloglevel_e loglevel;

static inline void nclog(const char* fmt, ...)
__attribute__ ((format (printf, 1, 2)));

static inline void
nclog(const char* fmt, ...){
  va_list va;
  va_start(va, fmt);
  vfprintf(stderr, fmt, va);
  va_end(va);
}

#define logpanic(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_PANIC){ \
    nclog("%s:%d:" fmt NL, __func__, __LINE__, ##__VA_ARGS__); } \
  } while(0);

#define logfatal(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_FATAL){ \
    nclog("%s:%d:" fmt NL, __func__, __LINE__, ##__VA_ARGS__); } \
  } while(0);

#define logerror(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_ERROR){ \
    nclog("%s:%d:" fmt NL, __func__, __LINE__, ##__VA_ARGS__); } \
  } while(0);

#define logwarn(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_WARNING){ \
    nclog("%s:%d:" fmt NL, __func__, __LINE__, ##__VA_ARGS__); } \
  } while(0);

#define loginfo(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_INFO){ \
    nclog("%s:%d:" fmt NL, __func__, __LINE__, ##__VA_ARGS__); } \
  } while(0);

#define logverbose(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_VERBOSE){ \
    nclog("%s:%d:" fmt NL, __func__, __LINE__, ##__VA_ARGS__); } \
  } while(0);

#define logdebug(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_DEBUG){ \
    nclog("%s:%d:" fmt NL, __func__, __LINE__, ##__VA_ARGS__); } \
  } while(0);

#define logtrace(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_TRACE){ \
    nclog("%s:%d:" fmt NL, __func__, __LINE__, ##__VA_ARGS__); } \
  } while(0);

#ifdef __cplusplus
}
#endif

#endif
