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

#include "xmlrpc_config.h"

#undef PACKAGE
#undef VERSION

#include <stddef.h>

#include "bool.h"
#include "mallocvar.h"
#include "xmlrpc.h"
#include "xmlrpc_int.h"
#include "xmlrpc_client.h"
#include "xmlrpc_client_int.h"

/* Include our libwww headers. */
#include "WWWLib.h"
#include "WWWHTTP.h"
#include "WWWInit.h"

/* Include our libwww SSL headers, if available. */
#if HAVE_LIBWWW_SSL
#include "WWWSSL.h"
#endif

#include "xmlrpc_libwww_transport.h"

/* This value was discovered by Rick Blair. His efforts shaved two seconds
** off of every request processed. Many thanks. */
#define SMALLEST_LEGAL_LIBWWW_TIMEOUT (21)

#define XMLRPC_CLIENT_USE_TIMEOUT   (2)


struct clientTransport {
    int saved_flags;
    HTList *xmlrpc_conversions;
    bool tracingOn;
};

static struct clientTransport clientTransport;


/*=========================================================================
**  Initialization and Shutdown
**=========================================================================
*/

static void 
create(xmlrpc_env *                     const envP ATTR_UNUSED,
       int                              const flags,
       const char *                     const appname,
       const char *                     const appversion,
       const struct xmlrpc_xportparms * const transportParmsP ATTR_UNUSED,
       size_t                           const parm_size ATTR_UNUSED,
       struct clientTransport **        const handlePP) {
/*----------------------------------------------------------------------------
   This does the 'create' operation for a Libwww client transport.
-----------------------------------------------------------------------------*/
    /* The Libwww transport is not re-entrant -- you can have only one
       per program instance.  Even if we changed the Xmlrpc-c code not
       to use global variables, that wouldn't help because Libwww
       itself is not re-entrant.  
       
       So we use a global variable ('clientTransport') for our transport state.
    */
    struct clientTransport * const clientTransportP = &clientTransport;
    *handlePP = clientTransportP;

    clientTransportP->saved_flags = flags;
    if (!(clientTransportP->saved_flags & XMLRPC_CLIENT_SKIP_LIBWWW_INIT)) {
    
        /* We initialize the library using a robot profile, because we don't
        ** care about redirects or HTTP authentication, and we want to
        ** reduce our application footprint as much as possible. */
        HTProfile_newRobot(appname, appversion);

        /* Ilya Goldberg <igg@mit.edu> provided the following code to access
        ** SSL-protected servers. */
#if HAVE_LIBWWW_SSL
        /* Set the SSL protocol method. By default, it is the highest
        ** available protocol. Setting it up to SSL_V23 allows the client
        ** to negotiate with the server and set up either TSLv1, SSLv3,
        ** or SSLv2 */
        HTSSL_protMethod_set(HTSSL_V23);

        /* Set the certificate verification depth to 2 in order to be able to
        ** validate self-signed certificates */
        HTSSL_verifyDepth_set(2);

        /* Register SSL stuff for handling ssl access. The parameter we pass
        ** is NO because we can't be pre-emptive with POST */
        HTSSLhttps_init(NO);
#endif /* HAVE_LIBWWW_SSL */
    
        /* For interoperability with Frontier, we need to tell libwww *not*
        ** to send 'Expect: 100-continue' headers. But if we're not sending
        ** these, we shouldn't wait for them. So set our built-in delays to
        ** the smallest legal values. */
        HTTP_setBodyWriteDelay (SMALLEST_LEGAL_LIBWWW_TIMEOUT,
                                SMALLEST_LEGAL_LIBWWW_TIMEOUT);
    
        /* We attempt to disable all of libwww's chatty, interactive
        ** prompts. Let's hope this works. */
        HTAlert_setInteractive(NO);

        /* Here are some alternate setup calls which will help greatly
        ** with debugging, should the need arise.
        **
        ** HTProfile_newNoCacheClient(appname, appversion);
        ** HTAlert_setInteractive(YES);
        ** HTPrint_setCallback(printer);
        ** HTTrace_setCallback(tracer); */
    }

    /* Set up our list of conversions for XML-RPC requests. This is a
    ** massively stripped-down version of the list in libwww's HTInit.c.
    ** XXX - This is hackish; 10.0 is an arbitrary, large quality factor
    ** designed to override the built-in converter for XML. */
    clientTransportP->xmlrpc_conversions = HTList_new();
    HTConversion_add(clientTransportP->xmlrpc_conversions, "text/xml", "*/*",
                     HTThroughLine, 10.0, 0.0, 0.0);

    if (getenv("XMLRPC_LIBWWW_TRACE"))
        clientTransportP->tracingOn = TRUE;
    else
        clientTransportP->tracingOn = FALSE;
}



static void 
destroy(struct clientTransport * const clientTransportP) {
/*----------------------------------------------------------------------------
   This does the 'destroy' operation for a Libwww client transport.
-----------------------------------------------------------------------------*/
    XMLRPC_ASSERT(clientTransportP != NULL);

    if (!(clientTransportP->saved_flags & XMLRPC_CLIENT_SKIP_LIBWWW_INIT)) {
        HTProfile_delete();
    }
}



/*=========================================================================
**  HTTP Error Reporting
**=======================================================================*/

static void 
set_fault_from_http_request(xmlrpc_env * const envP,
                            int          const status,
                            HTRequest *  const requestP) {
/*----------------------------------------------------------------------------
   Assuming 'requestP' identifies a completed libwww HTTP request, set
   *envP according to its success/error status.
-----------------------------------------------------------------------------*/
    XMLRPC_ASSERT_PTR_OK(requestP);

    if (status == 200) {
        /* No error.  Don't set one in *envP */
    } else {
        /* Get an error message from libwww. The middle three
           parameters to HTDialog_errorMessage appear to be ignored.
           XXX - The documentation for this API is terrible, so we may
           be using it incorrectly.  
        */
        HTList * const errStack = HTRequest_error(requestP);
    
        if (errStack == NULL) {
            /* I think this is probably impossible, because we didn't get
               status 200, but I don't completely understand HTTP and libwww.
            */
            xmlrpc_env_set_fault_formatted(
                envP, XMLRPC_NETWORK_ERROR,
                "HTTP error #%d occurred, but there was no additional "
                "error information supplied", status);
        } else {
            const char * const msg = 
                HTDialog_errorMessage(requestP, HT_A_MESSAGE, HT_MSG_NULL,
                                      "An error occurred", errStack);

            if (msg == NULL)
                xmlrpc_env_set_fault_formatted(
                    envP, XMLRPC_NETWORK_ERROR,
                    "HTTP error #%d occurred.  Libwww's "
                    "HTDialog_errorMessage() routine was mysteriously "
                    "unable to interpret the additional error information, "
                    "so we have none to report.", status);
            else {
                /* Set our fault.  Note that this may inlcude line breaks,
                   because 'msg' may.  We should fix that.  Formatting for
                   display is none of our business at this level.
                */
                xmlrpc_env_set_fault_formatted(
                    envP, XMLRPC_NETWORK_ERROR,
                    "HTTP error #%d occurred.  libwww says %s", status, msg);
                
                xmlrpc_strfree(msg);
            }
        }
    }
}



/*=========================================================================
  HTCookie callback to check for auth cookie and set it.
=========================================================================*/

PRIVATE BOOL 
xmlrpc_authcookie_store(HTRequest * const request ATTR_UNUSED, 
                        HTCookie *  const cookie, 
                        void *      const param ATTR_UNUSED) {

    /* First check to see if the cookie exists at all. */
    if (cookie) {
        /* Check for auth cookie. */
        if (!strcasecmp("auth", HTCookie_name(cookie)))
            /* Set auth cookie as HTTP_COOKIE_AUTH. */
            setenv("HTTP_COOKIE_AUTH", HTCookie_value(cookie), 1);
    }
    return YES;
}



/*=========================================================================
  HTCookie callback to get auth value and store it in cookie.
=========================================================================*/

PRIVATE HTAssocList *
xmlrpc_authcookie_grab(HTRequest * const request ATTR_UNUSED,
                       void *      const param ATTR_UNUSED) {

    /* Create associative list for cookies. */
    HTAssocList *alist = HTAssocList_new();

    /* If HTTP_COOKIE_AUTH is set, pass that to the list. */
    if (getenv("HTTP_COOKIE_AUTH") != NULL)
        HTAssocList_addObject(alist, "auth", getenv("HTTP_COOKIE_AUTH"));

    return alist;
}



typedef struct {
/*----------------------------------------------------------------------------
   This object represents one RPC.
-----------------------------------------------------------------------------*/
    struct clientTransport * clientTransportP;

    /* These fields are used when performing synchronous calls. */
    bool is_done;
    int http_status;

    /* Low-level information used by libwww. */
    HTRequest *request;
    HTChunk *response_data;
    HTParentAnchor *source_anchor;
    HTAnchor *dest_anchor;

    transport_asynch_complete complete;
    struct call_info * callInfoP; 
} rpc;



static void 
deleteSourceAnchor(HTParentAnchor * const anchor) {

    /* We need to clear the document first, or else libwww won't
    ** really delete the anchor. */
    HTAnchor_setDocument(anchor, NULL);

    /* XXX - Deleting this anchor causes HTLibTerminate to dump core. */
    /* HTAnchor_delete(anchor); */
}



static void
createSourceAnchor(xmlrpc_env *       const envP,
                   HTParentAnchor **  const sourceAnchorPP,
                   xmlrpc_mem_block * const xmlP) {

    HTParentAnchor * const sourceAnchorP = HTTmpAnchor(NULL);

    if (sourceAnchorP == NULL)
        xmlrpc_env_set_fault_formatted(
            envP, XMLRPC_INTERNAL_ERROR, 
            "Unable to build source anchor.  HTTmpAnchor() failed.");
    else {
        HTAnchor_setDocument(sourceAnchorP,
                             XMLRPC_MEMBLOCK_CONTENTS(char, xmlP));
        HTAnchor_setFormat(sourceAnchorP, HTAtom_for("text/xml"));
        HTAnchor_setLength(sourceAnchorP, XMLRPC_MEMBLOCK_SIZE(char, xmlP));

        *sourceAnchorPP = sourceAnchorP;
    }
}



static void
createDestAnchor(xmlrpc_env *               const envP,
                 HTAnchor **                const destAnchorPP,
                 const xmlrpc_server_info * const serverP) {
                 
    *destAnchorPP = HTAnchor_findAddress(serverP->_server_url);

    if (*destAnchorPP == NULL)
        xmlrpc_env_set_fault_formatted(
            envP, XMLRPC_INTERNAL_ERROR,
            "Could not build destination anchor.  HTAnchor_findAddress() "
            "failed.");
}



static void
rpcCreate(xmlrpc_env *               const envP,
          struct clientTransport *   const clientTransportP,
          const xmlrpc_server_info * const serverP,
          xmlrpc_mem_block *         const xmlP,
          transport_asynch_complete        complete, 
          struct call_info *         const callInfoP,
          rpc                   **   const rpcPP) {

    rpc *rpcP;
    HTRqHd request_headers;
    HTStream *target_stream;

    /* Allocate our structure. */
    MALLOCVAR(rpcP);
    XMLRPC_FAIL_IF_NULL(rpcP, envP, XMLRPC_INTERNAL_ERROR,
                        "Out of memory in rpcCreate()");

    /* Set up our basic members. */
    rpcP->clientTransportP = clientTransportP;
    rpcP->is_done = FALSE;
    rpcP->http_status = 0;
    rpcP->complete = complete;
    rpcP->callInfoP = callInfoP;

    /* Start cookie handler. */
    HTCookie_init();
    HTCookie_setCallbacks(xmlrpc_authcookie_store, NULL,
                          xmlrpc_authcookie_grab, NULL);

    /* Create a HTRequest object. */
    rpcP->request = HTRequest_new();
    XMLRPC_FAIL_IF_NULL(rpcP, envP, XMLRPC_INTERNAL_ERROR,
                        "HTRequest_new failed");
    
    /* Install ourselves as the request context. */
    HTRequest_setContext(rpcP->request, rpcP);

    /* XXX - Disable the 'Expect:' header so we can talk to Frontier. */
    request_headers = HTRequest_rqHd(rpcP->request);
    request_headers = request_headers & ~HT_C_EXPECT;
    HTRequest_setRqHd(rpcP->request, request_headers);

    /* Send an authorization header if we need one. */
    if (serverP->_http_basic_auth)
        HTRequest_addCredentials(rpcP->request, "Authorization",
                                 serverP->_http_basic_auth);
    
    /* Make sure there is no XML conversion handler to steal our data.
    ** The 'override' parameter is currently ignored by libwww, so our
    ** list of conversions must be designed to co-exist with the built-in
    ** conversions. */
    HTRequest_setConversion(rpcP->request, 
                            clientTransportP->xmlrpc_conversions, NO);

    /* Set up our response buffer. */
    target_stream = HTStreamToChunk(rpcP->request, &rpcP->response_data, 0);
    XMLRPC_FAIL_IF_NULL(rpcP->response_data, envP, XMLRPC_INTERNAL_ERROR,
                        "HTStreamToChunk failed");
    XMLRPC_ASSERT(target_stream != NULL);
    HTRequest_setOutputStream(rpcP->request, target_stream);
    HTRequest_setOutputFormat(rpcP->request, WWW_SOURCE);

    createSourceAnchor(envP, &rpcP->source_anchor, xmlP);

    if (!envP->fault_occurred) {
        createDestAnchor(envP, &rpcP->dest_anchor, serverP);

        if (envP->fault_occurred)
            /* See below for comments about deleting the source and dest
            ** anchors. This is a bit of a black art. */
            deleteSourceAnchor(rpcP->source_anchor);
    }
    
 cleanup:
    if (envP->fault_occurred) {
        if (rpcP) {
            if (rpcP->request)
                HTRequest_delete(rpcP->request);
            if (rpcP->response_data)
                HTChunk_delete(rpcP->response_data);
            free(rpcP);
        }
    }
    *rpcPP = rpcP;
}



static void 
rpcDestroy(rpc * const rpcP) {

    XMLRPC_ASSERT_PTR_OK(rpcP);
    XMLRPC_ASSERT(rpcP->request != XMLRPC_BAD_POINTER);
    XMLRPC_ASSERT(rpcP->response_data != XMLRPC_BAD_POINTER);

    /* Flush our request object and the data we got back. */
    HTRequest_delete(rpcP->request);
    rpcP->request = XMLRPC_BAD_POINTER;
    HTChunk_delete(rpcP->response_data);
    rpcP->response_data = XMLRPC_BAD_POINTER;

    /* This anchor points to private data, so we're allowed to delete it.  */
    deleteSourceAnchor(rpcP->source_anchor);

    /* WARNING: We can't delete the destination anchor, because this points
    ** to something in the outside world, and lives in a libwww hash table.
    ** Under certain circumstances, this anchor may have been reissued to
    ** somebody else. So over time, the anchor cache will grow. If this
    ** is a problem for your application, read the documentation for
    ** HTAnchor_deleteAll.
    **
    ** However, we CAN check to make sure that no documents have been
    ** attached to the anchor. This assertion may fail if you're using
    ** libwww for something else, so please feel free to comment it out. */
    /* XMLRPC_ASSERT(HTAnchor_document(rpcP->dest_anchor) == NULL);
     */

    free(rpcP);
}



static void
extract_response_chunk(xmlrpc_env *        const envP,
                       rpc *               const rpcP,
                       xmlrpc_mem_block ** const responseXmlPP) {

    /* Check to make sure that w3c-libwww actually sent us some data.
    ** XXX - This may happen if libwww is shut down prematurely, believe it
    ** or not--we'll get a 200 OK and no data. Gag me with a bogus design
    ** decision. This may also fail if some naughty libwww converter
    ** ate our data unexpectedly. */
    if (!HTChunk_data(rpcP->response_data))
        xmlrpc_env_set_fault(envP, XMLRPC_NETWORK_ERROR,
                             "w3c-libwww returned no data");
    else {
        *responseXmlPP = XMLRPC_MEMBLOCK_NEW(char, envP, 0);
        if (!envP->fault_occurred) {
            if (rpcP->clientTransportP->tracingOn) {
                fprintf(stderr, "HTTP chunk received: %u bytes: '%.*s'",
                        HTChunk_size(rpcP->response_data),
                        HTChunk_size(rpcP->response_data),
                        HTChunk_data(rpcP->response_data));
            }

            XMLRPC_MEMBLOCK_APPEND(char, envP, *responseXmlPP,
                                   HTChunk_data(rpcP->response_data),
                                   HTChunk_size(rpcP->response_data));
            if (envP->fault_occurred)
                XMLRPC_MEMBLOCK_FREE(char, *responseXmlPP);
        }
    }
}



static int 
synch_terminate_handler(HTRequest *  const request,
                        HTResponse * const response ATTR_UNUSED,
                        void *       const param ATTR_UNUSED,
                        int          const status) {
/*----------------------------------------------------------------------------
   This is a libwww request completion handler.

   HTEventList_newLoop() calls this when it completes a request (with this
   registered as the completion handler).
-----------------------------------------------------------------------------*/
    rpc *rpcP;

    rpcP = HTRequest_context(request);

    rpcP->is_done = TRUE;
    rpcP->http_status = status;

    HTEventList_stopLoop();

    return HT_OK;
}



static void
call(xmlrpc_env *               const envP,
     struct clientTransport *   const clientTransportP,
     const xmlrpc_server_info * const serverP,
     xmlrpc_mem_block *         const xmlP,
     xmlrpc_mem_block **        const responsePP) {
/*----------------------------------------------------------------------------
   This does the 'call' operation for a Libwww client transport.
-----------------------------------------------------------------------------*/
    rpc * rpcP;

    XMLRPC_ASSERT_ENV_OK(envP);
    XMLRPC_ASSERT_PTR_OK(serverP);
    XMLRPC_ASSERT_PTR_OK(xmlP);
    XMLRPC_ASSERT_PTR_OK(responsePP);

    rpcCreate(envP, clientTransportP, serverP, xmlP, NULL, NULL, &rpcP);
    if (!envP->fault_occurred) {
        int ok;
        
        /* Install our request handler. */
        HTRequest_addAfter(rpcP->request, &synch_terminate_handler, 
                           NULL, NULL, HT_ALL, HT_FILTER_LAST, NO);

        /* Start our request running. */
        ok = HTPostAnchor(rpcP->source_anchor, 
                          rpcP->dest_anchor, 
                          rpcP->request);
        if (!ok)
            xmlrpc_env_set_fault(
                envP, XMLRPC_INTERNAL_ERROR,
                "Libwww HTPostAnchor() failed to start POST request");
        else {
            /* Run our event-processing loop.  HTEventList_newLoop()
               is what calls synch_terminate_handler(), by virtue of
               it being registered as a handler.  It may return for
               other reasons than the request being complete, though.
               so we call it in a loop until synch_terminate_handler()
               really has been called.
            */
            while (!rpcP->is_done)
                HTEventList_newLoop();
        
            /* Fail if we didn't get a "200 OK" response from the server */
            if (rpcP->http_status != 200)
                set_fault_from_http_request(
                    envP, rpcP->http_status, 
                    rpcP->request);
            else {
                /* XXX - Check to make sure response type is text/xml here. */
                
                extract_response_chunk(envP, rpcP, responsePP);
            }
        }
        rpcDestroy(rpcP);
    }
}



/*=========================================================================
**  Event Loop
**=========================================================================
**  We manage a fair bit of internal state about our event loop. This is
**  needed to determine when (and if) we should exit the loop.
*/

static int outstanding_asynch_calls = 0;
static int event_loop_flags = 0;
static int timer_called = 0;

static void 
register_asynch_call(void) {
    XMLRPC_ASSERT(outstanding_asynch_calls >= 0);
    outstanding_asynch_calls++;
}



static void 
unregister_asynch_call(void) {

    XMLRPC_ASSERT(outstanding_asynch_calls > 0);
    outstanding_asynch_calls--;
    if (outstanding_asynch_calls == 0)
        HTEventList_stopLoop();
}



static int 
timer_callback(HTTimer *timer ATTR_UNUSED,
               void *user_data ATTR_UNUSED,
               HTEventType event) {
/*----------------------------------------------------------------------------
  A handy timer callback which cancels the running event loop. 
-----------------------------------------------------------------------------*/
    XMLRPC_ASSERT(event == HTEvent_TIMEOUT);
    timer_called = 1;
    HTEventList_stopLoop();
    
    /* XXX - The meaning of this return value is undocumented, but close
    ** inspection of libwww's source suggests that we want to return HT_OK. */
    return HT_OK;
}



static void 
eventLoopRun(int       const flags, 
             timeout_t const milliseconds) {
/*----------------------------------------------------------------------------
   Process all responses from outstanding requests as they come in.
   Return when there are no more outstanding responses.

   Or, if 'flags' has the XMLRPC_CLIENT_USE_TIMEOUT flag set, return
   when 'milliseconds' milliseconds have elapsed, regardless of whether
   there are still outstanding responses.

   The processing we do consists of telling libwww to process the
   completion of the libwww request.  That normally includes calling
   the xmlrpc_libwww_transport request termination handler, because
   the submitter of the libwww request would have registered that as a
   callback.
-----------------------------------------------------------------------------*/
    if (outstanding_asynch_calls > 0) {
        HTTimer *timer;

        event_loop_flags = flags;
        
        /* Run an appropriate event loop.  The HTEeventList_newLoop()
           is what calls asynch_terminate_handler(), by virtue of it
           being registered as a handler.
        */
        if (event_loop_flags & XMLRPC_CLIENT_USE_TIMEOUT) {
            
            /* Run our event loop with a timer. Note that we need to be very
            ** careful about race conditions--timers can be fired in either
            ** HTimer_new or HTEventList_newLoop. And if our callback were to
            ** get called before we entered the loop, we would never exit.
            ** So we use a private flag of our own--we can't even rely on
            ** HTTimer_hasTimerExpired, because that only checks the time,
            ** not whether our callback has been run. Yuck. */
            timer_called = 0;
            timer = HTTimer_new(NULL, &timer_callback, NULL,
                                milliseconds, YES, NO);
            XMLRPC_ASSERT(timer != NULL);
            if (!timer_called)
                HTEventList_newLoop();
            HTTimer_delete(timer);
            
        } else {
            /* Run our event loop without a timer. */
            HTEventList_newLoop();
        }
        
        /* Reset our flags, so we don't interfere with direct calls to the
        ** libwww event loop functions. */
        event_loop_flags = 0;
    } else {
        /* There are *no* calls to process.  This may mean that none
           of the asynch calls were ever set up, and the client's
           callbacks have already been called with an error, or that
           all outstanding calls were completed during a previous
           synchronous call.  
        */
    }
}



static void 
finishAsynch(struct clientTransport * const clientTransportP ATTR_UNUSED,
             enum timeoutType         const timeoutType,
             timeout_t                const timeout) {
/*----------------------------------------------------------------------------
   This does the 'finish_asynch' operation for a Libwww client transport.
-----------------------------------------------------------------------------*/
    eventLoopRun(timeoutType == timeout_yes ? XMLRPC_CLIENT_USE_TIMEOUT : 0,
                 timeout);
}



static int 
asynch_terminate_handler(HTRequest *  const request,
                         HTResponse * const response ATTR_UNUSED,
                         void *       const param ATTR_UNUSED,
                         int          const status) {
/*----------------------------------------------------------------------------
   Handle the completion of a libwww request.

   This is the bottom half of the xmlrpc_libwww_transport asynchronous
   call dispatcher.  It's what the dispatcher registers with libwww so
   that libwww calls it when a request that xmlrpc_libwww_transport
   submitted to it is complete.
-----------------------------------------------------------------------------*/
    xmlrpc_env env;
    rpc * rpcP;
    xmlrpc_mem_block * responseXmlP;

    XMLRPC_ASSERT_PTR_OK(request);

    xmlrpc_env_init(&env);

    rpcP = HTRequest_context(request);

    /* Unregister this call from the event loop. Among other things, this
    ** may decide to stop the event loop.
    **/
    unregister_asynch_call();

    /* Give up if an error occurred. */
    if (status != 200)
        set_fault_from_http_request(&env, status, request);
    else {
        /* XXX - Check to make sure response type is text/xml here. */
        extract_response_chunk(&env, rpcP, &responseXmlP);
    }
    rpcP->complete(rpcP->callInfoP, responseXmlP, env);

    if (!env.fault_occurred)
        XMLRPC_MEMBLOCK_FREE(char, responseXmlP);

    rpcDestroy(rpcP);
    
    xmlrpc_env_clean(&env);
    return HT_OK;
}



static void 
sendRequest(xmlrpc_env *               const envP, 
            struct clientTransport *   const clientTransportP,
            const xmlrpc_server_info * const serverP,
            xmlrpc_mem_block *         const xmlP,
            transport_asynch_complete        complete,
            struct call_info *         const callInfoP) {
/*----------------------------------------------------------------------------
   Initiate an XML-RPC rpc asynchronously.  Don't wait for it to go to
   the server.

   Unless we return failure, we arrange to have complete() called when
   the rpc completes.

   This does the 'send_request' operation for a Libwww client transport.
-----------------------------------------------------------------------------*/
    rpc * rpcP;

    XMLRPC_ASSERT_PTR_OK(envP);
    XMLRPC_ASSERT_PTR_OK(serverP);
    XMLRPC_ASSERT_PTR_OK(xmlP);
    XMLRPC_ASSERT_PTR_OK(callInfoP);

    rpcCreate(envP, clientTransportP, serverP, xmlP, complete, callInfoP, 
              &rpcP);
    if (!envP->fault_occurred) {
        int ok;

        /* Install our request handler. */
        HTRequest_addAfter(rpcP->request, &asynch_terminate_handler, 
                           NULL, NULL, HT_ALL, HT_FILTER_LAST, NO);

        /* Register our asynchronous call with the event loop.  This means
           the user's callback is guaranteed to be called eventually.
        */
        register_asynch_call();

        /* This makes the TCP connection and sends the XML to the server
           as an HTTP POST request.

           There was a comment here that said this might return failure
           (!ok) and still invoke our completion handler
           (asynch_terminate_handler().  The code attempted to deal with
           that.  Well, it's impossible to deal with that, so if it really
           happens, we must fix Libwww.  -Bryan 04.11.23.
        */

        ok = HTPostAnchor(rpcP->source_anchor, 
                          rpcP->dest_anchor, 
                          rpcP->request);
        if (!ok) {
            unregister_asynch_call();
            xmlrpc_env_set_fault(envP, XMLRPC_INTERNAL_ERROR,
                                 "Libwww (HTPostAnchor()) failed to start the "
                                 "POST request.");
        }
        if (envP->fault_occurred)
            rpcDestroy(rpcP);
    }
}



struct clientTransportOps xmlrpc_libwww_transport_ops = {
    &create,
    &destroy,
    &sendRequest,
    &call,
    &finishAsynch,
};
