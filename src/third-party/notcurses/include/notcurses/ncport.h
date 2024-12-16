#ifndef NOTCURSES_NCPORT
#define NOTCURSES_NCPORT

#ifdef __cplusplus
extern "C" {
#endif

// Platform-dependent preprocessor material (includes and definitions) needed
// to compile against Notcurses. A critical definition is htole(), which forces
// 32-bit values to little-endian (as used in the nccell gcluster field). This
// ought be defined so that it's a a no-op on little-endian builds.

#ifndef __MINGW32__                               // All but Windows
#include <netinet/in.h>
#endif

#if defined(__linux__)                            // Linux
#include <byteswap.h>
#define htole(x) (__bswap_32(htonl(x)))
#elif defined(__APPLE__)                          // macOS
#include <libkern/OSByteOrder.h>
#define htole(x) (OSSwapInt32(htonl(x)))
#elif defined(__gnu_hurd__)                       // Hurd
#include <string.h>
#include <byteswap.h>
#define htole(x) (__bswap_32(htonl(x)))
#define wcwidth(w) 1 // FIXME lol, no
#define wcswidth(w, s) (int)(wcslen(w)) // FIXME lol, no
#elif defined(__MINGW32__)                        // Windows
#include <string.h>
#define wcwidth(w) 1 // FIXME lol, no
#define wcswidth(w, s) (int)(wcslen(w)) // FIXME lol, no
#define htole(x) (x) // FIXME are all windows installs LE? ugh
#else                                             // BSDs
#include <sys/endian.h>
#define htole(x) (bswap32(htonl(x)))
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif
