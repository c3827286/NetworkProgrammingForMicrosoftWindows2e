//
// Sample: Non-Blocking IPv4/IPv6 Server
//
// Files:
//      nbserver.cpp    - this file
//      resolve.cpp     - routines for resovling addresses, etc.
//      resolve.h       - header file for resolve.c
//
// Description:
//      This sample illustrates simple blocking IO for TCP and UDP for
//      both IPv4 and IPv6. This sample uses the getaddrinfo/getnameinfo
//      APIs which allows this application to be IP agnostic. That is the
//      desired address family (AF_INET or AF_INET6) can be determined
//      simply from the string address passed via the -l command.
//
//      This sample is single threaded! For TCP, a listening socket is
//      created for each available address family which then enters the
//      select loop. If the listening sockets are signaled for read
//      notification then a client connection has been accepted. The socket
//      is accepted and then added the the FD_SET. From there its a matter
//      of checking for which events occured on what sockets and taking
//      the appropriate action. To handle the receive data, these buffers
//      are added to a queue for each socket connection. When a client socket
//      is signaled for write, any data pending on the queue is written.
//
//      For UDP the principle is the same except there are no listning sockets,
//      just a single socket for each available address family.
//
//      For example:
//          If this sample is called with the following command lines:
//              nbserver.exe -l fe80::2efe:1234 -e 5150
//              nbserver.exe -l ::
//          Then the server creates an IPv6 socket as an IPv6 address was
//          provided.
//
//          On the other hand, with the following command line:
//              nbserver.exe -l 7.7.7.1 -e 5150
//              nbserver.exe -l 0.0.0.0
//          Then the server creates an IPv4 socket.
//
// Compile:
//      cl -o nbserver.exe nbserver.cpp resolve.cpp ws2_32.lib
//
// Usage:
//      nbserver.exe [options]
//          -a 4|6     Address family, 4 = IPv4, 6 = IPv6 [default = IPv4]
//          -b size    Size of send/recv buffer in bytes
//          -e port    Port number
//          -l addr    Local address to bind to [default INADDR_ANY for IPv4 or INADDR6_AN
//          -p proto   Which protocol to use [default = TCP]
//             tcp         Use TCP protocol
//             udp         Use UDP protocol
//


// define this before include winsock2.h to up the allowed size
#define FD_SET_SIZE     1024

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "resolve.h"

#define DEFAULT_BUFFER_SIZE     4096    // default buffer size

int gAddressFamily = AF_UNSPEC,         // default to unspecified
    gSocketType    = SOCK_STREAM,       // default to TCP socket type
    gProtocol      = IPPROTO_TCP,       // default to TCP protocol
    gBufferSize    = DEFAULT_BUFFER_SIZE;

char *gBindAddr    = NULL,              // local interface to bind to
     *gBindPort    = "5150";            // local port to bind to

//
// Allocated for each receiver posted
//
typedef struct _BUFFER_OBJ
{
    char        *buf;           // Data buffer for data
    int          buflen;        // Length of buffer or number of bytes contained in buffer

    SOCKADDR_STORAGE addr;      // Address data was received from (UDP)
    int              addrlen;   // Length of address

    struct _BUFFER_OBJ *next;   // Used to maintain a linked list of buffers
} BUFFER_OBJ;

//
// Allocated for each socket handle
//
typedef struct _SOCKET_OBJ
{
    SOCKET      s;              // Socket handle
    int         listening;      // Socket is a listening socket (TCP)
    int         closing;        // Indicates whether the connection is closing

    SOCKADDR_STORAGE addr;      // Used for client's remote address
    int              addrlen;   // Length of the address

    BUFFER_OBJ *pending,        // List of pending buffers to be sent
               *pendingtail;    // Last entry in buffer list

    struct _SOCKET_OBJ *next,   // Used to link socket objects together
                       *prev;
} SOCKET_OBJ;

SOCKET_OBJ *gSocketList=NULL,       // Linked list of all sockets allocated
           *gSocketListEnd=NULL;    // End of socket list
int         gSocketCount=0;         // Number of socket objects in list

//
// Statistics counters
//
volatile LONG gBytesRead=0,
              gBytesSent=0,
              gStartTime=0,
              gBytesReadLast=0,
              gBytesSentLast=0,
              gStartTimeLast=0,
              gCurrentConnections=0;


//
// Function: usage
//
// Description:
//      Prints usage information and exits the process.
//
void usage(char *progname)
{
    fprintf(stderr, "usage: %s [-a 4|6] [-e port] [-l local-addr] [-p udp|tcp]\n",
            progname);
    fprintf(stderr, "  -a 4|6     Address family, 4 = IPv4, 6 = IPv6 [default = IPv4]\n"
                    "  -b size    Buffer size for send/recv [default = %d]\n"
                    "  -e port    Port number [default = %s]\n"
                    "  -l addr    Local address to bind to [default INADDR_ANY for IPv4 or INADDR6_ANY for IPv6]\n"
                    "  -p tcp|udp Which protocol to use [default = TCP]\n",
                    gBufferSize,
                    gBindPort
                    );
    ExitProcess(-1);
}

//
// Function: GetBufferObj
// 
// Description:
//    Allocate a BUFFER_OBJ. Each receive posted by a receive thread allocates
//    one of these. After the recv is successful, the BUFFER_OBJ is queued for
//    sending by the send thread. Again, lookaside lists may be used to increase
//    performance.
//
BUFFER_OBJ *GetBufferObj(int buflen)
{
    BUFFER_OBJ *newobj=NULL;

    // Allocate the object
    newobj = (BUFFER_OBJ *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BUFFER_OBJ));
    if (newobj == NULL)
    {
        fprintf(stderr, "GetBufferObj: HeapAlloc failed: %d\n", GetLastError());
        ExitProcess(-1);
    }
    // Allocate the buffer
    newobj->buf = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BYTE) *buflen);
    if (newobj->buf == NULL)
    {
        fprintf(stderr, "GetBufferObj: HeapAlloc failed: %d\n", GetLastError());
        ExitProcess(-1);
    }
    newobj->buflen = buflen;

    newobj->addrlen = sizeof(newobj->addr);

    return newobj;
}

//
// Function: FreeBufferObj
// 
// Description:
//    Free the buffer object.
//
void FreeBufferObj(BUFFER_OBJ *obj)
{
    HeapFree(GetProcessHeap(), 0, obj->buf);
    HeapFree(GetProcessHeap(), 0, obj);
}

//
// Function: GetSocketObj
//
// Description:
//    Allocate a socket object and initialize its members. A socket object is
//    allocated for each socket created (either by socket or accept). The
//    socket objects mantain a list of all buffers received that need to
//    be sent.
//
SOCKET_OBJ *GetSocketObj(SOCKET s, int listening)
{
    SOCKET_OBJ  *sockobj=NULL;

    sockobj = (SOCKET_OBJ *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SOCKET_OBJ));
    if (sockobj == NULL)
    {
        fprintf(stderr, "GetSocketObj: HeapAlloc failed: %d\n", GetLastError());
        ExitProcess(-1);
    }

    // Initialize the members
    sockobj->s = s;
    sockobj->listening = listening;
    sockobj->addrlen = sizeof(sockobj->addr);

    return sockobj;
}

//
// Function: FreeSocketObj
//
// Description:
//    Frees a socket object along with any queued buffer objects.
//
void FreeSocketObj(SOCKET_OBJ *obj)
{
    BUFFER_OBJ  *ptr=NULL,
                *tmp=NULL;

    ptr = obj->pending;
    while (ptr)
    {
        tmp = ptr;
        ptr = ptr->next;

        FreeBufferObj(tmp);
    }

    HeapFree(GetProcessHeap(), 0, obj);
}

//
// Function: InsertSocketObj
//
// Description:
//    Insert a socket object into the list of socket objects. Note that
//    no synchronization is performed because this app is single threaded!
//
void InsertSocketObj(SOCKET_OBJ *sock)
{
    sock->next = sock->prev = NULL;
    if (gSocketList == NULL)
    {
        // List is empty
        gSocketList = gSocketListEnd = sock;
    }
    else
    {
        // Non-empty; insert at the end
        sock->prev = gSocketListEnd;
        gSocketListEnd->next = sock;
        gSocketListEnd = sock;

    }
    gSocketCount++;
}

//
// Function: RemoveSocketObj
//
// Description:
//    Remove a socket object from the list of sockets. No synchronization is
//    is performed since this app is single threaded.
//
void RemoveSocketObj(SOCKET_OBJ *sock)
{
    if (sock->prev)
    {
        sock->prev->next = sock->next;
    }
    if (sock->next)
    {
        sock->next->prev = sock->prev;
    }

    if (gSocketList == sock)
        gSocketList = sock->next;
    if (gSocketListEnd == sock)
        gSocketListEnd = sock->prev;

    gSocketCount--;
}

//
// Function: EnqueueBufferObj
//
// Description:
//   Queue up a receive buffer for this connection.
//
void EnqueueBufferObj(SOCKET_OBJ *sock, BUFFER_OBJ *obj, BOOL AtHead)
{
    if (sock->pending == NULL)
    {
        // Queue is empty
        sock->pending = sock->pendingtail = obj;
    }
    else if (AtHead == FALSE)
    {
        // Put new object at the end 
        sock->pendingtail->next = obj;
        sock->pendingtail = obj;
    }
    else
    {
        // Put new object at the head
        obj->next = sock->pending;
        sock->pending = obj;
    }
}

// 
// Function: DequeueBufferObj
//
// Description:
//    Remove a BUFFER_OBJ from the given connection's queue for sending.
//
BUFFER_OBJ *DequeueBufferObj(SOCKET_OBJ *sock)
{
    BUFFER_OBJ *ret=NULL;

    if (sock->pendingtail != NULL)
    {
        // Queue is non empty
        ret = sock->pending;

        sock->pending = sock->pending->next;
        if (sock->pendingtail == ret)
        {
            // Item is the only item in the queue
            sock->pendingtail = NULL;
        }
    }

    return ret;
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
                case 'b':               // buffer size for send/recv
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gBufferSize = atol(argv[++i]);
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
                default:
                    usage(argv[0]);
                    break;
            }
        }
    }
}

//
// Function: ReceivePendingData
//
// Description:
//    Receive data pending on the socket into a SOCKET_OBJ buffer. Enqueue
//    the buffer into the socket object for sending later. This routine returns
//    -1 indicating that the socket is no longer valid and the calling function
//    should clean up (remove) the socket object. Zero is returned for success.
//
int ReceivePendingData(SOCKET_OBJ *sockobj)
{
    BUFFER_OBJ *buffobj=NULL;
    int         rc,
                ret;

    // Get a buffer to receive the data
    buffobj = GetBufferObj(gBufferSize);

    ret = 0;

    if (gProtocol == IPPROTO_TCP)
    {
        rc = recv(
                sockobj->s,
                buffobj->buf,
                buffobj->buflen,
                0
                );
    }
    else 
    {
        rc = recvfrom(
                sockobj->s,
                buffobj->buf,
                buffobj->buflen,
                0,
                (SOCKADDR *)&buffobj->addr,
               &buffobj->addrlen
                );
    }
    if (rc == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSAEWOULDBLOCK)
        {
            // Socket connection has failed, close the socket
            fprintf(stderr, "recv(from) failed: %d\n", WSAGetLastError());

            closesocket(sockobj->s);

            ret = -1;
        }
        FreeBufferObj(buffobj);
    }
    else if (rc == 0)
    {
        // Graceful close
        if (gProtocol == IPPROTO_TCP)
        {
            FreeBufferObj(buffobj);
        }
        else
        {
            buffobj->buflen = 0;
            EnqueueBufferObj(sockobj, buffobj, FALSE);
        }

        sockobj->closing = TRUE;

        if (sockobj->pending == NULL)
        {
            // If no sends are pending, close the socket for good
            closesocket(sockobj->s);

            ret = -1;
        }
    }
    else
    {
        // Read data, updated the counters and enqueue the buffer for sending
        gBytesRead += rc;
        gBytesReadLast += rc;

        buffobj->buflen = rc;
        EnqueueBufferObj(sockobj, buffobj, FALSE);
    }
    return ret;
}

//
// Function: SendPendingData
//
// Description:
//    Send any data pending on the socket. This routine goes through the 
//    queued buffer objects within the socket object and attempts to
//    send all of them. If the send fails with WSAEWOULDBLOCK, put the
//    remaining buffer back in the queue (at the front) for sending
//    later when select indicates sends can be made. This routine returns
//    -1 to indicate that an error has occured on the socket and the
//    calling routine should remove the socket structure; otherwise, zero
//    is returned.
//
int SendPendingData(SOCKET_OBJ *sock)
{
    BUFFER_OBJ *bufobj=NULL;
    BOOL        breakouter;
    int         nleft,
                idx,
                ret,
                rc;

    // Attempt to dequeue all the buffer objects on the socket
    ret = 0;
    while (bufobj = DequeueBufferObj(sock))
    {
        if (gProtocol == IPPROTO_TCP)
        {
            breakouter = FALSE;

            nleft = bufobj->buflen;
            idx = 0;

            // In the event not all the data was sent we need to increment
            // through the buffer. This only needs to be done for stream
            // sockets since UDP is datagram and its all or nothing for that.
            while (nleft)
            {
                rc = send(
                        sock->s,
                       &bufobj->buf[idx],
                        nleft,
                        0
                        );
                if (rc == SOCKET_ERROR)
                {
                    if (WSAGetLastError() == WSAEWOULDBLOCK)
                    {
                        BUFFER_OBJ *newbuf=NULL;

                        // Copy the unsent portion of the buffer and put it back
                        // at the head of the send queue
                        newbuf = GetBufferObj(nleft);
                        memcpy(newbuf->buf, &bufobj->buf[idx], nleft);

                        EnqueueBufferObj(sock, newbuf, TRUE);
                    }
                    else
                    {
                        // The connection was broken, indicate failure
                        ret = -1;
                    }
                    breakouter = TRUE;

                    break;
                }
                else
                {
                    // Update the stastics and increment the send counters
                    gBytesSent += rc;
                    gBytesSentLast += rc;

                    nleft -= rc;
                    idx += 0;
                }
            }
            FreeBufferObj(bufobj);

            if (breakouter)
                break;
        }
        else
        {
            rc = sendto(
                    sock->s,
                    bufobj->buf,
                    bufobj->buflen,
                    0,
                    (SOCKADDR *)&bufobj->addr,
                    bufobj->addrlen
                    );
            if (rc == SOCKET_ERROR)
            {
                if (WSAGetLastError() == WSAEWOULDBLOCK)
                {
                    // If the send couldn't be made, put the buffer
                    // back at the head of the queue
                    EnqueueBufferObj(sock, bufobj, TRUE);

                    ret = 0;
                }
                else
                {
                    // Socket error occured so indicate the error to the caller
                    ret = -1;
                }
                break;
            }
            else
            {
                FreeBufferObj(bufobj);
            }
        }
    }
    // If no more sends are pending and the socket was marked as closing (the
    // receiver got zero bytes) then close the socket and indicate to the caller
    // to remove the socket structure.
    if ((sock->pending == NULL) && (sock->closing))
    {
        closesocket(sock->s);
        ret = -1;

        printf("Closing connection\n");
    }
    return ret;
}

//
// Function: PrintStatistics
//
// Description:
//    Print the send/recv statistics for the server
//
void PrintStatistics()
{
    ULONG       bps, tick, elapsed;

    tick = GetTickCount();

    elapsed = (tick - gStartTime) / 1000;

    if (elapsed == 0)
        return;

    printf("\n");

    bps = gBytesSent / elapsed;
    printf("Average BPS sent: %lu [%lu]\n", bps, gBytesSent);

    bps = gBytesRead / elapsed;
    printf("Average BPS read: %lu [%lu]\n", bps, gBytesRead);

    elapsed = (tick - gStartTimeLast) / 1000;

    if (elapsed == 0)
        return;

    bps = gBytesSentLast / elapsed;
    printf("Current BPS sent: %lu\n", bps);

    bps = gBytesReadLast / elapsed;
    printf("Current BPS read: %lu\n", bps);

    printf("Current Connections: %lu\n", gCurrentConnections);

    InterlockedExchange(&gBytesSentLast, 0);
    InterlockedExchange(&gBytesReadLast, 0);

    gStartTimeLast = tick;
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
    SOCKET_OBJ      *sockobj=NULL,
                    *sptr=NULL,
                    *tmp=NULL;
    ULONG            lastprint=0;
    int              rc;
    struct fd_set    fdread,
                     fdwrite,
                     fdexcept;
    struct timeval   timeout;
    struct addrinfo *res=NULL,
                    *ptr=NULL;

    ValidateArgs(argc, argv);

    if (WSAStartup(MAKEWORD(2,2), &wsd) != 0)
    {
        fprintf(stderr, "unable to load Winsock!\n");
        return -1;
    }

    printf("Local address: %s; Port: %s; Family: %d\n",
            gBindAddr, gBindPort, gAddressFamily);

    res = ResolveAddress(gBindAddr, gBindPort, gAddressFamily, gSocketType, gProtocol);
    if (res == NULL)
    {
        fprintf(stderr, "ResolveAddress failed to return any addresses!\n");
        return -1;
    }

    // For each local address returned, create a listening/receiving socket
    ptr = res;
    while (ptr)
    {
        PrintAddress(ptr->ai_addr, ptr->ai_addrlen); printf("\n");

        // create the socket
        s = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (s == INVALID_SOCKET)
        {
            fprintf(stderr,"socket failed: %d\n", WSAGetLastError());
            return -1;
        }

        sockobj = GetSocketObj(s, (gProtocol == IPPROTO_TCP) ? TRUE : FALSE);

        InsertSocketObj(sockobj);

        // bind the socket to a local address and port
        rc = bind(sockobj->s, ptr->ai_addr, ptr->ai_addrlen);
        if (rc == SOCKET_ERROR)
        {
            fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
            return -1;
        }

        if (gProtocol == IPPROTO_TCP)
        {
            rc = listen(sockobj->s, 200);
            if (rc == SOCKET_ERROR)
            {
                fprintf(stderr, "listen failed: %d\n", WSAGetLastError());
                return -1;
            }
        }

        ptr = ptr->ai_next;
    }
    // free the addrinfo structure for the 'bind' address
    freeaddrinfo(res);

    gStartTime = gStartTimeLast = lastprint = GetTickCount();

    while (1)
    {
        FD_ZERO(&fdread);
        FD_ZERO(&fdwrite);
        FD_ZERO(&fdexcept);

        sptr = gSocketList;

        // Set each socket in the FD_SET structures
        while (sptr)
        {
            FD_SET(sptr->s, &fdread);
            FD_SET(sptr->s, &fdwrite);
            FD_SET(sptr->s, &fdexcept);

            sptr = sptr->next;
        }

        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        rc = select(0, &fdread, &fdwrite, &fdexcept, &timeout);
        if (rc == SOCKET_ERROR)
        {
            fprintf(stderr, "select failed: %d\n", WSAGetLastError());
            return -1;
        }
        else if (rc == 0)
        {
            // timeout
            PrintStatistics();
        }
        else
        {
            // Go through all the socket and see if they're present in the
            // fd_set structures.
            sptr = gSocketList;
            while (sptr)
            {
                if (FD_ISSET(sptr->s, &fdread))
                {
                    if (sptr->listening)
                    {
                        // Read is indicated on a listening socket, accept the connection
                        sockobj = GetSocketObj(INVALID_SOCKET, FALSE);

                        s = accept(sptr->s, (SOCKADDR *)&sockobj->addr, &sockobj->addrlen);
                        if (s == INVALID_SOCKET)
                        {
                            fprintf(stderr, "accept failed: %d\n", WSAGetLastError());
                            return -1;
                        }

                        InterlockedIncrement(&gCurrentConnections);

                        sockobj->s = s;
                        
                        /*
                        printf("Accepted connection from: ");
                        PrintAddress((SOCKADDR *)&sockobj->addr, sockobj->addrlen);
                        printf("\n");
                        */

                        InsertSocketObj(sockobj);
                    }
                    else
                    {
                        // Read is indicated on a client socket, receive data
                        if (ReceivePendingData(sptr) != 0)
                        {
                            printf("ReceivePendingData indicated to remove obj\n");
                            tmp = sptr;
                            sptr = sptr->next;

                            RemoveSocketObj(tmp);
                            FreeSocketObj(tmp);

                            // At the end of the list
                            if (sptr == NULL)
                                continue;
                        }

                        // Attempt to send pending data
                        if (SendPendingData(sptr) != 0)
                        {
                            tmp = sptr;
                            sptr = sptr->next;

                            RemoveSocketObj(tmp);
                            FreeSocketObj(tmp);

                            // At the end of the list
                            if (sptr == NULL)
                                continue;
                        }
                    }
                }
                if (FD_ISSET(sptr->s, &fdwrite))
                {
                    // Write is indicated so attempt to send the pending data
                    if (SendPendingData(sptr) != 0)
                    {
                        tmp = sptr;
                        sptr = sptr->next;

                        RemoveSocketObj(tmp);
                        FreeSocketObj(tmp);

                        // At the end of the list
                        if (sptr == NULL)
                            continue;
                    }
                }
                if (FD_ISSET(sptr->s, &fdexcept))
                {
                    // Not handling OOB data so just close the connection
                    tmp = sptr;
                    sptr = sptr->next;

                    RemoveSocketObj(tmp);
                    FreeSocketObj(tmp);

                    // At the end of the list
                    if (sptr == NULL)
                        continue;
                }

                sptr = sptr->next;
            }
        }

        // See if we should print statistics
        if ( (GetTickCount() - lastprint) > 5000)
        {
            PrintStatistics();

            lastprint = GetTickCount();
        }
    }

    WSACleanup();
    return 0;
}
