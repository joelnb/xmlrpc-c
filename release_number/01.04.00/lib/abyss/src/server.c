/* Copyright information is at end of file */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#include <grp.h>
/* Check this
#include <sys/io.h>
*/
#endif  /* WIN32 */
#include <fcntl.h>

#include "mallocvar.h"
#include "xmlrpc-c/base_int.h"

#include "xmlrpc-c/abyss.h"
#include "server.h"
#include "session.h"
#include "conn.h"
#include "socket.h"

#define BOUNDARY    "##123456789###BOUNDARY"

typedef int (*TQSortProc)(const void *, const void *);

static int
cmpfilenames(const TFileInfo **f1,const TFileInfo **f2) {
    if (((*f1)->attrib & A_SUBDIR) && !((*f2)->attrib & A_SUBDIR))
        return (-1);
    if (!((*f1)->attrib & A_SUBDIR) && ((*f2)->attrib & A_SUBDIR))
        return 1;

    return strcmp((*f1)->name,(*f2)->name);
}

static int
cmpfiledates(const TFileInfo **f1,const TFileInfo **f2) {
    if (((*f1)->attrib & A_SUBDIR) && !((*f2)->attrib & A_SUBDIR))
        return (-1);
    if (!((*f1)->attrib & A_SUBDIR) && ((*f2)->attrib & A_SUBDIR))
        return 1;

    return ((*f1)->time_write-(*f2)->time_write);
}



static abyss_bool
ServerDirectoryHandler(TSession *r,char *z,TDate *dirdate) {
    TFileInfo fileinfo,*fi;
    TFileFind findhandle;
    char *p,z1[26],z2[20],z3[9],u,*z4;
    TList list;
    int16_t i;
    uint32_t k;
    abyss_bool text=FALSE;
    abyss_bool ascending=TRUE;
    uint16_t sort=1;    /* 1=by name, 2=by date */
    struct tm ftm;
    TPool pool;
    TDate date;

    if (r->request_info.query) {
        if (strcmp(r->request_info.query,"plain")==0)
            text=TRUE;
        else if (strcmp(r->request_info.query,"name-up")==0)
        {
            sort=1;
            ascending=TRUE;
        }
        else if (strcmp(r->request_info.query,"name-down")==0)
        {
            sort=1;
            ascending=FALSE;
        }
        else if (strcmp(r->request_info.query,"date-up")==0)
        {
            sort=2;
            ascending=TRUE;
        }
        else if (strcmp(r->request_info.query,"date-down")==0)
        {
            sort=2;
            ascending=FALSE;
        }
        else 
        {
            ResponseStatus(r,400);
            return TRUE;
        };
    }
    if (DateCompare(&r->date,dirdate)<0)
        dirdate=&r->date;

    p=RequestHeaderValue(r,"If-Modified-Since");
    if (p) {
        if (DateDecode(p,&date)) {
            if (DateCompare(dirdate,&date)<=0)
            {
                ResponseStatus(r,304);
                ResponseWrite(r);
                return TRUE;
            };
        }
    }
    if (!FileFindFirst(&findhandle,z,&fileinfo))
    {
        ResponseStatusErrno(r);
        return TRUE;
    };

    ListInit(&list);

    if (!PoolCreate(&pool,1024))
    {
        ResponseStatus(r,500);
        return TRUE;
    };

    do
    {
        /* Files which names start with a dot are ignored */
        /* This includes implicitly the ./ and ../ */
        if (*fileinfo.name=='.') {
            if (strcmp(fileinfo.name,"..")==0)
            {
                if (strcmp(r->request_info.uri,"/")==0)
                    continue;
            }
            else
                continue;
        }
        fi=(TFileInfo *)PoolAlloc(&pool,sizeof(fileinfo));
        if (fi)
        {
            memcpy(fi,&fileinfo,sizeof(fileinfo));
            if (ListAdd(&list,fi))
                continue;
        };

        ResponseStatus(r,500);
        FileFindClose(&findhandle);
        ListFree(&list);
        PoolFree(&pool);
        return TRUE;

    } while (FileFindNext(&findhandle,&fileinfo));

    FileFindClose(&findhandle);

    /* Send something to the user to show that we are still alive */
    ResponseStatus(r,200);
    ResponseContentType(r,(text?"text/plain":"text/html"));

    if (DateToString(dirdate,z))
        ResponseAddField(r,"Last-Modified",z);
    
    ResponseChunked(r);
    ResponseWrite(r);

    if (r->request_info.method!=m_head)
    {
        if (text)
        {
            sprintf(z,"Index of %s" CRLF,r->request_info.uri);
            i=strlen(z)-2;
            p=z+i+2;

            while (i>0)
            {
                *(p++)='-';
                i--;
            };

            *p='\0';
            strcat(z,CRLF CRLF
                   "Name                      Size      "
                   "Date-Time             Type" CRLF
                   "------------------------------------"
                   "--------------------------------------------"CRLF);
        }
        else
        {
            sprintf(z,"<HTML><HEAD><TITLE>Index of %s</TITLE></HEAD><BODY>"
                    "<H1>Index of %s</H1><PRE>",r->request_info.uri,r->request_info.uri);
            strcat(z,"Name                      Size      "
                   "Date-Time             Type<HR WIDTH=100%>"CRLF);
        };

        HTTPWrite(r,z,strlen(z));

        /* Sort the files */
            qsort(list.item,list.size,sizeof(void *),
                  (TQSortProc)(sort==1?cmpfilenames:cmpfiledates));
        
        /* Write the listing */
        if (ascending)
            i=0;
        else
            i=list.size-1;

        while ((i<list.size) && (i>=0))
        {
            fi=list.item[i];

            if (ascending)
                i++;
            else
                i--;
            
            strcpy(z,fi->name);

            k=strlen(z);

            if (fi->attrib & A_SUBDIR)
            {
                z[k++]='/';
                z[k]='\0';
            };

            if (k>24)
            {
                z[10]='\0';
                strcpy(z1,z);
                strcat(z1,"...");
                strcat(z1,z+k-11);
                k=24;
                p=z1+24;
            }
            else
            {
                strcpy(z1,z);
                
                k++;
                p=z1+k;
                while (k<25)
                    z1[k++]=' ';

                z1[25]='\0';
            };

            ftm=*(gmtime(&fi->time_write));
            sprintf(z2,"%02u/%02u/%04u %02u:%02u:%02u",ftm.tm_mday,ftm.tm_mon,
                ftm.tm_year+1900,ftm.tm_hour,ftm.tm_min,ftm.tm_sec);

            if (fi->attrib & A_SUBDIR)
            {
                strcpy(z3,"   --  ");
                z4="Directory";
            }
            else
            {
                if (fi->size<9999)
                    u='b';
                else 
                {
                    fi->size/=1024;
                    if (fi->size<9999)
                        u='K';
                    else
                    {
                        fi->size/=1024;
                        if (fi->size<9999)
                            u='M';
                        else
                            u='G';
                    };
                };
                
                sprintf(z3, "%5llu %c", fi->size, u);
                
                if (strcmp(fi->name,"..")==0)
                    z4="";
                else
                    z4=MIMETypeFromFileName(fi->name);

                if (!z4)
                    z4="Unknown";
            };

            if (text)
                sprintf(z,"%s%s %s    %s   %s"CRLF,
                    z1,p,z3,z2,z4);
            else
                sprintf(z,"<A HREF=\"%s%s\">%s</A>%s %s    %s   %s"CRLF,
                    fi->name,(fi->attrib & A_SUBDIR?"/":""),z1,p,z3,z2,z4);

            HTTPWrite(r,z,strlen(z));
        };
        
        /* Write the tail of the file */
        if (text)
            strcpy(z,SERVER_PLAIN_INFO);
        else
            strcpy(z,"</PRE>" SERVER_HTML_INFO "</BODY></HTML>" CRLF CRLF);

        HTTPWrite(r,z,strlen(z));
    };
    
    HTTPWriteEnd(r);
    /* Free memory and exit */
    ListFree(&list);
    PoolFree(&pool);

    return TRUE;
}

static abyss_bool
ServerFileHandler(TSession *r,char *z,TDate *filedate) {
    char *mediatype;
    TFile file;
    uint64_t filesize,start,end;
    uint16_t i;
    TDate date;
    char *p;

    mediatype=MIMETypeGuessFromFile(z);

    if (!FileOpen(&file,z,O_BINARY | O_RDONLY))
    {
        ResponseStatusErrno(r);
        return TRUE;
    };

    if (DateCompare(&r->date,filedate)<0)
        filedate=&r->date;

    p=RequestHeaderValue(r,"if-modified-since");
    if (p) {
        if (DateDecode(p,&date)) {
            if (DateCompare(filedate,&date)<=0) {
                ResponseStatus(r,304);
                ResponseWrite(r);
                return TRUE;
            }
            else
                r->ranges.size=0;
        }
    }
    filesize=FileSize(&file);

    switch (r->ranges.size)
    {
    case 0:
        ResponseStatus(r,200);
        break;

    case 1:
        if (!RangeDecode((char *)(r->ranges.item[0]),filesize,&start,&end))
        {
            ListFree(&(r->ranges));
            ResponseStatus(r,200);
            break;
        }
        
        sprintf(z, "bytes %llu-%llu/%llu", start, end, filesize);

        ResponseAddField(r,"Content-range",z);
        ResponseContentLength(r,end-start+1);
        ResponseStatus(r,206);
        break;

    default:
        ResponseContentType(r,"multipart/ranges; boundary=" BOUNDARY);
        ResponseStatus(r,206);
        break;
    };
    
    if (r->ranges.size==0)
    {
        ResponseContentLength(r,filesize);
        ResponseContentType(r,mediatype);
    };
    
    if (DateToString(filedate,z))
        ResponseAddField(r,"Last-Modified",z);

    ResponseWrite(r);

    if (r->request_info.method!=m_head)
    {
        if (r->ranges.size==0)
            ConnWriteFromFile(r->conn,&file,0,filesize-1,z,4096,0);
        else if (r->ranges.size==1)
            ConnWriteFromFile(r->conn,&file,start,end,z,4096,0);
        else
            for (i=0;i<=r->ranges.size;i++)
            {
                ConnWrite(r->conn,"--",2);
                ConnWrite(r->conn,BOUNDARY,strlen(BOUNDARY));
                ConnWrite(r->conn,CRLF,2);

                if (i<r->ranges.size)
                    if (RangeDecode((char *)(r->ranges.item[i]),
                                    filesize,
                                    &start,&end))
                    {
                        /* Entity header, not response header */
                        sprintf(z,"Content-type: %s" CRLF
                                "Content-range: bytes %llu-%llu/%llu" CRLF
                                "Content-length: %llu" CRLF
                                CRLF, mediatype, start, end,
                                filesize, end-start+1);

                        ConnWrite(r->conn,z,strlen(z));

                        ConnWriteFromFile(r->conn,&file,start,end,z,4096,0);
                    };
            };
    };

    FileClose(&file);

    return TRUE;
}



static abyss_bool
ServerDefaultHandlerFunc(TSession * const sessionP) {

    struct _TServer * const srvP = ConnServer(sessionP->conn)->srvP;

    char *p;
    char z[4096];
    TFileStat fs;
    unsigned int i;
    abyss_bool endingslash=FALSE;
    TDate objdate;

    if (!RequestValidURIPath(sessionP)) {
        ResponseStatus(sessionP, 400);
        return TRUE;
    }

    /* Must check for * (asterisk uri) in the future */
    if (sessionP->request_info.method == m_options) {
        ResponseAddField(sessionP, "Allow", "GET, HEAD");
        ResponseContentLength(sessionP, 0);
        ResponseStatus(sessionP, 200);
        return TRUE;
    }

    if ((sessionP->request_info.method != m_get) &&
        (sessionP->request_info.method != m_head)) {
        ResponseAddField(sessionP, "Allow", "GET, HEAD");
        ResponseStatus(sessionP, 405);
        return TRUE;
    }

    strcpy(z, srvP->filespath);
    strcat(z, sessionP->request_info.uri);

    p = z + strlen(z) - 1;
    if (*p == '/') {
        endingslash = TRUE;
        *p = '\0';
    }

#ifdef WIN32
    p = z;
    while (*p) {
        if ((*p) == '/')
            *p= '\\';

        ++p;
    }
#endif  /* WIN32 */

    if (!FileStat(z, &fs)) {
        ResponseStatusErrno(sessionP);
        return TRUE;
    }

    if (fs.st_mode & S_IFDIR) {
        /* Redirect to the same directory but with the ending slash
        ** to avoid problems with some browsers (IE for examples) when
        ** they generate relative urls */
        if (!endingslash) {
            strcpy(z, sessionP->request_info.uri);
            p = z+strlen(z);
            *p = '/';
            *(p+1) = '\0';
            ResponseAddField(sessionP, "Location", z);
            ResponseStatus(sessionP, 302);
            ResponseWrite(sessionP);
            return TRUE;
        }

#ifdef WIN32
        *p = '\\';
#else
        *p = '/';
#endif  /* WIN32 */
        ++p;

        i = srvP->defaultfilenames.size;
        while (i-- > 0) {
            *p = '\0';        
            strcat(z, (srvP->defaultfilenames.item[i]));
            if (FileStat(z, &fs)) {
                if (!(fs.st_mode & S_IFDIR)) {
                    if (DateFromLocal(&objdate, fs.st_mtime))
                        return ServerFileHandler(sessionP, z, &objdate);
                    else
                        return ServerFileHandler(sessionP, z, NULL);
                }
            }
        }

        *(p-1) = '\0';
        
        if (!FileStat(z, &fs)) {
            ResponseStatusErrno(sessionP);
            return TRUE;
        }

        if (DateFromLocal(&objdate, fs.st_mtime))
            return ServerDirectoryHandler(sessionP, z ,&objdate);
        else
            return ServerDirectoryHandler(sessionP, z, NULL);

    } else {
        if (DateFromLocal(&objdate, fs.st_mtime))
            return ServerFileHandler(sessionP, z, &objdate);
        else
            return ServerFileHandler(sessionP, z, NULL);
    }
}



static void
initUnixStuff(struct _TServer * const srvP) {
#ifdef _UNIX
    srvP->pidfile = srvP->uid = srvP->gid = -1;
#endif
}



static abyss_bool
logOpen(struct _TServer * const srvP) {

    abyss_bool success;

    success = FileOpenCreate(&srvP->logfile, srvP->logfilename,
                             O_WRONLY | O_APPEND);
    if (success) {
        abyss_bool success;
        success = MutexCreate(&srvP->logmutex);
        if (success)
            srvP->logfileisopen = TRUE;
        else
            TraceMsg("Can't create mutex for log file");

        if (!success)
            FileClose(&srvP->logfile);
    } else
        TraceMsg("Can't open log file '%s'", srvP->logfilename);

    return success;
}



static void
logClose(struct _TServer * const srvP) {

    if (srvP->logfileisopen) {
        FileClose(&srvP->logfile);
        MutexFree(&srvP->logmutex);
        srvP->logfileisopen = FALSE;
    }
}



static void
initLogFile(struct _TServer * const srvP,
            const char *      const logfilename) {

    srvP->logfileisopen = FALSE;
    if (logfilename)
        srvP->logfilename = strdup(logfilename); 
    else
        srvP->logfilename = NULL;
}



static void
initSocketStuff(struct _TServer * const srvP,
                abyss_bool        const useBoundSocket,
                TSocket           const socketFd,
                abyss_bool        const makeSocket,
                uint16_t          const port,
                abyss_bool *      const successP) {

    if (useBoundSocket && makeSocket) {
        TraceMsg("You can't specify both 'useBoundSocket' and "
                 "'makeSocket'");
        *successP = FALSE;
    } else if (useBoundSocket) {
        *successP = TRUE;
        srvP->serverAcceptsConnections = TRUE;
        srvP->socketBound = TRUE;
        srvP->listensock = socketFd;
    } else if (makeSocket) {
        *successP = TRUE;
        srvP->serverAcceptsConnections = TRUE;
        srvP->socketBound = FALSE;
        srvP->port = port;
    } else {
        *successP = TRUE;
        srvP->serverAcceptsConnections = FALSE;
        srvP->socketBound = FALSE;
    }
}



static void
createServer(struct _TServer ** const srvPP,
             const char *       const name,
             abyss_bool         const useBoundSocket,
             TSocket            const socketFd,
             abyss_bool         const makeSocket,
             uint16_t           const port,
             const char *       const filespath,
             const char *       const logfilename,
             abyss_bool *       const successP) {

    struct _TServer * srvP;

    MALLOCVAR(srvP);

    if (srvP == NULL) {
        TraceMsg("Unable to allocate space for server descriptor");
        *successP = FALSE;
    } else {
        if (name)
            srvP->name = strdup(name);
        else
            srvP->name = strdup("unnamed");

        initSocketStuff(srvP, useBoundSocket, socketFd, makeSocket, port,
                        successP);
        if (*successP) {
            srvP->defaulthandler = ServerDefaultHandlerFunc;

            if (filespath)
                srvP->filespath = strdup(filespath);
            else
                srvP->filespath = strdup(DEFAULT_DOCS);

            srvP->keepalivetimeout = 15;
            srvP->keepalivemaxconn = 30;
            srvP->timeout = 15;
            srvP->advertise = TRUE;

            initUnixStuff(srvP);

            ListInit(&srvP->handlers);
            ListInitAutoFree(&srvP->defaultfilenames);

            initLogFile(srvP, logfilename);

            *successP = TRUE;
        }        
        if (!*successP)
            free(srvP);
    }
    *srvPP = srvP;
}



abyss_bool
ServerCreate(TServer *    const serverP,
             const char * const name,
             uint16_t     const port,
             const char * const filespath,
             const char * const logfilename) {

    abyss_bool const useBoundSocketFalse = FALSE;
    abyss_bool const makeSocketTrue      = TRUE;

    abyss_bool success;

    createServer(&serverP->srvP, name,
                 useBoundSocketFalse, 0,
                 makeSocketTrue, port,
                 filespath, logfilename, &success);

    return success;
}



abyss_bool
ServerCreateSocket(TServer *    const serverP,
                   const char * const name,
                   TSocket      const socketFd,
                   const char * const filespath,
                   const char * const logfilename) {

    abyss_bool const useBoundSocketTrue = TRUE;
    abyss_bool const makeSocketFalse    = FALSE;
    abyss_bool success;

    createServer(&serverP->srvP, name,
                 useBoundSocketTrue, socketFd,
                 makeSocketFalse, 0,
                 filespath, logfilename, &success);

    return success;
}



abyss_bool
ServerCreateNoAccept(TServer *    const serverP,
                     const char * const name,
                     const char * const filespath,
                     const char * const logfilename) {

    abyss_bool const useBoundSocketFalse = FALSE;
    abyss_bool const makeSocketFalse     = FALSE;
    abyss_bool success;

    createServer(&serverP->srvP, name,
                 useBoundSocketFalse, 0,
                 makeSocketFalse, 0,
                 filespath, logfilename, &success);

    return success;
}



static void
terminateHandlers(TList * const handlersP) {
/*----------------------------------------------------------------------------
   Terminate all handlers in the list '*handlersP'.

   I.e. call each handler's terminate function.
-----------------------------------------------------------------------------*/
    if (handlersP->item) {
        unsigned int i;
        for (i = handlersP->size; i > 0; --i) {
            URIHandler2 * const handlerP = handlersP->item[i-1];
            if (handlerP->term)
                handlerP->term(handlerP);
        }
    }
}



void
ServerFree(TServer * const serverP) {

    struct _TServer * const srvP = serverP->srvP;

    xmlrpc_strfree(srvP->name);

    xmlrpc_strfree(srvP->filespath);
    
    terminateHandlers(&srvP->handlers);

    ListFree(&srvP->handlers);

    ListInitAutoFree(&srvP->defaultfilenames);

    logClose(srvP);

    if (srvP->logfilename)
        xmlrpc_strfree(srvP->logfilename);

    free(srvP);
}



void
ServerSetKeepaliveTimeout(TServer * const serverP,
                          uint32_t  const keepaliveTimeout) {

    serverP->srvP->keepalivetimeout = keepaliveTimeout;
}



void
ServerSetKeepaliveMaxConn(TServer * const serverP,
                          uint32_t  const keepaliveMaxConn) {

    serverP->srvP->keepalivemaxconn = keepaliveMaxConn;
}



void
ServerSetTimeout(TServer * const serverP,
                 uint32_t  const timeout) {

    serverP->srvP->timeout = timeout;
}



void
ServerSetAdvertise(TServer *  const serverP,
                   abyss_bool const advertise) {

    serverP->srvP->advertise = advertise;
}


static void
runUserHandler(TSession *        const sessionP,
               struct _TServer * const srvP) {

    abyss_bool handled;
    int i;
    
    for (i = srvP->handlers.size-1, handled = FALSE;
         i >= 0 && !handled;
         --i) {
        URIHandler2 * const handlerP = srvP->handlers.item[i];
        
        if (handlerP->handleReq2)
            handlerP->handleReq2(handlerP, sessionP, &handled);
        else if (handlerP->handleReq1)
            handled = handlerP->handleReq1(sessionP);
    }
    
    if (!handled)
        ((URIHandler)(srvP->defaulthandler))(sessionP);
}



static void
processDataFromClient(TConn *      const connectionP,
                      abyss_bool   const lastReqOnConn,
                      abyss_bool * const keepAliveP) {

    TSession session;

    RequestInit(&session, connectionP);

    if (RequestRead(&session)) {
        /* Check if it is the last keepalive */
        if (lastReqOnConn)
            session.keepalive = FALSE;
        
        session.cankeepalive = session.keepalive;
        
        if (session.status == 0) {
            if (session.versionmajor >= 2)
                ResponseStatus(&session, 505);
            else if (!RequestValidURI(&session))
                ResponseStatus(&session, 400);
            else
                runUserHandler(&session, connectionP->server->srvP);
        }
    }
            
    HTTPWriteEnd(&session);

    if (!session.done)
        ResponseError(&session);
    
    SessionLog(&session);

    *keepAliveP = (session.keepalive && session.cankeepalive);
    
    RequestFree(&session);
}



static void
ServerFunc(TConn * const connectionP) {
/*----------------------------------------------------------------------------
   Do server stuff on one connection.  At its simplest, this means do
   one HTTP request.  But with keepalive, it can be many requests.
-----------------------------------------------------------------------------*/
    struct _TServer * const srvP = connectionP->server->srvP;

    unsigned int requestCount;
        /* Number of requests we've handled so far on this connection */
    abyss_bool connectionDone;
        /* No more need for this HTTP connection */

    requestCount = 0;
    connectionDone = FALSE;

    while (!connectionDone) {
        abyss_bool success;
        
        /* Wait to read until timeout */
        success = ConnRead(connectionP, srvP->keepalivetimeout);

        if (!success)
            connectionDone = TRUE;
        else {
            abyss_bool const lastReqOnConn =
                requestCount + 1 >= srvP->keepalivemaxconn;

            abyss_bool keepalive;
            
            processDataFromClient(connectionP, lastReqOnConn, &keepalive);
            
            ++requestCount;

            if (!keepalive)
                connectionDone = TRUE;
            else if (requestCount >= srvP->keepalivemaxconn)
                connectionDone = TRUE;
            
            /**************** Must adjust the read buffer *****************/
            ConnReadInit(connectionP);
        }
    }
    SocketClose(&connectionP->socket);
}



static void
createAndBindSocket(struct _TServer * const srvP) {

    abyss_bool success;

    success = SocketInit();
    if (!success)
        TraceMsg("Can't initialize TCP sockets");
    else {
        abyss_bool success;
        int socketFd;
        
        success = SocketCreate(&socketFd);
        
        if (!success)
            TraceMsg("Can't create a socket");
        else {
            abyss_bool success;
            
            success = SocketBind(&socketFd, NULL, srvP->port);

            if (!success)
                        TraceMsg("Can't bind");
            else {
                srvP->socketBound = TRUE;
                srvP->listensock = socketFd;
            }
        }
    }
}



void
ServerInit(TServer * const serverP) {
/*----------------------------------------------------------------------------
   Initialize a server to accept connections.

   Do not confuse this with creating the server -- ServerCreate().

   Not necessary or valid with a server that doesn't accept connections (i.e.
   user supplies the TCP connections).
-----------------------------------------------------------------------------*/
    struct _TServer * const srvP = serverP->srvP;
    abyss_bool success;
    
    if (!srvP->serverAcceptsConnections) {
        TraceMsg("ServerInit() is not valid on a server that doesn't "
                 "accept connections "
                 "(i.e. created with ServerCreateNoAccept)");
        success = FALSE;
    } else {
        if (!srvP->socketBound)
            createAndBindSocket(srvP);

        if (srvP->socketBound) {
            success = SocketListen(&srvP->listensock, MAX_CONN);

            if (!success)
                TraceMsg("Failed to listen on socket with file descriptor %d",
                         srvP->listensock);
        } else
            success = FALSE;
    }
    if (!success)
        exit(1);
}



/* With pthread configuration, our connections run as threads of a
   single address space, so we manage a pool of connection
   descriptors.  With fork configuration, our connections run as
   separate processes with their own memory, so we don't have the
   pool.
*/

static abyss_bool const usingPthreadsForConnections = 
#ifdef _THREAD
TRUE;
#else
FALSE;
#endif



static void
createConnectionPool(TConn ** const connectionPoolP) {

    unsigned int i;

    TConn * connectionPool;

    MALLOCARRAY_NOFAIL(connectionPool, MAX_CONN);
    
    for (i = 0; i < MAX_CONN; ++i)
        connectionPool[i].inUse = FALSE;

    *connectionPoolP = connectionPool;
}



static void
collectDeadConnections(TConn * const connectionPool) {
/*----------------------------------------------------------------------------
   Garbage-collect the resources associated with connections that are no
   longer connected.  Thread resources, connection pool descriptor, etc.
-----------------------------------------------------------------------------*/
    unsigned int i;

    for (i = 0; i < MAX_CONN; ++i) {
        TConn * const connectionP = &connectionPool[i];
        if (connectionP->inUse && !connectionP->connected) {
            ConnClose(connectionP);
            connectionP->inUse = FALSE;
        }
    }
}



static void
allocateConnection(TConn * const connectionPool,
                   TConn ** const connectionPP) {
    
    unsigned int i;

    collectDeadConnections(connectionPool);
    
    for (i = 0; i < MAX_CONN && connectionPool[i].inUse; ++i);
    
    if (i == MAX_CONN)
        /* Every connection descriptor was in use. */
        *connectionPP = NULL;
    else
        *connectionPP = &connectionPool[i];
}



static void 
serverRunThreaded(TServer * const serverP) {

    struct _TServer * const srvP = serverP->srvP;
    TSocket const listenSocket = srvP->listensock;
    TIPAddr peerIpAddr;
    TConn * connectionPool;

    createConnectionPool(&connectionPool);

    while (1) {
        TSocket connectedSocketP;
        TConn * connectionP;

        allocateConnection(connectionPool, &connectionP);

        if (connectionP == NULL)
            ThreadWait(2000);
        else {
            abyss_bool success;

            success = SocketAccept(&listenSocket,
                                   &connectedSocketP, &peerIpAddr);

            if (success) {
                abyss_bool success;
                connectionP->inUse = TRUE;
                success = ConnCreate2(connectionP, serverP, connectedSocketP,
                                      &ServerFunc, ABYSS_BACKGROUND);
                if (success) {
                    ConnProcess(connectionP);
                } else {
                    SocketClose(&connectedSocketP);
                    connectionP->inUse = FALSE;
                }
            } else
                TraceMsg("Socket Error=%d", SocketError());
        }
    }
    /* We never get here, but it's conceptually possible for someone to 
       terminate a server normally, so... 
    */
    free(connectionPool);
}



static void 
serverRunForked(TServer * const serverP) {

    struct _TServer * const srvP = serverP->srvP;

    TSocket socketFd;
    TSocket newSocketFd;
    TConn conn;
    TIPAddr ipAddr;

    socketFd = srvP->listensock;

    while (TRUE) {
        abyss_bool success;
        success = SocketAccept(&socketFd, &newSocketFd, &ipAddr);
        if (success) {
            abyss_bool success;
            success = ConnCreate2(&conn, serverP, newSocketFd, 
                                  ServerFunc, ABYSS_BACKGROUND);

                /* ConnCreate2() forks.  Child does not return. */
            if (success)
                ConnProcess(&conn);

            SocketClose(&newSocketFd); /* Close parent's copy of socket */
        } else
            TraceMsg("Socket Error=%d", SocketError());
    }
}



void 
ServerRun(TServer * const serverP) {

    struct _TServer * const srvP = serverP->srvP;

    if (!srvP->socketBound)
        TraceMsg("This server is not set up to accept connections "
                 "on its own, so you can't use ServerRun().  "
                 "Try ServerRunConn() or ServerInit()");
    else {
        if (usingPthreadsForConnections)
            serverRunThreaded(serverP);
        else
            serverRunForked(serverP);
    }
}



/* ServerRunOnce() supplied by Brian Quinlan of ActiveState. */

/* Bryan Henderson found this to be completely wrong on 2004.11.29
   and changed it so it does the same thing as ServerRun(), but only
   once.

   The biggest problem it had was that when it forked the child (via
   ConnCreate(), both the parent and the child read the socket and
   processed the request!

   But even fixing that, it works only on systems that do threading
   with Unix fork, because otherwise the background thread would get a
   connection object that lives on ServerRunOnce's stack, and that's
   no good.

   So in Xmlrpc-c 1.04, we just made ServerRunOnce() always do its thing
   in the foreground while Caller waits.
*/


static void
serverRunConn(TServer * const serverP,
              TSocket   const connectedSocket) {
/*----------------------------------------------------------------------------
   Do the HTTP transaction on the TCP connection on the socket 'socketP'
   (socket must be connected state, with nothing having been read or
   written on the connection yet).
-----------------------------------------------------------------------------*/
    struct _TServer * const srvP = serverP->srvP;

    TConn connection;
    abyss_bool success;

    srvP->keepalivemaxconn = 1;

    connection.connected = FALSE;
    connection.inUse = FALSE;
        
    success = ConnCreate2(&connection, 
                          serverP, connectedSocket,
                          &ServerFunc, ABYSS_FOREGROUND);
    if (!success)
        TraceMsg("Couldn't create HTTP connection out of socket "
                 "with file descriptor %d.", (int)connectedSocket);
    else
        ConnProcess(&connection);
}



void
ServerRunConn(TServer * const serverP,
              TSocket   const connectedSocket) {
/*----------------------------------------------------------------------------
   Do the HTTP transaction on the TCP connection on the socket 'socketP'
   (socket must be connected state, with nothing having been read or
   written on the connection yet).
-----------------------------------------------------------------------------*/
    struct _TServer * const srvP = serverP->srvP;

    if (srvP->serverAcceptsConnections)
        TraceMsg("This server is configured to accept connections on "
                 "its own socket.  "
                 "Try ServerRun() or ServerCreateNoAccept().");
    else
        serverRunConn(serverP, connectedSocket);
}



void
ServerRunOnce(TServer * const serverP) {
/*----------------------------------------------------------------------------
   Accept a connection from the listening socket and do the HTTP
   transaction that comes over it.
-----------------------------------------------------------------------------*/
    struct _TServer * const srvP = serverP->srvP;

    TSocket listenSocket;
    TSocket connectedSocket;
    TIPAddr remoteAddr;
    abyss_bool success;
    
    if (!srvP->socketBound)
        TraceMsg("This server is not set up to accept connections "
                 "on its own, so you can't use ServerRunOnce().  "
                 "Try ServerRunConn() or ServerInit()");
    else {
        srvP->keepalivemaxconn = 1;

        listenSocket = srvP->listensock;
        
        success = SocketAccept(&listenSocket, &connectedSocket, &remoteAddr);
        if (success) {
            serverRunConn(serverP, connectedSocket);
            SocketClose(&connectedSocket);
        } else
            TraceMsg("Socket Error=%d", SocketError());
    }
}



void
ServerRunOnce2(TServer *           const serverP,
               enum abyss_foreback const foregroundBackground ATTR_UNUSED) {
/*----------------------------------------------------------------------------
   This is a backward compatibility interface to ServerRunOnce().

   'foregroundBackground' is meaningless.  We always process the
   connection in the foreground.  The parameter exists because we once
   thought we could do them in the background, but we really can't do
   that in any clean way.  If Caller wants background execution, he can
   spin his own thread or process to call us.  It makes much more sense
   in Caller's context.
-----------------------------------------------------------------------------*/
    ServerRunOnce(serverP);
}



static void
setGroups(void) {

#ifdef HAVE_SETGROUPS   
    if (setgroups(0, NULL) == (-1))
        TraceExit("Failed to setup the group.");
#endif
}



void
ServerDaemonize(TServer * const serverP) {
/*----------------------------------------------------------------------------
   Turn Caller into a daemon (i.e. fork a child, then exit; the child
   returns to Caller).

   NOTE: It's ridiculous, but conventional, for us to do this.  It's
   ridiculous because the task of daemonizing is not something
   particular to Abyss.  It ought to be done by a higher level.  In
   fact, it should be done before the Abyss server program is even
   execed.  The user should run a "daemonize" program that creates a
   daemon which execs the Abyss server program.
-----------------------------------------------------------------------------*/
    struct _TServer * const srvP = serverP->srvP;

#ifndef _WIN32
    /* Become a daemon */
    switch (fork()) {
    case 0:
        break;
    case -1:
        TraceExit("Unable to become a daemon");
    default:
        /* We are the parent */
        exit(0);
    }
    
    setsid();

    /* Change the current user if we are root */
    if (getuid()==0) {
        if (srvP->uid == (uid_t)-1)
            TraceExit("Can't run under root privileges.  "
                      "Please add a User option in your "
                      "Abyss configuration file.");

        setGroups();

        if (srvP->gid != (gid_t)-1)
            if (setgid(srvP->gid)==(-1))
                TraceExit("Failed to change the group.");
        
        if (setuid(srvP->uid) == -1)
            TraceExit("Failed to change the user.");
    }
    
    if (srvP->pidfile != -1) {
        char z[16];
    
        sprintf(z, "%d", getpid());
        FileWrite(&srvP->pidfile, z, strlen(z));
        FileClose(&srvP->pidfile);
    }
#endif  /* _WIN32 */
}



void
ServerAddHandler2(TServer *     const serverP,
                  URIHandler2 * const handlerArgP,
                  abyss_bool *  const successP) {

    URIHandler2 * handlerP;

    MALLOCVAR(handlerP);
    if (handlerP == NULL)
        *successP = FALSE;
    else {
        *handlerP = *handlerArgP;

        if (handlerP->init == NULL)
            *successP = TRUE;
        else
            handlerP->init(handlerP, successP);

        if (*successP) {
            *successP = ListAdd(&serverP->srvP->handlers, handlerP);

            if (!*successP) {
                if (handlerP->term)
                    handlerP->term(handlerP);
            }
        }
        if (!*successP)
            free(handlerP);
    }
}



static void
destroyHandler(URIHandler2 * const handlerP) {

    free(handlerP);
}



static URIHandler2 *
createHandler(URIHandler const function) {

    URIHandler2 * handlerP;

    MALLOCVAR(handlerP);
    if (handlerP != NULL) {
        handlerP->init       = NULL;
        handlerP->term       = destroyHandler;
        handlerP->userdata   = NULL;
        handlerP->handleReq2 = NULL;
        handlerP->handleReq1 = function;
    }
    return handlerP;
}



abyss_bool
ServerAddHandler(TServer *  const serverP,
                 URIHandler const function) {

    URIHandler2 * handlerP;
    abyss_bool success;

    handlerP = createHandler(function);

    if (handlerP == NULL)
        success = FALSE;
    else {
        success = ListAdd(&serverP->srvP->handlers, handlerP);

        if (!success)
            free(handlerP);
    }
    return success;
}



void
ServerDefaultHandler(TServer *  const serverP,
                     URIHandler const handler) {

    serverP->srvP->defaulthandler = handler;
}



void
LogWrite(TServer *    const serverP,
         const char * const msg) {

    struct _TServer * const srvP = serverP->srvP;

    if (!srvP->logfileisopen && srvP->logfilename)
        logOpen(srvP);

    if (srvP->logfileisopen) {
        abyss_bool success;
        success = MutexLock(&srvP->logmutex);
        if (success) {
            FileWrite(&srvP->logfile, msg, strlen(msg));
            FileWrite(&srvP->logfile, LBR, strlen(LBR));
        
            MutexUnlock(&srvP->logmutex);
        }
    }
}
/*******************************************************************************
**
** server.c
**
** This file is part of the ABYSS Web server project.
**
** Copyright (C) 2000 by Moez Mahfoudh <mmoez@bigfoot.com>.
** All rights reserved.
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
** SUCH DAMAGE.
**
******************************************************************************/
