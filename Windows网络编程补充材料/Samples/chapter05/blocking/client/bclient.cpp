//
// Sample: Blocking IPv4/IPv6 Client
//
// Files:
//      bclient.cpp     - this file
//      resolve.cpp     - routines for resovling addresses, etc.
//      resolve.h       - header file for resolve.c
//
// Description:
//      This sample illustrates simple blocking IO for TCP and UDP for
//      both IPv4 and IPv6. This sample uses the getaddrinfo/getnameinfo
//      APIs which allows this application to be IP agnostic. That is the
//      desired address family (AF_INET or AF_INET6) can be determined
//      simply from the string address passed via the -l and -n command.
//
//      For both UDP and TCP, two threads are created: one for sending data
//      and the other for receiving the echoed data. After the send thread
//      sends the specified amount of data it either exits (UDP) or does
//      a shutdown(SD_SEND) (TCP) and exits the thread. The receive thread
//      receives data until the connection is closed or an error occurs.
//
//      For UDP, three zero byte sends are performed after sending the
//      requested data amount. The server will echo these back as well
//      which will indicate to the send thread to exit. Note that UDP
//      is inherently unreliable and therefore if invoked for UDP, the
//      client may not receive all the data send and may hang (as the
//      zero byte sends echoed back could be dropped).
//
//      For example:
//          If this sample is called with the following command lines:
//              bclient.exe -n fe80::2efe:1234 -e 5150
//          Then the client creates an IPv6 socket and attempts to connect
//          to the server at the address given by the -n parameter.
//
//          On the other hand, with the following command line:
//              bclient.exe -n 7.7.7.1 -e 5150
//          Then the server creates an IPv4 socket and attempts to connect
//          to the server specified by the -n parameter.
//
//          Specifying the hostname of the server will attempt to connect
//          to the server via all the addresses returned by DNS (which
//          could be IPv4, IPv6 or both):
//              bclient.exe -n server-name -e 5150
//
// Compile:
//      cl -o bclient.exe bclient.cpp resolve.cpp ws2_32.lib
//
// Usage:
//      bclient.exe [options]
//          -a 4|6     Address family, 4 = IPv4, 6 = IPv6 [default = IPv4]\n"
//          -e port    Port number [default = 5150]\n"
//          -l addr    Local address to bind to [default INADDR_ANY for IPv4 or INADDR6_ANY for IPv6
//          -n addr    Remote address to connect/send to
//          -p proto   Which protocol to use [default = TCP]
//             tcp        Use TCP
//             udp        Use UDP
//          -c         UDP: connect and send (opposed to sendto)
//          -b size    Buffer size (in bytes)
//          -x count   Number of sends to perform
//
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>

#include "resolve.h"

#define DEFAULT_BUFFER_SIZE     4096
#define DEFAULT_SEND_COUNT      100

int gAddressFamily = AF_UNSPEC,             // Address family to use
    gSocketType    = SOCK_STREAM,           // Default to TCP
    gProtocol      = IPPROTO_TCP,
    gBufferSize    = DEFAULT_BUFFER_SIZE,   // Default buffer size for sends
    gSendCount     = DEFAULT_SEND_COUNT;    // Default number of sends to perform

char *gBindAddr    = NULL,                  // Address to bind to locally
     *gServerAddr  = NULL,                  // Server name/address
     *gBindPort    = "5150";                // Port to connect to (on server side)

BOOL  bUdpConnect  = FALSE;                 // Connect the UDP socket before sending?

struct addrinfo *gConnectedEndpoint=NULL;   // Addresses that server name/address resolved to

// Statistics variables
volatile  LONG gBytesRead=0,
               gBytesSent=0,
               gStartTime=0;

//
// Function: usage
//
// Description:
//      Prints usage information and exits the process.
//
void usage(char *progname)
{
    fprintf(stderr, "usage: %s [-a 4|6] [-e port] [-l local-addr] [-n addr] [-p udp|tcp]\n",
            progname);
    fprintf(stderr, 
            "  -a 4|6     Address family, 4 = IPv4, 6 = IPv6 [default = IPv4]\n"
            "  -e port    Port number [default = 5150]\n"
            "  -l addr    Local address to bind to [default INADDR_ANY for IPv4 or INADDR6_ANY for IPv6]\n"
            "  -n addr    Remote address to connect/send to\n"
            "  -p tcp|udp Which protocol to use [default = TCP]\n"
            "  -c         UDP: connect and send (opposed to sendto)\n"
            "  -b size    Buffer size\n"
            "  -x count   Number of sends to perform\n"
           );
    ExitProcess(-1);
}

//
// Function: ValidateArgs
//
// Description:
//      Parses the command line arguments and sets up some global 
//      variables.
//
void ValidateArgs(int argc, char **argv)
{
    int     i;

    for(i=1; i < argc ;i++)
    {
        if (((argv[i][0] != '/') && (argv[i][0] != '-')) || (strlen(argv[i]) < 2))
            usage(argv[0]);
        else
        {
            switch (tolower(argv[i][1]))
            {
                case 'a':               // address family - IPv4 or IPv6
                    if (i+1 >= argc)
                        usage(argv[0]);
                    if (argv[i+1][0] == '4')
                        gAddressFamily = AF_INET;
                    else if (argv[i+1][0] == '6')
                        gAddressFamily = AF_INET6;
                    else
                        usage(argv[0]);
                    i++;
                    break;
                case 'b':               // buffer size
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gBufferSize = atol(argv[++i]);
                    break;
                case 'c':
                    bUdpConnect = TRUE;
                    break;
                case 'e':               // endpoint - port number
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gBindPort = argv[++i];
                    break;
                case 'l':               // local address for binding
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gBindAddr = argv[++i];
                    break;
                case 'n':               // address to connect/send to
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gServerAddr = argv[++i];
                    break;
                case 'p':               // protocol - TCP or UDP
                    if (i+1 >= argc)
                        usage(argv[0]);
                    if (_strnicmp(argv[i+1], "tcp", 3) == 0)
                    {
                        gProtocol = IPPROTO_TCP;
                        gSocketType = SOCK_STREAM;
                    }
                    else if (_strnicmp(argv[i+1], "udp", 3) == 0)
                    {
                        gProtocol = IPPROTO_UDP;
                        gSocketType = SOCK_DGRAM;
                    }
                    else
                        usage(argv[0]);
                    i++;
                    break;
                case 'x':               // sendcount
                    if (i+1 >=argc)
                        usage(argv[0]);
                    gSendCount = atoi(argv[++i]);
                    break;
                default:
                    usage(argv[0]);
                    break;
            }
        }
    }
}

//
// Function: SendThread
//
// Description:
//    This thread sends data on the socket. After all the requested data has
//    been send, the socket is shutdown (for TCP) or three zero byte sends
//    are performed (for UDP). The thread then exits.
//
DWORD WINAPI SendThread(LPVOID lpParam)
{
    SOCKET      s;
    char       *buf=NULL;
    int         buflen,
                nleft,
                idx,
                rc,
                i;

    s = (SOCKET)lpParam;

    // Allocate the send buffer
    buf = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BYTE) * gBufferSize);
    if (buf == NULL)
    {
        fprintf(stderr, "ReceiveThread: HeapAlloc failed: %d\n", GetLastError());
        ExitProcess(-1);
    }
    buflen = gBufferSize;

    memset(buf, '#', buflen);

    // Send the requested number of buffers
    for(i=0; i < gSendCount ;i++)
    {
        if ((gProtocol == IPPROTO_TCP) || (bUdpConnect))
        {
            idx = 0;
            nleft = buflen;
            while (nleft > 0)
            {
                rc = send(s, &buf[idx], nleft, 0);
                if (rc == SOCKET_ERROR)
                {
                    fprintf(stderr,"send failed: %d\n", WSAGetLastError());
                    return -1;
                }

                nleft -= rc;
                idx += rc;
            }
            rc = buflen;
        }
        else
        {
            rc = sendto(s, buf, buflen, 0, gConnectedEndpoint->ai_addr, gConnectedEndpoint->ai_addrlen);
            if (rc == SOCKET_ERROR)
            {
                // Its UDP so any failure we encounter is not likely to be serious
                fprintf(stderr, "sendto failed: %d\n", WSAGetLastError());
            }
        }

        // Update bytes sent count
        if (rc > 0)
        {
            InterlockedExchangeAdd(&gBytesSent, rc);
        }
    }

    // If TCP, shutdown the socket to indicate no more sends. For UDP
    // send three zero byte datagrams.
    if (gProtocol == IPPROTO_TCP)
    {
        shutdown(s, SD_SEND);
    }
    else
    {
        for(i=0; i < 3 ;i++)
        {
            rc = sendto(
                    s,
                    buf,
                    0,
                    0,
                    gConnectedEndpoint->ai_addr,
                    gConnectedEndpoint->ai_addrlen
                    );
        }
    }

    // Free the send buffer
    HeapFree(GetProcessHeap(), 0, buf);

    ExitThread(0);
    return 0;
}

//
// Function: ReceiveThread
//
// Description:
//    This is the receive thread that attempts to receive all data on
//    a given socket. For TCP this is done until the socket is closed
//    (and the recv call returns an error or 0). For UDP, receives
//    are performed until a zero byte datagram is received.
//
DWORD WINAPI ReceiveThread(LPVOID lpParam)
{
    SOCKET      s;
    char       *buf=NULL;
    int         buflen=0,
                rc;

    s = (SOCKET) lpParam;

    // Allocate the receive buffer for this socket
    buflen = gBufferSize;
    buf = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BYTE) * buflen);
    if (buf == NULL)
    {
        fprintf(stderr, "ReadThread: HeapAlloc failed: %d\n", WSAGetLastError());
        ExitProcess(-1);
    }

    while (1)
    {
        if (gProtocol == IPPROTO_UDP)
        {
            SOCKADDR_STORAGE addr;
            int              addrlen;

            addrlen = sizeof(addr);
            rc = recvfrom(
                    s,
                    buf,
                    buflen,
                    0,
                    (SOCKADDR *)&addr,
                   &addrlen
                    );
        } 
        else if (gProtocol == IPPROTO_TCP)
        {
            rc = recv(
                    s,
                    buf,
                    buflen,
                    0
                    );
        }
        if ((rc == SOCKET_ERROR) || (rc == 0))
        {
            // Either a zero byte datagram was read (UDP), the connection was
            // gracefully closed (TCP), or an error occured on the recv
            break;
        }
        else
        {
            InterlockedExchangeAdd(&gBytesRead, rc);
        }
    }

    // Free the receive buffer
    HeapFree(GetProcessHeap(), 0, buf);

    ExitThread(0);
    return 0;
}

//
// Function: main
//
// Description:
//      This is the main program. It parses the command line and creates
//      the main socket. For UDP this socket is used to receive datagrams.
//      For TCP the socket is used to accept incoming client connections.
//      Each client TCP connection is handed off to a worker thread which
//      will receive any data on that connection until the connection is
//      closed.
//
int __cdecl main(int argc, char **argv)
{
    WSADATA          wsd;
    SOCKET           s;
    HANDLE           hThreads[2];
    int              rc;                            // return code
    struct addrinfo *reslocal=NULL,
                    *resremote=NULL,
                    *ptr=NULL;

    // Parse the command line
    ValidateArgs(argc, argv);

    // Load Winsock
    if (WSAStartup(MAKEWORD(2,2), &wsd) != 0)
    {
        fprintf(stderr, "unable to load Winsock!\n");
        return -1;
    }

    // Resolve the server's name
    resremote = ResolveAddress(gServerAddr, gBindPort, gAddressFamily, gSocketType, gProtocol);
    if (resremote == NULL)
    {
        fprintf(stderr, "ResolveAddress failed to return any addresses!\n");
        return -1;
    }

    // Iterate through each address resolved from the server's name
    ptr = resremote;
    while (ptr)
    {
        printf("Local address: %s; Port: %s; Family: %d\n",
                gBindAddr, gBindPort, gAddressFamily);

        // Resolve the local address to bind to
        reslocal = ResolveAddress(gBindAddr, "0", ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (reslocal == NULL)
        {
            fprintf(stderr, "ResolveAddress failed to return any addresses!\n");
            return -1;
        }

        PrintAddress(reslocal->ai_addr, reslocal->ai_addrlen); printf("\n");

        // create the socket
        s = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (s == INVALID_SOCKET)
        {
            fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
            return -1;
        }

        // bind the socket to a local address and port
        rc = bind(s, reslocal->ai_addr, reslocal->ai_addrlen);
        if (rc == SOCKET_ERROR)
        {
            fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
            return -1;
        }

        // free the addrinfo structure for the 'bind' address
        freeaddrinfo(reslocal);

        if ((gProtocol == IPPROTO_TCP) || (bUdpConnect))
        {
            rc = connect(s, ptr->ai_addr, ptr->ai_addrlen);
            if (rc == SOCKET_ERROR)
            {
                printf("connect failed: %d\n", WSAGetLastError());
                closesocket(s);
                s = INVALID_SOCKET;
            }
            else
            {
                break;
            }
        }
        else
        {
            // Option is UDP with no connect, just take first address and go with it
            break;
        }

        // move to the next address resolved
        ptr = ptr->ai_next;
    }

    // See if we've got a good connection
    if (s == INVALID_SOCKET)
    {
        fprintf(stderr, "Unable to connect to server via resolved address(es)\n");
        return -1;
    }

    gConnectedEndpoint = ptr;

    gStartTime = GetTickCount();

    // Create the sending thread
    hThreads[0] = CreateThread(NULL, 0, SendThread, (LPVOID)s, 0, NULL);
    if (hThreads[0] == NULL)
    {
        fprintf(stderr, "CreateThread failed: %d\n", GetLastError());
        return -1;
    }
    hThreads[1] = CreateThread(NULL, 0, ReceiveThread, (LPVOID)s, 0, NULL);
    if (hThreads[1] == NULL)
    {
        fprintf(stderr, "CreateThread failed: %d\n", GetLastError());
        return -1;
    }

    while (1)
    {
        rc = WaitForMultipleObjects(2, hThreads, TRUE, 5000);
        if (rc == WAIT_FAILED)
        {
        }
        else if (rc == WAIT_TIMEOUT)
        {
            ULONG       bps, tick, elapsed;

            tick = GetTickCount();

            elapsed = (tick - gStartTime) / 1000;

            bps = gBytesRead / elapsed;
            printf("bytes per second read: %lu\n", bps);

            bps = gBytesSent / elapsed;
            printf("bytes per second sent: %lu\n", bps);
        }
        else
        {
            break;
        }
    }

    CloseHandle(hThreads[0]);
    CloseHandle(hThreads[1]);

    freeaddrinfo(resremote);

    closesocket(s);

    printf("\n");
    printf("total bytes sent %lu\n", gBytesSent);
    printf("total bytes read %lu\n", gBytesRead);

    WSACleanup();
    return 0;
}
