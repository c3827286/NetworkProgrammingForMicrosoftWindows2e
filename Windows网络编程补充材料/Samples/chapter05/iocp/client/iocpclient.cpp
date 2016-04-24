//
// Sample: I/O Completion Port IPv4/IPv6 Client
//
// Files:
//      iocpclient.cpp    - this file
//      resolve.cpp       - Common name resolution routines
//      resolve.h         - Header file for name resolution routines
//
// Description:
//      This is a sample client which uses IO completion ports and
//      uses the ConnectEx and TransmitFile APIs. As such this client
//      only works on Windows XP+. This client will attempt the specified
//      number of connections for each server address resovled. That is
//      if the server's address is explicitly specified (via the -c NUM 
//      option) then the client will establish NUM connections. If the
//      server's hostname is specified and it resolves to 5 different
//      addresses then NUM connections are attemped for each address.
//
//      Again, this client is an echo client. It sends a specified amount
//      of data on the connection and then waits to receive the data
//      back. Normally, the client uses WSASend for data, but if the
//      -t SIZE flag is given then TransmitFile is used with a temporary
//      file of SIZE bytes. This temp file is created on the fly and
//      deleted upon client exit.
//
//      The -r command is used for basic rate limiting of data transfers
//      from the client to the server. For reasonable limits the mechanism
//      works; however, the higher the limit is the less reliable this method
//      becomes (as the sleep time approaches zero - then the time it takes
//      to post the sends becomes an issue).  This option is provided to
//      allow a method of simulating a steady connection rate (as the 
//      connection requests are shaped as well) in addition to smoothing the
//      data transmission to the server to prevent sudden spikes and/or
//      saturating the network bandwidth.
//
//      NOTE: This client only supports the TCP protocol.
// 
// Compile:
//      cl -o iocpclient.exe iocpclient.cpp resolve.cpp ws2_32.lib
//
// Usage:
//      iocpclient.exe [options]
//          -a 4|6     Address family, 4 = IPv4, 6 = IPv6 [default = IPv4]
//          -b size    Buffer size for send/recv (in bytes)
//          -c count   Number of connections to establish
//          -e port    Port number
//          -n server  Server address or name to connect to
//          -l addr    Local address to bind to [default INADDR_ANY for IPv4 or INADDR6_ANY for IPv6]
//          -r rate    Rate at which to send data
//          -t size    Use TransmitFile instead of sends (size of file to send)
//          -x count   Number of sends
//

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "resolve.h"

#define DEFAULT_BUFFER_SIZE         4096   // default buffer size
#define DEFAULT_OVERLAPPED_COUNT    1      // Number of overlapped recv per socket
#define DEFAULT_CLIENT_CONNECTIONS  10     // Number of connections to initiate
#define DEFAULT_FILE_SIZE           2000000// Default size of file for TransmitFile
#define DEFAULT_SEND_COUNT          100    // How many send/TransmitFiles to perform

int gAddressFamily = AF_UNSPEC,         // default to unspecified
    gSocketType    = SOCK_STREAM,       // default to TCP socket type
    gProtocol      = IPPROTO_TCP,       // default to TCP protocol
    gBufferSize    = DEFAULT_BUFFER_SIZE,
    gOverlappedCount = DEFAULT_OVERLAPPED_COUNT,
    gConnectionCount = DEFAULT_CLIENT_CONNECTIONS,
    gFileSize        = DEFAULT_FILE_SIZE,
    gSendCount       = DEFAULT_SEND_COUNT,
    gRateLimit       = -1,
    gTimeout         = 0;

USHORT gLocalPort = 0x0000FFFD;

BOOL gTransmitFile = FALSE;             // Use TransmitFile instead

HANDLE gTempFile   = INVALID_HANDLE_VALUE;

char *gBindAddr    = NULL,              // local interface to bind to
     *gServerAddr  = NULL,              // Server address to connect to
     *gBindPort    = "5150";            // local port to bind to

//
// This is our per I/O buffer. It contains a WSAOVERLAPPED structure as well
//    as other necessary information for handling an IO operation on a socket.
//
typedef struct _BUFFER_OBJ
{
    WSAOVERLAPPED        ol;

    HANDLE               hFile;         // Open file handle for TransmitFile

    char                *buf;           // Buffer for recv/send/AcceptEx
    int                  buflen;        // Length of the buffer

    int                  operation;     // Type of operation issued
#define OP_CONNECT      0                   // ConnectEx
#define OP_READ         1                   // WSARecv/WSARecvFrom
#define OP_WRITE        2                   // WSASend/WSASendTo
#define OP_TRANSMIT     3                   // TransmitFile

    SOCKADDR_STORAGE     addr;
    int                  addrlen;

    struct _BUFFER_OBJ  *next;

} BUFFER_OBJ;

//
// This is our per socket buffer. It contains information about the socket handle
//    which is returned from each GetQueuedCompletionStatus call.
//
typedef struct _SOCKET_OBJ
{
    SOCKET               s;              // Socket handle
    int                  af;             // Address family of socket (AF_INET, AF_INET6)

    volatile LONG        OutstandingOps; // Number of outstanding overlapped ops on 
                                         //    socket
    volatile LONG        SendCount;      // Number of sends to perform on connection

    BOOL                 bConnected;
    BOOL                 bClosing;       // Is socket closing

    // Pointers to Microsoft specific extensions.
    LPFN_CONNECTEX       lpfnConnectEx;
    LPFN_TRANSMITFILE    lpfnTransmitFile;

    BUFFER_OBJ          *Repost;         // Send buffer to repost (used with rate limit)

    CRITICAL_SECTION     SockCritSec;

    struct _SOCKET_OBJ  *next,
                        *prev;
} SOCKET_OBJ;

// List of open sockets connected to server
SOCKET_OBJ *gConnectionList=NULL;

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
              gCurrentConnections=0,
              gConnectionRefused=0;

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
                    "  -c count   Number of connections to establish\n"
                    "  -e port    Port number [default = %s]\n"
                    "  -n server  Server address or name to connect to\n"
                    "  -p port    Local port number to bind to\n"
                    "  -l addr    Local address to bind to [default INADDR_ANY for IPv4 or INADDR6_ANY for IPv6]\n"
                    "  -r rate    Use the QOS provider to limit send rate\n"
                    "  -t size    Use TransmitFile instead of sends (size of file to send)\n"
                    "  -x count   Number of sends\n",
                    gBufferSize,
                    gBindPort
                    );
    ExitProcess(-1);
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

    InitializeCriticalSection(&sockobj->SockCritSec);

    // Initialize the members
    sockobj->s = s;
    sockobj->af = af;

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

    HeapFree(GetProcessHeap(), 0, obj);
}

//
// Function: InsertSocketObj
//
// Description:
//    Insert a SOCKET_OBJ into a list of socket objects. Insertions
//    are performed at the end of the list.
//
void InsertSocketObj(SOCKET_OBJ **head, SOCKET_OBJ *obj)
{
    SOCKET_OBJ *end=NULL,
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
// Function: RemoveSocketObj
//
// Description:
//    Removes the specified SOCKET_OBJ from the list of socket objects.
//
SOCKET_OBJ *RemoveSocketObj(SOCKET_OBJ **head, SOCKET_OBJ *buf)
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
// Function: CreateTempFile
//
// Description:
//    Creates a temporary file which is used by all TransmitFile
//    operations. This file is created as temporary and will be
//    deleted upon handle closure.
//
HANDLE CreateTempFile(char *filename, DWORD size)
{
    OVERLAPPED ol;
    HANDLE     hFile;
    DWORD      bytes2write,
               offset,
               nLeft,
               written,
               buflen=1024;
    char       buf[1024];
    int        rc;

    // Create the file as temporary.
    hFile = CreateFile(
            filename,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OVERLAPPED | 
                FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_DELETE_ON_CLOSE,
            NULL
            );
    if (hFile == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "CreateTempFile failed: %d\n", GetLastError());
        return hFile;
    }

    memset(buf, '$', buflen);

    memset(&ol, 0, sizeof(ol));
    offset = 0;

    ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (ol.hEvent == FALSE)
    {
        fprintf(stderr, "CreateTempFile: CreateEvent failed: %d\n",
                GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    // Write the specified amount of data to the file
    nLeft = size;
    while (nLeft > 0)
    {
        bytes2write = ((nLeft < buflen) ? nLeft : buflen);
        
        ol.Offset = offset;

        rc = WriteFile(
                hFile,
                buf,
                bytes2write,
               &written,
               &ol
                );
        if (rc == 0)
        {
            if (GetLastError() != ERROR_IO_PENDING)
            {
                fprintf(stderr, "CreateTempFile: WriteFile failed: %d\n",
                        GetLastError());
                return INVALID_HANDLE_VALUE;
            }
            else
            {
                rc = GetOverlappedResult(
                        hFile,
                       &ol,
                       &written,
                        TRUE
                        );
                if (rc == 0)
                {
                    fprintf(stderr, "CreateTempFile: GetOverlappedResult failed: %d\n",
                            GetLastError());
                    return INVALID_HANDLE_VALUE;
                }
            }
        }
        ResetEvent(ol.hEvent);

        // Ajust the offset and bytes left to write
        offset += written;
        nLeft  -= written;
    }

    printf("Created temp file of size: %d\n", offset);

    CloseHandle(ol.hEvent);

    return hFile;
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
                case 'c':               // Number of connections to make
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gConnectionCount = atol(argv[++i]);
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
                case 'n':               // server address/name to connect to
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gServerAddr = argv[++i];
                    break;
                case 'o':               // overlapped count
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gOverlappedCount = atol(argv[++i]);
                    break;
                case 'p':               // local port
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gLocalPort = (USHORT) atoi(argv[++i]);
                    break;
                case 'r':               // Use the QOS packet scheduler
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gRateLimit = atol(argv[++i]);
                    break;
                case 't':               // Use TransmitFile instead of sends
                    gTransmitFile = TRUE;
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gFileSize = atol(argv[++i]);
                    break;
                case 'x':               // Number of sends to post total
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gSendCount = atol(argv[++i]);
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
	{
		printf("Bytes sent         : %lu\n", gBytesSent);
		printf("Bytes received     : %lu\n", gBytesRead);
		printf("Current Connections: %lu\n", gCurrentConnections);
        printf("Total Connections  : %lu\n", gTotalConnections);
        printf("Connections Refused: %lu\n", gConnectionRefused);
        return;
	}
	

    printf("\n");

    printf("Current Connections: %lu\n", gCurrentConnections);
    printf("Total Connections  : %lu\n", gTotalConnections);
    printf("Connections Refused: %lu\n", gConnectionRefused);


    // Calculate average bytes per second
    bps = gBytesSent / elapsed;
    printf("Average BPS sent   : %lu [%lu]\n", bps, gBytesSent);

    bps = gBytesRead / elapsed;
    printf("Average BPS read   : %lu [%lu]\n", bps, gBytesRead);

    elapsed = (tick - gStartTimeLast) / 1000;

    if (elapsed == 0)
        return;

    // Calculate bytes per second over the last X seconds
    bps = gBytesSentLast / elapsed;
    printf("Current BPS sent   : %lu\n", bps);

    bps = gBytesReadLast / elapsed;
    printf("Current BPS read   : %lu\n", bps);

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

    rc = WSARecv(
            sock->s,
           &wbuf,
            1,
           &bytes,
           &flags,
           &recvobj->ol,
            NULL
            );

    if (rc == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            fprintf(stderr, "PostRecv: WSARecv* failed: %d\n", WSAGetLastError());
            return SOCKET_ERROR;
        }
    }

    // Increment outstanding overlapped operations
    InterlockedIncrement(&sock->OutstandingOps);

    //printf("POST_READ: op %d\n", sock->OutstandingOps);

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

    rc = WSASend(
            sock->s,
           &wbuf,
            1,
           &bytes,
            0,
           &sendobj->ol,
            NULL
            );

    if (rc == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            fprintf(stderr, "PostSend: WSASend* failed: %d\n", WSAGetLastError());
            return SOCKET_ERROR;
        }
    }

    // Increment the outstanding operation count
    InterlockedIncrement(&sock->OutstandingOps);
    InterlockedDecrement(&sock->SendCount);

    //printf("POST_SEND: op %d\n", sock->OutstandingOps);

    return NO_ERROR;
}

//
// Function: PostConnect
// 
// Description:
//    Post an overlapped accept on a listening socket.
//
int PostConnect(SOCKET_OBJ *sock, BUFFER_OBJ *connobj)
{
    DWORD   bytes;
    int     rc;

    connobj->operation = OP_CONNECT;

    /*
    printf("Connecting to: ");
    PrintAddress((SOCKADDR *)&connobj->addr, connobj->addrlen);
    printf("\n");
    */

    rc = sock->lpfnConnectEx(
            sock->s,
            (SOCKADDR *)&connobj->addr,
            connobj->addrlen,
            connobj->buf,
            connobj->buflen,
           &bytes,
           &connobj->ol
            );
    if (rc == FALSE)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            fprintf(stderr, "PostConnect: ConnectEx failed: %d\n",
                    WSAGetLastError());
            return SOCKET_ERROR;
        }
    }

    // Increment the outstanding overlapped count for this socket
    InterlockedIncrement(&sock->OutstandingOps);

    //printf("POST_CONNECT: op %d\n", sock->OutstandingOps);

    return NO_ERROR;
}

//
// Function: PostTransmitFile
//
// Description:
//    Post a TransmitFile operation on the given socket connection.
//
int PostTransmitFile(SOCKET_OBJ *sock, BUFFER_OBJ *tfobj)
{
     int    rc;

     tfobj->operation = OP_TRANSMIT;
     tfobj->hFile = gTempFile;

     // Zero out the OVERLAPPED, the offset must be zero
     memset(&tfobj->ol, 0, sizeof(tfobj->ol));

     rc = sock->lpfnTransmitFile(
             sock->s,
             tfobj->hFile,
             0,
             0,
            &tfobj->ol,
             NULL,
             0
             );
     if (rc == FALSE)
     {
         if (WSAGetLastError() != WSA_IO_PENDING)
         {
             fprintf(stderr, "PostTransmitFile: TransmitFile failed: %d\n",
                     WSAGetLastError());
             return SOCKET_ERROR;
         }
     }

    // Increment the outstanding overlapped count for this socket
    InterlockedIncrement(&sock->OutstandingOps);
    InterlockedDecrement(&sock->SendCount);

    return NO_ERROR;
}

//
// Function: HandleIo
//
// Description:
//    This function handles the IO on a socket. In the event of a receive, the 
//    completed receive is posted again. For completed accepts, another AcceptEx
//    is posted. For completed sends, the buffer is freed.
//
int HandleIo(SOCKET_OBJ *sock, BUFFER_OBJ *buf, DWORD BytesTransfered, DWORD error)
{
    BUFFER_OBJ *recvobj=NULL,       // Used to post new receives on accepted connections
               *sendobj=NULL;       // Used to post new sends for data received
    BOOL        bCleanupSocket;
    int         rc,
                i;

    bCleanupSocket = FALSE;

    if (error != NO_ERROR)
    {
        // An error occured on a TCP socket, free the associated per I/O buffer
        // and see if there are any more outstanding operations. If so we must
        // wait until they are complete as well.
        //
        printf("error = %d\n", error);

        closesocket(sock->s);
        sock->s = INVALID_SOCKET;
       
        if (buf->operation == OP_CONNECT)
        {
            if (error == WSAECONNREFUSED)
            {
                InterlockedIncrement(&gConnectionRefused);
            }

            FreeBufferObj(buf);
			RemoveSocketObj(&gConnectionList, sock);
            FreeSocketObj(sock);

            if (gConnectionList == NULL)
                return 0;
        }
        else
        {
			FreeBufferObj(buf);
            if (sock->OutstandingOps == 0)
            {
                RemoveSocketObj(&gConnectionList, sock);
                FreeSocketObj(sock);
            }
        }
        return SOCKET_ERROR;
    }
    else
    {
        if (buf->operation == OP_CONNECT)
        {
            int     optval=1;

            // Update counters
            InterlockedIncrement(&gCurrentConnections);
            InterlockedIncrement(&gTotalConnections);
            InterlockedExchangeAdd(&gBytesSent, BytesTransfered);
            InterlockedExchangeAdd(&gBytesSentLast, BytesTransfered);

            // Need to update the socket context in order to use the shutdown API
            rc = setsockopt(
                    sock->s,
                    SOL_SOCKET,
                    SO_UPDATE_CONNECT_CONTEXT,
                    (char *)&optval,
                    sizeof(optval)
                    );
            if (rc == SOCKET_ERROR)
            {
                fprintf(stderr, "setsockopt: SO_UPDATE_CONNECT_CONTEXT failed: %d\n",
                        WSAGetLastError());
            }

            sock->bConnected = TRUE;

            // Post the specified number of receives on the succeeded connection
            for(i=0; i < gOverlappedCount ;i++)
            {
                recvobj = GetBufferObj(gBufferSize);

                if (PostRecv(sock, recvobj) != NO_ERROR)
                {
                    FreeBufferObj(recvobj);
                    bCleanupSocket = TRUE;
                    break;
                }
            }

            for(i=0; ((i < gOverlappedCount) && (!bCleanupSocket)) ;i++)
            {
                sendobj = GetBufferObj(gBufferSize);

                if (gTransmitFile)
                {
                    rc = PostTransmitFile(sock, sendobj);
                }
                else
                {
                    rc = PostSend(sock, sendobj);
                }
                if (rc != NO_ERROR)
                {
                    FreeBufferObj(sendobj);
                    bCleanupSocket = TRUE;
                    break;
                }
                if (sock->SendCount == 0)
                    break;
            }

            FreeBufferObj(buf);
        }
        else if (buf->operation == OP_READ)
        {
            //
            // Receive completed successfully
            //
            if ((BytesTransfered > 0) && (!sock->bClosing))
            {
                InterlockedExchangeAdd(&gBytesRead, BytesTransfered);
                InterlockedExchangeAdd(&gBytesReadLast, BytesTransfered);

                if (PostRecv(sock, buf) != NO_ERROR)
                {
                    // In the event the recv fails, clean up the connection
                    FreeBufferObj(buf);
                    bCleanupSocket = TRUE;
                }
            }
            else
            {
                // Graceful close - the receive returned 0 bytes read
                sock->bClosing = TRUE;

                // Free the receive buffer
                FreeBufferObj(buf);

                //printf("zero byte read\n");
            }
        }
        else if (buf->operation == OP_WRITE)
        {
            // Update the counters
            InterlockedExchangeAdd(&gBytesSent, BytesTransfered);
            InterlockedExchangeAdd(&gBytesSentLast, BytesTransfered);

            // If there are sends to be made, call PostSend again
            EnterCriticalSection(&sock->SockCritSec);
            if (gRateLimit == -1)
            {
                // If no rate limiting just repost the send
                if (sock->SendCount > 0)
                {
                    rc = PostSend(sock, buf);
                    if (rc != NO_ERROR)
                    {
                        bCleanupSocket = TRUE;
                    }
                }
                else
                {
                    // Otherwise, shutdown the socket
                    if (shutdown(sock->s, SD_SEND) == SOCKET_ERROR)
                    {
                        printf("shutdown failed: %d (handle = 0x%p\n", WSAGetLastError(), sock->s);
                    }
                    FreeBufferObj(buf);
                }
            }
            else
            {
                // If rate limiting is turned on then save off the send object for
                // sending by the rate thread.
                //
                if (sock->SendCount > 0)
                {
                    buf->next = sock->Repost;
                    sock->Repost = buf;
                }
                else
                {
                    // Otherwise, shutdown the socket
                    if (shutdown(sock->s, SD_SEND) == SOCKET_ERROR)
                    {
                        printf("shutdown failed: %d (handle = 0x%p\n", WSAGetLastError(), sock->s);
                    }
                    FreeBufferObj(buf);
                }
            }
            LeaveCriticalSection(&sock->SockCritSec);
        }
        else if (buf->operation == OP_TRANSMIT)
        {
            // Update the counters
            InterlockedExchangeAdd(&gBytesSent, BytesTransfered);
            InterlockedExchangeAdd(&gBytesSentLast, BytesTransfered);

            // If there are more sends to be made, post another TransmitFile
            EnterCriticalSection(&sock->SockCritSec);
            if (gRateLimit == -1)
            {
                // If rate limiting is not set, just repost the send
                if (sock->SendCount > 0)
                {
                    rc = PostTransmitFile(sock, buf);
                    if (rc != NO_ERROR)
                    {
                        bCleanupSocket = TRUE;
                    }
                }
                else
                {
                    // Otherwise shutdown the socket
                    shutdown(sock->s, SD_SEND);
                    FreeBufferObj(buf);
                }
            }
            else
            {
                // If rate limiting is enabled, save off the operation for 
                // sending by the send thread.
                //
                if (sock->SendCount > 0)
                {
                    buf->next = sock->Repost;
                    sock->Repost = buf;
                }
                else
                {
                    // Otherwise shutdown the socket
                    shutdown(sock->s, SD_SEND);
                    FreeBufferObj(buf);
                }
            }
            LeaveCriticalSection(&sock->SockCritSec);
        }
    }
    //
    // Check to see if socket is closing
    //
    if ( (InterlockedDecrement(&sock->OutstandingOps) == 0) &&
         (sock->bClosing))
    {
        bCleanupSocket = TRUE;
    }

    if (sock->bClosing)
    {
        printf("CLOSING: ops outstanding %d\n", sock->OutstandingOps);
    }

    // If indicated to clean up, close the socket and free the objects
    if (bCleanupSocket)
    {
        InterlockedDecrement(&gCurrentConnections);

        EnterCriticalSection(&sock->SockCritSec);
        closesocket(sock->s);
        sock->s = INVALID_SOCKET;
        LeaveCriticalSection(&sock->SockCritSec);

        printf("removing conneciton object\n");
        if (gTimeout == -1)
        {
            RemoveSocketObj(&gConnectionList, sock);
            FreeSocketObj(sock);
        }
        else if (gCurrentConnections == 0)
        {
            return 0;
        }

        if (gConnectionList == NULL)
        {
            printf("List is NULL\n");
            return 0;
        }
    }
    return 1;
}

//
// Function: SetPort
//
// Description:
//    Sets the port number in the specified socket address.
//
void SetPort(int af, SOCKADDR *sa, USHORT port)
{
    if (af == AF_INET)
    {
        ((SOCKADDR_IN *)sa)->sin_port = htons(port);
    }
    else if (af == AF_INET6)
    {
        ((SOCKADDR_IN6 *)sa)->sin6_port = htons(port);
    }
}

//
// Function: SendThread
//
// Description:
//    This thread rate limits the send operations in an attempt to smooth the 
//    data flow. The sleep time is computed from the send size and the given
//    send rate. This method breaks down for very high or very low send rates.
//    Its main goal is to smooth the traffic so as to not overrun the local
//    network bandwidth and prevent extreme spikes in network usage.
//
DWORD WINAPI SendThread(LPVOID lpParam)
{
    SOCKET_OBJ *connobj=NULL,
               *tmp=NULL;
    BUFFER_OBJ *buf=NULL;

    printf("SendThread\n");

    Sleep(gTimeout);

    while (1)
    {
        // Walk the connection list to repost sends
        connobj = gConnectionList;
        
        if (connobj == NULL)
        {
            break;
        }

        while (connobj)
        {
            EnterCriticalSection(&connobj->SockCritSec);

            if ((connobj->s != INVALID_SOCKET) && (connobj->Repost != NULL))
            {
                buf = connobj->Repost;
                connobj->Repost = buf->next;

                // Post the appropriate send operation
                if (buf->operation == OP_WRITE)
                {
                    PostSend(connobj, buf);
                }
                else if (buf->operation == OP_TRANSMIT)
                {
                    PostTransmitFile(connobj, buf);
                }
            }

            LeaveCriticalSection(&connobj->SockCritSec);

            connobj = connobj->next;

            Sleep(gTimeout);

            if (gCurrentConnections == 0)
                break;
        }

    }

    // Free the connection objects if exiting
    connobj = gConnectionList;
    while (connobj)
    {
        tmp = connobj;
        connobj = connobj->next;

        FreeSocketObj(tmp);
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
    GUID         guidConnectEx = WSAID_CONNECTEX,
                 guidTransmitFile = WSAID_TRANSMITFILE;
    DWORD        bytes,
                 flags;
    SOCKET_OBJ  *sockobj=NULL;
    BUFFER_OBJ  *connobj=NULL,
                *buffobj=NULL;
    OVERLAPPED  *lpOverlapped=NULL;
    HANDLE       CompletionPort,
                 hThread,
                 hrc;

    WSADATA      wsd;
    ULONG        lastprint=0;
    int          error,
                 rc,
                 i;
    struct addrinfo *resremote=NULL,
                    *reslocal=NULL,
                    *ptr=NULL;

    // Validate the command line
    ValidateArgs(argc, argv);

    if (gTransmitFile && (gOverlappedCount > 1))
    {
        printf("Can only have one TransmitFile oustanding per connection!\n");
        gOverlappedCount = 1;
    }

    // Create the temp file if TransmitFile is to be used
    if (gTransmitFile)
    {
        gTempFile = CreateTempFile("txfile.tmp", gFileSize);
        if (gTempFile == INVALID_HANDLE_VALUE)
        {
            fprintf(stderr, "Unable to create temp file!\n");
            return -1;
        }
    }

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

    // Resolve the local address
    printf("Local address: %s; Port: %s; Family: %d\n",
            gBindAddr, gBindPort, gAddressFamily);

    resremote = ResolveAddress(gServerAddr, gBindPort, gAddressFamily, gSocketType, gProtocol);
    if (resremote == NULL)
    {
        fprintf(stderr, "ResolveAddress failed to return any addresses!\n");
        return -1;
    }

    // Compute the timeout if rate limiting is selected
    if (gRateLimit != -1)
    {
        gTimeout = (((gConnectionCount * gBufferSize) / gRateLimit) * 1000) / gConnectionCount;

        if (gRateLimit >= 1000000)
        {
            gTimeout /= 2;
        }

        printf("gTimeout == %lu\n", gTimeout);
    }

    // Start the timer for statistics counting
    gStartTime = gStartTimeLast = GetTickCount();

    // For each local address returned, create a listening/receiving socket
    ptr = resremote;
    while (ptr)
    {
        reslocal = ResolveAddress(gBindAddr, "0", ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (reslocal)
        {
            // Initiate the specified number of connections for each server
            //    address returned.
            for(i=0; i < gConnectionCount ;i++)
            {
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
                do
                {
                    SetPort(reslocal->ai_family, reslocal->ai_addr, gLocalPort);

                    rc = bind(sockobj->s, reslocal->ai_addr, reslocal->ai_addrlen);
                    if (rc == SOCKET_ERROR)
                    {
                        // Bail out if the port gets too low
                        if (--gLocalPort == 1024)
                        {
                            fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
                            return -1;
                        }
                    }
                    else
                    {
                        break;
                    }
                } while (1);

                gLocalPort--;

                // Need to load the Winsock extension functions from each provider
                //    -- e.g. AF_INET and AF_INET6. 
                rc = WSAIoctl(
                        sockobj->s,
                        SIO_GET_EXTENSION_FUNCTION_POINTER,
                       &guidConnectEx,
                        sizeof(guidConnectEx),
                       &sockobj->lpfnConnectEx,
                        sizeof(sockobj->lpfnConnectEx),
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
                       &guidTransmitFile,
                        sizeof(guidTransmitFile),
                       &sockobj->lpfnTransmitFile,
                        sizeof(sockobj->lpfnTransmitFile),
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

                connobj = GetBufferObj(gBufferSize);

                // Copy the remote address into the connect object
                memcpy(&connobj->addr, ptr->ai_addr, ptr->ai_addrlen);
                connobj->addrlen = ptr->ai_addrlen;

                sockobj->SendCount = gSendCount;

                // Insert this socket object into the list of pending connects
                InsertSocketObj(&gConnectionList, sockobj);

                PostConnect(sockobj, connobj);

                if (gRateLimit != -1)
                    Sleep(gTimeout);
            }
            freeaddrinfo(reslocal);
        }
        ptr = ptr->ai_next;
    }
    // free the addrinfo structure for the 'bind' address
    freeaddrinfo(resremote);

    if (gRateLimit != -1)
    {
        hThread = CreateThread(NULL, 0, SendThread, (LPVOID)NULL, 0, NULL);
        if (hThread == NULL)
        {
            fprintf(stderr, "CreateThread failed: %d\n", GetLastError());
            return -1;
        }
    }

    lastprint = GetTickCount();

    // Our worker thread is simly our main thread, process the completion
    //    notifications.
    while (1)
    {
        error = NO_ERROR;
        rc = GetQueuedCompletionStatus(
                CompletionPort,
               &bytes,
               (PULONG_PTR)&sockobj,
               &lpOverlapped,
                2000
                );
        if (rc == 0)
        {
            if (((error = GetLastError()) == WAIT_TIMEOUT) ||
				(error == STATUS_TIMEOUT))
            {
                PrintStatistics();
                lastprint = GetTickCount();
                
            }
            else
            {
                fprintf(stderr, "GetQueuedCompletionStatus failed: %d\n", WSAGetLastError());
                rc = WSAGetOverlappedResult(
                        sockobj->s,
                        lpOverlapped,
                       &bytes,
                        FALSE,
                       &flags
                        );
                error = WSAGetLastError();
            }
        }
        else
        {

            buffobj = CONTAINING_RECORD(lpOverlapped, BUFFER_OBJ, ol);

            // Handle IO until 0 is returned -- this indicates that no more socket
            //    connections remain open.
            if (HandleIo(sockobj, buffobj, bytes, error) == 0)
            {
                break;
            }

            if ((GetTickCount() - lastprint) > 2000)
            {
                PrintStatistics();
                lastprint = GetTickCount();
            }
        }

        if (gRateLimit != -1)
        {
            rc = WaitForSingleObject(hThread, 0);
            if (rc != WAIT_TIMEOUT && rc != WAIT_FAILED)
            {
                CloseHandle(hThread);
                break;
            }
        }
    }

    PrintStatistics();

    CloseHandle(CompletionPort);
    CloseHandle(gTempFile);

    WSACleanup();
    return 0;
}
