/* Copyright information is at end of file */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/errno.h>
#ifdef WIN32
  #include <io.h>
#else
  #include <unistd.h>
  #include <grp.h>
#endif
#include <fcntl.h>

#include "xmlrpc_config.h"
#include "mallocvar.h"
#include "xmlrpc-c/string_int.h"
#include "xmlrpc-c/sleep_int.h"

#include "xmlrpc-c/abyss.h"
#include "trace.h"
#include "session.h"
#include "conn.h"
#include "chanswitch.h"
#include "channel.h"
#include "socket.h"
#ifdef WIN32
  #include "socket_win.h"
#else
  #include "socket_unix.h"
#endif
#include "http.h"
#include "date.h"
#include "abyss_info.h"

#include "server.h"


void
ServerTerminate(TServer * const serverP) {

    struct _TServer * const srvP = serverP->srvP;

    srvP->terminationRequested = true;
}



void
ServerResetTerminate(TServer * const serverP) {

    struct _TServer * const srvP = serverP->srvP;

    srvP->terminationRequested = false;
}



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



static void
determineSortType(const char *  const query,
                  abyss_bool *  const ascendingP,
                  uint16_t *    const sortP,
                  abyss_bool *  const textP,
                  const char ** const errorP) {

    *ascendingP = TRUE;
    *sortP = 1;
    *textP = FALSE;
    *errorP = NULL;
    
    if (query) {
        if (xmlrpc_streq(query, "plain"))
            *textP = TRUE;
        else if (xmlrpc_streq(query, "name-up")) {
            *sortP = 1;
            *ascendingP = TRUE;
        } else if (xmlrpc_streq(query, "name-down")) {
            *sortP = 1;
            *ascendingP = FALSE;
        } else if (xmlrpc_streq(query, "date-up")) {
            *sortP = 2;
            *ascendingP = TRUE;
        } else if (xmlrpc_streq(query, "date-down")) {
            *sortP = 2;
            *ascendingP = FALSE;
        } else  {
            xmlrpc_asprintf(errorP, "invalid query value '%s'", query);
        }
    }
}



static void
generateListing(TList *       const listP,
                char *        const z,
                const char *  const uri,
                TPool *       const poolP,
                const char ** const errorP,
                uint16_t *    const responseStatusP) {
    
    TFileInfo fileinfo;
    TFileFind findhandle;

    *errorP = NULL;

    if (!FileFindFirst(&findhandle, z, &fileinfo)) {
        *responseStatusP = ResponseStatusFromErrno(errno);
        xmlrpc_asprintf(errorP, "Can't read first entry in directory");
    } else {
        ListInit(listP);

        do {
            TFileInfo * fi;
            /* Files whose names start with a dot are ignored */
            /* This includes implicitly the ./ and ../ */
            if (*fileinfo.name == '.') {
                if (xmlrpc_streq(fileinfo.name, "..")) {
                    if (xmlrpc_streq(uri, "/"))
                        continue;
                } else
                    continue;
            }
            fi = (TFileInfo *)PoolAlloc(poolP, sizeof(fileinfo));
            if (fi) {
                abyss_bool success;
                memcpy(fi, &fileinfo, sizeof(fileinfo));
                success =  ListAdd(listP, fi);
                if (!success)
                    xmlrpc_asprintf(errorP, "ListAdd() failed");
            } else
                xmlrpc_asprintf(errorP, "PoolAlloc() failed.");
        } while (!*errorP && FileFindNext(&findhandle, &fileinfo));

        if (*errorP) {
            *responseStatusP = 500;
            ListFree(listP);
        }            
        FileFindClose(&findhandle);
    }
}



static void
sendDirectoryDocument(TList *      const listP,
                      abyss_bool   const ascending,
                      uint16_t     const sort,
                      abyss_bool   const text,
                      const char * const uri,
                      MIMEType *   const mimeTypeP,
                      TSession *   const sessionP,
                      char *       const z) {

    char *p,z1[26],z2[20],z3[9],u;
    const char * z4;
    int16_t i;
    uint32_t k;

    if (text) {
        sprintf(z, "Index of %s" CRLF, uri);
        i = strlen(z)-2;
        p = z + i + 2;

        while (i > 0) {
            *(p++) = '-';
            --i;
        }

        *p = '\0';
        strcat(z, CRLF CRLF
               "Name                      Size      "
               "Date-Time             Type" CRLF
               "------------------------------------"
               "--------------------------------------------"CRLF);
    } else {
        sprintf(z, "<HTML><HEAD><TITLE>Index of %s</TITLE></HEAD><BODY>"
                "<H1>Index of %s</H1><PRE>",
                uri, uri);
        strcat(z, "Name                      Size      "
               "Date-Time             Type<HR WIDTH=100%>"CRLF);
    }

    HTTPWriteBodyChunk(sessionP, z, strlen(z));

    /* Sort the files */
    qsort(listP->item, listP->size, sizeof(void *),
          (TQSortProc)(sort == 1 ? cmpfilenames : cmpfiledates));
    
    /* Write the listing */
    if (ascending)
        i = 0;
    else
        i = listP->size - 1;

    while ((i < listP->size) && (i >= 0)) {
        TFileInfo * fi;
        struct tm ftm;

        fi = listP->item[i];

        if (ascending)
            ++i;
        else
            --i;
            
        strcpy(z, fi->name);

        k = strlen(z);

        if (fi->attrib & A_SUBDIR) {
            z[k++] = '/';
            z[k] = '\0';
        }

        if (k > 24) {
            z[10] = '\0';
            strcpy(z1, z);
            strcat(z1, "...");
            strcat(z1, z + k - 11);
            k = 24;
            p = z1 + 24;
        } else {
            strcpy(z1, z);
            
            ++k;
            p = z1 + k;
            while (k < 25)
                z1[k++] = ' ';
            
            z1[25] = '\0';
        }

        ftm = *gmtime(&fi->time_write);
        sprintf(z2, "%02u/%02u/%04u %02u:%02u:%02u",ftm.tm_mday,ftm.tm_mon,
                ftm.tm_year+1900,ftm.tm_hour,ftm.tm_min,ftm.tm_sec);

        if (fi->attrib & A_SUBDIR) {
            strcpy(z3, "   --  ");
            z4 = "Directory";
        } else {
            if (fi->size < 9999)
                u = 'b';
            else {
                fi->size /= 1024;
                if (fi->size < 9999)
                    u = 'K';
                else {
                    fi->size /= 1024;
                    if (fi->size < 9999)
                        u = 'M';
                    else
                        u = 'G';
                }
            }
                
            sprintf(z3, "%5llu %c", fi->size, u);
            
            if (xmlrpc_streq(fi->name, ".."))
                z4 = "";
            else
                z4 = MIMETypeFromFileName2(mimeTypeP, fi->name);

            if (!z4)
                z4 = "Unknown";
        }

        if (text)
            sprintf(z, "%s%s %s    %s   %s"CRLF, z1, p, z3, z2, z4);
        else
            sprintf(z, "<A HREF=\"%s%s\">%s</A>%s %s    %s   %s"CRLF,
                    fi->name, fi->attrib & A_SUBDIR ? "/" : "",
                    z1, p, z3, z2, z4);

        HTTPWriteBodyChunk(sessionP, z, strlen(z));
    }
        
    /* Write the tail of the file */
    if (text)
        strcpy(z, SERVER_PLAIN_INFO);
    else
        strcpy(z, "</PRE>" SERVER_HTML_INFO "</BODY></HTML>" CRLF CRLF);
    
    HTTPWriteBodyChunk(sessionP, z, strlen(z));
}



static void
fileDate(TSession * const sessionP,
         time_t     const statFilemodTime,
         TDate *    const fileDateP) {

    abyss_bool haveDate;
    TDate filemodDate;

    haveDate = DateFromLocal(&filemodDate, statFilemodTime);

    if (haveDate) {
        if (DateCompare(&sessionP->date, &filemodDate) < 0)
            *fileDateP = sessionP->date;
        else
            *fileDateP = filemodDate;
    } else
        *fileDateP = sessionP->date;
}



static abyss_bool
ServerDirectoryHandler(TSession * const r,
                       char *     const z,
                       time_t     const fileModTime,
                       MIMEType * const mimeTypeP) {

    TList list;
    abyss_bool text;
    abyss_bool ascending;
    uint16_t sort;    /* 1=by name, 2=by date */
    TPool pool;
    TDate date;
    const char * error;
    uint16_t responseStatus;
    TDate dirdate;
    const char * imsHdr;
    
    determineSortType(r->requestInfo.query, &ascending, &sort, &text, &error);

    if (error) {
        ResponseStatus(r,400);
        xmlrpc_strfree(error);
        return TRUE;
    }

    fileDate(r, fileModTime, &dirdate);

    imsHdr = RequestHeaderValue(r, "If-Modified-Since");
    if (imsHdr) {
        if (DateDecode(imsHdr, &date)) {
            if (DateCompare(&dirdate, &date) <= 0) {
                ResponseStatus(r, 304);
                ResponseWriteStart(r);
                return TRUE;
            }
        }
    }

    if (!PoolCreate(&pool, 1024)) {
        ResponseStatus(r, 500);
        return TRUE;
    }

    generateListing(&list, z, r->requestInfo.uri,
                    &pool, &error, &responseStatus);
    if (error) {
        ResponseStatus(r, responseStatus);
        xmlrpc_strfree(error);
        PoolFree(&pool);
        return TRUE;
    }

    /* Send something to the user to show that we are still alive */
    ResponseStatus(r, 200);
    ResponseContentType(r, (text ? "text/plain" : "text/html"));

    {
        const char * lastModifiedValue;
        DateToString(&dirdate, &lastModifiedValue);
        if (lastModifiedValue) {
            ResponseAddField(r, "Last-Modified", lastModifiedValue);
            xmlrpc_strfree(lastModifiedValue);
        }
    }
    
    ResponseChunked(r);
    ResponseWriteStart(r);

    if (r->requestInfo.method!=m_head)
        sendDirectoryDocument(&list, ascending, sort, text,
                              r->requestInfo.uri, mimeTypeP, r, z);

    HTTPWriteEndChunk(r);

    /* Free memory and exit */
    ListFree(&list);
    PoolFree(&pool);

    return TRUE;
}



#define BOUNDARY    "##123456789###BOUNDARY"

static void
sendBody(TSession *   const sessionP,
         TFile *      const fileP,
         uint64_t     const filesize,
         const char * const mediatype,
         uint64_t     const start0,
         uint64_t     const end0,
         char *       const z) {

    if (sessionP->ranges.size == 0)
        ConnWriteFromFile(sessionP->conn, fileP, 0, filesize - 1, z, 4096, 0);
    else if (sessionP->ranges.size == 1)
        ConnWriteFromFile(sessionP->conn, fileP, start0, end0, z, 4096, 0);
    else {
        uint64_t i;
        for (i = 0; i <= sessionP->ranges.size; ++i) {
            ConnWrite(sessionP->conn,"--", 2);
            ConnWrite(sessionP->conn, BOUNDARY, strlen(BOUNDARY));
            ConnWrite(sessionP->conn, CRLF, 2);

            if (i < sessionP->ranges.size) {
                uint64_t start;
                uint64_t end;
                abyss_bool decoded;
                    
                decoded = RangeDecode((char *)(sessionP->ranges.item[i]),
                                      filesize,
                                      &start, &end);
                if (decoded) {
                    /* Entity header, not response header */
                    sprintf(z, "Content-type: %s" CRLF
                            "Content-range: bytes %llu-%llu/%llu" CRLF
                            "Content-length: %llu" CRLF
                            CRLF, mediatype, start, end,
                            filesize, end-start+1);

                    ConnWrite(sessionP->conn, z, strlen(z));

                    ConnWriteFromFile(sessionP->conn, fileP, start, end, z,
                                      4096, 0);
                }
            }
        }
    }
}



static abyss_bool
ServerFileHandler(TSession * const r,
                  char *     const fileName,
                  time_t     const fileModTime,
                  MIMEType * const mimeTypeP) {
/*----------------------------------------------------------------------------
   This is an HTTP request handler for a GET.  It does the classic
   web server thing: send the file named in the URL to the client.
-----------------------------------------------------------------------------*/
    const char * mediatype;
    TFile file;
    uint64_t filesize;
    uint64_t start;
    uint64_t end;
    TDate date;
    char * p;
    TDate filedate;
    
    mediatype = MIMETypeGuessFromFile2(mimeTypeP, fileName);

    if (!FileOpen(&file, fileName, O_BINARY | O_RDONLY)) {
        ResponseStatusErrno(r);
        return TRUE;
    }

    fileDate(r, fileModTime, &filedate);

    p = RequestHeaderValue(r, "if-modified-since");
    if (p) {
        if (DateDecode(p,&date)) {
            if (DateCompare(&filedate, &date) <= 0) {
                ResponseStatus(r, 304);
                ResponseWriteStart(r);
                return TRUE;
            } else
                r->ranges.size = 0;
        }
    }
    filesize = FileSize(&file);

    switch (r->ranges.size) {
    case 0:
        ResponseStatus(r, 200);
        break;

    case 1: {
        abyss_bool decoded;
        decoded = RangeDecode((char *)(r->ranges.item[0]), filesize,
                              &start, &end);
        if (!decoded) {
            ListFree(&(r->ranges));
            ResponseStatus(r, 200);
            break;
        }

        {
            const char * contentRange;
            xmlrpc_asprintf(&contentRange, "bytes %llu-%llu/%llu",
                            start, end, filesize);
            ResponseAddField(r, "Content-range", contentRange);
            xmlrpc_strfree(contentRange);
        }
        ResponseContentLength(r, end - start + 1);
        ResponseStatus(r, 206);
    } break;

    default:
        ResponseContentType(r, "multipart/ranges; boundary=" BOUNDARY);
        ResponseStatus(r, 206);
        break;
    }
    
    if (r->ranges.size == 0) {
        ResponseContentLength(r, filesize);
        ResponseContentType(r, mediatype);
    }
    
    {
        const char * lastModifiedValue;
        DateToString(&filedate, &lastModifiedValue);
        if (lastModifiedValue) {
            ResponseAddField(r, "Last-Modified", lastModifiedValue);
            xmlrpc_strfree(lastModifiedValue);
        }
}

    ResponseWriteStart(r);

    if (r->requestInfo.method != m_head)
        sendBody(r, &file, filesize, mediatype, start, end, fileName);

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

    if (!RequestValidURIPath(sessionP)) {
        ResponseStatus(sessionP, 400);
        return TRUE;
    }

    /* Must check for * (asterisk uri) in the future */
    if (sessionP->requestInfo.method == m_options) {
        ResponseAddField(sessionP, "Allow", "GET, HEAD");
        ResponseContentLength(sessionP, 0);
        ResponseStatus(sessionP, 200);
        return TRUE;
    }

    if ((sessionP->requestInfo.method != m_get) &&
        (sessionP->requestInfo.method != m_head)) {
        ResponseAddField(sessionP, "Allow", "GET, HEAD");
        ResponseStatus(sessionP, 405);
        return TRUE;
    }

    strcpy(z, srvP->filespath);
    strcat(z, sessionP->requestInfo.uri);

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
            strcpy(z, sessionP->requestInfo.uri);
            p = z+strlen(z);
            *p = '/';
            *(p+1) = '\0';
            ResponseAddField(sessionP, "Location", z);
            ResponseStatus(sessionP, 302);
            ResponseWriteStart(sessionP);
            return TRUE;
        }

        *p = DIRECTORY_SEPARATOR[0];
        ++p;

        i = srvP->defaultfilenames.size;
        while (i-- > 0) {
            *p = '\0';        
            strcat(z, (srvP->defaultfilenames.item[i]));
            if (FileStat(z, &fs)) {
                if (!(fs.st_mode & S_IFDIR))
                    return ServerFileHandler(sessionP, z, fs.st_mtime,
                                             srvP->mimeTypeP);
            }
        }

        *(p-1) = '\0';
        
        if (!FileStat(z, &fs)) {
            ResponseStatusErrno(sessionP);
            return TRUE;
        }
        return ServerDirectoryHandler(sessionP, z, fs.st_mtime,
                                      srvP->mimeTypeP);
    } else
        return ServerFileHandler(sessionP, z, fs.st_mtime,
                                 srvP->mimeTypeP);
}



static void
initUnixStuff(struct _TServer * const srvP) {
#ifndef WIN32
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
initChanSwitchStuff(struct _TServer * const srvP,
                    abyss_bool        const noAccept,
                    TChanSwitch *         const userSwitchP,
                    uint16_t          const port,
                    const char **     const errorP) {
    
    if (userSwitchP) {
        *errorP = NULL;
        srvP->serverAcceptsConnections = TRUE;
        srvP->chanSwitchP = userSwitchP;
    } else if (noAccept) {
        *errorP = NULL;
        srvP->serverAcceptsConnections = FALSE;
        srvP->chanSwitchP = NULL;
    } else {
        *errorP = NULL;
        srvP->serverAcceptsConnections = TRUE;
        srvP->chanSwitchP = NULL;
        srvP->port = port;
    }
    srvP->weCreatedChanSwitch = FALSE;
}



static void
createServer(struct _TServer ** const srvPP,
             abyss_bool         const noAccept,
             TChanSwitch *          const userChanSwitchP,
             uint16_t           const portNumber,             
             const char **      const errorP) {

    struct _TServer * srvP;

    MALLOCVAR(srvP);

    if (srvP == NULL) {
        xmlrpc_asprintf(errorP,
                        "Unable to allocate space for server descriptor");
    } else {
        srvP->terminationRequested = false;

        initChanSwitchStuff(srvP, noAccept, userChanSwitchP, portNumber,
                            errorP);

        if (!*errorP) {
            srvP->defaulthandler = ServerDefaultHandlerFunc;

            srvP->name             = strdup("unnamed");
            srvP->filespath        = strdup(DEFAULT_DOCS);
            srvP->logfilename      = NULL;
            srvP->keepalivetimeout = 15;
            srvP->keepalivemaxconn = 30;
            srvP->timeout          = 15;
            srvP->advertise        = TRUE;
            srvP->mimeTypeP        = NULL;
            srvP->useSigchld       = FALSE;
            
            initUnixStuff(srvP);

            ListInitAutoFree(&srvP->handlers);
            ListInitAutoFree(&srvP->defaultfilenames);

            srvP->logfileisopen = FALSE;

            *errorP = NULL;
        }        
        if (*errorP)
            free(srvP);
    }
    *srvPP = srvP;
}



static void
setNamePathLog(TServer *    const serverP,
               const char * const name,
               const char * const filesPath,
               const char * const logFileName) {
/*----------------------------------------------------------------------------
   This odd function exists to help with backward compatibility.
   Today, we have the expandable model where you create a server with
   default parameters, then use ServerSet... functions to choose
   non-default parameters.  But before, you specified these three
   parameters right in the arguments of various create functions.
-----------------------------------------------------------------------------*/
    if (name)
        ServerSetName(serverP, name);
    if (filesPath)
        ServerSetFilesPath(serverP, filesPath);
    if (logFileName)
        ServerSetLogFileName(serverP, logFileName);
}



abyss_bool
ServerCreate(TServer *    const serverP,
             const char * const name,
             uint16_t     const portNumber,
             const char * const filesPath,
             const char * const logFileName) {

    abyss_bool const noAcceptFalse = FALSE;

    abyss_bool success;
    const char * error;

    createServer(&serverP->srvP, noAcceptFalse, NULL, portNumber, &error);

    if (error) {
        TraceMsg(error);
        xmlrpc_strfree(error);
        success = FALSE;
    } else {
        success = TRUE;
    
        setNamePathLog(serverP, name, filesPath, logFileName);
    }

    return success;
}



static void
createSwitchFromOsSocket(TOsSocket      const osSocket,
                         TChanSwitch ** const chanSwitchPP,
                         const char **  const errorP) {

#ifdef WIN32
    ChanSwitchWinCreateWinsock(osSocket, chanSwitchPP, errorP);
#else
    ChanSwitchUnixCreateFd(osSocket, chanSwitchPP, errorP);
#endif
}



static void
createChannelFromOsSocket(TOsSocket     const osSocket,
                          TChannel **   const channelPP,
                          void **       const channelInfoPP,
                          const char ** const errorP) {

#ifdef WIN32
    ChannelWinCreateWinsock(osSocket, channelPP,
                            channelInfoPP,
                            errorP);
#else
    ChannelUnixCreateFd(osSocket, channelPP,
                        (struct abyss_unix_chaninfo**)channelInfoPP,
                        errorP);
#endif
}



abyss_bool
ServerCreateSocket(TServer *    const serverP,
                   const char * const name,
                   TOsSocket    const socketFd,
                   const char * const filesPath,
                   const char * const logFileName) {

    abyss_bool success;
    TChanSwitch * chanSwitchP;
    const char * error;

    createSwitchFromOsSocket(socketFd, &chanSwitchP, &error);

    if (error) {
        success = FALSE;
        xmlrpc_strfree(error);
    } else {
        abyss_bool const noAcceptFalse = FALSE;

        const char * error;

        createServer(&serverP->srvP, noAcceptFalse, chanSwitchP, 0, &error);

        if (error) {
            TraceMsg(error);
            success = FALSE;
            xmlrpc_strfree(error);
        } else {
            success = TRUE;
            
            setNamePathLog(serverP, name, filesPath, logFileName);
        }
    }

    return success;
}



abyss_bool
ServerCreateNoAccept(TServer *    const serverP,
                     const char * const name,
                     const char * const filesPath,
                     const char * const logFileName) {

    abyss_bool const noAcceptTrue = TRUE;

    abyss_bool success;
    const char * error;

    createServer(&serverP->srvP, noAcceptTrue, NULL, 0, &error);

    if (error) {
        TraceMsg(error);
        success = FALSE;
        xmlrpc_strfree(error);
    } else {
        success = TRUE;
        
        setNamePathLog(serverP, name, filesPath, logFileName);
    }
    return success;
}



void
ServerCreateSwitch(TServer *     const serverP,
                   TChanSwitch *     const chanSwitchP,
                   const char ** const errorP) {
    
    abyss_bool const noAcceptFalse = FALSE;

    assert(serverP);
    assert(chanSwitchP);

    createServer(&serverP->srvP, noAcceptFalse, chanSwitchP, 0, errorP);
}



void
ServerCreateSocket2(TServer *     const serverP,
                    TSocket *     const socketP,
                    const char ** const errorP) {
    
    TChanSwitch * const chanSwitchP = SocketGetChanSwitch(socketP);

    assert(socketP);

    if (!chanSwitchP)
        xmlrpc_asprintf(
            errorP, "Socket is not a listening socket.  "
            "You should use ServerCreateSwitch() instead, anyway.");
    else
        ServerCreateSwitch(serverP, chanSwitchP, errorP);
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
                handlerP->term(handlerP->userdata);
        }
    }
}



void
ServerFree(TServer * const serverP) {

    struct _TServer * const srvP = serverP->srvP;

    if (srvP->weCreatedChanSwitch)
        ChanSwitchDestroy(srvP->chanSwitchP);

    xmlrpc_strfree(srvP->name);

    xmlrpc_strfree(srvP->filespath);
    
    ListFree(&srvP->defaultfilenames);

    terminateHandlers(&srvP->handlers);

    ListFree(&srvP->handlers);

    logClose(srvP);

    if (srvP->logfilename)
        xmlrpc_strfree(srvP->logfilename);

    free(srvP);
}



void
ServerSetName(TServer *    const serverP,
              const char * const name) {

    xmlrpc_strfree(serverP->srvP->name);
    
    serverP->srvP->name = strdup(name);
}



void
ServerSetFilesPath(TServer *    const serverP,
                   const char * const filesPath) {

    xmlrpc_strfree(serverP->srvP->filespath);
    
    serverP->srvP->filespath = strdup(filesPath);
}



void
ServerSetLogFileName(TServer *    const serverP,
                     const char * const logFileName) {
    
    struct _TServer * const srvP = serverP->srvP;

    if (srvP->logfilename)
        xmlrpc_strfree(srvP->logfilename);
    
    srvP->logfilename = strdup(logFileName);
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



void
ServerSetMimeType(TServer *  const serverP,
                  MIMEType * const MIMETypeP) {

    serverP->srvP->mimeTypeP = MIMETypeP;
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

    session.serverDeniesKeepalive = lastReqOnConn;
        
    RequestRead(&session);
    if (session.status == 0) {
        if (session.version.major >= 2)
            ResponseStatus(&session, 505);
        else if (!RequestValidURI(&session))
            ResponseStatus(&session, 400);
        else
            runUserHandler(&session, connectionP->server->srvP);
    }

    assert(session.status != 0);

    if (session.responseStarted)
        HTTPWriteEndChunk(&session);
    else
        ResponseError(&session);

    *keepAliveP = HTTPKeepalive(&session);

    SessionLog(&session);

    RequestFree(&session);
}


static TThreadProc serverFunc;

static void
serverFunc(void * const userHandle) {
/*----------------------------------------------------------------------------
   Do server stuff on one connection.  At its simplest, this means do
   one HTTP request.  But with keepalive, it can be many requests.
-----------------------------------------------------------------------------*/
    TConn *           const connectionP = userHandle;
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
            
            /**************** Must adjust the read buffer *****************/
            ConnReadInit(connectionP);
        }
    }
}



static void
createChanSwitch(struct _TServer * const srvP,
                 const char **     const errorP) {

    TChanSwitch * chanSwitchP;
    const char * error;
    
    /* Not valid to call this when channel switch already exists: */
    assert(srvP->chanSwitchP == NULL);

    ChanSwitchUnixCreate(srvP->port, &chanSwitchP, &error);
    
    if (error) {
        xmlrpc_asprintf(errorP,
                        "Can't create channel switch.  %s", error);
        xmlrpc_strfree(error);
    } else {
        *errorP = NULL;
        
        srvP->weCreatedChanSwitch = TRUE;
        srvP->chanSwitchP         = chanSwitchP;
    }
}



void
ServerInit(TServer * const serverP) {
/*----------------------------------------------------------------------------
   Initialize a server to accept connections.

   Do not confuse this with creating the server -- ServerCreate().

   Not necessary or valid with a server that doesn't accept connections (i.e.
   user supplies the channels (TCP connections)).
-----------------------------------------------------------------------------*/
    struct _TServer * const srvP = serverP->srvP;
    const char * retError;
    
    if (!srvP->serverAcceptsConnections)
        xmlrpc_asprintf(&retError,
                        "ServerInit() is not valid on a server that doesn't "
                        "accept connections "
                        "(i.e. created with ServerCreateNoAccept)");
    else {
        retError = NULL;  /* initial value */

        if (!srvP->chanSwitchP) {
            const char * error;
            createChanSwitch(srvP, &error);

            if (error) {
                xmlrpc_asprintf(&retError, "Unable to create a channel switch "
                                "for the server.  %s", error);
                xmlrpc_strfree(error);
            }
        }

        if (!retError) {
            const char * error;

            assert(srvP->chanSwitchP);

            ChanSwitchListen(srvP->chanSwitchP, MAX_CONN, &error);

            if (error) {
                xmlrpc_asprintf(&retError,
                                "Failed to listen on bound socket.  %s",
                                error);
                xmlrpc_strfree(error);
            }
        }
    }
    if (retError) {
        TraceMsg("ServerInit() failed.  %s", retError);
        exit(1);
        xmlrpc_strfree(retError);
    }
}



/* We don't do any locking on the outstanding connections list, so 
   we must make sure that only the master thread (the one that listens
   for connections) ever accesses it.

   That's why when a thread completes, it places the connection in
   "finished" status, but doesn't destroy the connection.
*/

typedef struct {

    TConn * firstP;
    unsigned int count;
        /* Redundant with 'firstP', for quick access */
} outstandingConnList;



static void
createOutstandingConnList(outstandingConnList ** const listPP) {

    outstandingConnList * listP;

    MALLOCVAR_NOFAIL(listP);

    listP->firstP = NULL;  /* empty list */
    listP->count = 0;

    *listPP = listP;
}



static void
destroyOutstandingConnList(outstandingConnList * const listP) {

    assert(listP->firstP == NULL);
    assert(listP->count == 0);

    free(listP);
}



static void
addToOutstandingConnList(outstandingConnList * const listP,
                         TConn *               const connectionP) {

    connectionP->nextOutstandingP = listP->firstP;

    listP->firstP = connectionP;

    ++listP->count;
}



static void
freeFinishedConns(outstandingConnList * const listP) {
/*----------------------------------------------------------------------------
   Garbage-collect the resources associated with connections that are
   finished with their jobs.  Thread resources, connection pool
   descriptor, etc.
-----------------------------------------------------------------------------*/
    TConn ** pp;

    pp = &listP->firstP;

    while (*pp) {
        TConn * const connectionP = (*pp);

        ThreadUpdateStatus(connectionP->threadP);
        
        if (connectionP->finished) {
            /* Take it out of the list */
            *pp = connectionP->nextOutstandingP;
            --listP->count;
            
            ConnWaitAndRelease(connectionP);
        } else {
            /* Move to next connection in list */
            pp = &connectionP->nextOutstandingP;
        }
    }
}



static void
waitForConnectionFreed(outstandingConnList * const outstandingConnListP
                       ATTR_UNUSED) {
/*----------------------------------------------------------------------------
  Wait for a connection descriptor in 'connectionPool' to be probably
  freed.
-----------------------------------------------------------------------------*/

    /* TODO: We should do something more sophisticated here.  For pthreads,
       we can have a thread signal us by semaphore when it terminates.
       For fork, we might be able to use the "done" handler argument
       to ConnCreate() to get interrupted when the death of a child
       signal happens.
    */

    xmlrpc_millisecond_sleep(2000);
}



static void
waitForNoConnections(outstandingConnList * const outstandingConnListP) {

    while (outstandingConnListP->firstP) {
        freeFinishedConns(outstandingConnListP);
    
        if (outstandingConnListP->firstP)
            waitForConnectionFreed(outstandingConnListP);
    }
}



static void
waitForConnectionCapacity(outstandingConnList * const outstandingConnListP) {
/*----------------------------------------------------------------------------
   Wait until there are fewer than the maximum allowed connections in
   progress.
-----------------------------------------------------------------------------*/
    /* We need to make this number configurable.  Note that MAX_CONN (16) is
       also the backlog limit on the TCP socket, and they really aren't
       related.  As it stands, we can have 16 connections in progress inside
       Abyss plus 16 waiting in the the channel switch.
    */

    while (outstandingConnListP->count >= MAX_CONN) {
        freeFinishedConns(outstandingConnListP);
        if (outstandingConnListP->firstP)
            waitForConnectionFreed(outstandingConnListP);
    }
}



#ifndef WIN32
void
ServerHandleSigchld(pid_t const pid) {

    ThreadHandleSigchld(pid);
}
#endif



void
ServerUseSigchld(TServer * const serverP) {

    struct _TServer * const srvP = serverP->srvP;
    
    srvP->useSigchld = TRUE;
}



static TThreadDoneFn destroyChannel;

static void
destroyChannel(void * const userHandle) {
/*----------------------------------------------------------------------------
   This is a "connection done" function for the connection the server
   serves.  It gets called some time after the connection has done its
   thing.  Its job is to clean up stuff the server created for use by
   the connection, but the server can't clean up because the
   connection might be processed asynchronously in a background
   thread.

   To wit, we destroy the connection's channel and release the memory
   for the information about that channel.
-----------------------------------------------------------------------------*/
    TConn * const connectionP = userHandle;

    ChannelDestroy(connectionP->channelP);
    free(connectionP->channelInfoP);
}



static void
acceptAndProcessNextConnection(
    TServer *             const serverP,
    outstandingConnList * const outstandingConnListP) {

    struct _TServer * const srvP = serverP->srvP;

    TConn * connectionP;
    const char * error;
    TChannel * channelP;
    void * channelInfoP;
        
    ChanSwitchAccept(srvP->chanSwitchP, &channelP, &channelInfoP, &error);
    
    if (error) {
        TraceMsg("Failed to accept the next connection from a client "
                 "at the channel level.  %s", error);
        xmlrpc_strfree(error);
    } else {
        if (channelP) {
            const char * error;

            freeFinishedConns(outstandingConnListP);
            
            waitForConnectionCapacity(outstandingConnListP);
            
            ConnCreate(&connectionP, serverP, channelP, channelInfoP,
                       &serverFunc, &destroyChannel, ABYSS_BACKGROUND,
                       srvP->useSigchld,
                       &error);
            if (!error) {
                addToOutstandingConnList(outstandingConnListP,
                                         connectionP);
                ConnProcess(connectionP);
                /* When connection is done (which could be later, courtesy
                   of a background thread), destroyChannel() will
                   destroy *channelP.
                */
            } else {
                xmlrpc_strfree(error);
                ChannelDestroy(channelP);
                free(channelInfoP);
            }
        } else {
            /* Accept function was interrupted before it got a connection */
        }
    }
}



static void 
serverRun2(TServer * const serverP) {

    struct _TServer * const srvP = serverP->srvP;
    outstandingConnList * outstandingConnListP;

    createOutstandingConnList(&outstandingConnListP);

    while (!srvP->terminationRequested)
        acceptAndProcessNextConnection(serverP, outstandingConnListP);

    waitForNoConnections(outstandingConnListP);
    
    destroyOutstandingConnList(outstandingConnListP);
}



void 
ServerRun(TServer * const serverP) {

    struct _TServer * const srvP = serverP->srvP;

    if (!srvP->chanSwitchP)
        TraceMsg("This server is not set up to accept connections "
                 "on its own, so you can't use ServerRun().  "
                 "Try ServerRunConn() or ServerInit()");
    else
        serverRun2(serverP);
}



static void
serverRunChannel(TServer *     const serverP,
                 TChannel *    const channelP,
                 void *        const channelInfoP,
                 const char ** const errorP) {
/*----------------------------------------------------------------------------
   Do the HTTP transaction on the channel 'channelP'.
   (channel must be at the beginning of the HTTP request -- nothing having
   been read or written yet).

   channelInfoP == NULL means no channel info supplied.
-----------------------------------------------------------------------------*/
    struct _TServer * const srvP = serverP->srvP;

    TConn * connectionP;
    const char * error;

    srvP->keepalivemaxconn = 1;

    ConnCreate(&connectionP, 
               serverP, channelP, channelInfoP,
               &serverFunc, NULL, ABYSS_FOREGROUND, srvP->useSigchld,
               &error);
    if (error) {
        xmlrpc_asprintf(errorP, "Couldn't create HTTP connection out of "
                        "channel (connected socket).  %s", error);
        xmlrpc_strfree(error);
    } else {
        *errorP = NULL;

        ConnProcess(connectionP);

        ConnWaitAndRelease(connectionP);
    }
}



void
ServerRunChannel(TServer *     const serverP,
                 TChannel *    const channelP,
                 void *        const channelInfoP,
                 const char ** const errorP) {
/*----------------------------------------------------------------------------
  Do the HTTP transaction on the channel 'channelP'.

  (channel must be at the beginning of the HTTP request -- nothing having
  been read or written yet).
-----------------------------------------------------------------------------*/
    struct _TServer * const srvP = serverP->srvP;

    if (srvP->serverAcceptsConnections)
        xmlrpc_asprintf(errorP,
                        "This server is configured to accept connections on "
                        "its own socket.  "
                        "Try ServerRun() or ServerCreateNoAccept().");
    else
        serverRunChannel(serverP, channelP, channelInfoP, errorP);
}



void
ServerRunConn2(TServer *     const serverP,
               TSocket *     const connectedSocketP,
               const char ** const errorP) {
/*----------------------------------------------------------------------------
   Do the HTTP transaction on the TCP connection on the socket
   *connectedSocketP.
   (socket must be connected state, with nothing having been read or
   written on the connection yet).
-----------------------------------------------------------------------------*/
    TChannel * const channelP = SocketGetChannel(connectedSocketP);

    if (!channelP)
        xmlrpc_asprintf(errorP, "The socket supplied is not a connected "
                        "socket.  You should use ServerRunChannel() instead, "
                        "anyway.");
    else
        ServerRunChannel(serverP,
                         channelP, SocketGetChannelInfo(connectedSocketP),
                         errorP);
}



void
ServerRunConn(TServer * const serverP,
              TOsSocket const connectedOsSocket) {

    TChannel * channelP;
    void * channelInfoP;
    const char * error;

    createChannelFromOsSocket(connectedOsSocket,
                              &channelP, &channelInfoP, &error);
    if (error) {
        TraceExit("Unable to use supplied socket");
        xmlrpc_strfree(error);
    } else {
        const char * error;

        ServerRunChannel(serverP, channelP, channelInfoP, &error);

        if (error) {
            TraceExit("Failed to run server on connection on file "
                      "descriptor %d.  %s", connectedOsSocket, error);
            xmlrpc_strfree(error);
        }
        ChannelDestroy(channelP);
        free(channelInfoP);
    }
}



void
ServerRunOnce(TServer * const serverP) {
/*----------------------------------------------------------------------------
   Accept a connection from the channel switch and do the HTTP
   transaction that comes over it.

   If no connection is presently waiting at the switch, wait for one.
   But return immediately if we receive a signal during the wait.
-----------------------------------------------------------------------------*/
    struct _TServer * const srvP = serverP->srvP;

    if (!srvP->chanSwitchP)
        TraceMsg("This server is not set up to accept connections "
                 "on its own, so you can't use ServerRunOnce().  "
                 "Try ServerRunChannel() or ServerInit()");
    else {
        const char * error;
        TChannel *   channelP;
        void *       channelInfoP;
    
        srvP->keepalivemaxconn = 1;

        ChanSwitchAccept(srvP->chanSwitchP, &channelP, &channelInfoP, &error);
        if (error) {
            TraceMsg("Failed to accept the next connection from a client "
                     "at the channel level.  %s", error);
            xmlrpc_strfree(error);
        } else {
            if (channelP) {
                const char * error;

                serverRunChannel(serverP, channelP, channelInfoP, &error);

                if (error) {
                    const char * peerDesc;
                    ChannelFormatPeerInfo(channelP, &peerDesc);
                    TraceExit("Got a connection from '%s', but failed to "
                              "run server on it.  %s", peerDesc, error);
                    xmlrpc_strfree(peerDesc);
                    xmlrpc_strfree(error);
                }
                ChannelDestroy(channelP);
                free(channelInfoP);
            }
        }
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

        if (*successP)
            *successP = ListAdd(&serverP->srvP->handlers, handlerP);

        if (!*successP)
            free(handlerP);
    }
}



static URIHandler2 *
createHandler(URIHandler const function) {

    URIHandler2 * handlerP;

    MALLOCVAR(handlerP);
    if (handlerP != NULL) {
        handlerP->init       = NULL;
        handlerP->term       = NULL;
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
            const char * const lbr = "\n";
            FileWrite(&srvP->logfile, msg, strlen(msg));
            FileWrite(&srvP->logfile, lbr, strlen(lbr));
        
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
