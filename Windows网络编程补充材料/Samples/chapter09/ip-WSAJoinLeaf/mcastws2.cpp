//
// Sample: IPv4/v6 Multicasting using WSAJoinLeaf
//
// Files:
//      mcastws2.cpp    - this file
//      resolve.cpp     - common routines for name resolution
//      resolve.h       - header file for common routines
//
// Description:
//    This sample illustrates how to use the WSJoinLeaf API for 
//    multicasting. This sample may be invoked as either a sender
//    or a receiver. This sample works for both IPv4 and IPv6.
//    The one drawback to using the WSAJoinLeaf API is that only
//    a single multicast group may be joined on a socket.
//
// Compile:
//    cl -o mcastws2.exe mcastws2.c resolve.cpp ws2_32.lib
//
// Command Line:
//    mcastws2.exe [-s] [-m str] [-p int] [-i str] [-b str] [-l] [-n int]
//       -i addr Local address to bind to
//       -j      Don't join group
//       -l 0/1  Disable/enable loopback
//       -m addr Multicast address to join
//       -n int  Send/recv count
//       -p int  Port number
//       -s      Act as sender; default is receiver
//       -t int  Set multicast TTL
//       -z int  Buffer size (in bytes)
//
#include <winsock2.h>
#include <ws2tcpip.h>

#include "resolve.h"

#include <stdio.h>
#include <stdlib.h>

#define MCASTADDRV4    "234.5.6.7"
#define MCASTADDRV6    "ff12::1"
#define MCASTPORT      "25000"
#define BUFSIZE        1024
#define DEFAULT_COUNT  500
#define DEFAULT_TTL    8

BOOL  bSender=FALSE,            // Act as as sender, default is receiver
      bLoopBack=FALSE,          // Enable or disable loopback
      bDontJoin=FALSE;          // Join the group (sender only)
int   gSocketType=SOCK_DGRAM,   // datagram
      gProtocol=IPPROTO_UDP,    // UDP
      gCount=DEFAULT_COUNT,     // Number of messages to send/recv
      gBufferSize=BUFSIZE,      // Buffer size for send/recv
      gLoopBack=0,              // Enable or disable loopback
      gTtl=DEFAULT_TTL;         // Set multicast TTL
char *gInterface=NULL,          // Local address to bind to
     *gMulticast=MCASTADDRV4,   // Multicast group to join
     *gPort=MCASTPORT;          // Port number to use

//
// Function: usage
//
// Description:
//    Display usage information.
//
void usage(char *progname)
{
    fprintf(stderr, "usage: %s [-s] [-i local-addr] [-m remote-addr] [-p port-num] [-n count]\n", progname);
    fprintf(stderr, 
                    "  -i addr Local address to bind to\n"
                    "  -l 0/1  Disable/enable loopback\n"
                    "  -m addr Multicast address to join\n"
                    "  -n int  Send/recv count\n"
                    "  -p int  Port number\n"
                    "  -s      Act as sender; default is receiver\n"
                    "  -t int  Set multicast TTL\n"
                    "  -z int  Buffer size (in bytes)\n"
                    );
    ExitProcess(-1);
}

//
// Function: ValidateArgs
//
// Description:
//    Parse the command line and setup the global variables which indicate
//    how this app should run.
//
void ValidateArgs(int argc, char **argv)
{
    int     i;

    for(i=1; i < argc ;i++)
    {
        if (((argv[i][0] != '/') && (argv[i][0] != '-')) || (strlen(argv[i]) < 2))
            usage(argv[0]);
        switch(tolower(argv[i][1]))
        {
            case 'i':               // local address to bind to
                if (i+1 >= argc)
                    usage(argv[0]);
                gInterface = argv[++i];
                break;
            case 'l':               // enable/disable loopback
                if (i+1 >= argc)
                    usage(argv[0]);
                bLoopBack = TRUE;
                gLoopBack = atoi(argv[++i]);
                break;
            case 'm':               // remote (multicast) address to connect/join to
                if (i+1 >= argc)
                    usage(argv[0]);
                gMulticast = argv[++i];
                break;
            case 'p':               // port number
                if (i+1 >= argc)
                    usage(argv[0]);
                gPort = argv[++i];
                break;
            case 'n':               // number of sends/recvs
                if (i+1 >= argc)
                    usage(argv[0]);
                gCount = atoi(argv[++i]);
                break;
            case 's':               // sender or receiver
                bSender = TRUE;
                break;
            case 't':               // multicast ttl
                if (i+1 >= argc)
                    usage(argv[0]);
                gTtl = atoi(argv[++i]);
                break;
            case 'z':               // buffer size
                if (i+1 >= argc)
                    usage(argv[0]);
                gBufferSize = atoi(argv[++i]);
                break;
            default:
                usage(argv[0]);
                break;
        }
    }
}

//
// Function: SetMulticastTtl
//
// Description:
//    This routine sets the multicast TTL value for the socket.
//
int SetMulticastTtl(SOCKET s, int af, int ttl)
{
    char *optval=NULL;
    int   optlevel,
          option,
          optlen,
          rc;

    rc = NO_ERROR;
    if (af == AF_INET)
    {
        // Set the options for V4
        optlevel = IPPROTO_IP;
        option   = IP_MULTICAST_TTL;
        optval   = (char *) &ttl;
        optlen   = sizeof(ttl);
    }
    else if (af == AF_INET6)
    {
        // Set the options for V6
        optlevel = IPPROTO_IPV6;
        option   = IPV6_MULTICAST_HOPS;
        optval   = (char *) &ttl;
        optlen   = sizeof(ttl);
    }
    else
    {
        fprintf(stderr, "Attemtping to set TTL for invalid address family!\n");
        rc = SOCKET_ERROR;
    }
    if (rc != SOCKET_ERROR)
    {
        // Set the TTL value
        rc = setsockopt(
                s,
                optlevel,
                option,
                optval,
                optlen
                );
        if (rc == SOCKET_ERROR)
        {
            fprintf(stderr, "SetMulticastTtl: setsockopt failed: %d\n", WSAGetLastError());
        }
        else
        {
            printf("Set multicast ttl to: %d\n", ttl);
        }
    }
    return rc;
}

//
// Function: SetMulticastLoopBack
//
// Description:
//    This function enabled or disables multicast loopback. If loopback is enabled
//    (and the socket is a member of the destination multicast group) then the
//    data will be placed in the receive queue for the socket such that if a
//    receive is posted on the socket its own data will be read. For this sample
//    it doesn't really matter as if invoked as the sender, no data is read.
//
int SetMulticastLoopBack(SOCKET s, int af, int loopval)
{
    char *optval=NULL;
    int   optlevel,
          option,
          optlen,
          rc;

    rc = NO_ERROR;
    if (af == AF_INET)
    {
        // Set the v4 options
        optlevel = IPPROTO_IP;
        option   = IP_MULTICAST_LOOP;
        optval   = (char *) &loopval;
        optlen   = sizeof(loopval);
    }
    else if (af == AF_INET6)
    {
        // Set the v6 options
        optlevel = IPPROTO_IPV6;
        option   = IPV6_MULTICAST_LOOP;
        optval   = (char *) &loopval;
        optlen   = sizeof(loopval);
    }
    else
    {
        fprintf(stderr, "Attemtping to set multicast loopback for invalid address family!\n");
        rc = SOCKET_ERROR;
    }
    if (rc != SOCKET_ERROR)
    {
        // Set the multpoint loopback
        rc = setsockopt(
                s,
                optlevel,
                option,
                optval,
                optlen
                );
        if (rc == SOCKET_ERROR)
        {
            fprintf(stderr, "SetMulticastLoopBack: setsockopt failed: %d\n", WSAGetLastError());
        }
        else
        {
            printf("Setting multicast loopback to: %d\n", loopval);
        }
    }
    return rc;
}

//
// Function: SetSendInterface
//
// Description:
//    This routine sets the send (outgoing) interface of the socket.
//    Again, for v4 the IP address is used to specify the interface while
//    for v6 its the scope-ID.
//
int SetSendInterface(SOCKET s, struct addrinfo *iface)
{
    char *optval=NULL;
    int   optlevel,
    option,
    optlen,
    rc;

    rc = NO_ERROR;
    if (iface->ai_family == AF_INET)
    {
        // Setup the v4 option values
        optlevel = IPPROTO_IP;
        option   = IP_MULTICAST_IF;
        optval   = (char *) &((SOCKADDR_IN *)iface->ai_addr)->sin_addr.s_addr;
        optlen   = sizeof(((SOCKADDR_IN *)iface->ai_addr)->sin_addr.s_addr);
    }
    else if (iface->ai_family == AF_INET6)
    {
        // Setup the v6 option values
        optlevel = IPPROTO_IPV6;
        option   = IPV6_MULTICAST_IF;
        optval   = (char *) &((SOCKADDR_IN6 *)iface->ai_addr)->sin6_scope_id;
        optlen   = sizeof(((SOCKADDR_IN6 *)iface->ai_addr)->sin6_scope_id);
    }
    else
    {
        fprintf(stderr, "Attemtping to set sent interface for invalid address family!\n");
        rc = SOCKET_ERROR;
    }
    // Set send IF
    if (rc != SOCKET_ERROR)
    {
        // Set the send interface
        rc = setsockopt(
                s,
                optlevel,
                option,
                optval,
                optlen
                );
        if (rc == SOCKET_ERROR)
        {
            printf("setsockopt failed: %d\n", WSAGetLastError());
        }
        else
        {
            printf("Set sending interface to: ");
            PrintAddress(iface->ai_addr, iface->ai_addrlen);
            printf("\n");
        }
    }
    return rc;
}


int __cdecl main(int argc, char **argv)
{
    WSADATA             wsd;
    struct addrinfo    *reslocal = NULL,
                       *resmulti = NULL;
    SOCKET              s, 
                        rs;
    char               *buf=NULL;
    int                 rc,
                        i;

    ValidateArgs(argc, argv);

    WSAStartup(MAKEWORD(2,2), &wsd);

    //
    // resolve the local interface first
    //
    reslocal = ResolveAddress(gInterface, (bSender ? "0": gPort), AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP);
    if (reslocal == NULL)
    {
        fprintf(stderr, "Unable to resolve local interface: %s\n", gInterface);
        return -1;
    }

    // Create the socket - remember to specify the multicast flags
    s = WSASocket(
            reslocal->ai_family, 
            reslocal->ai_socktype, 
            reslocal->ai_protocol, 
            NULL, 
            0, 
            WSA_FLAG_OVERLAPPED | WSA_FLAG_MULTIPOINT_C_LEAF | WSA_FLAG_MULTIPOINT_D_LEAF
            );
    if (s == INVALID_SOCKET)
    {
        fprintf(stderr, "socket(af = %d) failed: %d\n", reslocal->ai_family, WSAGetLastError());
        return -1;
    }
        
    rc = bind(s, reslocal->ai_addr, reslocal->ai_addrlen);
    if (rc == SOCKET_ERROR)
    {
        fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
        return -1;
    }

    printf("bound to: ");
    PrintAddress(reslocal->ai_addr, reslocal->ai_addrlen);
    printf("\n");
    
    // Resolve the multicast address
    resmulti = ResolveAddress(gMulticast, gPort, reslocal->ai_family, reslocal->ai_socktype, reslocal->ai_protocol);
    if (resmulti == NULL)
    {
        fprintf(stderr, "Unable to resolve multicast address: %s\n", gMulticast);
        freeaddrinfo(reslocal);
        freeaddrinfo(resmulti);
        return -1;
    }

    // Set the multicast TTL
    rc = SetMulticastTtl(s, resmulti->ai_family, gTtl);
    if (rc == SOCKET_ERROR)
    {
        freeaddrinfo(reslocal);
        freeaddrinfo(resmulti);
        return -1;
    }

    // Set the loopback value if indicated
    if (bLoopBack)
    {
        rc = SetMulticastLoopBack(s, resmulti->ai_family, gLoopBack);
        if (rc == SOCKET_ERROR)
        {
            freeaddrinfo(reslocal);
            freeaddrinfo(resmulti);
            return -1;
        }
    }

    // If the don't join option is set then just connect the socket
    if (bDontJoin == FALSE)
    {
        rs = WSAJoinLeaf(
                s, 
                resmulti->ai_addr, 
                resmulti->ai_addrlen, 
                NULL, 
                NULL, 
                NULL, 
                NULL, 
                (bSender ? JL_SENDER_ONLY : JL_RECEIVER_ONLY)
                        );
        if (rs == INVALID_SOCKET)
        {
            fprintf(stderr, "WSAJoinLeaf failed: %d\n", WSAGetLastError());
            freeaddrinfo(reslocal);
            freeaddrinfo(resmulti);
            return -1;
        }
    }
    else
    {
        rc = SetSendInterface(s, reslocal);
        if (rc == SOCKET_ERROR)
        {
            fprintf(stderr, "Unable to set outgoing multicast interface\n");
            freeaddrinfo(reslocal);
            freeaddrinfo(resmulti);
            return -1;
        }

        rc = connect(
                s,
                resmulti->ai_addr,
                resmulti->ai_addrlen
                );
        if (rc == SOCKET_ERROR)
        {
            fprintf(stderr, "conect failed: %d\n", WSAGetLastError());
            freeaddrinfo(reslocal);
            freeaddrinfo(resmulti);
            return -1;
        }
    }

    printf("joining group: ");
    PrintAddress(resmulti->ai_addr, resmulti->ai_addrlen);
    printf("\n");

    freeaddrinfo(reslocal);
    freeaddrinfo(resmulti);

    buf = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, gBufferSize);
    if (buf == NULL)
    {
        fprintf(stderr, "HeapAlloc failed: %d\n", GetLastError());
        return -1;
    }
    memset(buf, '%', gBufferSize);

    if (bSender)
    {
        for(i=0; i < gCount ;i++)
        {
            rc = send(s, buf, gBufferSize, 0);
            if (rc == SOCKET_ERROR)
            {
                fprintf(stderr, "send failed: %d\n", WSAGetLastError());
                HeapFree(GetProcessHeap(), 0, buf);
                return -1;
            }
            else
            {
                printf("wrote %d bytes\n", rc);
            }
        }
    }
    else
    {
        for(i=0; i < gCount ;i++)
        {
            rc = recv(s, buf, gBufferSize, 0);
            if (rc == SOCKET_ERROR)
            {
                fprintf(stderr, "recv failed: %d\n", rc);
                HeapFree(GetProcessHeap(), 0, buf);
                return -1;
            }
            else
            {
                printf("read %d bytes\n", rc);
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, buf);

    closesocket(s);
    
    WSACleanup();
    return 0;
}
