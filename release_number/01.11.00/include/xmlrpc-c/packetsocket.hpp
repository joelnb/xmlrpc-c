#ifndef PACKETSOCKET_HPP_INCLUDED
#define PACKETSOCKET_HPP_INCLUDED

/*============================================================================
                         packetsocket
==============================================================================
  This is a facility for communicating socket-style, with defined
  packets like a datagram socket but with reliable delivery like a
  stream socket.  It's like a POSIX "sequential packet" socket, except
  it is built on top of a stream socket, so it is usable on the many
  systems that have stream sockets but not sequential packet sockets.
============================================================================*/

#include <sys/types.h>
#include <string>
#include <queue>

#include <xmlrpc-c/girmem.hpp>

namespace xmlrpc_c {

class packet : public girmem::autoObject {

public:
    packet();

    packet::packet(const unsigned char * const data,
                   size_t                const dataLength);

    packet::packet(const char * const data,
                   size_t       const dataLength);

    ~packet();

    unsigned char *
    packet::getBytes() const { return this->bytes; }

    size_t
    packet::getLength() const { return this->length; }

    void
    packet::addData(const unsigned char * const data,
                    size_t                const dataLength);

private:
    unsigned char * bytes;  // malloc'ed
    size_t length;
    size_t allocSize;

    void
    initialize(const unsigned char * const data,
               size_t                const dataLength);
};



class packetPtr: public girmem::autoObjectPtr {

public:
    packetPtr();

    explicit packetPtr::packetPtr(packet * const packetP);

    packet *
    operator->() const;
};



class packetSocket {
/*----------------------------------------------------------------------------
   This is an Internet communication vehicle that transmits individual
   variable-length packets of text.

   It is based on a TCP socket.

   It would be much better to use a kernel SOCK_SEQPACKET socket, but
   Linux 2.4 does not have them.
-----------------------------------------------------------------------------*/
public:
    packetSocket(int sockFd);

    ~packetSocket();

    void
    writeWait(packetPtr const& packetPtr) const;

    void
    read(bool *      const eofP,
         bool *      const gotPacketP,
         packetPtr * const packetPP);

    void
    readWait(bool *      const eofP,
             packetPtr * const packetPP);

private:
    int sockFd;
        // The kernel stream socket we use.
    bool eof;
        // The packet socket is at end-of-file for reads.
        // 'readBuffer' is empty and there won't be any more data to fill
        // it because the underlying stream socket is closed.
    std::queue<packetPtr> readBuffer;
    packetPtr packetAccumP;
        // The packet we're currently accumulating; it will join
        // 'readBuffer' when we've received the whole packet (and we've
        // seen the PKT escape sequence so we know we've received it all).
    bool inEscapeSeq;
        // In our trek through the data read from the underlying stream
        // socket, we are after an ESC character and before the end of the
        // escape sequence.  'escAccum' shows what of the escape sequence
        // we've seen so far.
    struct {
        unsigned char bytes[3];
        size_t len;
    } escAccum;

    void
    packetSocket::bufferFinishedPacket();

    void
    packetSocket::takeSomeEscapeSeq(const unsigned char * const buffer,
                                    size_t                const length,
                                    size_t *              const bytesTakenP);

    void
    packetSocket::takeSomePacket(const unsigned char * const buffer,
                                 size_t                const length,
                                 size_t *              const bytesTakenP);

    void
    packetSocket::verifyNothingAccumulated();

    void
    packetSocket::readFromFile();

};



} // namespace

#endif
