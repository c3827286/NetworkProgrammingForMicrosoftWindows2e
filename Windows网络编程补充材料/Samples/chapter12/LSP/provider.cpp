// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (C) 1999  Microsoft Corporation.  All Rights Reserved.
//
// Module Name: provider.cpp
//
// Description:
//
//    This sample illustrates how to develop a layered service provider that is
//    capable of counting all bytes transmitted through a TCP/IP socket.
//
//    This file contains support functions that are common to the lsp and
//    the instlsp sample for enumerating the Winsock catalog of service
//    providers.
//    

#include <ws2spi.h>
#include <sporder.h>
#include "install.h"

//
// This is the hardcoded guid for our dummy (hidden) catalog entry
//
GUID ProviderGuid = { //c5fabbd0-9736-11d1-937f-00c04fad860d
    0xc5fabbd0,
    0x9736,
    0x11d1,
    {0x93, 0x7f, 0x00, 0xc0, 0x4f, 0xad, 0x86, 0x0d}
};

//
// Function: GetProviders
//
// Description:
//    This enumerates the Winsock catalog via the global variable ProtocolInfo.
//
LPWSAPROTOCOL_INFOW GetProviders(LPINT TotalProtocols)
{
	INT ErrorCode;

	LPWSAPROTOCOL_INFOW ProtocolInfo = NULL;
	DWORD ProtocolInfoSize = 0;
	*TotalProtocols = 0;

	// Find out how many entries we need to enumerate
	if (WSCEnumProtocols(NULL, ProtocolInfo, &ProtocolInfoSize, &ErrorCode) == SOCKET_ERROR)
	{
		if (ErrorCode != WSAENOBUFS)
		{
			return(NULL);
		}
	}

	if ((ProtocolInfo = (LPWSAPROTOCOL_INFOW) GlobalAlloc(GPTR, ProtocolInfoSize)) == NULL)
	{
		return(NULL);
	}

	if ((*TotalProtocols = WSCEnumProtocols(NULL, ProtocolInfo, &ProtocolInfoSize, &ErrorCode)) == SOCKET_ERROR)
	{

		return(NULL);
	}

	return(ProtocolInfo);
}

//
// Function: FreeProviders
//
// Description:
//    This function frees the global catalog.
//
void FreeProviders(LPWSAPROTOCOL_INFOW ProtocolInfo)
{
	GlobalFree(ProtocolInfo);
}
