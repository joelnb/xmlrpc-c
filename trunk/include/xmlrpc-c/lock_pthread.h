#ifndef LOCK_PTHREAD_H_INCLUDED
#define LOCK_PTHREAD_H_INCLUDED

#include "xmlrpc-c/c_util.h"  /* For XMLRPC_DLLEXPORT */
#include "lock.h"

/*
  XMLRPC_UTIL_EXPORTED marks a symbol in this file that is exported from
  libxmlrpc_util.

  XMLRPC_BUILDING_UTIL says this compilation is part of libxmlrpc_util, as
  opposed to something that _uses_ libxmlrpc_util.
*/
#ifdef XMLRPC_BUILDING_UTIL
#define XMLRPC_UTIL_EXPORTED XMLRPC_DLLEXPORT
#else
#define XMLRPC_UTIL_EXPORTED
#endif

XMLRPC_UTIL_EXPORTED
struct lock *
xmlrpc_lock_create_pthread(void);

#endif
