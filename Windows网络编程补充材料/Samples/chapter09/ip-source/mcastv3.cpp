//
// Sample: IP Source Multicasting (requires IGMPv3)
//
// Files:
//      mcastv3.cpp     - this file
//      resolve.cpp     - common routine for resolving host name and addresses
//      resolve.h       - header file for common routines
//
// Description:
//    This sample illustrates IP multicasting using source multicasting.
//    IP source multicasting is implemented on the wire via the IGMPv3 and
//    is currently supported only on Windows XP. This sample allows you to
//    specify one or more source addresses to include or exclude from the
//    multicast group. By default, the mode is to include the given sources.
//    If the -x paramter is specified then the list of sources is the list
//    of sources to exclude traffic from.
//
//    Remember that setting a IP multicast source filter only affects from
//    what sources data is accepted from. It does not prevent the app from
//    sending to that source.
//
// Compile:
//    cl -o mcastv3.exe mcastv3.cpp resolve.cpp ws2_32.lib
//
// Command Line Options/Parameters
//    mcastws1.exe [-s] [-m str] [-p int] [-i str] [-b str] [-l] [-n int] [...]
//       -b str    Local interface to bind to
//       -c        Connect to multicast address before sending?
//       -f        Use SIO_SET_MULTICAST_FILTER instead
//       -h str    Source (host) address (may be specified multiple times)
//       -i str    Local interface to use for the multicast join
//       -j        Don't join the multicast group (sender option)
//       -l 0/1    Disable the loopback 
//       -m str    Dotted decimal IP multicast address to join
//       -n int    Number of messages to send or receive
//       -p int    Port number to use
//       -s        Act as sender; otherwise receive data
//       -t        Set multicast TTL
//       -x        Switch to exclude (default is include)
//       -z        Size of send/recv buffer
//
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "resolve.h"

#include <stdio.h>
#include <stdlib.h>

#define MCASTADDRV4    "234.5.6.7"
#define MCASTPORT      "25000"
#define BUFSIZE        1024
#define DEFAULT_COUNT  500
#define DEFAULT_TTL    8

BOOL  bSender=FALSE,            // Act as sender?
      bConnect=FALSE,           // Connect before sending?
      bLoopBack=FALSE,          // Loopback parameter specified?
      bDontJoin=FALSE,          // Sender option - don't join the group before sending
      bUseFilter=FALSE;         // Use SIO_SET_MULTICAST_FILTER instead
int   gSocketType=SOCK_DGRAM,
      gProtocol=IPPROTO_UDP,
      gLoopBack=0,              // Disable loopback?
      gCount=DEFAULT_COUNT,     // Number of messages to send/receive
      gMode=MCAST_INCLUDE,      // Source mode: include or exclude
      gSourceCount=0,           // Number of sources specified
      gTtl=DEFAULT_TTL,         // Multicast ttl value
      gBufferSize=BUFSIZE;      // Size of send/recv buffer
char *gBindAddr=NULL,           // Interface to bind to
     *gInterface=NULL,          // Interface to join on
     *gMulticast=MCASTADDRV4,   // Multicast group to join
     *gPort=MCASTPORT,          // Port number to use
    **gSourceList=NULL;         // List of source IP addresses

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
    printf(" -b str String address to bind to\n");
    printf(" -c     Connect before sending?\n");
    printf(" -f     Use SIO_SET_MULTICAST_FILTER instead\n");
    printf(" -h str Source address\n");
    printf(" -i str Local interface to join groups\n");
    printf("          The default port is: %s\n", MCASTPORT);
    printf(" -j     Don't join the multicast group\n");
    printf(" -l 0/1 Turn on/off loopback\n");
    printf(" -m str Dotted decimal multicast IP addres to join\n");
    printf(" -n int Number of messages to send/receive\n");
    printf(" -p int Port number to use\n");
    printf(" -s     Act as server (send data); otherwise\n");
    printf("          receive data.\n");
    printf(" -t int Set multicast ttl value\n");
    printf(" -x     Switch to exclude mode (default mode is include)\n");
    printf(" -z int Size of send/recv buffer\n");
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
    int      hostcount=0,
             rc,
             i;

    // First count how many source IPs are given so we may allocat an array large
    //    enough to hold them
    for(i=1; i < argc ;i++)
    {
        if (tolower(argv[i][1]) == 'h')
            hostcount++;
    }
    gSourceList = (char **)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(char *) * hostcount);
    if (gSourceList == NULL)
    {
        fprintf(stderr, "ValidateArgs: HeapAlloc failed: %d\n", GetLastError());
        ExitProcess(-1);
    }
    // Now process the commands
    for(i=1; i < argc ;i++)
    {
        if ((argv[i][0] == '-') || (argv[i][0] == '/'))
        {
            switch (tolower(argv[i][1]))
            {
                case 'b':        // Address to bind to
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gBindAddr = argv[++i];
                    break;
                case 'c':        // Connect socket
                    bConnect = TRUE;
                    break;
                case 'f':        // Use SIO_SET_MULTICAST_FILTER instead
                    bUseFilter = TRUE;
                    break;
                case 'h':        // Source (host) address
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gSourceList[gSourceCount++] = argv[++i];
                    break;
                case 'i':        // local interface to use
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gInterface = argv[++i];
                    break;
                case 'j':       // Don't join multicast group
                    bDontJoin = TRUE;
                    break;
                case 'l':        // Disable loopback?
                    if (i+1 >= argc)
                        usage(argv[0]);
                    bLoopBack = TRUE;
                    gLoopBack = atoi(argv[++i]);
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
                case 'x':        // Switch mode to exclude
                    gMode = MCAST_EXCLUDE;
                    break;
                case 'z':        // Sizeo of send receive buffer
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
// Function: JoinMulticastGroup
//
// Description:
//    This routine joins the multicast group for the given sources. If the mode
//    is include (and we're not using SIO_SET_MULTICAST_FILTER) then 
//    IP_ADD_SOURCE_MEMBERSHIP is used on each source. If the mode is exclude
//    (and SIO_SET_MULTICAST_FILTER is not specified) then we join the group like
//    normal (using IP_ADD_MEMBERSHIP) and then we drop each of the source IPs
//    given using IP_DROP_MEMBERSHIP. On the other hand if SIO_SET_MULTICAST_FILTER
//    is specified (via the -f option), we simply use the ioctl in a one shot
//    does everything call.
//
int JoinMulticastGroup(SOCKET s, struct addrinfo *group, struct addrinfo *iface)
{
    struct addrinfo *ressrc=NULL;
    int              rc,
                     i;

    rc = NO_ERROR;

    if (bUseFilter == FALSE)
    {
        struct ip_mreq        mreqv4;
        struct ip_mreq_source mreqv4src;

        if (gMode == MCAST_INCLUDE)
        {
            // If the mode is include, call IP_ADD_SOURCE_MEMBERSHIP for each source.
            //    In this sample we only add sources but in a more dynamic environment
            //    you could remove sources that were previously added by calling
            //    IP_DROP_SOURCE_MEMBERSHIP.
            for(i=0; i < gSourceCount ;i++)
            {
                mreqv4src.imr_multiaddr = ((SOCKADDR_IN *)group->ai_addr)->sin_addr;
                mreqv4src.imr_interface = ((SOCKADDR_IN *)iface->ai_addr)->sin_addr;

                // Resolve the source IP
                ressrc = ResolveAddress(gSourceList[i], "0", group->ai_family, group->ai_socktype, group->ai_protocol);
                if (ressrc == NULL)
                {
                    fprintf(stderr, "JoinMulticastGroup: Unable to resolve address: %s\n",
                            gSourceList[i]);
                    continue;
                }

                mreqv4src.imr_sourceaddr = ((SOCKADDR_IN *)ressrc->ai_addr)->sin_addr;

                // Add the source membership
                rc = setsockopt(
                        s,
                        IPPROTO_IP,
                        IP_ADD_SOURCE_MEMBERSHIP,
                        (char *)&mreqv4src,
                        sizeof(mreqv4src)
                        );
                if (rc == SOCKET_ERROR)
                {
                    fprintf(stderr, "JoinMulticastGroup: setsockopt: IP_ADD_SOURCE_MEMBERSHIP failed: %d\n",
                            WSAGetLastError());
                }
                else
                {
                    printf("ADD SOURCE: ");
                    PrintAddress(ressrc->ai_addr, ressrc->ai_addrlen);
                    printf(" for GROUP: ");
                    PrintAddress(group->ai_addr, group->ai_addrlen);
                    printf(" on INTERFACE: ");
                    PrintAddress(iface->ai_addr, iface->ai_addrlen);
                    printf("\n");
                }

                freeaddrinfo(ressrc);
            }
        }
        else if (gMode == MCAST_EXCLUDE)
        {
            // For exclude mode, we first need to join the multicast group with
            //    IP_ADD_MEMBERSHIP (which sets the mode to EXCLUDE NONE). Afterwhich
            //    we drop those sources that we want to block from membership via the
            //    IP_BLOCK_SOURCE. This sample only drops sources but does not allow
            //    them back into the group. To add back a source that was previously
            //    dropped, you would call IP_UNBLOCK_SOURCE.
            mreqv4.imr_multiaddr = ((SOCKADDR_IN *)group->ai_addr)->sin_addr;
            mreqv4.imr_interface = ((SOCKADDR_IN *)iface->ai_addr)->sin_addr;

            // First join the group (state will be EXCLUDE NONE)
            rc = setsockopt(
                    s,
                    IPPROTO_IP,
                    IP_ADD_MEMBERSHIP,
                    (char *)&mreqv4,
                    sizeof(mreqv4)
                    );
            if (rc == SOCKET_ERROR)
            {
                fprintf(stderr, "JoinMulticastGroup: setsockopt: IP_ADD_MEMBERSHIP failed: %d\n",
                        WSAGetLastError());
                return SOCKET_ERROR;
            }
            else
            {
                printf("JOINED GROUP: ");
                PrintAddress(group->ai_addr, group->ai_addrlen);
                printf(" on INTERFACE: ");
                PrintAddress(iface->ai_addr, iface->ai_addrlen);
                printf("\n");
            }

            // Now drop each source from the group
            for(i=0; i < gSourceCount ;i++)
            {
                mreqv4src.imr_multiaddr = ((SOCKADDR_IN *)group->ai_addr)->sin_addr;
                mreqv4src.imr_interface = ((SOCKADDR_IN *)iface->ai_addr)->sin_addr;

                ressrc = ResolveAddress(gSourceList[i], "0", group->ai_family, group->ai_socktype, group->ai_protocol);
                if (ressrc == NULL)
                {
                    fprintf(stderr, "JoinMulticastGroup: Unable to resolve address: %s\n",
                            gSourceList[i]);
                    continue;
                }

                mreqv4src.imr_sourceaddr = ((SOCKADDR_IN *)ressrc->ai_addr)->sin_addr;

                rc = setsockopt(
                        s,
                        IPPROTO_IP,
                        IP_BLOCK_SOURCE,
                        (char *)&mreqv4src,
                        sizeof(mreqv4src)
                        );
                if (rc == SOCKET_ERROR)
                {
                    fprintf(stderr, "JoinMulticastGroup: setsockopt: IP_BLOCK_SOURCE failed: %d\n",
                            WSAGetLastError());
                }
                else
                {
                    printf("   DROPPED SOURCE: ");
                    PrintAddress(ressrc->ai_addr, ressrc->ai_addrlen);
                    printf("\n");
                }
                freeaddrinfo(ressrc);
            }
        }
    }
    else
    {
        struct ip_msfilter *filter=NULL;
        char               *filterbuf=NULL;
        int                 filterlen=0;
        DWORD               bytes;

        // Otherwise, use the SIO_SET_MULTICAST_FILTER to set all the sources
        //    with one call. First we have to build up a ip_msfilter structure
        //    which contains all the source's IP addresses.

        // calculate the size of the filter buffer necessary
        filterlen = sizeof(struct ip_msfilter) + ((gSourceCount-1) * sizeof(struct in_addr));

        filterbuf = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, filterlen);
        if (filterbuf == NULL)
        {
            fprintf(stderr, "JoinMulticastGroup: HeapAlloc failed: %d\n",
                    GetLastError());
            ExitProcess(-1);
        }

        filter = (struct ip_msfilter *)filterbuf;

        filter->imsf_multiaddr = ((SOCKADDR_IN *)group->ai_addr)->sin_addr;
        filter->imsf_interface = ((SOCKADDR_IN *)iface->ai_addr)->sin_addr;
        filter->imsf_fmode     = gMode;
        filter->imsf_numsrc    = gSourceCount;

        printf("SETTING MULTICAST FILTER STATE:\n");
        printf("   Multicast address: ");
        PrintAddress(group->ai_addr, group->ai_addrlen);
        printf("\n");
        printf("   Local interface  : ");
        PrintAddress(iface->ai_addr, iface->ai_addrlen);
        printf("\n");
        printf("   Mode is          : %s\n", ((gMode == MCAST_INCLUDE) ? "INCLUDE" : "EXCLUDE"));
        printf("   Source count     : %d\n", gSourceCount);

        // fill in each source address into the structure
        for(i=0; i < gSourceCount ;i++)
        {
            ressrc = ResolveAddress(gSourceList[i], "0", group->ai_family, group->ai_socktype, group->ai_protocol);
            if (ressrc == NULL)
            {
                fprintf(stderr, "JoinMulticastGroup: Unable to resolve source: %s\n",
                        gSourceList[i]);
                break;
            }
            else
            {
                printf("   Source [%d]   : ", i);
                PrintAddress(ressrc->ai_addr, ressrc->ai_addrlen);
                printf("\n");
            }

            filter->imsf_slist[i] = ((SOCKADDR_IN *)ressrc->ai_addr)->sin_addr;

            freeaddrinfo(ressrc);
        }

        // Set the multicast filter state with one call
        rc = WSAIoctl(
                s,
                SIO_SET_MULTICAST_FILTER,
                filterbuf,
                filterlen,
                NULL,
                0,
               &bytes,
                NULL,
                NULL
                );
        if (rc == SOCKET_ERROR)
        {
            fprintf(stderr, "JoinMulticastGroup: WSAIoctl: SIO_SET_MULTICAST_FILTER failed: %d\n",
                    WSAGetLastError());
            return SOCKET_ERROR;
        }
    }

    return NO_ERROR;
}

// 
// Function: GetMulticastState
// 
// Description:
//    Obtains the current multicat filter state and prints it to the console.
//
void GetMulticastState(SOCKET s, struct addrinfo *group, struct addrinfo *iface)
{
    struct ip_msfilter *filter=NULL;
    char                buf[15000];
    int                 buflen=15000,
                        rc,
                        i;

    filter = (struct ip_msfilter *)buf;

    filter->imsf_multiaddr = ((SOCKADDR_IN *)group->ai_addr)->sin_addr;
    filter->imsf_interface = ((SOCKADDR_IN *)iface->ai_addr)->sin_addr;

    rc = WSAIoctl(s, SIO_GET_MULTICAST_FILTER, buf, buflen, buf, buflen, (LPDWORD)&buflen, NULL, NULL);
    if (rc == SOCKET_ERROR)
    {
        fprintf(stderr, "GetMulticastState: WSAIoctl failed: %d\n", WSAGetLastError());
        return;
    }

    printf("imsf_multiaddr = %s\n", inet_ntoa(filter->imsf_multiaddr));
    printf("imsf_interface = %s\n", inet_ntoa(filter->imsf_interface));
    printf("imsf_fmode     = %s\n", (filter->imsf_fmode == MCAST_INCLUDE ? "MCAST_INCLUDE" : "MCAST_EXCLUDE"));
    printf("imsf_numsrc    = %d\n", filter->imsf_numsrc);
    for(i=0; i < (int)filter->imsf_numsrc ;i++)
    {
        printf("imsf_slist[%d]  = %s\n", i, inet_ntoa(filter->imsf_slist[i]));
    }
    return;
}

// 
// Function: SetSendInterface
//
// Description:
//    Set the send interface for the socket.
//
int SetSendInterface(SOCKET s, struct addrinfo *iface)
{
    char *optval=NULL;
    int   optlevel,
          option,
          optlen,
          rc;

    optlevel = IPPROTO_IP;
    option   = IP_MULTICAST_IF;
    optval   = (char *) &((SOCKADDR_IN *)iface->ai_addr)->sin_addr.s_addr;
    optlen   = sizeof(((SOCKADDR_IN *)iface->ai_addr)->sin_addr.s_addr);

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
    return rc;
}

//
// Function: SetMulticastTtl
//
// Description:
//    This routine sets the multicast TTL on the socket.
//
int SetMulticastTtl(SOCKET s, int af, int ttl)
{
    char *optval=NULL;
    int   optlevel,
          option,
          optlen,
          rc;

    optlevel = IPPROTO_IP;
    option   = IP_MULTICAST_TTL;
    optval   = (char *) &ttl;
    optlen   = sizeof(ttl);

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

    optlevel = IPPROTO_IP;
    option   = IP_MULTICAST_LOOP;
    optval   = (char *) &loopval;
    optlen   = sizeof(loopval);
 
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
    SOCKET              s;
    struct addrinfo    *resmulti=NULL,
                       *resbind=NULL,
                       *resif=NULL;
    char               *buf;
    int                 rc,
                        i=0;

    // Parse the command line
    ValidateArgs(argc, argv);

    if ((gMode == MCAST_INCLUDE) && (gSourceCount == 0) && (!bUseFilter))
    {
        printf("\nNo sources specified!\n\n"
               "At least one source must be specified when mode is INCLUDE\n"
               "   and not using the multicast filter option (-f)\n\n");
        usage(argv[0]);
    }

    // Load Winsock
    if (WSAStartup(MAKEWORD(1, 1), &wsd) != 0)
    {
        printf("WSAStartup failed\n");
        return -1;
    }

    // Resolve the multicast address
    resmulti = ResolveAddress(gMulticast, gPort, AF_UNSPEC, gSocketType, gProtocol);
    if (resmulti == NULL)
    {
        fprintf(stderr, "Unable to convert multicast address '%s': %d\n",
                gMulticast, WSAGetLastError());
        return -1;
    }
    if (resmulti->ai_family != AF_INET)
    {
        fprintf(stderr, "Source multicasting is only supported for IPv4\n");
        return -1;
    }

    // Resolve the binding address
    resbind = ResolveAddress(gBindAddr, (bSender ? "0" : gPort), resmulti->ai_family, resmulti->ai_socktype, resmulti->ai_protocol);
    if (resbind == NULL)
    {
        fprintf(stderr, "Unable to convert bind address '%s': %d\n",
                gBindAddr, WSAGetLastError());
        return -1;
    }
    if (resbind->ai_family != AF_INET)
    {
        fprintf(stderr, "Source multicasting is only supported for IPv4\n");
        return -1;
    }

    // Resolve the multicast interface
    resif   = ResolveAddress(gInterface, "0", resmulti->ai_family, resmulti->ai_socktype, resmulti->ai_protocol);
    if (resif == NULL)
    {
        fprintf(stderr, "Unable to convert interface address '%s': %d\n",
                gInterface, WSAGetLastError());
        return -1;
    }
    if (resif->ai_family != AF_INET)
    {
        fprintf(stderr, "Source multicasting is only supported for IPv4\n");
        return -1;
    }
    // 
    // Create the socket. In Winsock 1 you don't need any special
    // flags to indicate multicasting.
    //
    s = socket(resmulti->ai_family, resmulti->ai_socktype, resmulti->ai_protocol);
    if (s == INVALID_SOCKET)
    {
        printf("socket failed with: %d\n", WSAGetLastError());
        return -1;
    }
    printf("socket handle = 0x%p\n", s);
    //
    // Bind the socket to the local interface. This is done so we
    // can receive data
    //
    rc = bind(s, resbind->ai_addr, resbind->ai_addrlen);
    if (rc == SOCKET_ERROR)
    {
        printf("bind failed: %d\n", WSAGetLastError());
        return -1;
    }
    printf("Binding to ");
    PrintAddress(resbind->ai_addr, resbind->ai_addrlen);
    printf("\n");

    if (bDontJoin == FALSE)
    {
        rc = JoinMulticastGroup(s, resmulti, resif);
        if (rc == SOCKET_ERROR)
        {
            return -1;
        }
    }

    rc = SetSendInterface(s, resif);
    if (rc == SOCKET_ERROR)
    {
        return -1;
    }

    // Set the TTL to something else. The default TTL is one.
    rc = SetMulticastTtl(s, resmulti->ai_family, gTtl);
    if (rc == SOCKET_ERROR)
    {
        return -1;
    }

    // Disable the loopback if selected. Note that on NT4 and Win95
    // you cannot disable it.
    if (bLoopBack)
    {
        rc = SetMulticastLoopBack(s, resmulti->ai_family, gLoopBack);
        if (rc == SOCKET_ERROR)
        {
            return -1;
        }
    }

    //
    // When using sendto on an IPv6 multicast socket, the scope id needs
    // to be zero.
    //
    if ((bSender) && (resmulti->ai_family == AF_INET6))
        ((SOCKADDR_IN6 *)resmulti->ai_addr)->sin6_scope_id = 0;

    if (bConnect)
    {
        rc = connect(s, resmulti->ai_addr, resmulti->ai_addrlen);
        if (rc == SOCKET_ERROR)
        {
            printf("connect failed: %d\n", WSAGetLastError());
            return -1;
        }
    }

    GetMulticastState(s, resmulti, resif);

    buf = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, gBufferSize);
    if (buf == NULL)
    {
        fprintf(stderr, "HeapAlloc failed: %d\n", GetLastError());
        return -1;
    }

    if (!bSender)           // receiver
    {
        SOCKADDR_STORAGE safrom;
        int              fromlen;

        //
        for(i=0; i < gCount ;i++)
        {
            fromlen = sizeof(safrom);
            rc = recvfrom(
                    s, 
                    buf, 
                    gBufferSize, 
                    0,
                   (SOCKADDR *)&safrom, 
                   &fromlen
                   );
            if (rc == SOCKET_ERROR)
            {
                printf("recvfrom failed with: %d\n", 
                    WSAGetLastError());
                break;
            }

            printf("RECV %d bytes from <", rc);
            PrintAddress((SOCKADDR *)&safrom, fromlen);
            printf(">\n");
        }
    }
    else                    // sender
    {
        memset(buf, '%', gBufferSize);

        // Send some data
        for(i=0; i < gCount ; i++)
        {
            rc = sendto(
                    s, 
                    buf,
                    gBufferSize,
                    0,
                    resmulti->ai_addr,
                    resmulti->ai_addrlen
                    );
            if (rc == SOCKET_ERROR)
            {
                printf("sendto failed with: %d\n", WSAGetLastError());
                return -1;
            }

            printf("SENT %d bytes to ", rc);
            PrintAddress(resmulti->ai_addr, resmulti->ai_addrlen);
            printf("\n");

            Sleep(500);
        }
    }

    freeaddrinfo(resmulti);
    freeaddrinfo(resbind);
    freeaddrinfo(resif);

    HeapFree(GetProcessHeap(), 0, buf);
    
    closesocket(s);

    WSACleanup();
    return 0;
}
