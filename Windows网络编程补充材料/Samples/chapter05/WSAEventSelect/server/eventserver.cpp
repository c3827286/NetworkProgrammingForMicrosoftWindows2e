//
// Sample: WSAEventSelect IPv4/IPv6 Server
//
// Files:
//      eventserver.cpp - this file
//      resolve.cpp     - routines for resovling addresses, etc.
//      resolve.h       - header file for resolve.c
//
// Description:
//      This sample illustrates the WSAEventSelect IO for TCP and UDP for
//      both IPv4 and IPv6. This sample uses the getaddrinfo/getnameinfo
//      APIs which allows this application to be IP agnostic. That is the
//      desired address family (AF_INET or AF_INET6) can be determined
//      simply from the string address passed via the -l command.
//
//      Because of the limitation of waiting on a maximum of 64 events
//      at a time, this sample uses a thread pool to service client
//      connections. For TCP, a listening socket is created for each
//      accepted connection which registers for FD_ACCEPT notifications.
//      These sockets are assigned to a worker thread. Once a client
//      connection is established, read and write events are registered
//      and that socket is assigned to a worker thread as well. Once
//      a thread is waiting on the maximum events allowed, a new worker
//      thread will be created for additional connections, etc. For each
//      connection, data is read and then added to a send queue for that
//      connection. When data may be sent on the socket it is echoed back
//      to the client.
//
//      For UDP, this setup is similar except that only a single UDP socket
//      is created for each address family available.
//
//      For example:
//          If this sample is called with the following command lines:
//              eventserver.exe -l fe80::2efe:1234 -e 5150
//              eventserver.exe -l ::
//          Then the server creates an IPv6 socket as an IPv6 address was
//          provided.
//
//          On the other hand, with the following command line:
//              eventserver.exe -l 7.7.7.1 -e 5150
//              eventserver.exe -l 0.0.0.0
//          Then the server creates an IPv4 socket.
//
// Compile:
//      cl.exe -o eventserver.exe eventserver.cpp resolve.cpp ws2_32.lib
//
// Usage:
//      asyncserver.exe [options]
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
    HANDLE      event;          // Event handle
    int         listening;      // Socket is a listening socket (TCP)
    int         closing;        // Indicates whether the connection is closing

    SOCKADDR_STORAGE addr;      // Used for client's remote address
    int              addrlen;   // Length of the address

    BUFFER_OBJ *pending,        // List of pending buffers to be sent
               *pendingtail;    // Last entry in buffer list

    struct _SOCKET_OBJ *next,   // Used to link socket objects together
                       *prev;
} SOCKET_OBJ;

//
// Allocated for each trhead spawned
//
typedef struct _THREAD_OBJ
{
    SOCKET_OBJ *SocketList,            // Linked list of all sockets allocated
               *SocketListEnd;         // End of socket list
    int         SocketCount;           // Number of socket objects in list

    HANDLE      Event;                 // Used to signal new clients assigned
                                       //  to this thread
    HANDLE      Thread;

    HANDLE      Handles[MAXIMUM_WAIT_OBJECTS]; // Array of socket's event handles

    CRITICAL_SECTION ThreadCritSec;    // Protect access to SOCKET_OBJ lists

    struct _THREAD_OBJ *next;          // Next thread object in list
} THREAD_OBJ;

THREAD_OBJ *gChildThreads=NULL;        // List of thread objects allocated
int         gChildThreadsCount=0;      // Number of child threads created

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

    sockobj->event = WSACreateEvent();
    if (sockobj->event == NULL)
    {
        fprintf(stderr, "GetSocketObj: WSACreateEvent failed: %d\n", WSAGetLastError());
        ExitProcess(-1);
    }

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

    WSACloseEvent(obj->event);

    if (obj->s != INVALID_SOCKET)
    {
        closesocket(obj->s);
    }

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

    thread->Event = WSACreateEvent();
    if (thread->Event == NULL)
    {
        fprintf(stderr, "GetThreadObj: WSACreateEvent failed: %d\n", WSAGetLastError());
        ExitProcess(-1);
    }

    thread->Handles[0] = thread->Event;

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
// Function: InsertSocketObj
//
// Description:
//    Insert a socket object into the list of socket objects for
//    the given thread object.
//
int InsertSocketObj(THREAD_OBJ *thread, SOCKET_OBJ *sock)
{
    int     ret;

    EnterCriticalSection(&thread->ThreadCritSec);

    if (thread->SocketCount < MAXIMUM_WAIT_OBJECTS-1)
    {
        sock->next = sock->prev = NULL;
        if (thread->SocketList == NULL)
        {
            // List is empty
            thread->SocketList = thread->SocketListEnd = sock;
        }
        else
        {
            // Non-empty; insert at the end
            sock->prev = thread->SocketListEnd;
            thread->SocketListEnd->next = sock;
            thread->SocketListEnd = sock;

        }
        // Assign the socket's event into the thread's event list
        thread->Handles[thread->SocketCount + 1] = sock->event;
        thread->SocketCount++;

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
// Function: RemoveSocketObj
//
// Description:
//    Remove a socket object from the list of sockets for the given thread.
//
void RemoveSocketObj(THREAD_OBJ *thread, SOCKET_OBJ *sock)
{
    EnterCriticalSection(&thread->ThreadCritSec);
    if (sock->prev)
    {
        sock->prev->next = sock->next;
    }
    if (sock->next)
    {
        sock->next->prev = sock->prev;
    }

    if (thread->SocketList == sock)
        thread->SocketList = sock->next;
    if (thread->SocketListEnd == sock)
        thread->SocketListEnd = sock->prev;

    thread->SocketCount--;

    // Signal thread to rebuild array of events
    WSASetEvent(thread->Event);

    InterlockedDecrement(&gCurrentConnections);

    LeaveCriticalSection(&thread->ThreadCritSec);
}

//
// Function: FindSocketObj
// 
// Description:
//    Find a socket object within the list of sockets from a thread. The socket
//    object is found by index number -- this must be so because the index of
//    the event object in the thread's event array must match the order in which
//    the socket object appears in the thread's socket list.
//
SOCKET_OBJ *FindSocketObj(THREAD_OBJ *thread, int index)
{
    SOCKET_OBJ *ptr=NULL;
    int         i;

    EnterCriticalSection(&thread->ThreadCritSec);

    ptr = thread->SocketList;
    for(i=0; i < index ;i++)
    {
        ptr = ptr->next;
    }

    LeaveCriticalSection(&thread->ThreadCritSec);

    return ptr;
}

//
// Function: EnqueueBufferObj
//
// Description:
//   Queue up a receive buffer for this connection (socket).
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
            sockobj->s = INVALID_SOCKET;

            ret = -1;
        }
        else
        {
            ret = WSAEWOULDBLOCK;
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
            // Always enqueue the zero byte datagrams for UDP
            buffobj->buflen = 0;
            EnqueueBufferObj(sockobj, buffobj, FALSE);
        }

        // Set the socket object to closing
        sockobj->closing = TRUE;

        if (sockobj->pending == NULL)
        {
            // If no sends are pending, close the socket for good
            closesocket(sockobj->s);
            sockobj->s = INVALID_SOCKET;

            ret = -1;
        }
        else
        {
            // Sends are pending, just return
            ret = 0;
        }
    }
    else
    {
        // Read data, updated the counters and enqueue the buffer for sending
        InterlockedExchangeAdd(&gBytesRead, rc);
        InterlockedExchangeAdd(&gBytesReadLast, rc);

        buffobj->buflen = rc;
        EnqueueBufferObj(sockobj, buffobj, FALSE);

        ret = 1;
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

                        ret = WSAEWOULDBLOCK;
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
                    InterlockedExchangeAdd(&gBytesSent, rc);
                    InterlockedExchangeAdd(&gBytesSentLast, rc);

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

                    ret = WSAEWOULDBLOCK;
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
        sock->s = INVALID_SOCKET;
        ret = -1;
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

    printf("Total Connections  : %lu\n", gTotalConnections);
    printf("Current Connections: %lu\n", gCurrentConnections);

    InterlockedExchange(&gBytesSentLast, 0);
    InterlockedExchange(&gBytesReadLast, 0);

    gStartTimeLast = tick;
}

//
// Function: HandleIo
//
// Description:
//    This function handles the IO on a socket. First, the events signaled
//    on the socket are enuemrated, then the appropriate handler routine
//    for the event is called.
//
int  HandleIo(THREAD_OBJ *thread, SOCKET_OBJ *sock)
{
    WSANETWORKEVENTS nevents;
    int              rc;

    // Enumerate the events
    rc = WSAEnumNetworkEvents(
            sock->s,
            sock->event,
           &nevents
            );
    if (rc == SOCKET_ERROR)
    {
        fprintf(stderr, "HandleIo: WSAEnumNetworkEvents failed: %d\n", WSAGetLastError());
        return SOCKET_ERROR;
    }

    if (nevents.lNetworkEvents & FD_READ)
    {
        // Check for read error
        if (nevents.iErrorCode[FD_READ_BIT] == 0)
        {
            rc = ReceivePendingData(sock);
            if (rc == -1)
            {
                RemoveSocketObj(thread, sock);
                FreeSocketObj(sock);
                return SOCKET_ERROR;
            }
            rc = SendPendingData(sock);
            if (rc == -1)
            {
                RemoveSocketObj(thread, sock);
                FreeSocketObj(sock);
                return SOCKET_ERROR;
            }
        }
        else
        {
            fprintf(stderr, "HandleIo: FD_READ error %d\n", 
                    nevents.iErrorCode[FD_READ_BIT]);
            RemoveSocketObj(thread, sock);
            FreeSocketObj(sock);
            return SOCKET_ERROR;
        }
    }
    if (nevents.lNetworkEvents & FD_WRITE)
    {
        // Check for write error
        if (nevents.iErrorCode[FD_WRITE_BIT] == 0)
        {
            rc = SendPendingData(sock);
            if (rc == -1)
            {
                RemoveSocketObj(thread, sock);
                FreeSocketObj(sock);
                return SOCKET_ERROR;
            }
        }
        else
        {
            fprintf(stderr, "HandleIo: FD_WRITE error %d\n",
                    nevents.iErrorCode[FD_WRITE_BIT]);
            return SOCKET_ERROR;
        }
    }
    if (nevents.lNetworkEvents & FD_CLOSE)
    {
        // Check for close error
        if (nevents.iErrorCode[FD_CLOSE_BIT] == 0)
        {
            // Socket has been indicated as closing so make sure all the data
            // has been read
            while (1)
            {
                rc = ReceivePendingData(sock);
                if (rc == -1)
                {
                    RemoveSocketObj(thread, sock);
                    FreeSocketObj(sock);
                    return SOCKET_ERROR;
                }
                else if (rc != 0)
                {
                    continue;
                }
                else
                {
                    break;
                }
            }
            // See if there is any data pending, if so try to send it
            rc = SendPendingData(sock);
            if (rc == -1)
            {
                RemoveSocketObj(thread, sock);
                FreeSocketObj(sock);
                return SOCKET_ERROR;
            }
        }
        else
        {
            fprintf(stderr, "HandleIo: FD_CLOSE error %d\n",
                    nevents.iErrorCode[FD_CLOSE_BIT]);
            RemoveSocketObj(thread, sock);
            FreeSocketObj(sock);
            return SOCKET_ERROR;
        }
    }
    return NO_ERROR;
}

void RenumberThreadArray(THREAD_OBJ *thread)
{
    SOCKET_OBJ *sptr=NULL;
    int         i;

    EnterCriticalSection(&thread->ThreadCritSec);
    i = 0;
    sptr = thread->SocketList;
    while (sptr)
    {
        thread->Handles[i+1] = sptr->event;

        i++;
        sptr = sptr->next;
    }
    LeaveCriticalSection(&thread->ThreadCritSec);
}

//
// Function: ChildThread
//
// Description:
//    This is the child thread that handles socket connections. Each thread
//    can only wait on a maximum of 63 sockets. The main thread will assign
//    each client connection to one of the child threads. If there is no
//    thread to handle the socket, a new thread is created to handle the 
//    connection.
//
DWORD WINAPI ChildThread(LPVOID lpParam)
{
    THREAD_OBJ *thread=NULL;
    SOCKET_OBJ *sptr=NULL,
               *sockobj=NULL;
    int         index,
                rc,
                i;

    thread = (THREAD_OBJ *)lpParam;

    while (1)
    {
        rc = WaitForMultipleObjects(
                thread->SocketCount + 1,
                thread->Handles,
                FALSE,
                INFINITE
                );
        if (rc == WAIT_FAILED || rc == WAIT_TIMEOUT)
        {
            fprintf(stderr, "ChildThread: WaitForMultipleObjects failed: %d\n", GetLastError());
            break;
        }
        else
        {
            // Multiple events may be signaled at one time so check each
            // event to see if its signaled
            //
            for(i=0; i < thread->SocketCount + 1 ;i++)
            {
                rc = WaitForSingleObject(thread->Handles[i], 0);
                if (rc == WAIT_FAILED)
                {
                    fprintf(stderr, "ChildThread: WaitForSingleObject failed: %d\n", GetLastError());
                    ExitThread(-1);
                }
                else if (rc == WAIT_TIMEOUT)
                {
                    // This event isn't signaled, continue to the next one
                    continue;
                }

                index = i;

                if (index == 0)
                {
                    // If index 0 is signaled then rebuild the array of event
                    //    handles to wait on
                    WSAResetEvent(thread->Handles[index]);

                    RenumberThreadArray(thread);
                     
                    i = 1;
                }
                else
                {
                    // Otherwise, its an event associated with a socket that
                    //    was signaled. Handle the IO on that socket.
                    //
                    sockobj = FindSocketObj(thread, index-1);
                    if (sockobj != NULL)
                    {
                        if (HandleIo(thread, sockobj) == SOCKET_ERROR)
                        {
                            RenumberThreadArray(thread);
                        }
                    }
                    else
                    {
                        printf("Unable to find socket object!\n");
                    }
                }
            }
        }
    }

    ExitThread(0);
    return 0;
}

//
// Function: AssignToFreeThread
//
// Description:
//    This routine assigns a socket connection to an available child
//    thread to handle any IO on it. If no threads are available, a 
//    new thread is spawned to handle the connection.
//
void AssignToFreeThread(SOCKET_OBJ *sock)
{
    THREAD_OBJ *thread=NULL;

    thread = gChildThreads;
    while (thread)
    {
        // If this routine returns something other than SOCKET_ERROR
        //    that it was successfully assigned to a child thread.
        if (InsertSocketObj(thread, sock) != SOCKET_ERROR)
            break;
        thread = thread->next;
    }

    if (thread == NULL)
    {
        // No thread was found to assign the client socket to, create a new thread
        //
        printf("Creating new thread object\n");

        thread = GetThreadObj();

        thread->Thread = CreateThread(NULL, 0, ChildThread, (LPVOID)thread, 0, NULL);
        if (thread->Thread == NULL)
        {
            fprintf(stderr, "AssignToFreeThread: CreateThread failed: %d\n", GetLastError());
            ExitProcess(-1);
        }

        InsertSocketObj(thread, sock);

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
 
    // signal child thread to rebuild the event list
    WSASetEvent(thread->Event);

    return;
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
                    *newsock=NULL;
    int              index,
                     rc;
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

    printf("Local address: %s; Port: %s; Family: %d\n",
            gBindAddr, gBindPort, gAddressFamily);

    res = ResolveAddress(gBindAddr, gBindPort, gAddressFamily, gSocketType, gProtocol);
    if (res == NULL)
    {
        fprintf(stderr, "ResolveAddress failed to return any addresses!\n");
        return -1;
    }

    thread = GetThreadObj();

    // For each local address returned, create a listening/receiving socket
    ptr = res;
    while (ptr)
    {
        PrintAddress(ptr->ai_addr, ptr->ai_addrlen); printf("\n");

        sockobj = GetSocketObj(INVALID_SOCKET, (gProtocol == IPPROTO_TCP) ? TRUE : FALSE);

        // create the socket
        sockobj->s = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sockobj->s == INVALID_SOCKET)
        {
            fprintf(stderr,"socket failed: %d\n", WSAGetLastError());
            return -1;
        }

        InsertSocketObj(thread, sockobj);

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

            // Register events on the socket
            rc = WSAEventSelect(
                    sockobj->s,
                    sockobj->event,
                    FD_ACCEPT | FD_CLOSE
                    );
            if (rc == SOCKET_ERROR)
            {
                fprintf(stderr, "WSAEventSelect failed: %d\n", WSAGetLastError());
                return -1;
            }
        }
        else
        {
            // Register events on the socket
            rc = WSAEventSelect(
                    sockobj->s,
                    sockobj->event,
                    FD_READ | FD_WRITE | FD_CLOSE
                    );
            if (rc == SOCKET_ERROR)
            {
                fprintf(stderr, "WSAEventSelect failed: %d\n", WSAGetLastError());
                return -1;
            }
        }

        ptr = ptr->ai_next;
    }
    // free the addrinfo structure for the 'bind' address
    freeaddrinfo(res);

    gStartTime = gStartTimeLast = GetTickCount();

    while (1)
    {
        rc = WaitForMultipleObjects(
                thread->SocketCount + 1,
                thread->Handles,
                FALSE,
                5000
                );
        if (rc == WAIT_FAILED)
        {
            fprintf(stderr, "WaitForMultipleObjects failed: %d\n", GetLastError());
            break;
        }
        else if (rc == WAIT_TIMEOUT)
        {
            PrintStatistics();
        }
        else
        {
            index = rc - WAIT_OBJECT_0;

            sockobj = FindSocketObj(thread, index-1);

            if (gProtocol == IPPROTO_TCP)
            {
                SOCKADDR_STORAGE sa;
                WSANETWORKEVENTS ne;
                SOCKET           sc;
                int              salen;

                rc = WSAEnumNetworkEvents(
                        sockobj->s,
                        thread->Handles[index],
                       &ne
                        );
                if (rc == SOCKET_ERROR)
                {
                    fprintf(stderr, "WSAEnumNetworkEvents failed: %d\n", WSAGetLastError());
                    break;
                }

                while (1)
                {
                    sc = INVALID_SOCKET;
                    salen = sizeof(sa);

                    //
                    // For TCP, accept the connection and hand off the client socket
                    // to a worker thread
                    //

                    sc = accept(
                            sockobj->s, 
                            (SOCKADDR *)&sa,
                            &salen
                               );
                    if ((sc == INVALID_SOCKET) && (WSAGetLastError() != WSAEWOULDBLOCK))
                    {
                        fprintf(stderr, "accept failed: %d\n", WSAGetLastError());
                        break;
                    }
                    else if (sc != INVALID_SOCKET)
                    {
                        newsock = GetSocketObj(INVALID_SOCKET, FALSE);

                        // Copy address information
                        memcpy(&newsock->addr, &sa, salen);
                        newsock->addrlen = salen;

                        newsock->s = sc;

                        InterlockedIncrement(&gTotalConnections);
                        InterlockedIncrement(&gCurrentConnections);

                        /*
                           printf("Accepted connection from: ");
                           PrintAddress((SOCKADDR *)&newsock->addr, newsock->addrlen);
                           printf("\n");
                         */

                        // Register for read, write and close on the client socket
                        rc = WSAEventSelect(
                                newsock->s,
                                newsock->event,
                                FD_READ | FD_WRITE | FD_CLOSE
                                           );
                        if (rc == SOCKET_ERROR)
                        {
                            fprintf(stderr, "WSAEventSelect failed: %d\n", WSAGetLastError());
                            break;
                        }

                        AssignToFreeThread(newsock);

                    }
                    else
                    {
                        // Failed with WSAEWOULDBLOCK -- just continue
                        break;
                    }
                }


            }
            else
            {
                // For UDP all we have to do is handle events on the main
                //    threads.
                if (HandleIo(thread, sockobj) == SOCKET_ERROR)
                {
                    RenumberThreadArray(thread);
                }
            }
        }
    }

    WSACleanup();
    return 0;
}
