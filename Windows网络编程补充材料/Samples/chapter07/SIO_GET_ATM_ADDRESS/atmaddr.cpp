//
// Sample:  Enumerate local ATM addresses with SIO_GET_ATM_ADDRESS ioctl
//
// Files:
//      atmaddr.cpp     - this file
//      support.cpp     - support routines
//
// Description:
//      This sample illustrates the SIO_GET_ATM_ADDRESS ioctl for obtaining
//      the list of local ATM addresses.
//
// Compile:
//      cl -o atmaddr.exe atmaddr.cpp support.cpp ws2_32.lib
//
// Usage:
//      atmaddr.exe
//

#include "support.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    WSADATA      wsd;
    SOCKET       s;
    DWORD        deviceID,
                 bytes,
                 dwAddrLen = 256;
    ATM_ADDRESS  addr;
    SOCKADDR_ATM atm_addr;
    char         szAddress[256];
    WSAPROTOCOL_INFO pProtocolInfo;

    // Load Winsock
    if (WSAStartup(MAKEWORD(2,2), &wsd) != 0)
    {
        printf("WSAStartup() failed!\n");
        return -1;
    }

    // Find the ATM provider from the Winsock catalog
    if (FindProtocol(&pProtocolInfo) == FALSE)
    {
        printf("Unable to find an ATM provider!\n");
        WSACleanup();
        return -1;
    }

    // Create an ATM socket
    s = WSASocket(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO,
                  &pProtocolInfo, 0, WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET)
    {
        printf("WSASocket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }

    // Enumerate the addresses
    deviceID = 0;
    if (WSAIoctl(s, SIO_GET_ATM_ADDRESS, (LPVOID) &deviceID, 
        sizeof(DWORD), (LPVOID) &addr, sizeof(ATM_ADDRESS), 
        &bytes, NULL, NULL) == SOCKET_ERROR)
    {
        printf("Error: WSAIoctl %d\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }

    // Copy the address to a SOCKADDR_ATM structure so we can use
    //     WSAAddressToString
    ZeroMemory((PVOID)&atm_addr, sizeof(atm_addr));
    atm_addr.satm_family                 = AF_ATM;
    atm_addr.satm_number.AddressType     = ATM_NSAP;
    atm_addr.satm_number.NumofDigits     = ATM_ADDR_SIZE;
    atm_addr.satm_blli.Layer2Protocol    = SAP_FIELD_ANY;
    atm_addr.satm_blli.Layer3Protocol    = SAP_FIELD_ABSENT;
    atm_addr.satm_bhli.HighLayerInfoType = SAP_FIELD_ABSENT;
    memcpy(atm_addr.satm_number.Addr, &addr.Addr, ATM_ADDR_SIZE);
           ZeroMemory((PVOID)szAddress, sizeof(szAddress));

    // Get the string representation
    if (WSAAddressToString((LPSOCKADDR)&atm_addr, sizeof(atm_addr),
        &pProtocolInfo, szAddress, &dwAddrLen))
    {
        printf("WSAAddressToString: %d\n", WSAGetLastError());
        return(FALSE);
    }
    printf("atm address <%s>\n", szAddress);

    closesocket(s);
    WSACleanup();
    return 0;
}
