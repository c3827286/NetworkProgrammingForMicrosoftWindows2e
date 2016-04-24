//
// Sample: Support routines for ATM address manipulation
//
// Files:
//      support.cpp     - support routines for ATM addresses
//      support.h       - definition of support routines
//
// Description:
//      This file contains various support routines for ATM such as finding
//      the ATM Winsock provider by enumerating the Winsock catalog. It also
//      contains manual routines for converting strings into ATM addresses
//      as older platforms may not support the ATM address family in the
//      WSAAddressToString and WSAStringToAddress APIs.
//
// Compile:
//      See atmaddr.cpp
//
// Usage:
//      See atmaddr.cpp
//
#include "support.h"

#include <stdio.h>
#include <stdlib.h>

UCHAR BtoH( CHAR ch );

//
// Function: GetNumATMInterfaces
// 
// Description:
//      This routine returns the number of ATM interfaces present on the
//      local machine by calling the SIO_GET_NUMBER_OF_ATM_DEVICES ioctl.
//      The only input parameter to this function is an ATM socket previously
//      created.
//
int  GetNumATMInterfaces(SOCKET s)
{
    DWORD  dwNum,
           nbytes=sizeof(DWORD);

    if (WSAIoctl(s, SIO_GET_NUMBER_OF_ATM_DEVICES, NULL, 0,
        (LPVOID)&dwNum, sizeof(DWORD), &nbytes, NULL, NULL) == SOCKET_ERROR)
    {
        return -1;
    }
    return dwNum;
}

//
// Function: GetATMAddress
//
// Description:
//      This routine returns the ATM address for the specified ATM adapter
//      given by the adapter index (0 to n-1). The number of adapters is
//      obtained from the SIO_GET_NUMBER_OF_ATM_DEVICES ioctl called in
//      GetNumATMInterfaces.  This function returns the ATM_ADDRESS
//      structure for that adapter. The input parameter is a previously
//      created ATM socket, the device index, and an ATM_ADDRESS buffer.
//
BOOL GetATMAddress(SOCKET s,int device, ATM_ADDRESS *atmaddr)
{
    DWORD bytes;

    if (WSAIoctl(s, SIO_GET_ATM_ADDRESS, (LPVOID)&device, sizeof(DWORD),
        (LPVOID)atmaddr, sizeof(ATM_ADDRESS), &bytes, NULL, 
        NULL) == SOCKET_ERROR)
    {
        return FALSE;
    }
    return TRUE;
}

//
// Function: FindProtocol
// 
// Description:
//      This routine returns the WSAPROTOCOL_INFO of the ATM provider installed
//      in the Winsock catalog. If no suitable ATM provider is found FALSE is
//      returned.
//
BOOL FindProtocol(WSAPROTOCOL_INFO *lpProto)
{
    WSAPROTOCOL_INFO *lpProtocolBuf=NULL;
    DWORD             dwErr,
                      dwRet,
                      dwBufLen=0,
                      i;

    if (WSAEnumProtocols(NULL, lpProtocolBuf, 
        &dwBufLen) != SOCKET_ERROR)
    {
        // This should never happen as there is a NULL buffer
        //
        printf("WSAEnumProtocols failed!\n");
        return FALSE;
    }
    else if ((dwErr = WSAGetLastError()) != WSAENOBUFS)
    {
        // We failed for some reason not relating to buffer size - 
        // also odd
        //
        printf("WSAEnumProtocols failed: %d\n", dwErr);
        return FALSE;
    }
    // Allocate the correct buffer size for WSAEnumProtocols as
    // well as the buffer to return
    //
    lpProtocolBuf = (WSAPROTOCOL_INFO *)GlobalAlloc(GMEM_FIXED, 
        dwBufLen);

    if (lpProtocolBuf == NULL)
    {
        printf("GlobalAlloc failed: %d\n", GetLastError());
        return FALSE;
    }
    dwRet = WSAEnumProtocols(NULL, lpProtocolBuf, &dwBufLen);
    if (dwRet == SOCKET_ERROR)
    {
        printf("WSAEnumProtocols failed: %d\n", WSAGetLastError());
        GlobalFree(lpProtocolBuf);
        return FALSE;
    }
    // Loop through the returned protocol information looking for those
    // that are in the AF_NETBIOS address family.
    //
    for (i=0; i < dwRet ;i++)
    {
        printf("Protocol: %s\n", lpProtocolBuf[i].szProtocol);
	if ( (lpProtocolBuf[i].iAddressFamily == AF_ATM) &&
             (lpProtocolBuf[i].iSocketType == SOCK_RAW) &&
             (lpProtocolBuf[i].iProtocol == ATMPROTO_AAL5) )
        {
            memcpy(lpProto, &lpProtocolBuf[i], sizeof(WSAPROTOCOL_INFO));
            GlobalFree(lpProtocolBuf);
            return TRUE;
        }
    }
    GlobalFree(lpProtocolBuf);

    return FALSE;
}

//
// Function: AtoH
//
// Description:
//     AtoH () coverts the ATM address specified in the string(ascii) 
//     format to the binary(hexadecimal) format.
//
void AtoH( CHAR *szDest, CHAR *szSource, INT iCount )
{
    while (iCount--)
    {
        *szDest++ = ( BtoH ( *szSource++ ) << 4 ) + BtoH ( *szSource++ );
    }
    return;
}

//
// Function: BtoH
//
// Description:
//     BtoH () returns the equivalent binary value for an individual
//     character specified in the ascii format.
//
UCHAR BtoH( CHAR ch )
{
    if ( ch >= '0' && ch <= '9' )
    {
        return ( ch - '0' );
    }

    if ( ch >= 'A' && ch <= 'F' )
    {
        return ( ch - 'A' + 0xA );
    }

    if (ch >= 'a' && ch <= 'f' )
    {
        return ( ch - 'a' + 0xA );
    }
    //
    // Illegal values in the address will not be excepted
    //
    return -1;
}
