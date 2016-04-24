//
// Sample: I/O Completion Port IPv4/IPv6 Server
//
// Files:
//      iocpserver.cpp    - this file
//      resolve.cpp       - Common name resolution routines
//      resolve.h         - Header file for name resolution routines
//
// Description:
//      This sample illustrates overlapped IO with a completion port for
//      TCP and UDP over both IPv4 and IPv6. This sample uses the 
//      getaddrinfo/getnameinfo APIs which allows this application to be 
//      IP version independent. That is the desired address family 
//      (AF_INET or AF_INET6) can be determined simply from the string 
//      address passed via the -l command.
//
//      For TCP, a listening socket is created for each IP address family
//      available. Each socket is associated with a completion port and
//      worker threads are spawned (one for each CPU available). For each
//      listening thread, a number of AcceptEx are posted. The worker threads
//      then wait for one of these to complete. Upon completion, the client
//      socket is associated with the completion port and several receives
//      are posted. The AcceptEx is reposted as well. Once data is received
//      on a client socket, it is echoed back. 
//
//      For UDP, an echo socket is creatd for each IP address family available.
//      For each socket, several receives are posted. Once these receives 
//      complete, the data is sent back to the receiver.
//
//      The important thing to remember with IOCP is that the completion events
//      may occur out of order; however, the buffers are guaranteed to be filled
//      in the order posted. For our echo server this can cause problems as 
//      receive N+1 may complete before receive N. We can't echo back N+1 before
//      echoing N. There are two approaches possible. First, we could surmise
//      that since receive N+1 has completed then we can safely echo back receive
//      N and N+1 at that time (to maintain the data ordering). To do this properly
//      you'll have to call WSAGetOverlappedResult on receive N in order to find
//      out how many bytes were received to echo it back. The second approach
//      (which is implemented in this sample) is to keep a list of receive
//      buffers that completed out of order. This list is maintained in the
//      per-socket data structure. When receive N+1 completes, it will notice that
//      receive N has not completed. The buffer is then queued in the out of
//      order send list. Once receive N completes, its buffer is queued -- the
//      queue is ordered in the same order that the receive operations are.
//      Another routine (DoSends) goes through this list and sends those buffers
//      that are available and in order. If any gaps are detected no further buffers
//      are sent (as we will wait for that receive to complete and insert its
//      buffer into the list so that the next call to DoSends will correctly
//      send the buffers in the right order).
//
//      For example:
//          If this sample is called with the following command lines:
//              iocpserver.exe -l fe80::2efe:1234 -e 5150
//              iocpserver.exe -l ::
//          Then the server creates an IPv6 socket as an IPv6 address was
//          provided.
//
//          On the other hand, with the following command line:
//              iocpserver.exe -l 7.7.7.1 -e 5150
//              iocpserver.exe -l 0.0.0.0
//          Then the server creates an IPv4 socket.
//
//          Calling the server with no parameters will create a server that
//          listens both IPv4 and IPv6 (if installed).
//
// Compile:
//      cl -o iocpserver.exe iocpserver.cpp resolve.cpp ws2_32.lib
//
// Usage:
//      iocpserver.exe [options]
//          -a 4|6     Address family, 4 = IPv4, 6 = IPv6 [default = IPv4]
//          -b size    Buffer size for send/recv
//          -e port    Port number
//          -l addr    Local address to bind to [default INADDR_ANY for IPv4 or INADDR6_ANY for IPv6]
//          -p proto   Which protocol to use [default = TCP]
//              tcp         Use TCP
//              udp         Use UDP
//

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "resolve.h"

#define DEFAULT_BUFFER_SIZE         4096   // default buffer size
#define DEFAULT_OVERLAPPED_COUNT    5      // Number of overlapped recv per socket
#define MAX_COMPLETION_THREAD_COUNT 32     // Maximum number of completion threads allowed

int gAddressFamily = AF_UNSPEC,         // default to unspecified
    gSocketType    = SOCK_STREAM,       // default to TCP socket type
    gProtocol      = IPPROTO_TCP,       // default to TCP protocol
    gBufferSize    = DEFAULT_BUFFER_SIZE,
    gOverlappedCount = DEFAULT_OVERLAPPED_COUNT;

char *gBindAddr    = NULL,              // local interface to bind to
     *gBindPort    = "5150";            // local port to bind to

//
// This is our per I/O buffer. It contains a WSAOVERLAPPED structure as well
//    as other necessary information for handling an IO operation on a socket.
//
typedef struct _BUFFER_OBJ
{
    WSAOVERLAPPED        ol;

    SOCKET               sclient;       // Used for AcceptEx client socket

    char                *buf;           // Buffer for recv/send/AcceptEx
    int                  buflen;        // Length of the buffer

    int                  operation;     // Type of operation issued
#define OP_ACCEPT       0                   // AcceptEx
#define OP_READ         1                   // WSARecv/WSARecvFrom
#define OP_WRITE        2                   // WSASend/WSASendTo

    SOCKADDR_STORAGE     addr;
    int                  addrlen;

    ULONG                IoOrder;       // Order in which this I/O was posted

    struct _BUFFER_OBJ  *next;

} BUFFER_OBJ;

//
// This is our per socket buffer. It contains information about the socket handle
//    which is returned from each GetQueuedCompletionStatus call.
//
typedef struct _SOCKET_OBJ
{
    SOCKET               s;              // Socket handle

    int                  af,             // Address family of socket (AF_INET, AF_INET6)
                         bClosing;       // Is the socket closing?

    volatile LONG        OutstandingOps; // Number of outstanding overlapped ops on 
                                         //    socket

    BUFFER_OBJ         **PendingAccepts; // Pending AcceptEx buffers 
                                         //   (used for listening sockets only)

    ULONG                LastSendIssued, // Last sequence number sent
                         IoCountIssued;  // Next sequence number assigned to receives
    BUFFER_OBJ          *OutOfOrderSends;// List of send buffers that completed out of order

    // Pointers to Microsoft specific extensions. These are used by listening
    //   sockets only
    LPFN_ACCEPTEX        lpfnAcceptEx;
    LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs;

    CRITICAL_SECTION     SockCritSec;    // Protect access to this structure

    struct _SOCKET_OBJ  *next;
} SOCKET_OBJ;

//
// Statistics counters
//
volatile LONG gBytesRead=0,
              gBytesSent=0,
              gStartTime=0,
              gBytesReadLast=0,
              gBytesSentLast=0,
              gStartTimeLast=0;

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

void dbgprint(char *format,...)
{
#ifdef DEBUG
    va_list vl;
    char    dbgbuf[2048];

    if (pid == 0)
    {
        pid = GetCurrentProcessId();
    }

    va_start(vl, format);
    wvsprintf(dbgbuf, format, vl);
    va_end(vl);

    OutputDebugString(dbgbuf);
#endif
}

//
// Function: GetBufferObj
// 
// Description:
//    Allocate a BUFFER_OBJ. Each receive posted allocates one of these. 
//    After the recv is successful, the BUFFER_OBJ is queued for
//    sending by the send thread. To increase performance, a lookaside lists 
//    should be used to cache free BUFFER_OBJ.
//
BUFFER_OBJ *GetBufferObj(SOCKET_OBJ *sock, int buflen)
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
//    Free the buffer object. To increase performance, a lookaside list should be
//    implemented to cache BUFFER_OBJ when freed.
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
//    allocated for each socket created (either by socket or accept).
//    Again, a lookaside list can be implemented to cache freed SOCKET_OBJ to
//    improve performance.
//
SOCKET_OBJ *GetSocketObj(SOCKET s, int af)
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
    sockobj->af = af;

    // For TCP we initialize the IO count to one since the AcceptEx is posted
    //    to receive data
    sockobj->IoCountIssued = ((gProtocol == IPPROTO_TCP) ? 1 : 0);

    InitializeCriticalSection(&sockobj->SockCritSec);

    return sockobj;
}

//
// Function: FreeSocketObj
//
// Description:
//    Frees a socket object. If there are outstanding operations, the object
//    is not freed. 
//
void FreeSocketObj(SOCKET_OBJ *obj)
{
    BUFFER_OBJ  *ptr=NULL,
                *tmp=NULL;

    if (obj->OutstandingOps != 0)
    {
        // Still outstanding operations so just return
        return;
    }
    // Close the socket if it hasn't already been closed
    if (obj->s != INVALID_SOCKET)
    {
        closesocket(obj->s);
        obj->s = INVALID_SOCKET;
    }

    DeleteCriticalSection(&obj->SockCritSec);

    HeapFree(GetProcessHeap(), 0, obj);
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
                case 'o':               // overlapped count
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gOverlappedCount = atol(argv[++i]);
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

    // Calculate average bytes per second
    bps = gBytesSent / elapsed;
    printf("Average BPS sent: %lu [%lu]\n", bps, gBytesSent);

    bps = gBytesRead / elapsed;
    printf("Average BPS read: %lu [%lu]\n", bps, gBytesRead);

    elapsed = (tick - gStartTimeLast) / 1000;

    if (elapsed == 0)
        return;

    // Calculate bytes per second over the last X seconds
    bps = gBytesSentLast / elapsed;
    printf("Current BPS sent: %lu\n", bps);

    bps = gBytesReadLast / elapsed;
    printf("Current BPS read: %lu\n", bps);

    InterlockedExchange(&gBytesSentLast, 0);
    InterlockedExchange(&gBytesReadLast, 0);

    gStartTimeLast = tick;
}

//
// Function: PostRecv
// 
// Description: 
//    Post an overlapped receive operation on the socket.
//
int PostRecv(SOCKET_OBJ *sock, BUFFER_OBJ *recvobj)
{
    WSABUF  wbuf;
    DWORD   bytes,
            flags;
    int     rc;


    recvobj->operation = OP_READ;

    wbuf.buf = recvobj->buf;
    wbuf.len = recvobj->buflen;

    flags = 0;

    EnterCriticalSection(&sock->SockCritSec);

    // Assign the IO order to this receive. This must be performned within
    //    the critical section. The operation of assigning the IO count and posting
    //    the receive cannot be interupted.
    recvobj->IoOrder = sock->IoCountIssued;
    sock->IoCountIssued++;

    if (gProtocol == IPPROTO_TCP)
    {
        rc = WSARecv(
                sock->s,
               &wbuf,
                1,
               &bytes,
               &flags,
               &recvobj->ol,
                NULL
                );
    }
    else
    {
        rc = WSARecvFrom(
                sock->s,
               &wbuf,
                1,
               &bytes,
               &flags,
                (SOCKADDR *)&recvobj->addr,
               &recvobj->addrlen,
               &recvobj->ol,
                NULL
                );
    }

    LeaveCriticalSection(&sock->SockCritSec);

    if (rc == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            dbgprint("PostRecv: WSARecv* failed: %d\n", WSAGetLastError());
            return SOCKET_ERROR;
        }
    }

    // Increment outstanding overlapped operations
    InterlockedIncrement(&sock->OutstandingOps);

    return NO_ERROR;
}

//
// Function: PostSend
// 
// Description:
//    Post an overlapped send operation on the socket.
//
int PostSend(SOCKET_OBJ *sock, BUFFER_OBJ *sendobj)
{
    WSABUF  wbuf;
    DWORD   bytes;
    int     rc;

    sendobj->operation = OP_WRITE;

    wbuf.buf = sendobj->buf;
    wbuf.len = sendobj->buflen;

    EnterCriticalSection(&sock->SockCritSec);

    // Incrmenting the last send issued and issuing the send should not be
    //    interuptable.
    sock->LastSendIssued++;

    if (gProtocol == IPPROTO_TCP)
    {
        rc = WSASend(
                sock->s,
               &wbuf,
                1,
               &bytes,
                0,
               &sendobj->ol,
                NULL
                );
    }
    else
    {
        rc = WSASendTo(
                sock->s,
               &wbuf,
                1,
               &bytes,
                0,
                (SOCKADDR *)&sendobj->addr,
                sendobj->addrlen,
               &sendobj->ol,
                NULL
                );
    }

    LeaveCriticalSection(&sock->SockCritSec);

    if (rc == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            dbgprint("PostSend: WSASend* failed: %d\n", WSAGetLastError());
            return SOCKET_ERROR;
        }
    }

    // Increment the outstanding operation count
    InterlockedIncrement(&sock->OutstandingOps);

    return NO_ERROR;
}

//
// Function: PostAccept
// 
// Description:
//    Post an overlapped accept on a listening socket.
//
int PostAccept(SOCKET_OBJ *sock, BUFFER_OBJ *acceptobj)
{
    DWORD   bytes;
    int     rc;

    acceptobj->operation = OP_ACCEPT;

    // Create the client socket for an incoming connection
    acceptobj->sclient = socket(sock->af, SOCK_STREAM, IPPROTO_TCP);
    if (acceptobj->sclient == INVALID_SOCKET)
    {
        fprintf(stderr, "PostAccept: socket failed: %d\n", WSAGetLastError());
        return -1;
    }

    rc = sock->lpfnAcceptEx(
            sock->s,
            acceptobj->sclient,
            acceptobj->buf,
            acceptobj->buflen - ((sizeof(SOCKADDR_STORAGE) + 16) * 2),
            sizeof(SOCKADDR_STORAGE) + 16,
            sizeof(SOCKADDR_STORAGE) + 16,
           &bytes,
           &acceptobj->ol
            );
    if (rc == FALSE)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            dbgprint("PostAccept: AcceptEx failed: %d\n",
                    WSAGetLastError());
            return SOCKET_ERROR;
        }
    }

    // Increment the outstanding overlapped count for this socket
    InterlockedIncrement(&sock->OutstandingOps);

    return NO_ERROR;
}

//
// Function: InsertPendingSend
// 
// Description:
//    This routine inserts a send buffer object into the socket's list
//    of out of order sends. The routine DoSends will go through this
//    list to issue those sends that are in the correct order.
//
void InsertPendingSend(SOCKET_OBJ *sock, BUFFER_OBJ *send)
{
    BUFFER_OBJ *ptr=NULL,
               *prev=NULL;


    EnterCriticalSection(&sock->SockCritSec);

    send->next = NULL;

    // This loop finds the place to put the send within the list.
    //    The send list is in the same order as the receives were
    //    posted.
    ptr = sock->OutOfOrderSends;
    while (ptr)
    {
        if (send->IoOrder < ptr->IoOrder)
        {
            break;
        }

        prev = ptr;
        ptr = ptr->next;
    }
    if (prev == NULL)
    {
        // Inserting at head
        sock->OutOfOrderSends = send;
        send->next = ptr;
    }
    else
    {
        // Insertion somewhere in the middle
        prev->next = send;
        send->next = ptr;
    }

    LeaveCriticalSection(&sock->SockCritSec);
}

//
// Function: DoSends
//
// Description:
//    This routine goes through a socket object's list of out of order send
//    buffers and sends as many of them up to the current send count. For each
//    send posted, the LastSendIssued is incremented. This means that the next
//    buffer sent must have an IO sequence nubmer equal to the LastSendIssued.
//    This is to preserve the order of data echoed back.
//
int DoSends(SOCKET_OBJ *sock)
{
    BUFFER_OBJ *sendobj=NULL;
    int         ret;

    ret = NO_ERROR;

    EnterCriticalSection(&sock->SockCritSec);

    sendobj = sock->OutOfOrderSends;
    while ((sendobj) && (sendobj->IoOrder == sock->LastSendIssued))
    {
        if (PostSend(sock, sendobj) != NO_ERROR)
        {
            FreeBufferObj(sendobj);
            
            ret = SOCKET_ERROR;
            break;
        }
        sock->OutOfOrderSends = sendobj = sendobj->next;
    }

    LeaveCriticalSection(&sock->SockCritSec);

    return ret;
}

//
// Function: HandleIo
//
// Description:
//    This function handles the IO on a socket. In the event of a receive, the 
//    completed receive is posted again. For completed accepts, another AcceptEx
//    is posted. For completed sends, the buffer is freed.
//
void HandleIo(SOCKET_OBJ *sock, BUFFER_OBJ *buf, HANDLE CompPort, DWORD BytesTransfered, DWORD error)
{
    SOCKET_OBJ *clientobj=NULL;     // New client object for accepted connections
    BUFFER_OBJ *recvobj=NULL,       // Used to post new receives on accepted connections
               *sendobj=NULL;       // Used to post new sends for data received
    BOOL        bCleanupSocket;
    char       *tmp;
    int         i;

    if (error != 0)
        dbgprint("OP = %d; Error = %d\n", buf->operation, error);

    bCleanupSocket = FALSE;

    if ((error != NO_ERROR) && (gProtocol == IPPROTO_TCP))
    {
        // An error occured on a TCP socket, free the associated per I/O buffer
        // and see if there are any more outstanding operations. If so we must
        // wait until they are complete as well.
        //
        FreeBufferObj(buf);

        if (InterlockedDecrement(&sock->OutstandingOps) == 0)
        {
            dbgprint("Freeing socket obj in GetOverlappedResult\n");
            FreeSocketObj(sock);
        }
        return;
    }

    EnterCriticalSection(&sock->SockCritSec);
    if (buf->operation == OP_ACCEPT)
    {
        HANDLE            hrc;
        SOCKADDR_STORAGE *LocalSockaddr=NULL,
                         *RemoteSockaddr=NULL;
        int               LocalSockaddrLen,
                          RemoteSockaddrLen;

        // Update counters
        InterlockedExchangeAdd(&gBytesRead, BytesTransfered);
        InterlockedExchangeAdd(&gBytesReadLast, BytesTransfered);

        // Print the client's addresss
        sock->lpfnGetAcceptExSockaddrs(
                buf->buf,
                buf->buflen - ((sizeof(SOCKADDR_STORAGE) + 16) * 2),
                sizeof(SOCKADDR_STORAGE) + 16,
                sizeof(SOCKADDR_STORAGE) + 16,
                (SOCKADDR **)&LocalSockaddr,
               &LocalSockaddrLen,
                (SOCKADDR **)&RemoteSockaddr,
               &RemoteSockaddrLen
                );

        /*
        printf("Received connection from: ");
        PrintAddress((SOCKADDR *)RemoteSockaddr, RemoteSockaddrLen);
        printf("\n");
        */

        // Get a new SOCKET_OBJ for the client connection
        clientobj = GetSocketObj(buf->sclient, sock->af);

        // Associate the new connection to our completion port
        hrc = CreateIoCompletionPort(
                (HANDLE)buf->sclient,
                CompPort,
                (ULONG_PTR)clientobj,
                0
                );
        if (hrc == NULL)
        {
            fprintf(stderr, "CompletionThread: CreateIoCompletionPort failed: %d\n",
                    GetLastError());
            return;
        }

        // Get a BUFFER_OBJ to echo the data received with the accept back to the client
        sendobj = GetBufferObj(clientobj, BytesTransfered);

        // Copy the buffer to the sending object
        memcpy(sendobj->buf, buf->buf, BytesTransfered);

        // Post the send
        if (PostSend(clientobj, sendobj) == NO_ERROR)
        {
            // Now post some receives on this new connection
            for(i=0; i < gOverlappedCount ;i++)
            {
                recvobj = GetBufferObj(clientobj, gBufferSize);

                if (PostRecv(clientobj, recvobj) != NO_ERROR)
                {
                    // If for some reason the send call fails, clean up the connection
                    FreeBufferObj(recvobj);
                    error = SOCKET_ERROR;
                    break;
                }
            }
        }
        else
        {
            // If for some reason the send call fails, clean up the connection
            FreeBufferObj(sendobj);
            error = SOCKET_ERROR;
        }
        
        // Re-post the AcceptEx
        PostAccept(sock, buf);

		if (error != NO_ERROR)
		{
            if (clientobj->OutstandingOps == 0)
            {
                closesocket(clientobj->s);
                clientobj->s = INVALID_SOCKET;
                FreeSocketObj(clientobj);
            }
            else
            {
                clientobj->bClosing = TRUE;
            }
            error = NO_ERROR;
		}
    }
    else if ((buf->operation == OP_READ) && (error == NO_ERROR))
    {
        //
        // Receive completed successfully
        //
        if ((BytesTransfered > 0) || (gProtocol == IPPROTO_UDP))
        {
            InterlockedExchangeAdd(&gBytesRead, BytesTransfered);
            InterlockedExchangeAdd(&gBytesReadLast, BytesTransfered);

            // Create a buffer to send
            sendobj = GetBufferObj(sock, gBufferSize);

            if (gProtocol == IPPROTO_UDP)
            {
                memcpy(&sendobj->addr, &buf->addr, buf->addrlen);
            }

            // Swap the buffers (i.e. buffer we just received becomes the send buffer)
            tmp              = sendobj->buf;
            sendobj->buflen  = BytesTransfered;
            sendobj->buf     = buf->buf;
            sendobj->IoOrder = buf->IoOrder;

            buf->buf    = tmp;
            buf->buflen = gBufferSize;

            InsertPendingSend(sock, sendobj);

            if (DoSends(sock) != NO_ERROR)
            {
                error = SOCKET_ERROR;
            }
            else
            {
                // Post another receive
                if (PostRecv(sock, buf) != NO_ERROR)
                {
                    // In the event the recv fails, clean up the connection
                    FreeBufferObj(buf);
                    error = SOCKET_ERROR;
                }
            }
        }
        else
        {
            dbgprint("Got 0 byte receive\n");

            // Graceful close - the receive returned 0 bytes read
            sock->bClosing = TRUE;

            // Free the receive buffer
            FreeBufferObj(buf);

            if (DoSends(sock) != NO_ERROR)
            {
                dbgprint("0: cleaning up in zero byte handler\n");
                error = SOCKET_ERROR;
            }

            // If this was the last outstanding operation on socket, clean it up
            if ((sock->OutstandingOps == 0) && (sock->OutOfOrderSends == NULL))
            {
                dbgprint("1: cleaning up in zero byte handler\n");
                bCleanupSocket = TRUE;
            }
        }
    }
    else if ((buf->operation == OP_READ) && (error != NO_ERROR) && (gProtocol == IPPROTO_UDP))
    {
        // If for UDP, a receive completes with an error, we ignore it and re-post the recv
        if (PostRecv(sock, buf) != NO_ERROR)
        {
            error = SOCKET_ERROR;
        }
    }
    else if (buf->operation == OP_WRITE)
    {
        // Update the counters
        InterlockedExchangeAdd(&gBytesSent, BytesTransfered);
        InterlockedExchangeAdd(&gBytesSentLast, BytesTransfered);

        FreeBufferObj(buf);

        if (DoSends(sock) != NO_ERROR)
        {
            dbgprint("Cleaning up inside OP_WRITE handler\n");
            error = SOCKET_ERROR;
        }
    }

    if (error != NO_ERROR)
    {
        sock->bClosing = TRUE;
    }

    //
    // Check to see if socket is closing
    //
    if ( (InterlockedDecrement(&sock->OutstandingOps) == 0) &&
         (sock->bClosing) &&
         (sock->OutOfOrderSends == NULL) )
    {
        bCleanupSocket = TRUE;
    }
    else
    {
        if (DoSends(sock) != NO_ERROR)
        {
            bCleanupSocket = TRUE;
        }
    }

    LeaveCriticalSection(&sock->SockCritSec);

    if (bCleanupSocket)
    {
        closesocket(sock->s);
        sock->s = INVALID_SOCKET;

        FreeSocketObj(sock);
    }

    return;
}

//
// Function: CompletionThread
// 
// Description:
//    This is the completion thread which services our completion port. One of
//    these threads is created per processor on the system. The thread sits in 
//    an infinite loop calling GetQueuedCompletionStatus and handling socket
//    IO that completed.
//
DWORD WINAPI CompletionThread(LPVOID lpParam)
{
    SOCKET_OBJ  *sockobj=NULL;          // Per socket object for completed I/O
    BUFFER_OBJ  *bufobj=NULL;           // Per I/O object for completed I/O
    OVERLAPPED  *lpOverlapped=NULL;     // Pointer to overlapped structure for completed I/O
    HANDLE       CompletionPort;        // Completion port handle
    DWORD        BytesTransfered,       // Number of bytes transfered
                 Flags;                 // Flags for completed I/O
    int          rc, 
                 error;

    CompletionPort = (HANDLE)lpParam;
    while (1)
    {
        error = NO_ERROR;
        rc = GetQueuedCompletionStatus(
                CompletionPort,
               &BytesTransfered,
                (PULONG_PTR)&sockobj,
               &lpOverlapped,
                INFINITE
                );

        bufobj = CONTAINING_RECORD(lpOverlapped, BUFFER_OBJ, ol);

        if (rc == FALSE)
        {
            // If the call fails, call WSAGetOverlappedResult to translate the
            //    error code into a Winsock error code.
            dbgprint("CompletionThread: GetQueuedCompletionStatus failed: %d\n",
                    GetLastError());
            rc = WSAGetOverlappedResult(
                    sockobj->s,
                   &bufobj->ol,
                   &BytesTransfered,
                    FALSE,
                   &Flags
                    );
            if (rc == FALSE)
            {
                error = WSAGetLastError();
            }
        }
        // Handle the IO operation
        HandleIo(sockobj, bufobj, CompletionPort, BytesTransfered, error);
    }

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
    SYSTEM_INFO      sysinfo;
    SOCKET_OBJ      *sockobj=NULL,
                    *ListenSockets=NULL;
    HANDLE           CompletionPort,
                     CompThreads[MAX_COMPLETION_THREAD_COUNT],
                     hrc;
    int              endpointcount=0,
                     interval,
                     rc,
                     i;
    struct addrinfo *res=NULL,
                    *ptr=NULL;

    // Validate the command line
    ValidateArgs(argc, argv);

    // Load Winsock
    if (WSAStartup(MAKEWORD(2,2), &wsd) != 0)
    {
        fprintf(stderr, "unable to load Winsock!\n");
        return -1;
    }

    // Create the completion port used by this server
    CompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, (ULONG_PTR)NULL, 0);
    if (CompletionPort == NULL)
    {
        fprintf(stderr, "CreateIoCompletionPort failed: %d\n", GetLastError());
        return -1;
    }

    // Find out how many processors are on this system
    GetSystemInfo(&sysinfo);

    if (sysinfo.dwNumberOfProcessors > MAX_COMPLETION_THREAD_COUNT)
    {
        sysinfo.dwNumberOfProcessors = MAX_COMPLETION_THREAD_COUNT;
    }
    
    // Create the worker threads to service the completion notifications
    for(i=0; i < (int)sysinfo.dwNumberOfProcessors ;i++)
    {
        CompThreads[i] = CreateThread(NULL, 0, CompletionThread, (LPVOID)CompletionPort, 0, NULL);
        if (CompThreads[i] == NULL)
        {
            fprintf(stderr, "CreatThread failed: %d\n", GetLastError());
            return -1;
        }
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

        sockobj = GetSocketObj(INVALID_SOCKET, ptr->ai_family);

        // create the socket
        sockobj->s = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sockobj->s == INVALID_SOCKET)
        {
            fprintf(stderr,"socket failed: %d\n", WSAGetLastError());
            return -1;
        }

        // Associate the socket and its SOCKET_OBJ to the completion port
        hrc = CreateIoCompletionPort((HANDLE)sockobj->s, CompletionPort, (ULONG_PTR)sockobj, 0);
        if (hrc == NULL)
        {
            fprintf(stderr, "CreateIoCompletionPort failed: %d\n", GetLastError());
            return -1;
        }

        // bind the socket to a local address and port
        rc = bind(sockobj->s, ptr->ai_addr, ptr->ai_addrlen);
        if (rc == SOCKET_ERROR)
        {
            fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
            return -1;
        }

        if (gProtocol == IPPROTO_TCP)
        {
            BUFFER_OBJ *acceptobj=NULL;
            GUID        guidAcceptEx = WSAID_ACCEPTEX,
                        guidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
            DWORD       bytes;

            // Need to load the Winsock extension functions from each provider
            //    -- e.g. AF_INET and AF_INET6. 
            rc = WSAIoctl(
                    sockobj->s,
                    SIO_GET_EXTENSION_FUNCTION_POINTER,
                   &guidAcceptEx,
                    sizeof(guidAcceptEx),
                   &sockobj->lpfnAcceptEx,
                    sizeof(sockobj->lpfnAcceptEx),
                   &bytes,
                    NULL,
                    NULL
                    );
            if (rc == SOCKET_ERROR)
            {
                fprintf(stderr, "WSAIoctl: SIO_GET_EXTENSION_FUNCTION_POINTER failed: %d\n",
                        WSAGetLastError());
                return -1;
            }
            rc = WSAIoctl(
                    sockobj->s,
                    SIO_GET_EXTENSION_FUNCTION_POINTER,
                   &guidGetAcceptExSockaddrs,
                    sizeof(guidGetAcceptExSockaddrs),
                   &sockobj->lpfnGetAcceptExSockaddrs,
                    sizeof(sockobj->lpfnGetAcceptExSockaddrs),
                   &bytes,
                    NULL,
                    NULL
                    );
            if (rc == SOCKET_ERROR)
            {
                fprintf(stderr, "WSAIoctl: SIO_GET_EXTENSION_FUNCTION_POINTER faled: %d\n",
                        WSAGetLastError());
                return -1;
            }

            // For TCP sockets, we need to "listen" on them
            rc = listen(sockobj->s, 100);
            if (rc == SOCKET_ERROR)
            {
                fprintf(stderr, "listen failed: %d\n", WSAGetLastError());
                return -1;
            }

            // Keep track of the pending AcceptEx operations
            sockobj->PendingAccepts = (BUFFER_OBJ **)HeapAlloc(
                    GetProcessHeap(), 
                    HEAP_ZERO_MEMORY, 
                    (sizeof(BUFFER_OBJ *) * gOverlappedCount));
            if (sockobj->PendingAccepts == NULL)
            {
                fprintf(stderr, "HeapAlloc failed: %d\n", GetLastError());
                ExitProcess(-1);
            }

            // Post the AcceptEx(s)
            for(i=0; i < gOverlappedCount ;i++)
            {
                sockobj->PendingAccepts[i] = acceptobj = GetBufferObj(sockobj, gBufferSize);

                PostAccept(sockobj, acceptobj);
            }
            //
            // Maintain a list of the listening socket structures
            //
            if (ListenSockets == NULL)
            {
                ListenSockets = sockobj;
            }
            else
            {
                sockobj->next = ListenSockets;
                ListenSockets = sockobj;
            }
        }
        else
        {
            BUFFER_OBJ *recvobj=NULL;
            DWORD       bytes;
            int         optval;

            // Turn off UDP errors resulting from ICMP messages (port/host unreachable, etc)
            optval = 0;
            rc = WSAIoctl(
                    sockobj->s,
                    SIO_UDP_CONNRESET,
                   &optval,
                    sizeof(optval),
                    NULL,
                    0,
                   &bytes,
                    NULL,
                    NULL
                    );
            if (rc == SOCKET_ERROR)
            {
                fprintf(stderr, "WSAIoctl: SIO_UDP_CONNRESET failed: %d\n", 
                        WSAGetLastError());
            }

            // For UDP, simply post some receives
            for(i=0; i < gOverlappedCount ;i++)
            {
                recvobj = GetBufferObj(sockobj, gBufferSize);

                PostRecv(sockobj, recvobj);
            }
        }

        endpointcount++;
        ptr = ptr->ai_next;
    }
    // free the addrinfo structure for the 'bind' address
    freeaddrinfo(res);

    gStartTime = gStartTimeLast = GetTickCount();

    interval = 0;
    while (1)
    {
        rc = WSAWaitForMultipleEvents(
                sysinfo.dwNumberOfProcessors,
                CompThreads,
                TRUE,
                5000,
                FALSE
                );
        if (rc == WAIT_FAILED)
        {
            fprintf(stderr, "WSAWaitForMultipleEvents failed: %d\n", WSAGetLastError());
            break;
        }
        else if (rc == WAIT_TIMEOUT)
        {
            interval++;

            PrintStatistics();

            if (interval == 12)
            {
                SOCKET_OBJ  *listenptr=NULL;
                int          optval,
                optlen;

                // For TCP, cycle through all the outstanding AcceptEx operations
                //   to see if any of the client sockets have been connected but
                //   haven't received any data. If so, close them as they could be
                //   a denial of service attack.
                listenptr = ListenSockets;
                while (listenptr)
                {
                    for(i=0; i < gOverlappedCount ;i++)
                    {
                        optlen = sizeof(optval);
                        rc = getsockopt(
                                listenptr->PendingAccepts[i]->sclient,
                                SOL_SOCKET,
                                SO_CONNECT_TIME,
                                (char *)&optval,
                                &optlen
                                       );
                        if (rc == SOCKET_ERROR)
                        {
                            fprintf(stderr, "getsockopt: SO_CONNECT_TIME failed: %d\n",
                                    WSAGetLastError());
                            return -1;
                        }
                        // If the socket has been connected for more than 5 minutes,
                        //    close it. If closed, the AcceptEx call will fail in the
                        //    completion thread.
                        if ((optval != 0xFFFFFFFF) && (optval > 300))
                        {
                            closesocket(listenptr->PendingAccepts[i]->sclient);
                        }
                    }
                    listenptr = listenptr->next;
                }
                interval = 0;
            }
        }
    }

    WSACleanup();
    return 0;
}
