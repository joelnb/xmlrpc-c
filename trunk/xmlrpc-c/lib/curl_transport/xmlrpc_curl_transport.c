/*=============================================================================
                           xmlrpc_curl_transport
===============================================================================
   Curl-based client transport for Xmlrpc-c

   By Bryan Henderson 04.12.10.

   Contributed to the public domain by its author.
=============================================================================*/

#include "xmlrpc_config.h"

#include "bool.h"
#include "mallocvar.h"
#include "linklist.h"
#include "casprintf.h"
#include "xmlrpc.h"
#include "xmlrpc_int.h"
#include "xmlrpc_client.h"
#include "xmlrpc_client_int.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>

#if defined (WIN32) && defined(_DEBUG)
#  include <crtdbg.h>
#  define new DEBUG_NEW
#  define malloc(size) _malloc_dbg( size, _NORMAL_BLOCK, __FILE__, __LINE__)
#  undef THIS_FILE
   static char THIS_FILE[] = __FILE__;
#endif /*WIN32 && _DEBUG*/



struct clientTransport {
    pthread_mutex_t listLock;
    struct list_head rpcList;
        /* List of all RPCs that exist for this transport.  An RPC exists
           from the time the user requests it until the time the user 
           acknowledges it is done.
        */
    const char * interfaceId;
        /* This identifies the network interface on the local side to
           use for the session.  It is an ASCIIZ string in the form
           that the Curl recognizes for setting its CURLOPT_INTERFACE
           option (also the --interface option of the Curl program).
           E.g. "9.1.72.189" or "giraffe-data.com" or "eth0".  

           It isn't necessarily valid, but it does have a terminating NUL.

           NULL means we have no preference.
        */
};

typedef struct {
    /* This is all stuff that really ought to be in the CURL object,
       but the Curl library is a little too simple for that.  So we
       build a layer on top of it, and call it a "transaction," as
       distinct from the Curl "session" represented by the CURL object.
    */
    CURL * curlSessionP;
        /* Handle for Curl library session object */
    char curlError[CURL_ERROR_SIZE];
        /* Error message from Curl */
    struct curl_slist * headerList;
        /* The HTTP headers for the transaction */
    const char * serverUrl;  /* malloc'ed - belongs to this object */
} curlTransaction;



typedef struct {
    struct list_head link;  /* link in transport's list of RPCs */
    curlTransaction * curlTransactionP;
        /* The object which does the HTTP transaction, with no knowledge
           of XML-RPC or Xmlrpc-c.
        */
    xmlrpc_mem_block * responseXmlP;
    xmlrpc_bool threadExists;
    pthread_t thread;
    transport_asynch_complete complete;
        /* Routine to call to complete the RPC after it is complete HTTP-wise.
           NULL if none.
        */
    struct call_info * callInfoP;
        /* User's identifier for this RPC */
} rpc;



static size_t 
collect(void *  const ptr, 
        size_t  const size, 
        size_t  const nmemb,  
        FILE  * const stream) {
/*----------------------------------------------------------------------------
   This is a Curl output function.  Curl calls this to deliver the
   HTTP response body.  Curl thinks it's writing to a POSIX stream.
-----------------------------------------------------------------------------*/
    xmlrpc_mem_block * const responseXmlP = (xmlrpc_mem_block *) stream;
    char * const buffer = ptr;
    size_t const length = nmemb * size;

    size_t retval;
    xmlrpc_env env;

    xmlrpc_env_init(&env);
    xmlrpc_mem_block_append(&env, responseXmlP, buffer, length);
    if (env.fault_occurred)
        retval = (size_t)-1;
    else
        /* Really?  Shouldn't it be like fread() and return 'nmemb'? */
        retval = length;
    
    return retval;
}



static void
initWindowsStuff(xmlrpc_env * const envP) {

#if defined (WIN32)
    /* This is CRITICAL so that cURL-Win32 works properly! */
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;
    wVersionRequested = MAKEWORD(1, 1);
    
    err = WSAStartup(wVersionRequested, &wsaData);
    if (err)
        xmlrpc_env_set_fault_formatted(
            envP, XMLRPC_INTERNAL_ERROR,
            "Winsock startup failed.  WSAStartup returned rc %d", err);
    else {
        if (LOBYTE(wsaData.wVersion) != 1 || HIBYTE(wsaData.wVersion) != 1) {
            /* Tell the user that we couldn't find a useable */ 
            /* winsock.dll. */ 
            xmlrpc_env_set_fault_formatted(
                envP, XMLRPC_INTERNAL_ERROR, "Winsock reported that "
                "it does not implement the requested version 1.1.");
        }
        if (envP->fault_occurred)
            WSACleanup();
    }
#else
    if (0)
        envP->fault_occurred = TRUE;  /* Avoid unused parm warning */
#endif
}


static void 
create(xmlrpc_env *                     const envP,
       int                              const flags ATTR_UNUSED,
       const char *                     const appname ATTR_UNUSED,
       const char *                     const appversion ATTR_UNUSED,
       const struct xmlrpc_xportparms * const transportparmsP,
       size_t                           const parm_size,
       struct clientTransport **        const handlePP) {
/*----------------------------------------------------------------------------
   This does the 'create' operation for a Curl client transport.
-----------------------------------------------------------------------------*/
    struct xmlrpc_curl_xportparms * const curlXportParmsP = 
        (struct xmlrpc_curl_xportparms *) transportparmsP;

    struct clientTransport * transportP;

    initWindowsStuff(envP);

    MALLOCVAR(transportP);
    if (transportP == NULL)
        xmlrpc_env_set_fault_formatted(
            envP, XMLRPC_INTERNAL_ERROR, 
            "Unable to allocate transport descriptor.");
    else {
        pthread_mutex_init(&transportP->listLock, NULL);
        
        list_make_empty(&transportP->rpcList);

        /*
         * This is the main global constructor for the app. Call this before
         * _any_ libcurl usage. If this fails, *NO* libcurl functions may be
         * used, or havoc may be the result.
         */
        curl_global_init(CURL_GLOBAL_ALL);

        /* The above makes it look like Curl is not re-entrant.  We should
           check into that.
        */
        
        if (!curlXportParmsP || parm_size < XMLRPC_CXPSIZE(interfaceId))
            transportP->interfaceId = NULL;
        else if (curlXportParmsP->interfaceId == NULL)
            transportP->interfaceId = NULL;
        else {
            transportP->interfaceId = strdup(curlXportParmsP->interfaceId);
            if (transportP->interfaceId == NULL)
                xmlrpc_env_set_fault_formatted(
                    envP, XMLRPC_INTERNAL_ERROR,
                    "Unable to allocate space for interface name.");
        }

        if (envP->fault_occurred)
            free(transportP);
    }
    *handlePP = transportP;
}



static void
termWindowStuff(void) {

#if defined (WIN32)
    WSACleanup();
#endif
}



static void 
destroy(struct clientTransport * const clientTransportP) {
/*----------------------------------------------------------------------------
   This does the 'destroy' operation for a Curl client transport.
-----------------------------------------------------------------------------*/
    XMLRPC_ASSERT(clientTransportP != NULL);

    XMLRPC_ASSERT(list_is_empty(&clientTransportP->rpcList));

    pthread_mutex_destroy(&clientTransportP->listLock);

    if (clientTransportP->interfaceId)
        strfree(clientTransportP->interfaceId);

    curl_global_cleanup();

    termWindowStuff();

    free(clientTransportP);
}



static void
createCurlHeaderList(xmlrpc_env *               const envP,
                     const xmlrpc_server_info * const serverP,
                     struct curl_slist **       const headerListP) {

    struct curl_slist * headerList;

    headerList = NULL;  /* initial value */

    headerList = curl_slist_append(headerList, "Content-Type: text/xml");

    if (headerList == NULL)
        xmlrpc_env_set_fault_formatted(
                envP, XMLRPC_INTERNAL_ERROR, 
                "Could not add header.  curl_slist_append() failed.");
    else {
        /* Send an authorization header if we need one. */
        if (serverP->_http_basic_auth) {
            /* Make the authentication header "Authorization: " */

            const char * authHeader;
            
            casprintf(&authHeader, "Authorization: %s",
                      serverP->_http_basic_auth);
            
            if (authHeader == NULL)
                xmlrpc_env_set_fault_formatted(
                    envP, XMLRPC_INTERNAL_ERROR,
                    "Couldn't allocate memory for authentication header");
            else {
                headerList = curl_slist_append(headerList, authHeader);
                if (headerList == NULL)
                    xmlrpc_env_set_fault_formatted(
                        envP, XMLRPC_INTERNAL_ERROR,
                        "Could not add authentication header.  "
                        "curl_slist_append() failed.");
                strfree(authHeader);
            }
        }
        if (envP->fault_occurred)
            free(headerList);
    }
    *headerListP = headerList;
}



static void
setupCurlSession(xmlrpc_env *       const envP,
                 curlTransaction *  const curlTransactionP,
                 const char *       const interfaceId,
                 xmlrpc_mem_block * const callXmlP,
                 xmlrpc_mem_block * const responseXmlP) {

    CURL * const curlSessionP = curlTransactionP->curlSessionP;

    curl_easy_setopt(curlSessionP, CURLOPT_POST, 1 );
    if (interfaceId)
        curl_easy_setopt(curlSessionP, CURLOPT_INTERFACE, interfaceId);
    curl_easy_setopt(curlSessionP, CURLOPT_URL, curlTransactionP->serverUrl);
    XMLRPC_MEMBLOCK_APPEND(char, envP, callXmlP, "\0", 1);
    if (!envP->fault_occurred) {
        curl_easy_setopt(curlSessionP, CURLOPT_POSTFIELDS, 
                         XMLRPC_MEMBLOCK_CONTENTS(char, callXmlP));
        
        curl_easy_setopt(curlSessionP, CURLOPT_FILE, responseXmlP);
        curl_easy_setopt(curlSessionP, CURLOPT_HEADER, 0 );
        curl_easy_setopt(curlSessionP, CURLOPT_WRITEFUNCTION, collect);
        curl_easy_setopt(curlSessionP, CURLOPT_ERRORBUFFER, 
                         curlTransactionP->curlError);
        curl_easy_setopt(curlSessionP, CURLOPT_NOPROGRESS, 1);
        
        curl_easy_setopt(curlSessionP, CURLOPT_HTTPHEADER, 
                         curlTransactionP->headerList);
    }
}



static void
createCurlTransaction(xmlrpc_env *               const envP,
                      const char *               const interfaceId,
                      const xmlrpc_server_info * const serverP,
                      xmlrpc_mem_block *         const callXmlP,
                      xmlrpc_mem_block *         const responseXmlP,
                      curlTransaction **         const curlTransactionPP) {

    curlTransaction * curlTransactionP;

    MALLOCVAR(curlTransactionP);
    if (curlTransactionP == NULL)
        xmlrpc_env_set_fault_formatted(
            envP, XMLRPC_INTERNAL_ERROR,
            "No memory to create Curl transaction.");
    else {
        CURL * const curlSessionP = curl_easy_init();
    
        if (curlSessionP == NULL)
            xmlrpc_env_set_fault_formatted(
                envP, XMLRPC_INTERNAL_ERROR,
                "Could not create Curl session.  curl_easy_init() failed.");
        else {
            curlTransactionP->curlSessionP = curlSessionP;

            curlTransactionP->serverUrl = strdup(serverP->_server_url);
            if (curlTransactionP->serverUrl == NULL)
                xmlrpc_env_set_fault_formatted(
                    envP, XMLRPC_INTERNAL_ERROR,
                    "Out of memory to store server URL.");
            else {
                createCurlHeaderList(envP, serverP, 
                                     &curlTransactionP->headerList);

                if (!envP->fault_occurred)
                    setupCurlSession(envP, curlTransactionP, interfaceId,
                                     callXmlP, responseXmlP);

                if (envP->fault_occurred)
                    strfree(curlTransactionP->serverUrl);
            }
            if (envP->fault_occurred)
                curl_easy_cleanup(curlSessionP);
        }
        if (envP->fault_occurred)
            free(curlTransactionP);
    }
    *curlTransactionPP = curlTransactionP;
}



static void
destroyCurlTransaction(curlTransaction * const curlTransactionP) {

    curl_slist_free_all(curlTransactionP->headerList);
    strfree(curlTransactionP->serverUrl);
    curl_easy_cleanup(curlTransactionP->curlSessionP);

    free(curlTransactionP);
}



static void
performCurlTransaction(xmlrpc_env *      const envP,
                       curlTransaction * const curlTransactionP) {

    CURL * const curlSessionP = curlTransactionP->curlSessionP;

    CURLcode res;

    res = curl_easy_perform(curlSessionP);
    
    if (res != CURLE_OK)
        xmlrpc_env_set_fault_formatted(
            envP, XMLRPC_NETWORK_ERROR, "Curl failed to perform "
            "HTTP POST request.  curl_easy_perform() says: %s", 
            curlTransactionP->curlError);
    else {
        CURLcode res;
        long http_result;
        res = curl_easy_getinfo(curlSessionP, CURLINFO_HTTP_CODE, 
                                &http_result);

        if (res != CURLE_OK)
            xmlrpc_env_set_fault_formatted(
                envP, XMLRPC_INTERNAL_ERROR, 
                "Curl performed the HTTP POST request, but was "
                "unable to say what the HTTP result code was.  "
                "curl_easy_getinfo(CURLINFO_HTTP_CODE) says: %s", 
                curlTransactionP->curlError);
        else {
            if (http_result != 200)
                xmlrpc_env_set_fault_formatted(
                    envP, XMLRPC_NETWORK_ERROR, "HTTP response: %ld",
                    http_result);
        }
    }
}



static void
doAsyncRpc2(void * const arg) {

    rpc * const rpcP = arg;

    xmlrpc_env env;

    xmlrpc_env_init(&env);

    performCurlTransaction(&env, rpcP->curlTransactionP);

    rpcP->complete(rpcP->callInfoP, rpcP->responseXmlP, env);

    xmlrpc_env_clean(&env);
}



#ifdef WIN32

static unsigned __stdcall 
doAsyncRpc(void * arg) {
    doAsyncRpc2(arg);
    return 0;
}

#else

static void *
doAsyncRpc(void * arg) {
    doAsyncRpc2(arg);
    return NULL;
}

#endif



static void
createRpcThread(xmlrpc_env *              const envP,
                rpc *                     const rpcP,
                pthread_t *               const threadP) {

    int rc;

    rc = pthread_create(threadP, NULL, doAsyncRpc, rpcP);
    switch (rc) {
    case 0: 
        break;
    case EAGAIN:
        xmlrpc_env_set_fault_formatted(
            envP, XMLRPC_INTERNAL_ERROR, 
            "pthread_create() failed:  System Resources exceeded.");
        break;
    case EINVAL:
        xmlrpc_env_set_fault_formatted(
            envP, XMLRPC_INTERNAL_ERROR, 
            "pthread_create() failed:  Param Error for attr.");
        break;
    case ENOMEM:
        xmlrpc_env_set_fault_formatted(
            envP, XMLRPC_INTERNAL_ERROR, 
            "pthread_create() failed:  No memory for new thread.");
        break;
    default:
        xmlrpc_env_set_fault_formatted(
            envP, XMLRPC_INTERNAL_ERROR, 
            "pthread_create() failed: Unrecognized error code %d.", rc);
        break;
    }
}



static void
rpcCreate(xmlrpc_env *               const envP,
          struct clientTransport *   const clientTransportP,
          const xmlrpc_server_info * const serverP,
          xmlrpc_mem_block *         const callXmlP,
          xmlrpc_mem_block *         const responseXmlP,
          transport_asynch_complete        complete, 
          struct call_info *         const callInfoP,
          rpc **                     const rpcPP) {

    rpc * rpcP;

    MALLOCVAR(rpcP);
    if (rpcP == NULL)
        xmlrpc_env_set_fault_formatted(
            envP, XMLRPC_INTERNAL_ERROR,
            "Couldn't allocate memory for rpc object");
    else {
        rpcP->callInfoP = callInfoP;
        rpcP->complete  = complete;
        rpcP->responseXmlP = responseXmlP;
        rpcP->threadExists = FALSE;

        createCurlTransaction(envP, clientTransportP->interfaceId, serverP,
                              callXmlP, responseXmlP, 
                              &rpcP->curlTransactionP);
        if (!envP->fault_occurred) {
            if (complete) {
                createRpcThread(envP, rpcP, &rpcP->thread);
                if (!envP->fault_occurred)
                    rpcP->threadExists = TRUE;
            }
            if (!envP->fault_occurred) {
                list_init_header(&rpcP->link, rpcP);
                pthread_mutex_lock(&clientTransportP->listLock);
                list_add_head(&clientTransportP->rpcList, &rpcP->link);
                pthread_mutex_unlock(&clientTransportP->listLock);
            }
            if (envP->fault_occurred)
                    destroyCurlTransaction(rpcP->curlTransactionP);
        }
        if (envP->fault_occurred)
            free(rpcP);
    }
    *rpcPP = rpcP;
}



static void 
rpcDestroy(rpc * const rpcP) {

    XMLRPC_ASSERT_PTR_OK(rpcP);
    XMLRPC_ASSERT(!rpcP->threadExists);

    destroyCurlTransaction(rpcP->curlTransactionP);

    list_remove(&rpcP->link);

    free(rpcP);
}


static void 
sendRequest(xmlrpc_env *               const envP, 
            struct clientTransport *   const clientTransportP,
            const xmlrpc_server_info * const serverP,
            xmlrpc_mem_block *         const callXmlP,
            transport_asynch_complete        complete,
            struct call_info *         const callInfoP) {
/*----------------------------------------------------------------------------
   Initiate an XML-RPC rpc asynchronously.  Don't wait for it to go to
   the server.

   Unless we return failure, we arrange to have complete() called when
   the rpc completes.

   This does the 'send_request' operation for a Curl client transport.
-----------------------------------------------------------------------------*/
    rpc * rpcP;
    xmlrpc_mem_block * responseXmlP;

    responseXmlP = XMLRPC_MEMBLOCK_NEW(char, envP, 0);
    if (!envP->fault_occurred) {
        rpcCreate(envP, clientTransportP, serverP, callXmlP, responseXmlP,
                  complete, callInfoP,
                  &rpcP);

        if (envP->fault_occurred)
            XMLRPC_MEMBLOCK_FREE(char, responseXmlP);
    }
    /* The user's eventual finish_asynch call will destroy this RPC
       and response buffer
    */
}



static void * 
finishRpc(struct list_head * const headerP, 
          void *             const context ATTR_UNUSED) {
    
    rpc * const rpcP = headerP->itemP;

    if (rpcP->threadExists) {
        void *status;
        int result;

        result = pthread_join(rpcP->thread, &status);
        
        rpcP->threadExists = FALSE;
    }

    XMLRPC_MEMBLOCK_FREE(char, rpcP->responseXmlP);

    rpcDestroy(rpcP);

    return NULL;
}



static void 
finishAsynch(struct clientTransport * const clientTransportP ATTR_UNUSED,
             enum timeoutType         const timeoutType ATTR_UNUSED,
             timeout_t                const timeout ATTR_UNUSED) {
/*----------------------------------------------------------------------------
   Wait for the threads of all outstanding RPCs to exit and destroy those
   RPCs.

   This does the 'finish_asynch' operation for a Curl client transport.
-----------------------------------------------------------------------------*/
    /* We ignore any timeout request.  Some day, we should figure out how
       to set an alarm and interrupt running threads.
    */

    pthread_mutex_lock(&clientTransportP->listLock);

    list_foreach(&clientTransportP->rpcList, finishRpc, NULL);

    pthread_mutex_unlock(&clientTransportP->listLock);
}



static void
call(xmlrpc_env *               const envP,
     struct clientTransport *   const clientTransportP,
     const xmlrpc_server_info * const serverP,
     xmlrpc_mem_block *         const callXmlP,
     xmlrpc_mem_block **        const responsePP) {

    xmlrpc_mem_block * responseXmlP;
    rpc * rpcP;

    XMLRPC_ASSERT_ENV_OK(envP);
    XMLRPC_ASSERT_PTR_OK(serverP);
    XMLRPC_ASSERT_PTR_OK(callXmlP);
    XMLRPC_ASSERT_PTR_OK(responsePP);

    responseXmlP = XMLRPC_MEMBLOCK_NEW(char, envP, 0);
    if (!envP->fault_occurred) {
        rpcCreate(envP, clientTransportP, serverP, callXmlP, responseXmlP,
                  NULL, NULL, &rpcP);
        if (!envP->fault_occurred) {
            performCurlTransaction(envP, rpcP->curlTransactionP);
            
            *responsePP = responseXmlP;
            
            rpcDestroy(rpcP);
        }
        if (envP->fault_occurred)
            XMLRPC_MEMBLOCK_FREE(char, responseXmlP);
    }
}



struct clientTransportOps xmlrpc_curl_transport_ops = {
    &create,
    &destroy,
    &sendRequest,
    &call,
    &finishAsynch,
};
