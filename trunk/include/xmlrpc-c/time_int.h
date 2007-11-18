#ifndef TIME_H_INCLUDED
#define TIME_H_INCLUDED

#include <time.h>

#include "xmlrpc_config.h"
#include "xmlrpc-c/util.h"
#include "xmlrpc-c/int.h"

#if HAVE_TIMESPEC
  typedef struct timespec xmlrpc_timespec;
#else
  typedef struct {
      uint32_t tv_sec;
      uint32_t tv_nsec;
  } xmlrpc_timespec;
#endif

void
xmlrpc_timegm(const struct tm  * const brokenTime,
              time_t *           const timeValueP,
              const char **      const errorP);

void
xmlrpc_localtime(time_t      const datetime,
                 struct tm * const tmP);

void
xmlrpc_gmtime(time_t      const datetime,
              struct tm * const resultP);

#endif
