// Sample: Reliable multicasting sample
//
// File:
//      rmcast.cpp      - this file
//      resolve.cpp     - common name resolution routines
//      resolve.h       - header file for common name resolution routines
//
// Purpose:
//    This sample illustrates the reliable multicast protocol. The client is
//    simple, create a reliable multicast socket, set the receive interfaces,
//    and wait for a session. After the session is joined, receive data on the
//    accepted session. For the server, the window size and other opttions may
//    be specified on the command line.
//
// Compile:
//    cl -o rmcast rmcast.c resolve.cpp ws2_32.lib
//
// Command Line Options/Parameters
//    rmcast.exe
//       -fb int   FEC block size
//       -fg int   FEC group size
//       -fo       Enable on-demand FEC
//       -fp int   Set FEC pro-active count
//       -i        Local interface
//                    Sender: This specifies the send interface
//                    Receiver: This is the listen interface - may be specified multiple times
//       -j  int   Late join percentage (sender only)
//       -m  str   Dotted decimal multicast IP addres to join
//       -n  int   Number of messages to send/receive
//       -p  int   Port number to use
//       -s        Act as server (send data); otherwise receive data.
//       -t  int   Set multicast TTL
//       -wb int   Set the send window size in bytes
//       -wr int   Set the send window rate in bytes/second
//       -ws int   Set the send window size in seconds
//       -z  int   Size of the send/recv buffer
//
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wsrm.h>
#include <windows.h>

#include "resolve.h"

#include <stdio.h>
#include <stdlib.h>

#define MCASTADDRV4    "234.5.6.7"
#define MCASTPORT      "25000"
#define BUFSIZE        1024
#define DEFAULT_COUNT  500
#define DEFAULT_TTL    8

#define MAX_LOCAL_INTERFACES    64

BOOL  bSender=FALSE,            // Act as sender?
      bUseFec=FALSE,            // Enable FEC
      bFecOnDemand=FALSE,       // Enable FEC on demand
      bSetSendWindow=FALSE;
int   gSocketType=SOCK_RDM,     // Reliable datagram
      gProtocol=IPPROTO_RM,     // Reliable multicast protocol
      gCount=DEFAULT_COUNT,     // Number of messages to send/receive
      gTtl=DEFAULT_TTL,         // Multicast TTL value
      gBufferSize=BUFSIZE;      // Buffer size for send/recv
int   gInterfaceCount=0;        // Number of local interfaces specified
char *gListenInterface[MAX_LOCAL_INTERFACES], // Set of listening interfaces
     *gMulticast=MCASTADDRV4,   // Multicast group to join
     *gPort=MCASTPORT;          // Port number to use

// Window size paramters
int   gWindowRateKbitsSec=0,
      gWindowSizeBytes=0,
      gWindowSizeMSec=0;

// FEC parameters
int   gFecBlockSize=8,
      gFecGroupSize=16,
      gFecProActive=8;

// Late join option
int   gLateJoin=-1;

//
// Function: usage
//
// Description:
//    Print usage information and exit.
//
void usage(char *progname)
{
    printf("usage: %s -s -m str -p int -i str -l -n int\n",
        progname);
    printf(" -fb int   FEC block size\n");
    printf(" -fg int   FEC group size\n");
    printf(" -fo       Enable on-demand FEC\n");
    printf(" -fp int   Set FEC pro-active count\n");
    printf(" -i        Local interface\n");
    printf("              Sender: This specifies the send interface\n");
    printf("              Receiver: This is the listen interface - may be specified multiple times\n");
    printf(" -j  int   Late join percentage (sender only)\n");
    printf(" -m  str   Dotted decimal multicast IP addres to join\n");
    printf(" -n  int   Number of messages to send/receive\n");
    printf(" -p  int   Port number to use\n");
    printf("              The default port is: %s\n", MCASTPORT);
    printf(" -s        Act as server (send data); otherwise\n");
    printf("              receive data.\n");
    printf(" -t  int   Set multicast TTL\n");
    printf(" -wb int   Set the send window size in bytes\n");
    printf(" -wr int   Set the send window rate in bytes/second\n");
    printf(" -ws int   Set the send window size in seconds\n");
    printf(" -z  int   Size of the send/recv buffer\n");
    ExitProcess(-1);
}

//
// Function: ValidateArgs
//
// Description
//    Parse the command line arguments and set some global flags
//    depeding on the values.
//
void ValidateArgs(int argc, char **argv)
{
    int      i, rc;

    for(i=1; i < argc ;i++)
    {
        if ((argv[i][0] == '-') || (argv[i][0] == '/'))
        {
            switch (tolower(argv[i][1]))
            {
                case 'f':        // Use fec
                    if (strlen(argv[i]) != 3)
                        usage(argv[0]);
                    bUseFec = TRUE;
                    switch (tolower(argv[i][2]))
                    {
                        case 'b':       // FEC block size
                            if (i+1 >= argc)
                                usage(argv[0]);
                            gFecBlockSize = atol(argv[++i]);
                            break;
                        case 'g':       // FEC group size
                            if (i+1 >= argc)
                                usage(argv[0]);
                            gFecGroupSize = atol(argv[++i]);
                            break;
                        case 'o':       // FEC on demand
                            bFecOnDemand = TRUE;
                            break;
                        case 'p':       // Pro active FEC count
                            if (i+1 >= argc)
                                usage(argv[0]);
                            gFecProActive = atol(argv[++i]);
                            break;
                        default:
                            usage(argv[0]);
                            break;
                    }
                    break;
                case 'i':        // local interface to use
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gListenInterface[gInterfaceCount++] = argv[++i];
                    break;
                case 'j':        // Late join value
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gLateJoin = atol(argv[++i]);
                    if (gLateJoin > SENDER_MAX_LATE_JOINER_PERCENTAGE)
                    {
                        gLateJoin = SENDER_MAX_LATE_JOINER_PERCENTAGE;
                        printf("Exceeded maximum late join value (%d%%)!\n", gLateJoin);
                        printf("   Setting value to maximum allowed\n");
                    }
                    break;
                case 'm':        // multicast group to join
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gMulticast = argv[++i];
                    break;
                case 'n':        // Number of messages to send/recv
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gCount = atoi(argv[++i]);
                    break;
                case 'p':        // Port number to use
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gPort = argv[++i];
                    break;
                case 's':        // sender
                    bSender = TRUE;
                    break;
                case 't':        // Multicast ttl
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gTtl = atoi(argv[++i]);
                    break;
                case 'w':        // Send window size
                    if ((i+1 >= argc) || (strlen(argv[i]) != 3))
                        usage(argv[0]);
                    bSetSendWindow = TRUE;
                    switch (tolower(argv[i][2]))
                    {
                        case 'b':           // Window size in bytes
                            gWindowSizeBytes = atol(argv[++i]);
                            break;
                        case 's':           // Window size in seconds
                            gWindowSizeMSec = atol(argv[++i]) * 1000;
                            break;
                        case 'r':           // Window size in bytes/sec
                            gWindowRateKbitsSec = (atol(argv[++i])/1000) * 8;
                            break;
                        default:
                            usage(argv[0]);
                            break;
                    }
                    break;
                case 'z':        // Buffer size for send/recv
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gBufferSize = atol(argv[++i]);
                    break;
                default:
                    usage(argv[0]);
                    break;
            }
        }
    }
    return;
}

//
// Function: SetSendInterface
//
// Description:
//    This routine sets the sending interface. This option may only be set
//    on senders.
//
int SetSendInterface(SOCKET s, struct addrinfo *iface)
{
    int   rc;

    // Set the send interface
    rc = setsockopt(
            s, 
            IPPROTO_RM, 
            RM_SET_SEND_IF,
            (char *)&((SOCKADDR_IN *)iface->ai_addr)->sin_addr.s_addr,
            sizeof(ULONG)
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
    return rc;
}

//
// Function: AddReceiveInterface
//
// Description:
//    This routine adds the given interface to the receiving interfac 
//    list. This option is valid only for receivers.
//
int AddReceiveInterface(SOCKET s, struct addrinfo *iface)
{
    int rc;

    rc = setsockopt(
            s,
            IPPROTO_RM,
            RM_ADD_RECEIVE_IF,
            (char *)&((SOCKADDR_IN *)iface->ai_addr)->sin_addr.s_addr,
            sizeof(ULONG)
            );
    if (rc == SOCKET_ERROR)
    {
        printf("setsockopt: RM_ADD_RECEIVE_IF failed: %d\n", WSAGetLastError());
    }
    else
    {
        printf("Adding receive interface: ");
        PrintAddress(iface->ai_addr, iface->ai_addrlen);
        printf("\n");
    }
    return rc;
}

//
// Function: SetMulticastTtl
//
// Description:
//    This routine sets the multicast TTL value for the socket.
//
int SetMulticastTtl(SOCKET s, int af, int ttl)
{
    int   rc;

    // Set the TTL value
    rc = setsockopt(
            s,
            IPPROTO_RM, 
            RM_SET_MCAST_TTL,
            (char *)&ttl, 
            sizeof(ttl)
            );
    if (rc == SOCKET_ERROR)
    {
        fprintf(stderr, "SetMulticastTtl: setsockopt failed: %d\n", WSAGetLastError());
    }
    else
    {
        printf("Set multicast ttl to: %d\n", ttl);
    }
    return rc;
}

//
// Function: SetFecParamters
//
// Description:
//    This routine sets the requested FEC parameters on a sender socket.
//    A client does not have to do anything special when the sender enables
//    or disables FEC.
//
int SetFecParameters(SOCKET s, int blocksize, int groupsize, int ondemand, int proactive)
{
    RM_FEC_INFO fec;
    int         rc;

    memset(&fec, 0, sizeof(fec));

    fec.FECBlockSize              = (USHORT) blocksize;
    fec.FECProActivePackets       = (USHORT) proactive;
    fec.FECGroupSize              = (UCHAR)  blocksize;
    fec.fFECOnDemandParityEnabled = (BOOLEAN)ondemand;

    rc = setsockopt(
            s,
            IPPROTO_RM,
            RM_USE_FEC,
            (char *)&fec,
            sizeof(fec)
            );
    if (rc == SOCKET_ERROR)
    {
        printf("Setting FEC parameters:\n");
        printf("   Block size: %d\n", blocksize);
        printf("   Pro active: %d\n", proactive);
        printf("   Group size: %d\n", groupsize);
        printf("   On demand : %s\n", (ondemand ? "TRUE" : "FALSE"));
    }
    else
    {
        fprintf(stderr, "setsockopt: RM_USE_FEC failed: %d\n", WSAGetLastError());
    }
    return rc;
}

//
// Function: SetWindowSize
//
// Description:
//     This routine sets the window size for the sending socket which includes
//     the byte rate, window size, and window time parameters. Before setting
//     the parameters a simple calculation is performed to determine whether
//     the values passed in make sense. If they don't an error message is 
//     displayed but the set is attempted anyway. If the values don't jive
//     then the option will fail with WSAEINVAL and the default window
//     rate, size, and time are used instead.
//
//
int SetWindowSize(SOCKET s, int windowsize, int windowtime, int windowrate)
{
    RM_SEND_WINDOW  window;
    int             rc;

    memset(&window, 0, sizeof(window));

    if (windowsize != ((windowrate/8) * windowtime))
    {
        printf("Window paramters don't compute!\n");
    }

    window.RateKbitsPerSec = windowrate;
    window.WindowSizeInMSecs = windowtime;
    window.WindowSizeInBytes = windowsize;

    rc = setsockopt(
            s,
            IPPROTO_RM,
            RM_RATE_WINDOW_SIZE,
            (char *)&window,
            sizeof(window)
            );
    if (rc == SOCKET_ERROR)
    {
        fprintf(stderr, "setsockopt: RM_RATE_WINDOW_SIZE failed: %d\n", WSAGetLastError());
    }
    else
    {
        printf("Setting window paramters:\n");
        printf("   Rate (kbits/sec): %d\n", windowrate);
        printf("   Size (bytes)    : %d\n", windowsize);
        printf("   Time (msec)     : %d\n", windowtime);
    }
    return rc;
}

//
// Function: SetLateJoin
//
// Description:
//    This option sets the latejoin value. This specifies the percentage of the
//    window that a receiver can NAK in the event the receiver picked up the 
//    session in the middle of the sender's transmission. This option is set
//    on the sender side and is advertised to the receivers when the session
//    is joined.
//
int SetLateJoin(SOCKET s, int latejoin)
{
    int     rc;

    rc = setsockopt(
            s,
            IPPROTO_RM,
            RM_LATEJOIN,
            (char *)&latejoin,
            sizeof(latejoin)
            );
    if (rc == SOCKET_ERROR)
    {
        fprintf(stderr, "setsockopt: RM_LATEJOIN failed: %d\n", WSAGetLastError());
    }
    else
    {
        printf("Setting latejoin: %d\n", latejoin);
    }
    return rc;
}

//
// Function: main
// 
// Description:
//    Parse the command line arguments, load the Winsock library, 
//    create a socket and join the multicast group. If set as a
//    sender then begin sending messages to the multicast group;
//    otherwise, call recvfrom() to read messages send to the 
//    group.
//    
int _cdecl main(int argc, char **argv)
{
    WSADATA             wsd;
    SOCKET              s,
                        sc;
    SOCKADDR_STORAGE    remote;
    struct addrinfo    *resmulti=NULL,
                       *resif=NULL,
                       *resbind=NULL;
    char               *buf=NULL;
    int                 remotelen,
                        err,
                        rc,
                        i=0;

    // Parse the command line
    ValidateArgs(argc, argv);

    // Load Winsock
    if (WSAStartup(MAKEWORD(1, 1), &wsd) != 0)
    {
        printf("WSAStartup failed\n");
        return -1;
    }

    // Resolve the multicast address
    resmulti = ResolveAddress(gMulticast, gPort, AF_INET, 0, 0);
    if (resmulti == NULL)
    {
        fprintf(stderr, "Unable to convert multicast address '%s': %d\n",
                gMulticast, WSAGetLastError());
        return -1;
    }

    // 
    // Create the socket. In Winsock 1 you don't need any special
    // flags to indicate multicasting.
    //
    s = socket(resmulti->ai_family, gSocketType, gProtocol);
    if (s == INVALID_SOCKET)
    {
        printf("socket failed with: %d\n", WSAGetLastError());
        return -1;
    }
    printf("socket handle = 0x%p\n", s);

    // Allocate the send/receive buffer
    buf = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, gBufferSize);
    if (buf == NULL)
    {
        fprintf(stderr, "HeapAlloc failed: %d\n", GetLastError());
        return -1;
    }

    if (bSender)
    {
        // Bind to the wildcard address
        resbind = ResolveAddress(NULL, gPort, AF_INET, 0, 0);
        if (resbind == NULL)
        {
            fprintf(stderr, "Unable to obtain bind address!\n");
            return -1;
        }

        // Bind the socket
        rc = bind(s, resbind->ai_addr, resbind->ai_addrlen);
        if (rc == SOCKET_ERROR)
        {
            fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
            return -1;
        }
        freeaddrinfo(resbind);

        // If sepcified, set the send interface
        if (gInterfaceCount == 1)
        {
            resif = ResolveAddress(gListenInterface[0], gPort, AF_INET, 0, 0);
            if (resif == NULL)
            {
                return -1;
            }
            rc = SetSendInterface(s, resif);

            freeaddrinfo(resif);

            // Set the TTL to something else. The default TTL is one.
            rc = SetMulticastTtl(s, resmulti->ai_family, gTtl);
            if (rc == SOCKET_ERROR)
            {
                return -1;
            }
        }
        
        // If specified set the late joiner option
        if (gLateJoin != -1)
        {
            SetLateJoin(s, gLateJoin);
        }

        // If specified set the window paramters
        if (bSetSendWindow)
        {
            SetWindowSize(s, gWindowSizeBytes, gWindowSizeMSec, gWindowRateKbitsSec);
        }

        // If specified set the FEC paramters
        if (bUseFec == TRUE)
        {
            SetFecParameters(s, gFecBlockSize, gFecGroupSize, bFecOnDemand, gFecProActive);
        }

        // Connect the socket to the multicast group the session is to be on
        rc = connect(s, resmulti->ai_addr, resmulti->ai_addrlen);
        if (rc == SOCKET_ERROR)
        {
            printf("connect failed: %d\n", WSAGetLastError());
            return -1;
        }

        memset(buf, '^', gBufferSize);

        // Send some data
        for(i=0; i < gCount ; i++)
        {
            rc = send(
                    s, 
                    buf, 
                    gBufferSize,
                    0
                    );
            if (rc == SOCKET_ERROR)
            {
                fprintf(stderr, "send failed with: %d\n", WSAGetLastError());
                return -1;
            }

            printf("SENT %d bytes\n", rc);
        }
    }
    else
    {
        // Bind the socket to the multicast address on which the session will take place
        rc = bind(s, resmulti->ai_addr, resmulti->ai_addrlen);
        if (rc == SOCKET_ERROR)
        {
            fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
            return -1;
        }
        printf("Binding to ");
        PrintAddress(resmulti->ai_addr, resmulti->ai_addrlen);
        printf("\n");

        // Add each supplied interface as a receive interface
        if (gInterfaceCount > 0)
        {
            for(i=0; i < gInterfaceCount ;i++)
            {
                resif = ResolveAddress(gListenInterface[i], "0", AF_INET, 0, 0);
                if (resif == NULL)
                {
                    return -1;
                }
                rc = AddReceiveInterface(s, resif);

                freeaddrinfo(resif);
            }
        }

        // Listen for sessions
        rc = listen(s, 1);
        if (rc == SOCKET_ERROR)
        {
            fprintf(stderr, "listen failed: %d\n", WSAGetLastError());
            return -1;
        }

        // Wait for a session to become available
        remotelen = sizeof(remote);
        sc = accept(s, (SOCKADDR *)&remote, &remotelen);
        if (sc == INVALID_SOCKET)
        {
            fprintf(stderr, "accept failed: %d\n", WSAGetLastError());
            return -1;
        }

        printf("Join multicast session from: ");
        PrintAddress((SOCKADDR *)&remote, remotelen);
        printf("\n");

        while (1)
        {
            // Receive data until an error or until the session is closed
            rc = recv(sc, buf, gBufferSize, 0);
            if (rc == SOCKET_ERROR)
            {
                if ((err = WSAGetLastError()) != WSAEDISCON)
                {
                    fprintf(stderr, "recv failed: %d\n", err);
                }
                break;
            }
            else 
            {
                printf("received %d bytes\n", rc);
            }
        }

        // Close the session socket
        closesocket(sc);
    }

    // Clean up
    freeaddrinfo(resmulti);

    closesocket(s);

    WSACleanup();
    return 0;
}
