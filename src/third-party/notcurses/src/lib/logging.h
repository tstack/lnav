#ifndef NOTCURSES_LOGGING
#define NOTCURSES_LOGGING

#ifdef __cplusplus
extern "C" {
#endif

// logging
extern ncloglevel_e loglevel;

void nclog(ncloglevel_e level, const char* file, int line, const char* fmt, ...)
__attribute__ ((format (printf, 4, 5)));

void
nclog(ncloglevel_e level, const char* file, int line, const char* fmt, ...);

#define logpanic(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_PANIC){ \
    nclog(NCLOGLEVEL_PANIC, __func__, __LINE__, fmt, ##__VA_ARGS__); } \
  } while(0);

#define logfatal(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_FATAL){ \
    nclog(NCLOGLEVEL_FATAL, __func__, __LINE__, fmt, ##__VA_ARGS__); } \
  } while(0);

#define logerror(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_ERROR){ \
    nclog(NCLOGLEVEL_ERROR, __func__, __LINE__, fmt, ##__VA_ARGS__); } \
  } while(0);

#define logwarn(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_WARNING){ \
    nclog(NCLOGLEVEL_WARNING, __func__, __LINE__, fmt, ##__VA_ARGS__); } \
  } while(0);

#define loginfo(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_INFO){ \
    nclog(NCLOGLEVEL_INFO, __func__, __LINE__, fmt, ##__VA_ARGS__); } \
  } while(0);

#define logverbose(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_VERBOSE){ \
    nclog(NCLOGLEVEL_VERBOSE, __func__, __LINE__, fmt, ##__VA_ARGS__); } \
  } while(0);

#define logdebug(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_DEBUG){ \
    nclog(NCLOGLEVEL_DEBUG, __func__, __LINE__, fmt, ##__VA_ARGS__); } \
  } while(0);

#define logtrace(fmt, ...) do{ \
  if(loglevel >= NCLOGLEVEL_TRACE){ \
    nclog(NCLOGLEVEL_TRACE, __func__, __LINE__, fmt, ##__VA_ARGS__); } \
  } while(0);

#ifdef __cplusplus
}
#endif

#endif
