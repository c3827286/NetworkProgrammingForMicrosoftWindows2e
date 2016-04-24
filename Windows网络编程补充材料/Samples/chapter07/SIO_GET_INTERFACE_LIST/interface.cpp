//
// Sample: Illustrates the SIO_GET_INTERFACE_LIST ioctl to enumerate local addresses
//
// Files:
//      interface.cpp       - this file
//
// Description:
//      This sample illustrates using the SIO_GET_INTERFACE_LIST ioctl to obtain
//      a list of the local IP interfaces.  This ioctl has been superceded by the
//      SIO_ADDRESS_LIST_QUERY ioctl. Older implementations of ths ioctl used an
//      older structure representation for IPv6 addresses which is no longer 
//      correct.
//
// Compile:
//      cl -o interface.exe interface.cpp ws2_32.lib
//
// Usage:
//      interface.exe
//
#include<winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

int __cdecl main(int argc, char **argv)
{
    WSADATA        wsd; 
    SOCKET         s;
    DWORD          bytesReturned;
    char          *pAddrString=NULL;
    SOCKADDR_IN   *pAddrInet=NULL;
    INTERFACE_INFO localAddr[10];   
    u_long         SetFlags;
    int            numLocalAddr,
                   wsError;

    wsError = WSAStartup(MAKEWORD(2,2), &wsd); 
    if (wsError)
    { 
        fprintf (stderr, "Startup failed\n");
        return -1;
    }

    s = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, 0);
    if (s == INVALID_SOCKET) 
    {
        fprintf (stderr, "Socket creation failed\n");
        WSACleanup();
        return -1;
    }

    fprintf(stderr, "Scanning Interfaces . . .\n\n");
    wsError = WSAIoctl(s, SIO_GET_INTERFACE_LIST, NULL, 0, &localAddr,
            sizeof(localAddr), &bytesReturned, NULL, NULL);
    if (wsError == SOCKET_ERROR) 
    {
        fprintf(stderr, "WSAIoctl fails with error %d\n", GetLastError());
        closesocket(s);
        WSACleanup();
        return -1;
    }

    closesocket(s);

    numLocalAddr = (bytesReturned/sizeof(INTERFACE_INFO));
    for (int i=0; i<numLocalAddr; i++) 
    {
        pAddrInet = (SOCKADDR_IN*)&localAddr[i].iiAddress;
        pAddrString = inet_ntoa(pAddrInet->sin_addr);
        if (pAddrString)
            printf("IP: %s  ", pAddrString);

        pAddrInet = (SOCKADDR_IN*)&localAddr[i].iiNetmask;
        pAddrString = inet_ntoa(pAddrInet->sin_addr);
        if (pAddrString)
            printf(" SubnetMask: %s ", pAddrString);

        pAddrInet = (SOCKADDR_IN*)&localAddr[i].iiBroadcastAddress;
        pAddrString = inet_ntoa(pAddrInet->sin_addr);
        if (pAddrString)
            printf(" Bcast Addr: %s\n", pAddrString);

        SetFlags = localAddr[i].iiFlags;
        if (SetFlags & IFF_UP)
            printf("This interface is up");
        if (SetFlags & IFF_BROADCAST)
            printf(", broadcasts are supported");
        if (SetFlags & IFF_MULTICAST)
            printf(", and so are multicasts");
        if (SetFlags & IFF_LOOPBACK)
            printf(". BTW, this is the loopback interface");
        if (SetFlags & IFF_POINTTOPOINT)
            printf(". BTW, this is a point-to-point link");
        printf("\n\n");

    }
    WSACleanup();

    return 0;
}
