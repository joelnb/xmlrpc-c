/* A simple synchronous XML-RPC client written in C, as an example of
  an xmlrpc-c client.  This invokes the sample.add procedure that the
  xmlrpc-c example server.c server provides.  I.e. it adds to numbers
  together, the hard way.
*/

#include <stdio.h>

#include <xmlrpc.h>
#include <xmlrpc_client.h>

#include "config.h"  /* information about this build environment */

#define NAME "XML-RPC C Test Client"
#define VERSION "1.0"

static void 
die_if_fault_occurred (xmlrpc_env *env) {
    if (env->fault_occurred) {
        fprintf(stderr, "XML-RPC Fault: %s (%d)\n",
                env->fault_string, env->fault_code);
        exit(1);
    }
}



int 
main(int           const argc, 
     const char ** const argv ATTR_UNUSED) {
    xmlrpc_env env;
    xmlrpc_value *result;
    int sum;
    char * const url = "http://localhost:8080/RPC2";
    char * const methodName = "sample.add";

    if (argc-1 > 0) {
        fprintf(stderr, "This program has no arguments\n");
        exit(1);
    }

    /* Initialize our error-handling environment. */
    xmlrpc_env_init(&env);

    /* Start up our XML-RPC client library. */
    xmlrpc_client_init2(&env, XMLRPC_CLIENT_NO_FLAGS, NAME, VERSION, NULL, 0);
    die_if_fault_occurred(&env);

    printf("Making XMLRPC call to server url '%s' method '%s' "
           "to request the sum "
           "of 5 and 7...\n", url, methodName);

    /* Make the remote procedure call */
    result = xmlrpc_client_call(&env, url, methodName,
                "(ii)", (xmlrpc_int32) 5, (xmlrpc_int32) 7);
    die_if_fault_occurred(&env);
    
    /* Get our state name and print it out. */
    xmlrpc_parse_value(&env, result, "i", &sum);
    die_if_fault_occurred(&env);
    printf("The sum  is %d\n", sum);
    
    /* Dispose of our result value. */
    xmlrpc_DECREF(result);

    /* Clean up our error-handling environment. */
    xmlrpc_env_clean(&env);
    
    /* Shutdown our XML-RPC client library. */
    xmlrpc_client_cleanup();

    return 0;
}

