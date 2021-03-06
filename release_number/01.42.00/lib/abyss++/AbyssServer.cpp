#include <cassert>
#include <string>
#include <stdexcept>
#if defined(__GNUC__) && __GNUC__ < 3
#include <iostream>
#else
#include <ostream>
#endif
#include <map>
#include <string.h>
#include <cstdlib>

using namespace std;

#include "xmlrpc-c/girerr.hpp"
using girerr::error;
using girerr::throwf;
#include "xmlrpc-c/string_int.h"
#include "xmlrpc-c/abyss.h"

#include "xmlrpc-c/AbyssServer.hpp"



namespace xmlrpc_c {

AbyssServer::Exception::Exception(unsigned short const  httpStatusCode,
                                  string         const& explanation) :

    runtime_error(explanation),
    _httpStatusCode(httpStatusCode)
{}



unsigned short
AbyssServer::Exception::httpStatusCode() const {
    return this->_httpStatusCode;
}



AbyssServer::ReqHandler::~ReqHandler() {}



size_t
AbyssServer::ReqHandler::handleReqStackSize() const {

    return 0;  // Means default
}



void
AbyssServer::ReqHandler::terminate() {}



}  // namespace

ostream&
operator<<(ostream                                     & out,
           xmlrpc_c::AbyssServer::Session::Method const& method) {

    string desc;

    typedef xmlrpc_c::AbyssServer::Session Session;

    switch (method) {
    case Session::METHOD_UNKNOWN: desc = "UNKNOWN"; break;
    case Session::METHOD_GET:     desc = "GET";     break;
    case Session::METHOD_PUT:     desc = "PUT";     break;
    case Session::METHOD_HEAD:    desc = "HEAD";    break;
    case Session::METHOD_POST:    desc = "POST";    break;
    case Session::METHOD_DELETE:  desc = "DELETE";  break;
    case Session::METHOD_TRACE:   desc = "TRACE";   break;
    case Session::METHOD_OPTIONS: desc = "OPTIONS"; break;
    }
    return out << desc;
}



namespace xmlrpc_c {

        
class AbyssServer::Session::Impl {

public:

    Impl(TSession * const cSessionP);

    size_t
    contentLength() const;

    void
    readSomeRequestBody(size_t          const max,
                        unsigned char * const buffer,
                        bool *          const eofP,
                        size_t *        const byteCtP);

    void
    readRequestBody(unsigned char * const buffer,
                    size_t          const size);

    string const
    body();

    void
    startWriteResponse();

    // The external class has lots of trivial methods that are just frontends
    // to the C object, so we make 'cSessionP' public to avoid all the work
    // of writing Impl methods for all of those.

    TSession * const cSessionP;

private:

    bool responseStarted;
        // We've started (and possibly finished) a response in this session.

    bool requestBodyDelivered;
        // We have delivered the request body to the object user.  (or tried
        // to and failed).

    size_t bodyReadCt;
        // Number of bytes of the request body the object user has read.
};



AbyssServer::Session::Impl::Impl(TSession* const cSessionP) :
    cSessionP(cSessionP)
{
    this->responseStarted = false;
    this->requestBodyDelivered = false;
    this->bodyReadCt = 0;
}



size_t
AbyssServer::Session::Impl::contentLength() const {

    try {
        const char * const contentLength = 
            RequestHeaderValue(this->cSessionP, "content-length");
        
        if (contentLength == NULL)
            throwf("Header is not present");
        else {
            if (contentLength[0] == '\0')
                throwf("The value is a null string");
            else {
                unsigned long contentLengthValue;
                char * tail;
        
                contentLengthValue = strtoul(contentLength, &tail, 10);
        
                if (*tail != '\0')
                    throwf("There's non-numeric crap in the value: '%s'",
                           tail);
                else if ((unsigned long)(size_t)contentLengthValue 
                         != contentLengthValue)
                    throwf("Value is too large; "
                           "we can't even do arithmetic on it: '%s'",
                           contentLength);
                else
                    return (size_t)contentLengthValue;
            }
        }
    } catch (exception const& e) {
        throw AbyssServer::Exception(
            400, string("Invalid content-length header field.  ") + e.what());
    }
}



size_t
AbyssServer::Session::contentLength() const {

    return this->implP->contentLength();
}



void
AbyssServer::Session::Impl::readSomeRequestBody(
    size_t          const max,
    unsigned char * const buffer,
    bool *          const eofP,
    size_t *        const byteCtP) {
/*-----------------------------------------------------------------------------
   Read some of the request body.  We read up to 'size' bytes, returning as
   soon as we have at least one byte.  If we know there is no more data
   coming, we send an indication of that instead.

   We know there is no more data coming in one of two ways: With chunked
   transfer encoding, we know when the client sends an end-of-data indication
   in the data stream.  Without chunked transfer encoding, we know when the
   this object has already transferred the full length indicated by the
   content-size header field, when there is one.  If the client doesn't supply
   content-size and does not use chunked transfer encoding, we never know the
   client isn't going to send any more data, so never return *eofP == true.

   If the client closes the TCP connection before sending anything, we
   throw an error.

   You can call this multiple times for one request, getting more of the
   body each time.

   If you call this method and 'body' on the same session, results are
   undefined.
-----------------------------------------------------------------------------*/
    if (RequestHeaderValue(this->cSessionP, "content-length")
        && this->contentLength() <= this->bodyReadCt)
        *eofP = true;
    else {
        const char * piecePtr;
        size_t pieceLen;
        abyss_bool eof;
        const char * error;

        SessionGetBody(this->cSessionP, max,
                       &eof, &piecePtr, &pieceLen, &error);

        if (error) {
            string const errorMsg(error);
            xmlrpc_strfree(error);
            throw girerr::error(errorMsg);
        }

        if (eof)
            *eofP = true;
        else {
            this->bodyReadCt += pieceLen;

            *eofP    = false;
            *byteCtP = pieceLen;

            memcpy(buffer, piecePtr, pieceLen);
        }
    }
}



void
AbyssServer::Session::readSomeRequestBody(
    size_t          const max,
    unsigned char * const buffer,
    bool *          const eofP,
    size_t *        const byteCtP) {

    this->implP->readSomeRequestBody(max, buffer, eofP, byteCtP);
}



void
AbyssServer::Session::Impl::readRequestBody(unsigned char * const buffer,
                                            size_t          const size) {
/*-----------------------------------------------------------------------------
   Read some of the request body.  We read 'size' bytes, waiting as long as
   necessary for the client to send that much.  If the client sends an
   end-of-data indication (possible with chunked transfer encoding only) or
   closes the TCP connection before sending that much, we throw an error.

   You can call this multiple times for one request, getting more of the
   body each time.

   If you call this method and 'body' on the same session, results are
   undefined.
-----------------------------------------------------------------------------*/
    for (size_t bytesXferredCt = 0; bytesXferredCt < size; ) {

        size_t pieceLen;

        try {
            bool eof;

            this->readSomeRequestBody(size - bytesXferredCt,
                                      &buffer[bytesXferredCt],
                                      &eof,
                                      &pieceLen);

            if (eof)
                throwf("Request body ended early (client sent the chunked "
                       "transfer end-of-data mark "
                       "or we exhausted content-length)");
        } catch (exception const& e) {
            throwf("Failed to get a piece of the request body.  %s",
                   e.what());
        }
        bytesXferredCt += pieceLen;
        assert(bytesXferredCt <= size);
    }
}



void
AbyssServer::Session::readRequestBody(unsigned char * const buffer,
                                      size_t          const size) {

    this->implP->readRequestBody(buffer, size);
}



string const
AbyssServer::Session::Impl::body() {
/*-----------------------------------------------------------------------------
   The body of the HTTP request (client may send a body with PUT or POST).

   We throw an error if there is no content-size header field.  That means
   we don't work with a chunked request.

   Some of the body may already be in Abyss's buffer.  We retrieve that before
   reading more, but then do the network I/O while we run, waiting as
   necessary for the body to arrive.

   This works only once.  If you call it a second time, it throws an error.

   If you call this method and 'readRequestBody' on the same session,
   results are undefined.
-----------------------------------------------------------------------------*/
    if (this->requestBodyDelivered)
        throwf("The request body has already been delivered; you cannot "
               "retrieve it twice");

    this->requestBodyDelivered = true;

    size_t const contentLength(this->contentLength());

    string body;
    abyss_bool eof;

    body.reserve(contentLength);

    for (eof = false; body.size() < contentLength && !eof; ) {
        const char * piecePtr;
        size_t pieceLen;
        const char * error;
        
        SessionGetBody(this->cSessionP, contentLength - body.size(), 
                       &eof, &piecePtr, &pieceLen, &error);

        if (error) {
            string const errorMsg(error);
            xmlrpc_strfree(error);
            throw girerr::error(errorMsg);
        }
        if (!eof)
            body.append(piecePtr, pieceLen);
    }
    return body;
}



string const
AbyssServer::Session::body() {

    return this->implP->body();
}



void
AbyssServer::Session::getHeaderField(string   const& fieldName,
                                     bool *   const  isPresentP,
                                     string * const  valueP) const {

    const char * const value = 
        RequestHeaderValue(this->implP->cSessionP, fieldName.c_str());
        
    if (value == NULL)
        *isPresentP = false;
    else {
        *isPresentP = true;

        *valueP = string(value);
    }
}



string const
AbyssServer::Session::headerFieldValue(string const& fieldName) const {

    bool isPresent;
    string value;

    this->getHeaderField(fieldName, &isPresent, &value);

    if (isPresent)
        return value;
    else
        throw AbyssServer::Exception(
            400, string("No '") + fieldName + "' field in header");
}



void
AbyssServer::Session::Impl::startWriteResponse() {

    // Note that ResponseWriteStart() assumes no response has been started; it
    // asserts that fact.

    if (this->responseStarted)
        throwf("Attempt to write multiple responses in same session");

    ResponseWriteStart(this->cSessionP);

    this->responseStarted = true;
}



void
AbyssServer::Session::startWriteResponse() {

    this->implP->startWriteResponse();
}



AbyssServer::Session::Session(TSession * const cSessionP) :
    implP(new Impl(cSessionP))
{}



AbyssServer::Session::~Session() {
    delete this->implP;
}



AbyssServer::Session::Method
AbyssServer::Session::method() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    switch (requestInfoP->method) {
    case m_unknown: return METHOD_UNKNOWN;
    case m_get:     return METHOD_GET;
    case m_put:     return METHOD_PUT;
    case m_head:    return METHOD_HEAD;
    case m_post:    return METHOD_POST;
    case m_delete:  return METHOD_DELETE;
    case m_trace:   return METHOD_TRACE;
    case m_options: return METHOD_OPTIONS;
    }
    assert(false);  // All values of TMethod are handled in switch.
}



string const
AbyssServer::Session::requestLine() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    assert(requestInfoP->requestline);

    return string(requestInfoP->requestline);
}



string const
AbyssServer::Session::uriPathName() const {
/*-----------------------------------------------------------------------------
   The path name from the URI for the HTTP request in this session.

   If the path name is empty (e.g. for URI http://example.com), this is a null
   string (in contrast to the Abyss C interface, in which it is "*").

   According to the URI RFC, RFC 3986, if the path name is
   "http://example.com/example.html", the path name is "/example.html" (note
   the leading slash), and that is what we return.
-----------------------------------------------------------------------------*/
    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    string const requestInfoUri(requestInfoP->uri);

    // There seems to be a weird design in which if the URI has a null
    // string path name, requestInfoP->uri is "*".  According to the RFC,
    // all URIs have a path name, and it may be a null string.

    return requestInfoUri == "*" ? "" : requestInfoUri;
}



vector<string> const
AbyssServer::Session::uriPathNameSegment() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    string const requestInfoUri(requestInfoP->uri);

    vector<string> retval;

    retval.reserve(10);  // A guess

    // There seems to be a weird design in which if the URI has an empty
    // path name, requestInfoP->uri is "*".  According to the RFC,
    // all URIs have a path name, and it may be empty.

    if (requestInfoUri == "*") {
        // empty path; no segments
    } else {
        string const pathName(requestInfoUri);

        if (pathName.length() < 1 || pathName[0] != '/')
            throwf("INTERNAL ERROR: SessionGetRequestInfo returned "
                   "'uri' field that does not begin with a slash: '%s'",
                   pathName.c_str());

        for (size_t p = 1; p < pathName.length(); ) {
            size_t const slashPos(pathName.find('/', p));

            size_t const segEnd(slashPos == string::npos ?
                                pathName.length() : slashPos);

            retval.push_back(pathName.substr(p, segEnd - p));

            p = slashPos == string::npos ? segEnd : slashPos + 1;
        }
    }
    return retval;
}



bool
AbyssServer::Session::uriHasQuery() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    return requestInfoP->query != NULL;
}



string const
AbyssServer::Session::uriQuery() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    if (requestInfoP->query)
        return string(requestInfoP->query);
    else
        throwf("Request URI has no query part");
}



static map<string, string> const
formInputFromQuery(string const& query) {
/*-----------------------------------------------------------------------------
   'query' interpreted as HTML form input.

   E.g. if 'query' is "employee=5&department=Q", we return a map with two
   elements:

     "employee"  => "5"
     "department => "Q"

-----------------------------------------------------------------------------*/
    map<string, string> retval;

    for (size_t pairBeg = 0; pairBeg < query.length(); ) {
        size_t pairEnd;

        pairEnd = query.find('&', pairBeg);
        if (pairEnd == string::npos)
            pairEnd = query.length();

        size_t keyEnd;

        keyEnd = query.find('=', pairBeg);

        if (keyEnd == string::npos || keyEnd >= pairEnd)
            throwf("form input key/value pair '%s' "
                   "does not contain an equal sign",
                   query.substr(pairBeg, pairEnd - pairBeg).c_str());

        size_t const keyBeg(pairBeg);
        size_t const valueBeg(keyEnd + 1);
        size_t const valueEnd(pairEnd);

        retval[query.substr(keyBeg, keyEnd - keyBeg)] =
            query.substr(valueBeg, valueEnd - valueBeg);

        assert(pairEnd >= pairBeg);

        pairBeg = pairEnd + 1;
    }

    return retval;
}



map<string, string> const
AbyssServer::Session::formInput() const {
/*-----------------------------------------------------------------------------
   The query portion of the URI for this session, interpreted as HTML form
   input.

   HTML form input takes the form of text key/value pairs.  Our result is a
   std::map that represents those key/value pairs in the obvious way.

   If the URI has no query part, throw an error.
-----------------------------------------------------------------------------*/
    return formInputFromQuery(this->uriQuery());
}



bool
AbyssServer::Session::hasHost() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    return requestInfoP->host != NULL;
}



string const
AbyssServer::Session::host() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    if (requestInfoP->host)
        return string(requestInfoP->host);
    else
        throwf("Request does not specify a host");
}



bool
AbyssServer::Session::hasFrom() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    return requestInfoP->from != NULL;
}



string const
AbyssServer::Session::from() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    if (requestInfoP->from)
        return string(requestInfoP->from);
    else
        throwf("Request header does not have a 'from' field");
}



bool
AbyssServer::Session::hasUseragent() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    return requestInfoP->useragent != NULL;
}



string const
AbyssServer::Session::useragent() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    if (requestInfoP->from)
        return string(requestInfoP->useragent);
    else
        throwf("Request header does not have a 'useragent' field");
}



bool
AbyssServer::Session::hasReferer() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    return requestInfoP->referer != NULL;
}



string const
AbyssServer::Session::referer() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    if (requestInfoP->referer)
        return string(requestInfoP->from);
    else
        throwf("Request header does not have a 'referer' field");
}



bool
AbyssServer::Session::userIsAuthenticated() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    return requestInfoP->user != NULL;
}



string const
AbyssServer::Session::user() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    if (requestInfoP->user)
        return string(requestInfoP->user);
    else
        throwf("Request header does not identify a user or "
               "server could not authenticate the identity");
}



unsigned short
AbyssServer::Session::port() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    return requestInfoP->port;
}



bool
AbyssServer::Session::keepalive() const {

    const TRequestInfo * requestInfoP;

    SessionGetRequestInfo(this->implP->cSessionP, &requestInfoP);

    return requestInfoP->keepalive;
}



bool
AbyssServer::Session::hasContentLength() const {

    return
        RequestHeaderValue(this->implP->cSessionP, "content-length")!= NULL;
}



void
AbyssServer::Session::setRespStatus(unsigned short const statusCode) {

    ResponseStatus(this->implP->cSessionP, statusCode);
}



void
AbyssServer::Session::setRespContentType(string const& contentType) {

    ResponseContentType(this->implP->cSessionP, contentType.c_str());
}



void
AbyssServer::Session::setRespContentLength(uint64_t const contentLength) {

    ResponseContentLength(this->implP->cSessionP, contentLength);
}



void
AbyssServer::Session::endWriteResponse() {

    ResponseWriteEnd(this->implP->cSessionP);
}



void
AbyssServer::Session::writeResponseBody(const unsigned char * const data,
                                        size_t                const size) {
/*-----------------------------------------------------------------------------
   Write some of the response body.  If it's a chunked response, this
   constitutes one chunk.  If not, it's just part of the monolithic response.
   Either way, you can call this multiple times for one response.
-----------------------------------------------------------------------------*/
    ResponseWriteBody(this->implP->cSessionP,
                      reinterpret_cast<const char *>(data),
                      size);
}



void
AbyssServer::Session::writeResponse(string const& body) {

    this->startWriteResponse();

    this->writeResponseBody(
        reinterpret_cast<const unsigned char *>(body.data()),
        body.size());

    this->endWriteResponse();
}



void
AbyssServer::Session::sendErrorResponse(string const& explanation) {
/*-----------------------------------------------------------------------------
  Send a response indicating that the HTTP request failed, with text
  explanation 'explanation'.  The response is HTML and contains an appropriate
  content-type header, regardless of any content type that may be set in the
  object already.

  Assume the HTTP status code is already set in the object.
-----------------------------------------------------------------------------*/
    ResponseError2(this->implP->cSessionP, explanation.c_str());
}



void
AbyssServer::Session::sendErrorResponse(Exception const& e) {
/*-----------------------------------------------------------------------------
  Send a response indicating that the HTTP request failed, with HTTP status
  code and text explanation 'e'.  The response is HTML and contains an
  appropriate content-type header, regardless of any content type that may be
  set in the object already.
-----------------------------------------------------------------------------*/
    this->setRespStatus(e.httpStatusCode());

    this->sendErrorResponse(e.what());
}



AbyssServer::AbyssServer(AbyssChanSwitch * const chanSwitchP) {

    const char * error;

    ServerCreateSwitch(&this->cServer,
                       chanSwitchP->cChanSwitchP(),
                       &error);

    if (error)
        throw runtime_error(error);
}



void
AbyssServer::init() {

    const char * error;

    ServerInit2(&this->cServer, &error);

    if (error)
        throw runtime_error(error);
}



void
AbyssServer::run() {

    ServerRun(&this->cServer);
}



void
AbyssServer::runOnce() {

    ServerRunOnce(&this->cServer);
}



AbyssServer::~AbyssServer() {

    ServerFree(&this->cServer);
}



static void
cHandleRequest(void *       const handlerPtr,
               TSession *   const cSessionP,
               abyss_bool * const handledP) {
/*-----------------------------------------------------------------------------
   This is an HTTP request handler function for the C abyss server.
   I.e. it translates a call from the C Abyss server to a call to the
   C++ HTTP request handler method.

   'handlerPtr' is the handle of an Abyss::ReqHandler object; we call that
   object's 'handleRequest' method.
-----------------------------------------------------------------------------*/
    AbyssServer::ReqHandler * const reqHandlerP(
        reinterpret_cast<AbyssServer::ReqHandler *>(handlerPtr));

    try {
        AbyssServer::Session session(cSessionP);

        bool handled;

        reqHandlerP->handleRequest(&session, &handled);

        *handledP = handled;
    } catch (...) {
        *handledP = false;
    }
}



static void
cTermHandler(void * const handlerPtr) {

    AbyssServer::ReqHandler * const reqHandlerP(
        reinterpret_cast<AbyssServer::ReqHandler *>(handlerPtr));

    // It's not supposed to throw an error, and there's nothing we could do
    // but abort the program if it does, so we don't catch exceptions.
    
    reqHandlerP->terminate();
}



void
AbyssServer::addRequestHandler(ReqHandler * const handlerP) {

    ServerReqHandler3 cReqHandler;

    cReqHandler.handleReq          = cHandleRequest;

    cReqHandler.userdata           = handlerP;

    cReqHandler.handleReqStackSize = handlerP->handleReqStackSize();

    cReqHandler.term               = cTermHandler;

    abyss_bool success;

    ServerAddHandler3(&this->cServer, &cReqHandler, &success);

    if (!success)
        throwf("ServerAddHandler3() failed");
}



}  // namespace
