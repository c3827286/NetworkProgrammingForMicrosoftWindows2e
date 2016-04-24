/******************************************************************************\
*       This is a part of the Microsoft Source Code Samples.
*       Copyright 1996 - 2001 Microsoft Corporation.
*       All rights reserved.
*       This source code is only intended as a supplement to
*       Microsoft Development Tools and/or WinHelp documentation.
*       See these sources for detailed information regarding the
*       Microsoft samples programs.
\******************************************************************************/

/*
Module Name:

    Ipconfigv6.cpp

Abstract:

    This module illustrates how to programmatically retrieve IPv6 configuration
    information similar to the IPV6.EXE or NETSH.EXE commands.  It demonstrates 
    how to use the IP Helper APIs GetNetworkParams() and GetAdaptersAddresses().

    To execute this application, simply build the application using the Microsoft 
    Visual C++ nmake.exe program generation utility to make an executable 
    ipconfig2.exe.  After the build is complete, simply execute the resulting 
    ipconfig2.exe program.


Author:

    Anthony Jones 11-Jul-01

Revision History:

*/


#include <winsock2.h>
#include <iphlpapi.h>
#include <iptypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

// String constants for IP_PREFIX_ORIGIN enumerated type
const char *PrefixOriginStr[] =
{
    "Other",
    "Manual",
    "Well Known",
    "DHCP",
    "Router Advertisement",
    "6to4"
};

// String constants for IP_SUFFIX_ORIGIN enumerated type
const char *SuffixOriginStr[] =
{
    "Other",
    "Manual",
    "Well Known",
    "DHCP",
    "Link Layer Address",
    "Random"
};

// String constants for IP_DAD_STATE enumerated type
const char *DadStateStr[] =
{
    "Invalid",
    "Tentative",
    "Duplicate",
    "Deprecated",
    "Preferred"
};

// String constants for IF_OPER_STATUS enumerated type
const char *OperStatusStr[] =
{
    "Invalid",
    "Up",                   // IfOperStatusUp
    "Down",                 // IfOperStatusDown
    "Testing",              // IfOperStatusTesting
    "Unknown",              // IfOperStatusUnknown
    "Dormant",              // IfOperStatusDormant
    "Not Present",          // IfOperStatusNotPresent
    "Lower Layer Down"      // IfOperStatusLowerLayerDown
};

//
// Function: usage
// 
// Description:
//    Prints usage information and exits.
//
void usage(char *progname)
{
    fprintf(stderr, "usage: %s [-4] [-6]\n", progname);
    fprintf(stderr, "      -4       Query AF_INET only\n"
                    "      -6       Query AF_INET6 only\n"
                    "      -su      Skip unicast addresses\n"
                    "      -sa      Skip anycast addresses\n"
                    "      -sm      Skip multicast addresses\n"
            );
    ExitProcess(-1);
}

//
// Function: FormatPhysicalAddress
//
// Description:
//    Takes a BYTE array and converts each byte into two hexadecimal
//    digits followd by a dash (-). The converted data is placed
//    into the supplied string buffer.
//
void FormatPhysicalAddress(BYTE *addr, int addrlen, char *buf, int buflen)
{
    char   *ptr=NULL;
    int     i,
            idx=0;

    ptr = buf;
    if (addrlen == 0)
    {
        strcpy(ptr, "NONE");
    }
    else
    {
        for(i=0; i < addrlen ;i++)
        {
            // Take each byte and convert to a hex digit
            _itoa( addr[i] >> 4, ptr, 16);
            ptr++;

            _itoa( addr[i] & 0x0F, ptr, 16);
            ptr++;

            // Add the dash if we're not at the end
            if (i+1 < addrlen)
            {
                *ptr = '-';
                ptr++;
            }
        }
        *ptr = '\0';
    }
}

//
// Function: FormatAdapterType
//
// Description:
//    This function takes the adapter type which is a simple integer
//    and returns the string representation for that type. Since there
//    are many adapter types we just worry about the most common ones.
//    The whole list of adapter types is defined in ipifcons.h.
//
void FormatAdapterType(int type, char *buf, int buflen)
{
    switch (type)
    {
        case MIB_IF_TYPE_OTHER:
            strncpy(buf, "other", buflen);
            break;
        case MIB_IF_TYPE_ETHERNET:
            strncpy(buf, "ethernet", buflen);
            break;
        case MIB_IF_TYPE_TOKENRING:
            strncpy(buf, "token ring", buflen);
            break;
        case MIB_IF_TYPE_FDDI:
            strncpy(buf, "FDDI", buflen);
            break;
        case MIB_IF_TYPE_PPP:
            strncpy(buf, "PPP", buflen);
            break;
        case MIB_IF_TYPE_LOOPBACK:
            strncpy(buf, "loopback", buflen);
            break;
        case MIB_IF_TYPE_SLIP:
            strncpy(buf, "SLIP", buflen);
            break;
        default:
            sprintf(buf, "Other type %d", type);
            break;
    }
}

//
// Function: main
//
// Description:
//    Parse the command line parameters and then obtain the adapter
//    information via the GetAdaptersAddresses function. Print the
//    returned structures to stdout.
//
int _cdecl main(int argc, char **argv) 
{
    PFIXED_INFO                   pFixedInfo;
    DWORD                         FixedInfoSize = 0;

    PIP_ADAPTER_ADDRESSES         pAdapterAddrs, 
                                  pAdapt;
    DWORD                         AdapterAddrsSize;
    PIP_ADDR_STRING               pAddrStr;
    PIP_ADAPTER_UNICAST_ADDRESS   pUnicastAddress;
    PIP_ADAPTER_ANYCAST_ADDRESS   pAnycastAddress;
    PIP_ADAPTER_MULTICAST_ADDRESS pMulticastAddress;

    WSADATA         wsd;
    DWORD           Err,
                    Flags=0;
    int             af, 
                    i;
    char            szAddress[128];
    DWORD           dwAddressLen=128;

    char            buf[128];
    int             buflen=128;

    BOOL            bIndent;

    // Load Winsock for the string conversion utilities
    if (WSAStartup(MAKEWORD(2,2), &wsd) != 0)
    {
        printf("Unable to load winsock!\n");
        return -1;
    }

    // Initialize the variables
    af = AF_UNSPEC;
    Flags = 0;

    // Parse the command line
    for(i=1; i < argc ;i++)
    {
        if (strlen(argv[i]) < 2)
            usage(argv[0]);
        if (argv[i][0] == '-' || argv[i][0] == '/')
        {
            switch (tolower(argv[i][1]))
            {
                case '4':
                    af = AF_INET;
                    break;
                case '6':
                    af = AF_INET6;
                    break;
                case 's':
                    switch (tolower(argv[i][2]))
                    {
                        case 'u':
                            Flags |= GAA_FLAG_SKIP_UNICAST;
                            break;
                        case 'a':
                            Flags |= GAA_FLAG_SKIP_ANYCAST;
                            break;
                        case 'm':
                            Flags |= GAA_FLAG_SKIP_MULTICAST;
                            break;
                        default:
                            usage(argv[0]);
                            break;
                    }
                    break;
                default:
                    usage(argv[0]);
                    break;
            }
        }
    }

    //
    // Get the main IP configuration information for this machine using a FIXED_INFO structure
    //
    if ((Err = GetNetworkParams(NULL, &FixedInfoSize)) != 0)
    {
        if ((Err != ERROR_BUFFER_OVERFLOW) && (Err != ERROR_INSUFFICIENT_BUFFER))
        {
            printf("GetNetworkParams sizing failed with error %d\n", Err);
            return -1;
        }
    }

    // Allocate memory from sizing information
    if ((pFixedInfo = (PFIXED_INFO) GlobalAlloc(GPTR, FixedInfoSize)) == NULL)
    {
        printf("Memory allocation error\n");
        return -1;
    }

    // Print the fixed network parameters
    if ((Err = GetNetworkParams(pFixedInfo, &FixedInfoSize)) == 0)
    {
        printf("\tHost Name . . . . . . . . . : %s\n", pFixedInfo->HostName);
        printf("\tDomain Name . . . . . . . . : %s\n", pFixedInfo->DomainName);
        printf("\tDNS Servers . . . . . . . . : %s\n", pFixedInfo->DnsServerList.IpAddress.String);
        pAddrStr = pFixedInfo->DnsServerList.Next;
        while(pAddrStr)
        {
            printf("                                      %-15s\n", pAddrStr->IpAddress.String);
            pAddrStr = pAddrStr->Next;
        }

        printf("\tNode Type . . . . . . . . . : ");
        switch (pFixedInfo->NodeType)
        {
            case 1:
                printf("%s\n", "Broadcast");
                break;
            case 2:
                printf("%s\n", "Peer to peer");
                break;
            case 4:
                printf("%s\n", "Mixed");
                break;
            case 8:
                printf("%s\n", "Hybrid");
                break;
            default:
                printf("\n");
        }

        printf("\tNetBIOS Scope ID. . . . . . : %s\n", pFixedInfo->ScopeId);
        printf("\tIP Routing Enabled. . . . . : %s\n", (pFixedInfo->EnableRouting ? "yes" : "no"));
        printf("\tWINS Proxy Enabled. . . . . : %s\n", (pFixedInfo->EnableProxy ? "yes" : "no"));
        printf("\tNetBIOS Resolution Uses DNS : %s\n", (pFixedInfo->EnableDns ? "yes" : "no"));
    } else
    {
        printf("GetNetworkParams failed with error %d\n", Err);
        return -1;
    }

    //
    // Enumerate all of the adapter specific information using the 
    // IP_ADAPTER_ADDRESSES structure.
    // Note:  IP_ADAPTER_INFO contains a linked list of adapter entries.
    //
    AdapterAddrsSize = 0;
    if ((Err = GetAdaptersAddresses(af, Flags, NULL, NULL, &AdapterAddrsSize)) != 0)
    {
        if ((Err != ERROR_BUFFER_OVERFLOW) && (Err != ERROR_INSUFFICIENT_BUFFER))
        {
            printf("GetAdaptersAddresses sizing failed with error %d\n", Err);
            printf("err = %d; AdapterAddrsSize = %d\n", Err, AdapterAddrsSize);
            return -1;
        }
    }

    // Allocate memory from sizing information
    if ((pAdapterAddrs = (PIP_ADAPTER_ADDRESSES) GlobalAlloc(GPTR, AdapterAddrsSize)) == NULL)
    {
        printf("Memory allocation error\n");
        return -1;
    }

    // Get actual adapter information
    if ((Err = GetAdaptersAddresses(af, Flags, NULL, pAdapterAddrs, &AdapterAddrsSize)) != ERROR_SUCCESS)
    {
        printf("GetAdaptersAddresses failed with error %d\n", Err);
        return -1;
    }

    // Enumerate through each retuned adapter and print its information
    pAdapt = pAdapterAddrs;
    while (pAdapt)
    {
        printf("\n");
        printf("\tDescription  : %S\n", pAdapt->Description);
        printf("\t   Adapter Name : %s\n", pAdapt->AdapterName);

        printf("\t   DNS Suffix   : %S\n", pAdapt->DnsSuffix);
        printf("\t   Friendly Name: %S\n", pAdapt->FriendlyName);

        FormatPhysicalAddress(pAdapt->PhysicalAddress, pAdapt->PhysicalAddressLength, buf, buflen);
        printf("\t   Physical Addr: %s\n", buf);

        printf("\t   MTU . . . . . . . . . . : %d\n",  pAdapt->Mtu);
        FormatAdapterType(pAdapt->IfType, buf, buflen);
        printf("\t   Interface Type  . . . . : %s\n",  buf);
        printf("\t   Interface Index:  . . . : %d\n",  pAdapt->IfIndex);

        printf("\t   Flags:  . . . . . . . . : ");
        bIndent = FALSE;
        if (pAdapt->Flags & IP_ADAPTER_DDNS_ENABLED)
        {
            printf("DDNS Enabled\n");
            bIndent = TRUE;
        }
        if (pAdapt->Flags & IP_ADAPTER_REGISTER_ADAPTER_SUFFIX)
        {
            if (bIndent == TRUE)
                printf("\t                          ");
            printf("Register DNS Adapter Suffix\n");
            bIndent = TRUE;
        }
        if (pAdapt->Flags & IP_ADAPTER_DHCP_ENABLED)
        {
            if (bIndent == TRUE)
                printf("\t                          ");
            printf("DHCP Enabled\n");
            bIndent = TRUE;
        }

        pUnicastAddress = pAdapt->FirstUnicastAddress;
        if (pUnicastAddress)
            printf("\t   UNICAST ADDRESS(ES):\n");
        while (pUnicastAddress)
        {
            printf("\t      Flags:  . . . . . . . . : ");
            if (pUnicastAddress->Flags == 0)
                printf("None");
            if ((pUnicastAddress->Flags & IP_ADAPTER_ADDRESS_DNS_ELIGIBLE) == IP_ADAPTER_ADDRESS_DNS_ELIGIBLE)
                printf("DNS_ELIGIBLE ");
            if ((pUnicastAddress->Flags & IP_ADAPTER_ADDRESS_TRANSIENT) == IP_ADAPTER_ADDRESS_TRANSIENT)
                printf("TRANSIENT");
            printf("\n");

            dwAddressLen = 128;
            memset(szAddress, 0, 128);
            if (WSAAddressToStringA(
                pUnicastAddress->Address.lpSockaddr,
                pUnicastAddress->Address.iSockaddrLength,
                NULL,
                szAddress,
                &dwAddressLen) == SOCKET_ERROR)
            {
                printf("WSAAddressToString failed:% d\n", WSAGetLastError());
            }
            printf("\t      Address:  . . . . . . . : %s\n", szAddress);

            printf("\t      Valid Lifetime  . . . . : %lu\n", pUnicastAddress->ValidLifetime);
            printf("\t      Preferred Lifetime: . . : %lu\n", pUnicastAddress->PreferredLifetime);
            printf("\t      Lease Lifetime: . . . . : %lu\n", pUnicastAddress->LeaseLifetime);
            printf("\t      Prefix Origin:  . . . . : %s\n",  PrefixOriginStr[pUnicastAddress->PrefixOrigin]);
            printf("\t      Suffix Origin:  . . . . : %s\n",  SuffixOriginStr[pUnicastAddress->SuffixOrigin]);
            printf("\t      Dad State:  . . . . . . : %s\n",  DadStateStr[pUnicastAddress->DadState]);

            pUnicastAddress = pUnicastAddress->Next;
            printf("\n");
        }
        //
        // Print anycast addresses
        //
        pAnycastAddress = pAdapt->FirstAnycastAddress;
        if (pAnycastAddress)
            printf("\t   ANYCAST ADDRESS(ES):\n");
        while (pAnycastAddress)
        {
            dwAddressLen = 128;
            memset(szAddress, 0, 128);
            if (WSAAddressToStringA(
                pAnycastAddress->Address.lpSockaddr,
                pAnycastAddress->Address.iSockaddrLength,
                NULL,
                szAddress,
                &dwAddressLen) == SOCKET_ERROR)
            {
                printf("WSAAddressToString failed:% d\n", WSAGetLastError());
            }
            printf("\t      Address:  . . . . . . . : %s\n", szAddress);
            printf("\t      Flags:  . . . . . . . . : "); //lu\n", pAnycastAddress->Flags);
            if (pAnycastAddress->Flags == 0)
                printf("None");
            else if ((pAnycastAddress->Flags & IP_ADAPTER_ADDRESS_DNS_ELIGIBLE) == IP_ADAPTER_ADDRESS_DNS_ELIGIBLE)
                printf("DNS_ELIGIBLE ");
            else if ((pAnycastAddress->Flags & IP_ADAPTER_ADDRESS_TRANSIENT) == IP_ADAPTER_ADDRESS_TRANSIENT)
                printf("TRANSIENT");
            printf("\n");

            pAnycastAddress = pAnycastAddress->Next;
        }
        //
        // Print multicast addresses
        //
        pMulticastAddress = pAdapt->FirstMulticastAddress;
        if (pMulticastAddress)
            printf("\t   MULTICAST ADDRESS(ES):\n", pAdapt->FirstMulticastAddress);
        while (pMulticastAddress)
        {
            dwAddressLen = 128;
            memset(szAddress, 0, 128);
            if (WSAAddressToStringA(
                pMulticastAddress->Address.lpSockaddr,
                pMulticastAddress->Address.iSockaddrLength,
                NULL,
                szAddress,
                &dwAddressLen) == SOCKET_ERROR)
            {
                printf("WSAAddressToString failed:% d\n", WSAGetLastError());
            }
            printf("\t      Address:  . . . . . . . : %s\n", szAddress);
            printf("\t      Flags:  . . . . . . . . : ");
            if (pMulticastAddress->Flags == 0)
                printf("None");
            else if ((pMulticastAddress->Flags & IP_ADAPTER_ADDRESS_DNS_ELIGIBLE) == IP_ADAPTER_ADDRESS_DNS_ELIGIBLE)
                printf("DNS_ELIGIBLE ");
            else if ((pMulticastAddress->Flags & IP_ADAPTER_ADDRESS_TRANSIENT) == IP_ADAPTER_ADDRESS_TRANSIENT)
                printf("TRANSIENT");
            printf("\n");

            pMulticastAddress = pMulticastAddress->Next;
        }

        pAdapt = pAdapt->Next;
    }
    WSACleanup();

    return 0;
}

