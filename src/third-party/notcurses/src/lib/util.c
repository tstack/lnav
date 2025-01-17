#ifndef __MINGW32__
#include <pwd.h>
#include <unistd.h>
#if defined(__linux__) || defined(__gnu_hurd__)
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#elif !defined(__MINGW32__)
#if 0
#include <sys/sysctl.h>
#include <sys/utsname.h>
#endif
#endif
#else
#include <winsock2.h>
#define SECURITY_WIN32
#include <secext.h>
#include <sysinfoapi.h>
#endif
#include "internal.h"

int set_loglevel_from_env(ncloglevel_e* llptr){
  const char* ll = getenv("NOTCURSES_LOGLEVEL");
  if(ll == NULL){
    return 0;
  }
  char* endl;
  long l = strtol(ll, &endl, 10);
  if(l < NCLOGLEVEL_PANIC || l > NCLOGLEVEL_TRACE){
    logpanic("illegal NOTCURSES_LOGLEVEL: %s", ll);
    return -1;
  }
  *llptr = l;
  loginfo("got loglevel from environment: %ld", l);
  return 0;
}

char* notcurses_accountname(void){
#ifndef __MINGW32__
  const char* un;
  if( (un = getenv("LOGNAME")) ){
    return strdup(un);
  }
  uid_t uid = getuid();
  struct passwd* p = getpwuid(uid);
  if(p == NULL){
    return NULL;
  }
  return strdup(p->pw_name);
#else
  DWORD unlen = UNLEN + 1;
  char* un = malloc(unlen);
  if(un == NULL){
    return NULL;
  }
  if(!GetUserNameExA(NameSamCompatible, un, &unlen)){
    logerror("couldn't get user name");
    free(un);
    return NULL;
  }
  return un;
#endif
}

char* notcurses_hostname(void){
#ifndef __MINGW32__
  char hostname[_POSIX_HOST_NAME_MAX + 1];
  if(gethostname(hostname, sizeof(hostname)) == 0){
    char* fqdn = strchr(hostname, '.');
    if(fqdn){
      *fqdn = '\0';
    }
    return strdup(hostname);
  }
#else // windows
  char lp[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD s = sizeof(lp);
  if(GetComputerNameA(lp, &s)){
    return strdup(lp);
  }
#endif
  return NULL;
}

#if 0
char* notcurses_osversion(void){
#ifdef __MINGW32__
  // FIXME get version
  return strdup("Microsoft Windows");
#else
#ifdef __APPLE__
#define PREFIX "macOS "
  char osver[30] = PREFIX; // shrug
  size_t oldlenp = sizeof(osver) - strlen(PREFIX);
  if(sysctlbyname("kern.osproductversion", osver + strlen(PREFIX),
                  &oldlenp, NULL, 0) == 0){
    return strdup(osver);
  }
  return strdup("macOS");
#else
  struct utsname uts;
  if(uname(&uts)){
    logerror("failure invoking uname (%s)", strerror(errno));
    return NULL;
  }
  const size_t nlen = strlen(uts.sysname);
  const size_t rlen = strlen(uts.release);
  size_t tlen = nlen + rlen + 2;
  char* ret = malloc(tlen);
  memcpy(ret, uts.sysname, nlen);
  ret[nlen] = ' ';
  strcpy(ret + nlen + 1, uts.release);
  return ret;
#endif
#undef PREFIX
#endif
}
#endif
