//
// Sample: Function prototypes for Winsock catalog support routines
// 
// Files:
//      provider.h      - this file
//      
// Description:
//      This file contains the prototype for Winsock provider related
//      support routines.
//
#ifndef _PROVIDER_H_
#define _PROVIDER_H_

WSAPROTOCOL_INFO *FindProtocolInfo(int af, int type, int protocol, DWORD flags);

#endif
