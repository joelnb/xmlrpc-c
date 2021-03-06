// SystemProxy.h - xmlrpc-c C++ proxy class
// Auto-generated by xml-rpc-api2cpp.

#ifndef _SystemProxy_H_
#define _SystemProxy_H_ 1

#include <XmlRpcCpp.h>

class SystemProxy {
    XmlRpcClient mClient;

public:
    SystemProxy (const XmlRpcClient& client)
        : mClient(client) {}
    SystemProxy (const string& server_url)
        : mClient(XmlRpcClient(server_url)) {}
    SystemProxy (const SystemProxy& o)
        : mClient(o.mClient) {}

    SystemProxy& operator= (const SystemProxy& o) {
        if (this != &o) mClient = o.mClient;
        return *this;
    }

    /* Return an array of all available XML-RPC methods on this server. */
    XmlRpcValue /*array*/ listMethods ();

    /* Given the name of a method, return an array of legal
       signatures. Each signature is an array of strings. The first item of
       each signature is the return type, and any others items are
       parameter types. */
    XmlRpcValue /*array*/ methodSignature (string string1);

    /* Given the name of a method, return a help string. */
    string methodHelp (string string1);

    /* Process an array of calls, and return an array of results. Calls
       should be structs of the form {'methodName': string, 'params':
       array}. Each result will either be a single-item array containg the
       result value, or a struct of the form {'faultCode': int,
       'faultString': string}. This is useful when you need to make lots of
       small calls without lots of round trips. */
    XmlRpcValue /*array*/ multicall (XmlRpcValue /*array*/ array1);
};

#endif /* _SystemProxy_H_ */
