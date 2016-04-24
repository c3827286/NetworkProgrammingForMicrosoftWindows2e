//
// Sample: Definition for common ATM routines
// 
// Files:
//      support.h       - this file
//      
// Description:
//      This file contains the definitions for the common ATM address
//      routines contained in support.cpp.
//      
// Compile:
//      See atmaddr.cpp
//
// Usage:
//      See atmaddr.cpp
//
#include <winsock2.h>
#include <ws2atm.h>

int  GetNumATMInterfaces(SOCKET s);
BOOL GetATMAddress(SOCKET s, int device, ATM_ADDRESS *atmaddr);
BOOL FindProtocol(WSAPROTOCOL_INFO *lpProto);
void AtoH( CHAR *szDest, CHAR *szSource, INT iCount );
