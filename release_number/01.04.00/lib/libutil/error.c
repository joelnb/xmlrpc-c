/* Copyright information is at end of file */

#include "xmlrpc_config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "xmlrpc-c/util_int.h"
#include "xmlrpc-c/util.h"


#define ERROR_BUFFER_SZ (256)

void
xmlrpc_assertion_failed(const char * const fileName,
                        int          const lineNumber) {

    fprintf(stderr, "%s:%d: assertion failed\n", fileName, lineNumber);
    abort();
}



static const char * const default_fault_string =
    "Not enough memory for error message";

void xmlrpc_env_init (xmlrpc_env* env)
{
    XMLRPC_ASSERT(env != NULL);

    env->fault_occurred = 0;
    env->fault_code     = 0;
    env->fault_string   = NULL;
}

void xmlrpc_env_clean (xmlrpc_env* env)
{
    XMLRPC_ASSERT(env != NULL);

    /* env->fault_string may be one of three things:
    **   1) a NULL pointer
    **   2) a pointer to the default_fault_string
    **   3) a pointer to a malloc'd fault string
    ** If we have case (3), we'll need to free it. */
    if (env->fault_string && env->fault_string != default_fault_string)
        free(env->fault_string);
    env->fault_string = XMLRPC_BAD_POINTER;
}



void 
xmlrpc_env_set_fault(xmlrpc_env * const envP,
                     int          const faultCode, 
                     const char * const faultDescription) {

    XMLRPC_ASSERT(envP != NULL); 
    XMLRPC_ASSERT(faultDescription != NULL);

    /* Clean up any leftover pointers. */
    xmlrpc_env_clean(envP);

    envP->fault_occurred = 1;
    envP->fault_code     = faultCode;

    /* Try to copy the fault string. If this fails, use a default. */
    envP->fault_string = (char*) malloc(strlen(faultDescription) + 1);
    if (envP->fault_string)
        strcpy(envP->fault_string, faultDescription);
    else
        envP->fault_string = (char *)default_fault_string;
}



static void
set_fault_formatted_v(xmlrpc_env * const envP,
                      int          const code,
                      const char * const format,
                      va_list      const args) {

    char buffer[ERROR_BUFFER_SZ];

    vsnprintf(buffer, ERROR_BUFFER_SZ, format, args);

    /* vsnprintf is guaranteed to terminate the buffer, but we're paranoid. */
    buffer[ERROR_BUFFER_SZ - 1] = '\0';

    /* Set the fault. */
    xmlrpc_env_set_fault(envP, code, buffer);
}



void 
xmlrpc_env_set_fault_formatted(xmlrpc_env * const envP, 
                               int          const code,
                               const char * const format, 
                               ...) {
    va_list args;

    XMLRPC_ASSERT(envP != NULL);
    XMLRPC_ASSERT(format != NULL);

    /* Print our error message to the buffer. */
    va_start(args, format);
    set_fault_formatted_v(envP, code, format, args);
    va_end(args);
}



void
xmlrpc_faultf(xmlrpc_env * const envP,
              const char * const format,
              ...) {

    va_list args;

    XMLRPC_ASSERT(envP != NULL);
    XMLRPC_ASSERT(format != NULL);

    /* Print our error message to the buffer. */
    va_start(args, format);
    set_fault_formatted_v(envP, XMLRPC_INTERNAL_ERROR, format, args);
    va_end(args);

}



/* Copyright (C) 2001 by First Peer, Inc. All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission. 
**  
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE. */

