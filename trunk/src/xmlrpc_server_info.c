/*=============================================================================
                             xmlrpc_server_info
===============================================================================
  The xmlrpc_server_info class.

  By Bryan Henderson, San Jose CA 2007.10.17.

  Contributed to the public domain by its author
=============================================================================*/

#include "bool.h"
#include "mallocvar.h"

#include "xmlrpc-c/base.h"
#include "xmlrpc-c/base_int.h"
#include "xmlrpc-c/string_int.h"
#include "xmlrpc-c/client.h"
#include "xmlrpc-c/client_int.h"



xmlrpc_server_info *
xmlrpc_server_info_new(xmlrpc_env * const envP,
                       const char * const serverUrl) {
    
    xmlrpc_server_info * serverInfoP;

    XMLRPC_ASSERT_ENV_OK(envP);
    XMLRPC_ASSERT_PTR_OK(serverUrl);

    MALLOCVAR(serverInfoP);
    if (serverInfoP == NULL)
        xmlrpc_faultf(envP, "Couldn't allocate memory for xmlrpc_server_info");
    else {
        serverInfoP->serverUrl = strdup(serverUrl);
        if (serverInfoP->serverUrl == NULL)
            xmlrpc_faultf(envP, "Couldn't allocate memory for server URL");
        else {
            serverInfoP->allowedAuth.basic        = false;
            serverInfoP->allowedAuth.digest       = false;
            serverInfoP->allowedAuth.gssnegotiate = false;
            serverInfoP->allowedAuth.ntlm         = false;
            serverInfoP->userNamePw = NULL;
            serverInfoP->basicAuthHdrValue = NULL;
            if (envP->fault_occurred)
                xmlrpc_strfree(serverInfoP->serverUrl);
        }
        if (envP->fault_occurred)
            free(serverInfoP);
    }
    return serverInfoP;
}



static void
copyServerInfoContent(xmlrpc_env *               const envP,
                      xmlrpc_server_info *       const dstP,
                      const xmlrpc_server_info * const srcP) {
                              
    dstP->serverUrl = strdup(srcP->serverUrl);
    if (dstP->serverUrl == NULL)
        xmlrpc_faultf(envP, "Couldn't allocate memory for server URL");
    else {
        if (srcP->userNamePw == NULL)
            dstP->userNamePw = NULL;
        else {
            dstP->userNamePw =
                strdup(srcP->userNamePw);
            if (dstP->userNamePw == NULL)
                xmlrpc_faultf(envP, "Couldn't allocate memory "
                              "for authentication info");

            if (srcP->basicAuthHdrValue == NULL)
                dstP->basicAuthHdrValue = NULL;
            else {
                dstP->basicAuthHdrValue =
                    strdup(srcP->basicAuthHdrValue);
                if (dstP->basicAuthHdrValue == NULL)
                    xmlrpc_faultf(envP, "Couldn't allocate memory "
                                  "for authentication info");

                dstP->allowedAuth.basic        =
                    srcP->allowedAuth.basic;
                dstP->allowedAuth.digest       =
                    srcP->allowedAuth.digest;
                dstP->allowedAuth.gssnegotiate =
                    srcP->allowedAuth.gssnegotiate;
                dstP->allowedAuth.ntlm         =
                    srcP->allowedAuth.ntlm;
            }
            if (envP->fault_occurred)
                xmlrpc_strfree(dstP->serverUrl);
        }
    }
}



xmlrpc_server_info *
xmlrpc_server_info_copy(xmlrpc_env *         const envP,
                        xmlrpc_server_info * const srcP) {

    xmlrpc_server_info * serverInfoP;

    XMLRPC_ASSERT_ENV_OK(envP);
    XMLRPC_ASSERT_PTR_OK(srcP);

    MALLOCVAR(serverInfoP);
    if (serverInfoP == NULL)
        xmlrpc_faultf(envP,
                      "Couldn't allocate memory for xmlrpc_server_info");
    else {
        copyServerInfoContent(envP, serverInfoP, srcP);

        if (envP->fault_occurred)
            free(serverInfoP);
    }
    return serverInfoP;
}



void
xmlrpc_server_info_free(xmlrpc_server_info * const serverInfoP) {

    XMLRPC_ASSERT_PTR_OK(serverInfoP);
    XMLRPC_ASSERT(serverInfoP->serverUrl != XMLRPC_BAD_POINTER);
    
    if (serverInfoP->userNamePw)
        xmlrpc_strfree(serverInfoP->userNamePw);
    serverInfoP->userNamePw = XMLRPC_BAD_POINTER;

    if (serverInfoP->basicAuthHdrValue)
        xmlrpc_strfree(serverInfoP->basicAuthHdrValue);
    serverInfoP->basicAuthHdrValue = XMLRPC_BAD_POINTER;

    xmlrpc_strfree(serverInfoP->serverUrl);
    serverInfoP->serverUrl = XMLRPC_BAD_POINTER;

    free(serverInfoP);
}



void 
xmlrpc_server_info_set_user(xmlrpc_env *         const envP,
                            xmlrpc_server_info * const serverInfoP,
                            const char *         const username,
                            const char *         const password) {

    const char * userNamePw;
    xmlrpc_mem_block * userNamePw64;

    XMLRPC_ASSERT_ENV_OK(envP);
    XMLRPC_ASSERT_PTR_OK(serverInfoP);
    XMLRPC_ASSERT_PTR_OK(username);
    XMLRPC_ASSERT_PTR_OK(password);

    xmlrpc_asprintf(&userNamePw, "%s:%s", username, password);

    userNamePw64 =
        xmlrpc_base64_encode_without_newlines(envP, 
                                              (unsigned char*) userNamePw,
                                              strlen(userNamePw));
    if (!envP->fault_occurred) {
        const char * const data = XMLRPC_MEMBLOCK_CONTENTS(char, userNamePw64);
        size_t       const len  = XMLRPC_MEMBLOCK_SIZE(char, userNamePw64);

        char * hdrValue;

        hdrValue = malloc(sizeof("Basic: ") + len + 1);
        if (hdrValue == NULL)
            xmlrpc_faultf(envP, "Could not allocate memory to store "
                          "authorization header value.");
        else {
            strcpy(hdrValue, "Basic: ");
            strncat(hdrValue, data, len);

            if (serverInfoP->basicAuthHdrValue)
                xmlrpc_strfree(serverInfoP->basicAuthHdrValue);

            serverInfoP->basicAuthHdrValue = hdrValue;
        }
        XMLRPC_MEMBLOCK_FREE(char, userNamePw64);
    }
    serverInfoP->userNamePw = userNamePw;
}



void 
xmlrpc_server_info_set_basic_auth(xmlrpc_env *         const envP,
                                  xmlrpc_server_info * const serverInfoP,
                                  const char *         const username,
                                  const char *         const password) {

    xmlrpc_server_info_set_user(envP, serverInfoP, username, password);

    if (!envP->fault_occurred) {
        serverInfoP->allowedAuth.basic        = true;
        serverInfoP->allowedAuth.digest       = false;
        serverInfoP->allowedAuth.gssnegotiate = false;
        serverInfoP->allowedAuth.ntlm         = false;
    }
}



void
xmlrpc_server_info_allow_auth_basic(xmlrpc_env *         const envP,
                                    xmlrpc_server_info * const sP) {

    if (sP->userNamePw == NULL)
        xmlrpc_faultf(envP, "You must set username/password with "
                      "xmlrpc_server_info_set_user()");
    else
        sP->allowedAuth.basic = true;
}



void
xmlrpc_server_info_disallow_auth_basic(
    xmlrpc_env *         const envP ATTR_UNUSED,
    xmlrpc_server_info * const sP) {

    sP->allowedAuth.basic = false;
}



void
xmlrpc_server_info_allow_auth_digest(xmlrpc_env *         const envP,
                                     xmlrpc_server_info * const sP) {

    if (sP->userNamePw == NULL)
        xmlrpc_faultf(envP, "You must set username/password with "
                      "xmlrpc_server_info_set_user()");
    else
        sP->allowedAuth.digest = true;
}



void
xmlrpc_server_info_disallow_auth_digest(
    xmlrpc_env *         const envP ATTR_UNUSED,
    xmlrpc_server_info * const sP) {

    sP->allowedAuth.digest = false;
}



void
xmlrpc_server_info_allow_auth_negotiate(xmlrpc_env *         const envP,
                                        xmlrpc_server_info * const sP) {

    if (sP->userNamePw == NULL)
        xmlrpc_faultf(envP, "You must set username/password with "
                      "xmlrpc_server_info_set_user()");
    else
        sP->allowedAuth.gssnegotiate = true;
}



void
xmlrpc_server_info_disallow_auth_negotiate(
    xmlrpc_env *         const envP ATTR_UNUSED,
    xmlrpc_server_info * const sP) {

    sP->allowedAuth.gssnegotiate = false;
}



void
xmlrpc_server_info_allow_auth_ntlm(xmlrpc_env *         const envP,
                                   xmlrpc_server_info * const sP) {

    if (sP->userNamePw == NULL)
        xmlrpc_faultf(envP, "You must set username/password with "
                      "xmlrpc_server_info_set_user()");
    else
        sP->allowedAuth.ntlm = true;
}



void
xmlrpc_server_info_disallow_auth_ntlm(
    xmlrpc_env *         const envP ATTR_UNUSED,
    xmlrpc_server_info * const sP) {
    
    sP->allowedAuth.ntlm = true;
}
