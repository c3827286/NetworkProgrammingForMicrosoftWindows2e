//
// Sample: Overlapped IPv4/IPv6 Server
//
// Files:
//      overserver.cpp    - this file
//      resolve.cpp       - Common name resolution routines
//      resolve.h         - Header file for name resolution routines
//
// Description:
//      This sample illustrates simple overlapped IO for TCP and UDP for
//      both IPv4 and IPv6. This sample uses the getaddrinfo/getnameinfo
//      APIs which allows this application to be IP agnostic. That is the
//      desired address family (AF_INET or AF_INET6) can be determined
//      simply from the string address passed via the -l command.
//
//      For TCP, this sample creates a listening socket for each address family 
//      available.  For each socket, a number of AcceptEx are posted.
//      We use one event handle for each overlapped operation. This means
//      that we assign an individual I/O operation to each worker thread 
//      at a time (not on a socket by socket basis). As such, it is possible
//      that some reads on a socket may be handled by one worker thread and
//      some other reads and writes for the same socket may be handled by 
//      another thread.
//
//      Once an AcceptEx completes, a new socket object is created which 
//      initiates a number of overlapped receives on the accepted connection.
//      Once a read completes, a write is posted back to that client and the
//      read is re-posted.
//
//      For UDP, a socket is created for each IP address family; however, 
//      a number of reads are immediately posted on each instead of accepts.
//      Once the UDP read is completed, a send back to the source is posted
//      and the receive is re-posted.
//      
//      For example:
//          If this sample is called with the following command lines:
//              overserver.exe -l fe80::2efe:1234 -e 5150
//              overserver.exe -l ::
//          Then the server creates an IPv6 socket as an IPv6 address was
//          provided.
//
//          On the other hand, with the following command line:
//              overserver.exe -l 7.7.7.1 -e 5150
//              overserver.exe -l 0.0.0.0
//          Then the server creates an IPv4 socket.
//          
//          Calling this sample with no arguments creates both IPv4 and IPv6
//              (if installed) servers.
//
// Compile:
//      cl -o overserver.exe overserver.cpp resolve.cpp ws2_32.lib
//
// Usage:
//      overserver.exe [options]
//          -a 4|6     Address family, 4 = IPv4, 6 = IPv6 [default = IPv4]
//          -b size    Size of send/recv buffer in bytes
//          -e port    Port number
//          -l addr    Local address to bind to [default INADDR_ANY for IPv4 or INADDR6_ANY for IPv6]
//          -p proto   Which protocol to use [default = TCP]
//             tcp         Use TCP protocol
//             udp         Use UDP protocol
//

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "resolve.h"

#define DEFAULT_BUFFER_SIZE      4096   // default buffer size
#define DEFAULT_OVERLAPPED_COUNT 5      // default number of overlapped recvs to post

int gAddressFamily = AF_UNSPEC,         // default to unspecified
    gSocketType    = SOCK_STREAM,       // default to TCP socket type
    gProtocol      = IPPROTO_TCP,       // default to TCP protocol
    gBufferSize    = DEFAULT_BUFFER_SIZE,
    gOverlappedCount = DEFAULT_OVERLAPPED_COUNT;

char *gBindAddr    = NULL,              // local interface to bind to
     *gBindPort    = "5150";            // local port to bind to

struct _SOCKET_OBJ;
struct _THREAD_OBJ;

//
// This is our per I/O data. It describes a single I/O operation.
//
typedef struct _BUFFER_OBJ
{
    WSAOVERLAPPED        ol;            // Overlapped structure

    SOCKET               sclient;       // Used for AcceptEx client socket

    char                *buf;           // Buffer for send/recv/AcceptEx
    int                  buflen;        // Length of the buffer

    int                  operation;     // Type of operation submitted
#define OP_ACCEPT       0                   // AcceptEx
#define OP_READ         1                   // WSARecv/WSARecvFrom
#define OP_WRITE        2                   // WSASend?WSASendTo

    struct _SOCKET_OBJ *Socket;         // SOCKET_OBJ that this I/O belongs to
    struct _THREAD_OBJ *Thread;         // THREAD_OBJ this I/O is assigned to

    SOCKADDR_STORAGE     addr;          // Remote address (UDP)
    int                  addrlen;       // Remote address length

    struct _BUFFER_OBJ *next,
                       *prev;
} BUFFER_OBJ;

//
// This is our per handle data. One of these structures is allocated for
//    each socket created by our server.
//
typedef struct _SOCKET_OBJ
{
    SOCKET               s;             // Socket handle for client connection

    int                  af,            // Address family of socket (AF_INET or AF_INET6)
                         bClosing;      // Indicates socket is closing

    volatile LONG        OutstandingOps;    // Number of outstanding overlapped ops

    BUFFER_OBJ         **PendingAccepts;    // Array of pending AcceptEx calls (listening socket only)

    // Pointers to Microsoft specific extensions (listening socket only)
    LPFN_ACCEPTEX        lpfnAcceptEx;
    LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs;

    CRITICAL_SECTION     SockCritSec;   // Synchronize access to this SOCKET_OBJ

    struct _SOCKET_OBJ  *next;          // Used to chain SOCKET_OBJ together
} SOCKET_OBJ;

//
// Allocated for each thread spawned. Each overlapped I/O issued on a socket is
//    assigned to one of the threads in the thread pool.
//
typedef struct _THREAD_OBJ
{
    BUFFER_OBJ *BufferList;            // Linked list of all sockets allocated

    int         EventCount;            // How many events are in the array to wait on?

    HANDLE      Event;                 // Used to signal new clients assigned
                                       //  to this thread
    HANDLE      Thread;                // Handle to the curren thread

    HANDLE      Handles[MAXIMUM_WAIT_OBJECTS]; // Array of socket's event handles

    CRITICAL_SECTION ThreadCritSec;    // Protect access to SOCKET_OBJ lists

    struct _THREAD_OBJ *next;          // Next thread object in list
} THREAD_OBJ;

THREAD_OBJ *gChildThreads=NULL;        // List of thread objects allocated
int         gChildThreadsCount=0;      // Number of child threads created

CRITICAL_SECTION gThreadListCritSec;

//
// Statistics counters
//
volatile LONG gBytesRead=0,
              gBytesSent=0,
              gStartTime=0,
              gBytesReadLast=0,
              gBytesSentLast=0,
              gStartTimeLast=0,
              gTotalConnections=0,
              gCurrentConnections=0;

//
// Prototypes
//
void AssignIoToThread(BUFFER_OBJ *buf);
void RemoveBufferFromThread(SOCKET_OBJ *sock, BUFFER_OBJ *buf);
void InsertBufferObj(BUFFER_OBJ **head, BUFFER_OBJ *obj);

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
//    Allocate a BUFFER_OBJ. Each send, receive, and accept posted by a 
//    by the server uses one of these objects. That is, there is one BUFFER_OBJ
//    allocated per I/O operation. After the I/O is initiated it is assigned to
//    one of the completion threads. To increase performance, a look aside list
//    may be used to cache freed BUFFER_OBJ.
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

    newobj->Socket = sock;

    // Create the event that is to be signed upon completion
    newobj->ol.hEvent = WSACreateEvent();
    if (newobj->ol.hEvent == NULL)
    {
        fprintf(stderr, "GetBufferObj: WSACreateEvent failed: %d\n", WSAGetLastError());
        ExitProcess(-1);
    }

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
    // Close the event
    WSACloseEvent(obj->ol.hEvent);
    obj->ol.hEvent = NULL;
    // Free the buffers
    HeapFree(GetProcessHeap(), 0, obj->buf);
    HeapFree(GetProcessHeap(), 0, obj);
}

//
// Function: GetSocketObj
//
// Description:
//    Allocate a socket object and initialize its members. A socket object is
//    allocated for each socket created (either by socket or accept).
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

    InitializeCriticalSection(&sockobj->SockCritSec);

    return sockobj;
}

//
// Function: FreeSocketObj
//
// Description:
//    Frees a socket object along.
//
void FreeSocketObj(SOCKET_OBJ *obj)
{
    BUFFER_OBJ  *ptr=NULL,
                *tmp=NULL;


    if (obj->OutstandingOps != 0)
    {
        return;
    }
    if (obj->s != INVALID_SOCKET)
    {
        closesocket(obj->s);
        obj->s = INVALID_SOCKET;
    }

    DeleteCriticalSection(&obj->SockCritSec);

    HeapFree(GetProcessHeap(), 0, obj);
}

//
// Function: GetThreadObj
//
// Description:
//    Allocate a thread object and initializes its members.
//
THREAD_OBJ *GetThreadObj()
{
    THREAD_OBJ *thread=NULL;

    thread = (THREAD_OBJ *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(THREAD_OBJ));
    if (thread == NULL)
    {
        fprintf(stderr, "GetThreadObj: HeapAlloc failed: %d\n", GetLastError());
        ExitProcess(-1);
    }

    // Create an event that the thread will wait on. When signaled, the thread
    //    rebuilds the array of event handles.
    thread->Event = WSACreateEvent();
    if (thread->Event == NULL)
    {
        fprintf(stderr, "GetThreadObj: WSACreateEvent failed: %d\n", WSAGetLastError());
        ExitProcess(-1);
    }

    // The first event is always the thread's event
    thread->Handles[0] = thread->Event;

    thread->EventCount = 1;

    InitializeCriticalSection(&thread->ThreadCritSec);

    return thread;
}

//
// Function: FreeThreadObj
//
// Description:
//    Free a thread object and is member fields.
//
void FreeThreadObj(THREAD_OBJ *thread)
{
    WSACloseEvent(thread->Event);

    CloseHandle(thread->Thread);

    DeleteCriticalSection(&thread->ThreadCritSec);

    HeapFree(GetProcessHeap(), 0, thread);
}

//
// Function: InsertBufferObjToThread
//
// Description:
//    Insert a buffer object into the list of pending buffers objects for
//    the given thread object. If the buffer can fit in the thread's queue,
//    NO_ERROR is returned. If the thread is already waiting on the maximum
//    allowable events, then SOCKET_ERROR is returned.
//
int InsertBufferObjToThread(THREAD_OBJ *thread, BUFFER_OBJ *buf)
{
    int     ret;

    EnterCriticalSection(&thread->ThreadCritSec);

    // See if the thread is full
    if (thread->EventCount < MAXIMUM_WAIT_OBJECTS-1)
    {
        InsertBufferObj(&thread->BufferList, buf);

        thread->Handles[thread->EventCount++] = buf->ol.hEvent;

        ret = NO_ERROR;
    }
    else
    {
        ret = SOCKET_ERROR;
    }

    LeaveCriticalSection(&thread->ThreadCritSec);

    return ret;
}

//
// Function: RenumberEvents
//
// Description:
//    This routine goes through the list of pending buffers within a thread
//    and rebuilds the array of event handles that the thread waits on. When
//    a new connection is accepted and several receive operations are posted,
//    they are assigned to a thread and the thread is signaled to indicate
//    new I/O has been placed in its queue. The thread needs to reinitialize
//    its array so that it may be signaled for completion on that new I/O.
//
void RenumberEvents(THREAD_OBJ *thread)
{
    BUFFER_OBJ *bptr=NULL;
    int         i;

    //
    // If index 0 is signaled then rebuild the array of event
    //    handles to wait on
    EnterCriticalSection(&thread->ThreadCritSec);
    i = 0;
    bptr = thread->BufferList;
    thread->EventCount  = 1;
    while (bptr)
    {
        thread->Handles[thread->EventCount++] = bptr->ol.hEvent;

        i++;
        bptr = bptr->next;
    }
    LeaveCriticalSection(&thread->ThreadCritSec);
}

//
// Function: InsertBufferObj
//
// Description:
//   This routine inserts a BUFFER_OBJ into a list of BUFFER_OBJs.
//   First the end of the list is found and then the new buffer is
//   added to the end.
//
void InsertBufferObj(BUFFER_OBJ **head, BUFFER_OBJ *obj)
{
    BUFFER_OBJ *end=NULL, 
               *ptr=NULL;

    // Find the end of the list
    ptr = *head;
    if (ptr)
    {
        while (ptr->next)
            ptr = ptr->next;
        end = ptr;
    }

    obj->next = NULL;
    obj->prev = end;

    if (end == NULL)
    {
        // List is empty
        *head = obj;
    }
    else
    {
        // Put new object at the end 
        end->next = obj;
        obj->prev = end;
    }
}

// 
// Function: RemoveBufferObj
//
// Description:
//    Remove a BUFFER_OBJ from the list.
//
BUFFER_OBJ *RemoveBufferObj(BUFFER_OBJ **head, BUFFER_OBJ *buf)
{
    // Make sure list isn't empty
    if (*head != NULL)
    {
        // Fix up the next and prev pointers
        if (buf->prev)
            buf->prev->next = buf->next;
        if (buf->next)
            buf->next->prev = buf->prev;

        if (*head == buf)
            (*head) = buf->next;
    }

    return buf;
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

    printf("Total Connections  : %lu\n", gTotalConnections);
    printf("Current Connections: %lu\n", gCurrentConnections);

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
int PostRecv(BUFFER_OBJ *recvobj)
{
    WSABUF  wbuf;
    DWORD   bytes,
            flags;
    int     rc=NO_ERROR;


    EnterCriticalSection(&recvobj->Socket->SockCritSec);

    recvobj->operation = OP_READ;

    wbuf.buf = recvobj->buf;
    wbuf.len = recvobj->buflen;

    flags = 0;

    if (gProtocol == IPPROTO_TCP)
    {
        rc = WSARecv(
                recvobj->Socket->s,
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
                recvobj->Socket->s,
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
    if (rc == SOCKET_ERROR)
    {
        rc = NO_ERROR;
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            fprintf(stderr, "PostRecv: WSARecv* failed: %d\n", WSAGetLastError());
            rc = SOCKET_ERROR;
        }
    }

    // Increment outstanding overlapped operations
    InterlockedIncrement(&recvobj->Socket->OutstandingOps);

    LeaveCriticalSection(&recvobj->Socket->SockCritSec);

    return NO_ERROR;
}

//
// Function: PostSend
//
// Description:
//    Post an overlapped send operation on the socket.
//
int PostSend(BUFFER_OBJ *sendobj)
{
    WSABUF  wbuf;
    DWORD   bytes;
    int     rc;

    rc = NO_ERROR;

    sendobj->operation = OP_WRITE;

    wbuf.buf = sendobj->buf;
    wbuf.len = sendobj->buflen;

    EnterCriticalSection(&sendobj->Socket->SockCritSec);
    if (gProtocol == IPPROTO_TCP)
    {
        rc = WSASend(
                sendobj->Socket->s,
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
                sendobj->Socket->s,
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
    if (rc == SOCKET_ERROR)
    {
        rc = NO_ERROR;
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            fprintf(stderr, "PostSend: WSASend* failed: %d\n", WSAGetLastError());
            rc = SOCKET_ERROR;
        }
    }

    // Increment the outstanding operation count
    InterlockedIncrement(&sendobj->Socket->OutstandingOps);

    LeaveCriticalSection(&sendobj->Socket->SockCritSec);

    return rc;
}

//
// Function: PostAccept
//
// Description:
//    Post an overlapped accept on a listening socket.
//
int PostAccept(BUFFER_OBJ *acceptobj)
{
    DWORD   bytes;
    int     rc=NO_ERROR;

    acceptobj->operation = OP_ACCEPT;

    EnterCriticalSection(&acceptobj->Socket->SockCritSec);

    // Create the client socket for an incoming connection
    acceptobj->sclient = socket(acceptobj->Socket->af, SOCK_STREAM, IPPROTO_TCP);
    if (acceptobj->sclient == INVALID_SOCKET)
    {
        fprintf(stderr, "PostAccept: socket failed: %d\n", WSAGetLastError());
        return -1;
    }

    rc = acceptobj->Socket->lpfnAcceptEx(
            acceptobj->Socket->s,
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
        rc = NO_ERROR;
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            fprintf(stderr, "PostAccept: AcceptEx failed: %d\n",
                    WSAGetLastError());
            rc = SOCKET_ERROR;
        }
    }

    // Increment the outstanding overlapped count for this socket
    InterlockedIncrement(&acceptobj->Socket->OutstandingOps);

    LeaveCriticalSection(&acceptobj->Socket->SockCritSec);

    return rc;
}

//
// Function: HandleIo
//
// Description:
//    This function handles the IO on a socket. First, the events signaled
//    on the socket are enuemrated, then the appropriate handler routine
//    for the event is called.
//
void HandleIo(BUFFER_OBJ *buf)
{
    SOCKET_OBJ *sock=NULL,
               *clientobj=NULL;     // New client object for accepted connections
    BUFFER_OBJ *recvobj=NULL,       // Used to post new receives on accepted connections
               *sendobj=NULL;       // Used to post sends for data received
    DWORD       bytes,
                flags;
    BOOL        bFreeSocketObj;
    int         error,
                rc;

    // Extract the SOCKET_OBJ from the BUFFER_OBJ for easy reference
    sock = buf->Socket;
    error = NO_ERROR;

    bFreeSocketObj = FALSE;

    InterlockedDecrement(&sock->OutstandingOps);

    // Get the results of the overlapped operation that completed
    rc = WSAGetOverlappedResult(
            sock->s,
           &buf->ol,
           &bytes,
            FALSE,
           &flags
            );
    if (rc == FALSE)
    {
        error = WSAGetLastError();

        fprintf(stderr, "HandleIo: WSAGetOverlappedResult failed: %d\n", error);
        if (gProtocol == IPPROTO_TCP)
        {
            // An error occured on a TCP socket, so remove this I/O and if no
            //    more I/O is outstanding, free the socket object. Otherwise,
            //    wait for the remaining I/O on this socket to complete as well.
            RemoveBufferFromThread(sock, buf);
            FreeBufferObj(buf);

            if (InterlockedDecrement(&sock->OutstandingOps) == 0)
            {
                printf("Freeing socket obj in GetOverlapepdResult\n");
                FreeSocketObj(sock);
            }

            return;
        }
    }

    if (buf->operation == OP_ACCEPT)
    {
        SOCKADDR_STORAGE *LocalSockaddr=NULL,
                         *RemoteSockaddr=NULL;
        int               LocalSockaddrLen,
                          RemoteSockaddrLen;


        //  Update the counters
        InterlockedExchangeAdd(&gBytesRead, bytes);
        InterlockedExchangeAdd(&gBytesReadLast, bytes);

        InterlockedIncrement(&gTotalConnections);
        InterlockedIncrement(&gCurrentConnections);

        //  Retrieve the client's address
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

        // Under high connection stress this print slows things down

        /*
        printf("Received connection from: ");
        PrintAddress((SOCKADDR *)RemoteSockaddr, RemoteSockaddrLen);
        printf("\n");
        */

        // Create a new SOCKET_OBJ for client socket
        clientobj = GetSocketObj(buf->sclient, buf->Socket->af);

        // Echo back any data received with the AcceptEx call
        sendobj = GetBufferObj(clientobj, gBufferSize);

        // Copy the data from the accept buffer to the send buffer
        sendobj->buflen = bytes;
        memcpy(sendobj->buf, buf->buf, bytes);

        // Assign the send operation to a thread
        AssignIoToThread(sendobj);

        // Initiate the overlapped send
        if (PostSend(sendobj) != NO_ERROR)
        {
            // In the event of an error, clean up the socket object
            RemoveBufferFromThread(clientobj, sendobj);
            FreeBufferObj(sendobj);

            closesocket(clientobj->s);
            clientobj->s = INVALID_SOCKET;

            FreeSocketObj(clientobj);
        }
        PostAccept(buf);
    }
    else if ((buf->operation == OP_READ) && (error == NO_ERROR))
    {
        //
        // Receive compeleted successfully
        //
        if ((bytes > 0) || (gProtocol == IPPROTO_UDP))
        {
            // Increment the counters
            InterlockedExchangeAdd(&gBytesRead, bytes);
            InterlockedExchangeAdd(&gBytesReadLast, bytes);

            // Create a buffer to send
            sendobj = buf;

            sendobj->buflen = bytes;

            // Initiate the send 
            if (PostSend(sendobj) != NO_ERROR)
            {
                // In the event of an error, clean up the socket object
                RemoveBufferFromThread(sock, sendobj);
                FreeBufferObj(sendobj);

                closesocket(sock->s);
                sock->s = INVALID_SOCKET;

                bFreeSocketObj = TRUE;
            }
        }
        else
        {
            // Graceful close
            sock->bClosing = TRUE;

            // Free the completed operation
            RemoveBufferFromThread(sock, buf);
            FreeBufferObj(buf);

            // Check to see if there are more outstanding operations. If so, wait
            //    for them to complete; otherwise, clean up the socket object.
            EnterCriticalSection(&sock->SockCritSec);
            if (sock->OutstandingOps == 0)
            {
                closesocket(sock->s);

                bFreeSocketObj = TRUE;
            }
            LeaveCriticalSection(&sock->SockCritSec);
        }
    }
    else if ((buf->operation == OP_READ) && (error != NO_ERROR) && (gProtocol == IPPROTO_UDP))
    {
        // If a UDP receive failed, we really don't care. Just re-post it - that is
        //    we probably got an ICMP error.
        if (PostRecv(buf) != NO_ERROR)
        {
            // In the event of an error, clean up the socket object
            RemoveBufferFromThread(sock, buf);
            FreeBufferObj(buf);

            closesocket(sock->s);
            sock->s = INVALID_SOCKET;

            bFreeSocketObj = TRUE;
        }
    }
    else if (buf->operation == OP_WRITE)
    {
        // Increment the counters
        InterlockedExchangeAdd(&gBytesSent, bytes);
        InterlockedExchangeAdd(&gBytesSentLast, bytes);

        // See if the socket is closing, if so check to see if there are any outstanding
        //    operations. If not, clean up the connection; othewise wait for them
        //    to complete.
        EnterCriticalSection(&sock->SockCritSec);
        if (sock->bClosing && (sock->OutstandingOps == 0))
        {
            RemoveBufferFromThread(sock, buf);
            closesocket(sock->s);
            FreeBufferObj(buf);

            bFreeSocketObj = TRUE;
        }
        else
        {
            buf->buflen = gBufferSize;

            // Free the send op that just completed
            if (PostRecv(buf) != NO_ERROR)
            {
                RemoveBufferFromThread(sock, buf);
                FreeBufferObj(buf);
            }
        }
        LeaveCriticalSection(&sock->SockCritSec);
    }

    if (bFreeSocketObj)
    {   
        FreeSocketObj(sock);
    }

    return;
}

//
// Function: FindBufferObj
//
// Description:
//    When I/O is assigned to a thread, the thread iterates through a list
//    of BUFFER_OBJ and picks out the event assoicated with that operation.
//    When the operation completes, the worker thread must get the BUFFER_OBJ
//    corresponded to the signaled event. This routine searches through the
//    list of BUFFER_OBJ of a thread and returns the one that corresponds
//    to the signaled event.
//
BUFFER_OBJ *FindBufferObj(THREAD_OBJ *thread, WSAEVENT hEvent)
{
    BUFFER_OBJ *ptr=NULL;

    EnterCriticalSection(&thread->ThreadCritSec);

    ptr = thread->BufferList;
    while (ptr)
    {
        if (ptr->ol.hEvent == hEvent)
            break;
        ptr = ptr->next;
    }

    LeaveCriticalSection(&thread->ThreadCritSec);

    return ptr;
}

//
// Function: IoThread
//
// Description:
//    This is the I/O thread spawned to handle overlapped requests. When an
//    overlapped operation is initialized, the I/O is first asisgned to a 
//    worker thread. This is the worker thread that waits for I/O to complete.
//    Once an I/O operation is assigned to a thread, the thread's event is 
//    signaled which causes the thread to initialize its list of pending
//    overlapped event handles to include any new operations assigned to it.
//    Once one of the overlapepd I/O events is signaled, the thread calls the
//    I/O handler routine to handle that particular operation and perform
//    the necessariy steps.
//
DWORD WINAPI IoThread(LPVOID lpParam)
{
    THREAD_OBJ      *thread=NULL;
    int              index,
                     count,
                     rc,
                     i;

    thread = (THREAD_OBJ *)lpParam;

    // Initialize the event list to start with
    RenumberEvents(thread);

    while (1)
    {
        // Wait on the events
        rc = WaitForMultipleObjects(
                thread->EventCount,
                thread->Handles,
                FALSE,
                INFINITE
                );
        if (rc == WAIT_FAILED || rc == WAIT_TIMEOUT)
        {
            if (GetLastError() == ERROR_INVALID_HANDLE)
            {
                RenumberEvents(thread);
                continue;
            }
            else
            {
                fprintf(stderr, "IoThread: WaitForMultipleObjects failed: %d\n",
                        GetLastError());
                break;
            }
        }

        // Iterate through the events to see if more than one were signaled
        count = thread->EventCount;
        for(i=0; i < count ;i++)
        {

            rc = WaitForSingleObject(
                    thread->Handles[i],
                    0
                    );
            if (rc == WAIT_TIMEOUT)
            {
                // This event wasn't signaled continue to the next one
                continue;
            }
            index = i;

            // Reset the event first
            WSAResetEvent(thread->Handles[index]);

            if (index == 0)
            {
                // The thread's event was signaled indicating new I/O assigned
                RenumberEvents(thread);
                break;
            }
            else
            {
                // Otherwise, an overlapped I/O operation completed, service it
                HandleIo(FindBufferObj(thread, thread->Handles[index]));
            }
        }
    }

    ExitThread(0);
    return 0;
}

//
// Function: AssignIoToThread
//
// Description:
//    This routine assigns a socket connection to an available child
//    thread to handle any IO on it. If no threads are available, a 
//    new thread is spawned to handle the connection.
//
void AssignIoToThread(BUFFER_OBJ *buf)
{
    THREAD_OBJ *thread=NULL;

    EnterCriticalSection(&gThreadListCritSec);

    thread = gChildThreads;
    while (thread)
    {
        // If this routine returns something other than SOCKET_ERROR
        //    that it was successfully assigned to a child thread.
        if (InsertBufferObjToThread(thread, buf) == NO_ERROR)
        {
            break;
        }

        thread = thread->next;
    }

    if (thread == NULL)
    {
        // No thread was found to assign the client socket to, create a new thread
        //
        thread = GetThreadObj();

        thread->Thread = CreateThread(NULL, 0, IoThread, (LPVOID)thread, 0, NULL);
        if (thread->Thread == NULL)
        {
            fprintf(stderr, "AssignToFreeThread: CreateThread failed: %d\n", GetLastError());
            ExitProcess(-1);
        }

        // Assign operation to a free thread
        InsertBufferObjToThread(thread, buf);

        // Insert the thread the list of threads
        if (gChildThreads == NULL)
        {
            gChildThreads = thread;
        }
        else
        {
            thread->next = gChildThreads;
            gChildThreads = thread;
        }

        gChildThreadsCount++;

    }

    buf->Thread = thread;
 
    // signal child thread to rebuild the event list
    WSASetEvent(thread->Event);

    LeaveCriticalSection(&gThreadListCritSec);

    return;
}

//
// Function: RemoveBufferFromThread
//
// Description:
//    This routine removes the specified BUFFER_OBJ from a THREAD_OBJ's
//    list of pending overlapped operations. Once the object is removed,
//    the thread's event is signaled to force the thread to re-initialize
//    it's list of pending events.
//
void RemoveBufferFromThread(SOCKET_OBJ *sock, BUFFER_OBJ *buf)
{
    EnterCriticalSection(&buf->Thread->ThreadCritSec);

    // Remove buffer from the list
    RemoveBufferObj(&buf->Thread->BufferList, buf);
    // Decrement the event count for the thread
    buf->Thread->EventCount--;
    // Set the thread's event
    WSASetEvent(buf->Thread->Event);

    LeaveCriticalSection(&buf->Thread->ThreadCritSec);

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
    THREAD_OBJ      *thread=NULL;
    SOCKET_OBJ      *sockobj=NULL,
                    *ListenSockets=NULL;
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

    InitializeCriticalSection(&gThreadListCritSec);

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

            // Load the extension functions
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
            rc = listen(sockobj->s, 200);
            if (rc == SOCKET_ERROR)
            {
                fprintf(stderr, "listen failed: %d\n", WSAGetLastError());
                return -1;
            }

            // Allocate the overlapped structures for the accepts
            sockobj->PendingAccepts = (BUFFER_OBJ **)HeapAlloc(
                    GetProcessHeap(), 
                    HEAP_ZERO_MEMORY, 
                    (sizeof(BUFFER_OBJ *) * gOverlappedCount));
            if (sockobj->PendingAccepts == NULL)
            {
                fprintf(stderr, "HeapAlloc failed: %d\n", GetLastError());
                ExitProcess(-1);
            }

            // Post the initial accepts
            for(i=0; i < gOverlappedCount ;i++)
            {
                sockobj->PendingAccepts[i] = acceptobj = GetBufferObj(sockobj, gBufferSize);

                acceptobj->Socket = sockobj;

                AssignIoToThread(acceptobj);

                if (PostAccept(acceptobj) == NO_ERROR)
                {
                    // If we can't post accepts just bail
                    ExitProcess(-1);
                }
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
                     
            // Post the initial UDP receives
            for(i=0; i < gOverlappedCount ;i++)
            {
                recvobj = GetBufferObj(sockobj, gBufferSize);

                recvobj->Socket = sockobj;

                AssignIoToThread(recvobj);

                if (PostRecv(recvobj) == NO_ERROR)
                {
                }
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
        Sleep(5000);

        interval++;

        PrintStatistics();

        if (interval == 12)
        {
            SOCKET_OBJ  *listenptr=NULL;
            int          optval,
                         optlen;

            // Walk the list of outstanding accepts
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

    WSACleanup();
    return 0;
}
