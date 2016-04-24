//
// Sample: SIO_ADDRESS_LIST_QUERY and SIO_ADDRESS_LIST_CHANGE ioctls
//
// Files:
//      addrquery.cpp       - this file
//      resolve.cpp         - common routines for resolving addresses, etc.
//      resolve.h           - header file for resolve.cpp
//
// Description:
//      This sample illustrates how an application can query for all local
//      IPv4 and IPv6 interfaces as well as register for notification when
//      the local addresses change. This sample enumerates the local interfaces
//      and then registers for a change notfication. To trigger the notification
//      you can either disable the adapter from the network controll panel or
//      unplug the network cable. After the event occurs, the sample retrieves
//      the current interface list and registers for the event again, etc.
//
// Compile:
//      cl -o addrquery.exe addrquery.cpp resolve.cpp ws2_32.lib
//
// Usage:
//      addrquery.exe [options]
//          -a 4|6 Specifies the address family (default = AF_UNSPEC)
//             4       AF_INET
//             6       AF_INET6
//          -s     Sort addresses (supported on v6 only)
//
#include <winsock2.h>
#include <ws2tcpip.h>

#include "resolve.h"

#include <stdio.h>
#include <stdlib.h>

#define ADDRESS_LIST_BUFFER_SIZE        4096

int gAddressFamily=AF_UNSPEC;           // Address family to use
int gSortAddresses=FALSE;               // Sort addresses first?

// 
// Function: usage
// 
// Description:
//    Print usage information and exit.
//
void usage(char *progname)
{
    fprintf(stderr, "usage: %s [-a 4|6] [-s]\n"
                    "       -a 4|6      Specifies the address family (default = AF_UNSPEC)\n"
                    "           4       AF_INET\n"
                    "           6       AF_INET6\n"
                    "       -s          Sort addresses\n",
                    progname
                    );
    ExitProcess(-1);
}

//
// Function: main
//
// Description:
//    Parse the command line, enumeate the local addresses (IPv4/6), enumerate the
//    interfaces, and listen for address change notifications. This is performed
//    in an infinite loop.
//
int __cdecl main(int argc, char **argv)
{
    WSADATA          wsd;
    SOCKET           s[MAXIMUM_WAIT_OBJECTS];
    WSAEVENT         hEvent[MAXIMUM_WAIT_OBJECTS];
    WSAOVERLAPPED    ol[MAXIMUM_WAIT_OBJECTS];
    struct addrinfo *local=NULL,
                    *ptr=NULL;
    SOCKET_ADDRESS_LIST *slist=NULL;
    DWORD            bytes;
    char             addrbuf[ADDRESS_LIST_BUFFER_SIZE];
    int              socketcount=0,
                     addrbuflen=ADDRESS_LIST_BUFFER_SIZE,
                     rc,
                     i, j;


    for(i=1; i < argc ;i++)
    {
        if ( ((argv[i][0] != '-') && (argv[i][0] != '/')) || (strlen(argv[i]) < 2) )
            usage(argv[0]);

        switch (tolower(argv[i][1]))
        {
            case 'a':
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
            case 's':
                gSortAddresses = TRUE;
                break;
            default:
                usage(argv[0]);
                break;
        }
    }

    // Load Winsock
    rc = WSAStartup(MAKEWORD(2,2), &wsd);
    if (rc != 0)
    {
        fprintf(stderr, "Unable to load Winsock: %d\n", rc);
        return -1;
    }

    // Enumerate the local bind addresses - to wait for changes we only need
    //    one socket but to enumerate the addresses for a particular address
    //    family, we need a socket of that type
    local = ResolveAddress(NULL, "0", gAddressFamily, SOCK_DGRAM, IPPROTO_UDP);
    if (local == NULL)
    {
        fprintf(stderr, "Unable to resolve the bind address!\n");
        return -1;
    }

    // Create a socket and event for each address returned
    ptr = local;
    while (ptr)
    {
        s[socketcount] = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (s[socketcount] == INVALID_SOCKET)
        {
            fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
            return -1;
        }

        hEvent[socketcount] = WSACreateEvent();
        if (hEvent == NULL)
        {
            fprintf(stderr, "WSACreateEvent failed: %d\n", WSAGetLastError());
            return -1;
        }

        socketcount++;

        ptr = ptr->ai_next;

        if (ptr && (socketcount > MAXIMUM_WAIT_OBJECTS))
        {
            printf("Too many address families returned!\n");
            break;
        }
    }

    // Go into a loop - first enumerate the addresses then wait for a change
    while (1)
    {
        for(i=0; i < socketcount ;i++)
        {
            memset(&ol[i], 0, sizeof(WSAOVERLAPPED));

            ol[i].hEvent = hEvent[i];

            rc = WSAIoctl(
                    s[i],
                    SIO_ADDRESS_LIST_QUERY,
                    NULL,
                    0,
                    addrbuf,
                    addrbuflen,
                   &bytes,
                    NULL,
                    NULL
                    );
            if (rc == SOCKET_ERROR)
            {
                fprintf(stderr, "WSAIoctl: SIO_ADDRESS_LIST_QUERY failed: %d\n", WSAGetLastError());
                return -1;
            }

            // If requestd, sort the addresses. Note that the sort ioctl only works
            //    with IPv6 addresses (as it doesn't make sense for IPv4)
            if (gSortAddresses)
            {
                rc = WSAIoctl(
                        s[i],
                        SIO_ADDRESS_LIST_SORT,
                        addrbuf,
                        bytes,
                        addrbuf,
                        addrbuflen,
                       &bytes,
                        NULL,
                        NULL
                        );
                if (rc == SOCKET_ERROR)
                {
                    fprintf(stderr, "WSAIoctl: SIO_ADDRESS_LIST_SORT failed: %d\n", WSAGetLastError());
                }
            }

            // Print out the addresses
            slist = (SOCKET_ADDRESS_LIST *)addrbuf;
            for(j=0; j < slist->iAddressCount ;j++)
            {
                printf("Address [%d]: ", j);
                PrintAddress(slist->Address[j].lpSockaddr, slist->Address[j].iSockaddrLength);
                printf("\n");
            }
            printf("\n");

            // Register for change notification
            rc = WSAIoctl(
                    s[i],
                    SIO_ADDRESS_LIST_CHANGE,
                    NULL,
                    0,
                    NULL,
                    0,
                   &bytes,
                   &ol[i],
                    NULL
                    );
            if (rc == SOCKET_ERROR)
            {
                if (WSAGetLastError() != WSA_IO_PENDING)
                {
                    fprintf(stderr, "WSAIoctl: SIO_ADDRESS_LIST_CHANGE failed: %d\n", WSAGetLastError());
                    return -1;
                }
            }
        }

        // Actually, when you unplug/plug in the cable the socket will receive
        //    more than one notification -- one for the cable removal/insertion
        //    and one or more for DHCP nofication
        printf("Unplug network cable or disable adapter...\n");

        rc = WaitForMultipleObjectsEx(socketcount, hEvent, FALSE, INFINITE, TRUE);
        if (rc == WAIT_FAILED || rc == WAIT_TIMEOUT)
        {
            fprintf(stderr, "WaitForMultipleObjectsEx failed: %d\n", GetLastError());
            return -1;
        }

        printf("Address list change signaled!\n");

        WSAResetEvent(hEvent[rc - WAIT_OBJECT_0]);

        // Event was signaled, go back and enumerate the addresses...
    }

    freeaddrinfo(local);

    for(i=0; i < socketcount ;i++)
    {
        closesocket(s[i]);
    }

    WSACleanup();

    return 0;
}
