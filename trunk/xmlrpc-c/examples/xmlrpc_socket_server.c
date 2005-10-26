/* A simple standalone XML-RPC server written in C as an example of use of
   the Xmlrpc-c libraries.

   This example expects an already bound socket on Standard Input, ready to
   be listened on for client connections.  Also see xmlrpc_sample_add_server,
   which is the same thing, except you tell it a TCP port number and it
   creates the socket itself.
 */

#include <stdlib.h>
#include <stdio.h>
#ifndef WIN32
#include <unistd.h>
#endif

#include <xmlrpc-c/base.h>
#include <xmlrpc-c/server.h>
#include <xmlrpc-c/server_abyss.h>

#include "config.h"  /* information about this build environment */

static xmlrpc_value *
sample_add(xmlrpc_env *   const env, 
           xmlrpc_value * const param_array, 
           void *         const user_data ATTR_UNUSED) {

    xmlrpc_int32 x, y, z;

    /* Parse our argument array. */
    xmlrpc_decompose_value(env, param_array, "(ii)", &x, &y);
    if (env->fault_occurred)
        return NULL;

    /* Add our two numbers. */
    z = x + y;

    /* Sometimes, make it look hard (so client can see what it's like
       to do an RPC that takes a while).
    */
    if (y == 1)
        sleep(2);

    /* Return our result. */
    return xmlrpc_build_value(env, "i", z);
}



int 
main(int           const argc, 
     const char ** const argv) {

    xmlrpc_server_abyss_parms serverparm;
    xmlrpc_registry * registryP;
    xmlrpc_env env;

    if (argc-1 != 0) {
        fprintf(stderr, "There are no arguments.  You must supply a "
                "bound socket on which to listen for client connections "
                "as Standard Input\n");
        if (argv) {} /* silence unused parameter warning */
        exit(1);
    }
    
    xmlrpc_env_init(&env);

    registryP = xmlrpc_registry_new(&env);

    xmlrpc_registry_add_method(
        &env, registryP, NULL, "sample.add", &sample_add, NULL);

    /* In the modern form of the Abyss API, we supply parameters in memory
       like a normal API.  We select the modern form by setting
       config_file_name to NULL: 
    */
    serverparm.config_file_name   = NULL;
    serverparm.registryP          = registryP;
    serverparm.log_file_name      = "/tmp/xmlrpc_log";
    serverparm.keepalive_timeout  = 0;
    serverparm.keepalive_max_conn = 0;
    serverparm.timeout            = 0;
    serverparm.dont_advertise     = FALSE;
    serverparm.socket_bound       = TRUE;
    serverparm.socket_handle      = STDIN_FILENO;

    printf("Running XML-RPC server...\n");

    xmlrpc_server_abyss(&env, &serverparm, XMLRPC_APSIZE(socket_handle));

    /* xmlrpc_server_abyss() never returns */

    return 0;
}
