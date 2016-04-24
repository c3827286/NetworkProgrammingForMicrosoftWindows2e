// Module Name: nbproto.c
//
// Description:
//
// Compile:
//
// Command Line Options:
//
#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdio.h>
#include <stdlib.h>

//
// A bug in the Winsock catalog entries causes the entry corresponding
// to LANA 0 to evaluate to this number.
//
#define LANA_ZERO_VALUE        0x80000000

//
// Function: FindServiceProvider
//
// Description:
//    Query the installed protocols, searching for those of the 
//    AF_NETBIOS family and printing out the LANA number.
//
int FindServiceProvider()
{
    WSAPROTOCOL_INFO   *lpProtocolBuf = NULL;
    DWORD               dwBufLen = 0,
                        dwErr,
                        dwRet,
                        i;
    long                lAbs;

    // force WSAEnumProtocols to indicate proper size for buffer
    //
    if (SOCKET_ERROR != WSAEnumProtocols(NULL, lpProtocolBuf, 
        &dwBufLen))
    {
        // This should never happen as there is a NULL buffer
        //
	printf("WSAEnumProtocols failed!\n");
	return 1;
    }
    else if (WSAENOBUFS != (dwErr = WSAGetLastError()))
    {
	// We failed for some reason not relating to buffer size - 
        // also odd
        //
	printf("WSAEnumProtocols failed: %d\n", dwErr);
	return 1;
    }
    //
    // Allocate the correct buffer size
    //
    lpProtocolBuf = (WSAPROTOCOL_INFO *)GlobalAlloc(GMEM_FIXED, 
        dwBufLen);
    if (lpProtocolBuf == NULL)
    {
	printf("GlobalAlloc failed: %d\n", GetLastError());
	return 1;
    }
    dwRet = WSAEnumProtocols(NULL, lpProtocolBuf, &dwBufLen);
    if (dwRet == SOCKET_ERROR)
    {
	printf("WSAEnumProtocols failed: %d\n", WSAGetLastError());
	GlobalFree(lpProtocolBuf);
	return 1;
    }
    //
    // Loop through the returned protocol information looking for those
    // that are in the AF_NETBIOS address family.
    //
    for (i=0; i < dwRet ;i++)
    {
	if (lpProtocolBuf[i].iAddressFamily == AF_NETBIOS)
	{
	    lAbs = abs(lpProtocolBuf[i].iProtocol);
            if (lAbs == LANA_ZERO_VALUE)
                printf(" LANA: 0  ");
            else
                printf(" LANA: %ld  ", lAbs);
            printf("Protocol: '%s'\n", lpProtocolBuf[i].szProtocol);
	}
    }
    GlobalFree(lpProtocolBuf);

    return 0;
}

//
// Function: main
//
// Description:
//    Load the Winsock library and call the protocol enumeration 
//    routine.
//
int main(int argc, char *argv[])
{
    WSADATA   wsd; 
    
    if (WSAStartup(MAKEWORD(2,2), &wsd))
    {
	printf("WSAStartup failed to initialize!\n");
        return 1;
    }
    FindServiceProvider();

    WSACleanup();

    return 0;
}
