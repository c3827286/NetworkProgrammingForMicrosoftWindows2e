//
// Sample: SIO_INTERFACE_LIST_QUERY and SIO_INTERFACE_LIST_CHANGE ioctls
//
// Files:
//      routequery.cpp      - this file
//      resolve.cpp         - common routines for resolving addresses, etc.
//      resolve.h           - header file for resolve.cpp
//
// Description:
//      This sample illustrates how an application can query for the local
//      route which is used to reach a given destination address. After
//      the route is resolved, it registers for any changes on that route.
//      To trigger nofication you can either disable the corresponding
//      interface in the network connection folder or by unplugging the 
//      network cable. After the event is triggered, the route is queried
//      again.
//
// Compile:
//      cl -o routequery.exe routequery.cpp resolve.cpp ws2_32.lib
//
// Usage:
//      routequery.exe [options]
//          -a 4|6      Specifies the address family (default = AF_UNSPEC)
//              4       AF_INET
//              6       AF_INET6
//          -n dest     Destination address/host to find a route to
//
#include <winsock2.h>
#include <ws2tcpip.h>

#include "resolve.h"

#include <stdio.h>
#include <stdlib.h>

#define ADDRESS_LIST_BUFFER_SIZE        4096

int   gAddressFamily=AF_UNSPEC;           // Address family to use
char *gRouteToAddress=NULL;               // Address to find the local interface
                                          // that it is reachable from
// 
// Function: usage
// 
// Description:
//    Print usage information and exit.
//
void usage(char *progname)
{
    fprintf(stderr, "usage: %s [-a 4|6] [-n destination]\n"
                    "       -a 4|6      Specifies the address family (default = AF_UNSPEC)\n"
                    "           4       AF_INET\n"
                    "           6       AF_INET6\n"
                    "       -n dest     Destination address/host to find a route to\n",
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
    SOCKADDR_STORAGE LocalIf;
    struct addrinfo *routeto=NULL,
                    *ptr=NULL;
    DWORD            bytes;
    int              socketcount=0,
                     rc,
                     i;

    // Parse the command line
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
            case 'n':
                gRouteToAddress = argv[++i];
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
    routeto = ResolveAddress(gRouteToAddress, "0", gAddressFamily, SOCK_DGRAM, IPPROTO_UDP);
    if (routeto == NULL)
    {
        fprintf(stderr, "Unable to resolve the bind address!\n");
        return -1;
    }

    // Create a socket and event for each address returned
    ptr = routeto;
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
        i=0;
        ptr = routeto;
        while (ptr)
        {
            memset(&ol[i], 0, sizeof(WSAOVERLAPPED));

            ol[i].hEvent = hEvent[i];

            rc = WSAIoctl(
                    s[i],
                    SIO_ROUTING_INTERFACE_QUERY,
                    ptr->ai_addr,
                    ptr->ai_addrlen,
                    (SOCKADDR *)&LocalIf,
                    sizeof(LocalIf),
                   &bytes,
                    NULL,
                    NULL
                    );
            if (rc == SOCKET_ERROR)
            {
                if (WSAGetLastError() == WSAEHOSTUNREACH)
                {
                    printf("No route to host: %s\n", gRouteToAddress);
                }
                else
                {
                    fprintf(stderr, "WSAIoctl: SIO_ROUTING_INTERFACE_QUERY failed: %d\n", WSAGetLastError());
                    return -1;
                }
            }
            else
            {
                // Print out the addresses
                printf("Local interface: ");
                PrintAddress((SOCKADDR *)&LocalIf, bytes);
                printf(" to reach: %s\n", gRouteToAddress);
            }

            // Register for change notification
            rc = WSAIoctl(
                    s[i],
                    SIO_ROUTING_INTERFACE_CHANGE,
                    ptr->ai_addr,
                    ptr->ai_addrlen,
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
                    fprintf(stderr, "WSAIoctl: SIO_ROUTING_INTERFACE_CHANGE failed: %d\n", WSAGetLastError());
                    return -1;
                }
            }

            // Increment our counter/pointer
            i++;
            ptr = ptr->ai_next;

            // If a ton of address families were returned, make sure we don't exceed
            //    our limit.
            if (ptr && (socketcount > MAXIMUM_WAIT_OBJECTS))
            {
                break;
            }
        }
        printf("\n");

        printf("Unplug network cable or disable adapter...\n");

        rc = WaitForMultipleObjectsEx(socketcount, hEvent, FALSE, INFINITE, TRUE);
        if (rc == WAIT_FAILED || rc == WAIT_TIMEOUT)
        {
            fprintf(stderr, "WaitForMultipleObjectsEx failed: %d\n", GetLastError());
            return -1;
        }

        printf("Routing interface change signaled!\n");

        WSAResetEvent(hEvent[rc - WAIT_OBJECT_0]);

        // Event was signaled, go back and find the route to the destination...
    }

    freeaddrinfo(routeto);

    for(i=0; i < socketcount ;i++)
    {
        closesocket(s[i]);
    }

    WSACleanup();

    return 0;
}
