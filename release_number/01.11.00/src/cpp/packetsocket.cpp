/*============================================================================
                         packetsocket
==============================================================================

  This is a facility for communicating socket-style, with defined
  packets like a datagram socket but with reliable delivery like a
  stream socket.  It's like a POSIX "sequential packet" socket, except
  it is built on top of a stream socket, so it is usable on the many
  systems that have stream sockets but not sequential packet sockets.

  By Bryan Henderson 2007.05.12

  Contributed to the public domain by its author.
============================================================================*/


/*============================================================================
  The protocol for carrying packets on a character stream:

  The protocol consists of the XML text to be transported with a bare
  minimum of framing information added:

     An ASCII Escape (<ESC> == 0x1B) character marks the start of a
     4-ASCII-character control word.  These are defined:

       <ESC>PKT : marks the end of a packet.
       <ESC>ESC : represents an <ESC> character in the packet

     Any other bytes after <ESC> is a protocol error.  End of
     stream anywhere but after <ESC>PKT or beginning of stream is
     a protocol error.  

     All bytes not part of a control word are literal bytes of a packet.

  You can create a pstream transport from any file descriptor from which
  you can read and write a bidirectional character stream.  Typically,
  it's a TCP socket.
============================================================================*/

#define _BSD_SOURCE        // gets uint defined

#include <string>
#include <queue>
#include <iostream>
#include <sstream>
#include <cstdio>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>

#include "c_util.h"
#include "xmlrpc-c/string_int.h"
#include "xmlrpc-c/girerr.hpp"
using girerr::throwf;

#include "xmlrpc-c/packetsocket.hpp"


#define ESC 0x1B   //  ASCII Escape character
#define ESC_STR "\x1B"

namespace xmlrpc_c {


packet::packet() :
    bytes(NULL), length(0), allocSize(0) {}
 


void
packet::initialize(const unsigned char * const data,
                   size_t                const dataLength) {

    this->bytes = reinterpret_cast<unsigned char *>(malloc(dataLength));

    if (this->bytes == NULL)
        throwf("Can't get storage for a %u-byte packet.", dataLength);

    this->allocSize = dataLength;

    memcpy(this->bytes, data, dataLength);

    this->length = dataLength;
}



packet::packet(const unsigned char * const data,
               size_t                const dataLength) {

    this->initialize(data, dataLength);
}



packet::packet(const char * const data,
               size_t       const dataLength) {

    this->initialize(reinterpret_cast<const unsigned char *>(data),
                     dataLength);
}



packet::~packet() {

    if (this->bytes)
        free(bytes);
}



void
packet::addData(const unsigned char * const data,
                size_t                const dataLength) {
/*----------------------------------------------------------------------------
   Add the 'length' bytes at 'data' to the packet.

   We allocate whatever additional memory is needed to fit the new
   data in.
-----------------------------------------------------------------------------*/
    size_t const neededSize(this->length + dataLength);

    if (this->allocSize < neededSize)
        this->bytes = reinterpret_cast<unsigned char *>(
            realloc(this->bytes, neededSize));

    if (this->bytes == NULL)
        throwf("Can't get storage for a %u-byte packet.", neededSize);

    memcpy(this->bytes + this->length, data, dataLength);

    this->length += dataLength;
}



packetPtr::packetPtr() {
    // Base class constructor will construct pointer that points to nothing
}



packetPtr::packetPtr(packet * const packetP) : autoObjectPtr(packetP) {}



packet *
packetPtr::operator->() const {

    girmem::autoObject * const p(this->objectP);
    return dynamic_cast<packet *>(p);
}



packetSocket::packetSocket(int const sockFd) {

    int dupRc;

    dupRc = dup(sockFd);
    
    if (dupRc < 0)
        throwf("dup() failed.  errno=%d (%s)", errno, strerror(errno));
    else {
        this->sockFd = dupRc;

        this->inEscapeSeq = false;

        this->escAccum.len = 0;
        
        this->packetAccumP = packetPtr(new packet);

        fcntl(this->sockFd, F_SETFL, O_NONBLOCK);

        this->eof = false;
    }
}



packetSocket::~packetSocket() {

    close(this->sockFd);
}



static void
writeFd(int                   const fd,
        const unsigned char * const data,
        size_t                const size) {

    size_t totalBytesWritten;

    totalBytesWritten = 0;

    while (totalBytesWritten < size) {
        ssize_t rc;

        rc = write(fd, data, size);

        if (rc < 0)
            throwf("write() of socket failed with errno %d (%s)",
                   errno, strerror(errno));
        else if (rc == 0)
            throwf("Zero byte short write.");
        else {
            size_t const bytesWritten(rc);
            totalBytesWritten += bytesWritten;
        }
    }
}



void
packetSocket::writeWait(packetPtr const& packetP) const {

    const unsigned char * const packetMarker(
        reinterpret_cast<const unsigned char *>(ESC_STR "PKT"));

    writeFd(this->sockFd, packetP->getBytes(), packetP->getLength());

    writeFd(this->sockFd, packetMarker, 4);
}



static ssize_t
libc_read(int    const fd,
          void * const buf,
          size_t const count) {

    return read(fd, buf, count);
}



void
packetSocket::bufferFinishedPacket() {
/*----------------------------------------------------------------------------
   Assume the packet currently being accumulated (in *this->packetAccumP)
   is complete and move it to the packet buffer (this->readBuffer).
-----------------------------------------------------------------------------*/
    this->readBuffer.push(this->packetAccumP);

    this->packetAccumP = packetPtr(new packet);
}



void
packetSocket::takeSomeEscapeSeq(const unsigned char * const buffer,
                                size_t                const length,
                                size_t *              const bytesTakenP) {

    size_t bytesTaken;

    bytesTaken = 0;

    while (this->escAccum.len < 3 && bytesTaken < length)
        this->escAccum.bytes[this->escAccum.len++] = buffer[bytesTaken++];

    assert(this->escAccum.len <= 3);

    if (this->escAccum.len == 3) {
        if (xmlrpc_memeq(this->escAccum.bytes, "PKT", 3)) {
            bufferFinishedPacket();
        } else if (xmlrpc_memeq(this->escAccum.bytes, "ESC", 3)) {
            this->packetAccumP->addData((const unsigned char *)ESC_STR, 1);
        } else
            throwf("Invalid escape sequence 0x%02x%02x%02x read from "
                   "stream socket under packet socket",
                   this->escAccum.bytes[0],
                   this->escAccum.bytes[1],
                   this->escAccum.bytes[2]);
        
        this->inEscapeSeq = false;
        this->escAccum.len = 0;
    }
    *bytesTakenP = bytesTaken;
}



void
packetSocket::takeSomePacket(const unsigned char * const buffer,
                             size_t                const length,
                             size_t *              const bytesTakenP) {

    assert(!this->inEscapeSeq);

    const unsigned char * const escPos(
        (const unsigned char *)memchr(buffer, ESC, length));

    if (escPos) {
        size_t const escOffset(escPos - &buffer[0]);
        // move everything before the escape sequence into the
        // packet accumulator.
        this->packetAccumP->addData(buffer, escOffset);

        this->inEscapeSeq = true;

        // Caller can pick up from here; we don't know nothin' 'bout
        // no escape sequences.

        // +1 below reflects the fact that we're taking the ESC character
        // too.
        *bytesTakenP = escOffset + 1;
    } else {
        // No complete packet yet and no substitution to do;
        // just throw the whole thing into the accumulator.
        this->packetAccumP->addData(buffer, length);
        *bytesTakenP = length;
    }
}



void
packetSocket::verifyNothingAccumulated() {
/*----------------------------------------------------------------------------
   Throw an error if there is a partial packet accumulated.
-----------------------------------------------------------------------------*/
    if (this->inEscapeSeq)
        throwf("Streams socket closed in the middle of an "
               "escape sequence");
    
    if (this->packetAccumP->getLength() > 0)
        throwf("Stream socket closed in the middle of a packet "
               "(%u bytes of packet received; no PKT marker to mark "
               "end of packet)", this->packetAccumP->getLength());
}



void
packetSocket::readFromFile() {
/*----------------------------------------------------------------------------
   Read some data from the underlying stream socket.  Read as much as is
   available right now, up to 4K.  Update 'this' to reflect the data read.

   E.g. if we read an entire packet, we add it to the packet buffer
   (this->readBuffer).  If we read the first part of a packet, we add
   it to the packet accumulator (*this->packetAccumP).  If we read the end
   of a packet, we add the full packet to the packet buffer and empty
   the packet accumulator.  Etc.
-----------------------------------------------------------------------------*/
    bool wouldblock;

    wouldblock = false;

    while (this->readBuffer.empty() && !this->eof && !wouldblock) {
        unsigned char buffer[4096];
        ssize_t rc;

        rc = libc_read(this->sockFd, buffer, sizeof(buffer));

        if (rc < 0) {
            if (errno == EWOULDBLOCK)
                wouldblock = true;
            else
                throwf("read() of socket failed with errno %d (%s)",
                       errno, strerror(errno));
        } else {
            size_t const bytesRead(rc);

            if (bytesRead == 0) {
                this->eof = true;
                verifyNothingAccumulated();
            } else {
                uint cursor;  // Cursor into buffer[]
                cursor = 0;
                while (cursor < bytesRead) {
                    size_t bytesTaken;

                    if (this->inEscapeSeq)
                        this->takeSomeEscapeSeq(&buffer[cursor],
                                                bytesRead - cursor,
                                                &bytesTaken);
                    else
                        this->takeSomePacket(&buffer[cursor],
                                             bytesRead - cursor,
                                             &bytesTaken);
                    cursor += bytesTaken;
                }
            }
        }
    }
}



void
packetSocket::read(bool *      const eofP,
                   bool *      const gotPacketP,
                   packetPtr * const packetPP) {
/*----------------------------------------------------------------------------
   Read one packet from the socket, through the internal packet buffer.

   If there is a packet immediately available, return it as *packetPP and
   return *gotPacketP true.  Otherwise, return *gotPacketP false.

   Iff the socket has no more data coming (it is shut down) and there
   is no complete line in the line buffer, return *eofP.

   This leaves one other possibility: there is no full packet immediately
   available, but there may be in the future because the socket is still
   alive.  In that case, we return *eofP == false and *gotPacketP == false.

   Any packet we return belongs to caller; Caller must delete it.
-----------------------------------------------------------------------------*/
    // Move any packets now waiting to be read in the underlying stream
    // socket into our packet buffer (this->readBuffer).

    this->readFromFile();

    if (this->readBuffer.empty()) {
        *gotPacketP = false;
        *eofP = this->eof;
    } else {
        *gotPacketP = true;
        *eofP = false;
        *packetPP = this->readBuffer.front();
        readBuffer.pop();
    }
}



void
packetSocket::readWait(bool *      const eofP,
                       packetPtr * const packetPP) {

    bool gotPacket;
    bool eof;

    gotPacket = false;
    eof = false;

    while (!gotPacket && !eof) {
        struct pollfd pollfds[1];

        pollfds[0].fd = this->sockFd;
        pollfds[0].events = POLLIN;

        poll(pollfds, ARRAY_SIZE(pollfds), -1);

        this->read(&eof, &gotPacket, packetPP);
    }
    *eofP = eof;
}



} // namespace
