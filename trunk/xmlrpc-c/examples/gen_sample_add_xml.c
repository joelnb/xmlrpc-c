/* This program generates on Standard Output the XML for an XML-RPC
   call suitable for the xmlrpc_sample_add_server program.

   This is the same XML that the xmlrpc_sample_add_client program sends.
*/

#include <stdio.h>

#include <xmlrpc.h>

#include "config.h"

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

    char * const methodName = "sample.add";

    xmlrpc_env env;
    xmlrpc_value * params;
    xmlrpc_mem_block * xmlmemblockP;

    if (argc-1 > 0) {
        fprintf(stderr, "This program has no arguments\n");
        exit(1);
    }

    /* Initialize our error-handling environment. */
    xmlrpc_env_init(&env);

    params = xmlrpc_build_value(&env, "(ii)", 
                                (xmlrpc_int32) 5, (xmlrpc_int32) 7);
    
    die_if_fault_occurred(&env);

    xmlmemblockP = XMLRPC_MEMBLOCK_NEW(char, &env, 0);

    xmlrpc_serialize_call(&env, xmlmemblockP, methodName, params);

    die_if_fault_occurred(&env);

    fwrite(XMLRPC_MEMBLOCK_CONTENTS(char, xmlmemblockP), 
           sizeof(char), 
           XMLRPC_MEMBLOCK_SIZE(char, xmlmemblockP), 
           stdout);

    XMLRPC_MEMBLOCK_FREE(char, xmlmemblockP);

    /* Dispose of our parameter array. */
    xmlrpc_DECREF(params);

    /* Clean up our error-handling environment. */
    xmlrpc_env_clean(&env);
    
    return 0;
}

