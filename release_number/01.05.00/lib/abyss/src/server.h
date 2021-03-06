#ifndef SERVER_H_INCLUDED
#define SERVER_H_INCLUDED

#include "xmlrpc-c/abyss.h"

#include "file.h"
#include "data.h"

struct _TServer {
    abyss_bool socketBound;
        /* The listening socket exists and is bound to a local address
           (may already be listening as well)
        */
    TSocket listensock;
        /* Meaningful only when 'socketBound' is true: file descriptor of
           the listening socket ("listening socket" means socket for listening,
           not a socket that is listening right now).
        */
    abyss_bool weCreatedListenSocket;
        /* We created the listen socket (whose fd is 'listensock'), as
           opposed to 1) User supplied it; or 2) there isn't one.
        */
    const char * logfilename;
    abyss_bool logfileisopen;
    TFile logfile;
    TMutex logmutex;
    const char * name;
    const char * filespath;
    abyss_bool serverAcceptsConnections;
        /* We listen for and accept TCP connections for HTTP transactions.
           (The alternative is the user supplies a TCP-connected socket
           for each transaction)
        */
    uint16_t port;
        /* Meaningful only when 'socketBound' is false: port number to which
           we should bind the listening socket
        */
    uint32_t keepalivetimeout;
    uint32_t keepalivemaxconn;
    uint32_t timeout;
        /* Maximum time in seconds the server will wait to read a header
           or a data chunk from the socket.
        */
    TList handlers;
    TList defaultfilenames;
    void * defaulthandler;
    abyss_bool advertise;
    MIMEType * mimeTypeP;
        /* NULL means to use the global MIMEType object */
#ifdef _UNIX
    uid_t uid;
    gid_t gid;
    TFile pidfile;
#endif  
};


#endif
