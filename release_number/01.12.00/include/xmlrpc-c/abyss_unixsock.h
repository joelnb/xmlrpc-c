/* This is just a sub-file for abyss.h */

#include <sys/socket.h>

struct abyss_unix_chaninfo {
    size_t peerAddrLen;
    struct sockaddr peerAddr;
};

void
ChanSwitchUnixCreate(uint16_t       const portNumber,
                     TChanSwitch ** const chanSwitchPP,
                     const char **  const errorP);

void
ChanSwitchUnixCreateFd(int            const fd,
                       TChanSwitch ** const chanSwitchPP,
                       const char **  const errorP);

void
ChannelUnixCreateFd(int                           const fd,
                    TChannel **                   const channelPP,
                    struct abyss_unix_chaninfo ** const channelInfoPP,
                    const char **                 const errorP);

void
ChannelUnixGetPeerName(TChannel *         const channelP,
                       struct sockaddr ** const sockaddrPP,
                       size_t  *          const sockaddrLenP,
                       const char **      const errorP);

void
SocketUnixCreateFd(int        const fd,
                   TSocket ** const socketPP);

typedef int TOsSocket;
    /* TOsSocket is the type of a conventional socket offered by our OS.
       This is for backward compatibility; everyone should use TChanSwitch
       and TChannel instead today.
    */


