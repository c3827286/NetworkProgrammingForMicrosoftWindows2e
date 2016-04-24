// Module: nspsvc.cpp
//
// Description:
//    This file contains support routines used both by our namespace
//    DLL and by our namespace service. These functions pertain to 
//    marshalling and demarshalling WSASERVICECLASSINFOW and 
//    WSAQUERYSETW structures. Additionally, this module contains
//    functions for sending and receiving data.
//
#include <winsock2.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>

#include "nspsvc.h"

//
// Function: MarshallServiceClassInfo
//
// Description:
//    This routine takes a WSASERVICECLASSINFOW structure and marshalls
//    the data contained within into a contiguous buffer. This is done
//    so the structure can be sent across a socket connection and the
//    other side can reassemble the structure. The size parameter will
//    be set to the total size of the marshalled structure.
//
int MarshallServiceClassInfo(WSASERVICECLASSINFOW *sc, char *buf, int *size)
{
    BYTE *ptr;
    int   totalsz, sz;

    ptr = (BYTE *)buf;
    totalsz = 0;

    // First copy the structure into the buffer
    //
    memcpy(buf, sc, (sz = sizeof(WSASERVICECLASSINFOW)));
    ptr += sz;
    totalsz += sz;

    // For each field in the structure copy its data into the buffer
    //
    if (sc->lpServiceClassId)
    {
        memcpy(ptr, sc->lpServiceClassId, (sz = sizeof(GUID)));
        ptr += sz;
        totalsz += sz;
    }
    if (sc->lpszServiceClassName)
    {        
        memcpy(ptr, sc->lpszServiceClassName, 
            (sz = (lstrlenW(sc->lpszServiceClassName)+1) * sizeof(WCHAR)));
        ptr += sz;
        totalsz += sz; 
    }
    if (sc->lpClassInfos)
    {
        memcpy(ptr, sc->lpClassInfos, (sz = sizeof(WSANSCLASSINFOW) * sc->dwCount));
        ptr += sz;
        totalsz += sz;
    }
    *size = totalsz;

    return 0;
}

//
// Function: DeMarshallServiceClassInfo
//
// Description:
//    This routine is the opposite of MarshallServiceClassInfo. It takes
//    a buffer which contains the raw data received on a socket and
//    reassembles it into a WSASERVICECLASSINFOW structure.
//
int DeMarshallServiceClassInfo(WSASERVICECLASSINFOW *sc, char *buf)
{
    BYTE          *ptr=NULL,            // source buffer
                  *ptrdest=NULL;        // the reassembled structure
    unsigned int   sz;

    // Setup some pointers to both the source and destination buffers
    //
    ptr = (BYTE *)buf;
    ptrdest = (BYTE *)sc;
    //
    // Copy the first part of the source into the destinatin buffer.
    //  This is the WSASERVICECLASSINFOW structure.
    //
    sz = sizeof(WSASERVICECLASSINFOW);
    memcpy(sc, ptr, sz);
    ptr = ptr + sz;
    ptrdest = ptrdest + sz;
    //
    // Copy each field that is present into the destination buffer.
    //  If the source field is a pointer and is NULL this means 
    //  there is no data for that field (i.e. a NULL pointer).
    //  Otherwise make sure the field that do contain data point
    //  to memory locations valid on this side of the connection.
    // 
    if (sc->lpServiceClassId)
    {
        sz = sizeof(GUID);
        memcpy(ptrdest, ptr, sz);
        sc->lpServiceClassId = (LPGUID)ptrdest;

        ptr = ptr + sz;
        ptrdest = ptrdest + sz;
    }
    if (sc->lpszServiceClassName)
    {
        sz = (lstrlenW((WCHAR *)ptr) + 1) * sizeof(WCHAR);
        memcpy(ptrdest, ptr, sz);
        sc->lpszServiceClassName = (LPWSTR)ptrdest;

        ptr = ptr + sz;
        ptrdest = ptrdest + sz;
    }
    if (sc->lpClassInfos)
    {
        sz = sizeof(WSANSCLASSINFOW) * sc->dwCount;
        memcpy(ptrdest, ptr, sz);
        sc->lpClassInfos = (LPWSANSCLASSINFOW)ptrdest;

        ptr = ptr + sz;
        ptrdest = ptrdest + sz;
    }

    return 0;
}

//
// Function: MarshallServiceInfo
//
// Description:
//    This routine takes a WSAQUERYSETW structure and marshalls
//    the data contained within into a contiguous buffer. This is done
//    so the structure can be sent across a socket connection and the
//    other side can reassemble the structure. The size parameter will
//    be set to the total size of the marshalled structure.
//
int MarshallServiceInfo(WSAQUERYSETW *qs, char *buf, int *size)
{
    BYTE        *ptr=NULL;
    unsigned int sz;
    int          totalsz;
        
    // Setup a pointer to the destination buffer
    //
    ptr = (BYTE *)buf;
    totalsz = 0;
    //
    // Copy the WSAQUERYSETW structure at the head of the buffer
    //
    memcpy(buf, qs, sizeof(WSAQUERYSETW));
        sz = sizeof(WSAQUERYSETW);
        
    ptr = ptr + sz;
    totalsz += sz;
    //
    // Take each non-NULL pointer and copy its data into the buffer
    //
    if (qs->lpszServiceInstanceName)
    {
        sz = (lstrlenW(qs->lpszServiceInstanceName) + 1) * sizeof(WCHAR);
        memcpy(ptr, qs->lpszServiceInstanceName, sz);

        ptr = ptr + sz;        	
        totalsz += sz;
    }
    if (qs->lpServiceClassId)
    {
        sz = sizeof(GUID);
        memcpy(ptr, qs->lpServiceClassId, sz);

        ptr = ptr + sz;
        totalsz += sz; 
    }
    if (qs->lpVersion)
    {
        sz = sizeof(WSAVERSION);
        memcpy(ptr, qs->lpVersion, sz);
        
        ptr = ptr + sz;
        totalsz += sz;
    }
    if (qs->lpszComment)
    {
        sz = (lstrlenW(qs->lpszComment) + 1) * sizeof(WCHAR);
        memcpy(ptr, qs->lpszComment, sz);
                
        ptr = ptr + sz;
        totalsz += sz;
    }
    if (qs->lpNSProviderId)
    {
        sz = sizeof(GUID);
        memcpy(ptr, qs->lpNSProviderId, sz);
        
        ptr = ptr + sz;
        totalsz += sz;
    }
    if (qs->lpszContext)
    {
        sz = (lstrlenW(qs->lpszContext) + 1) * sizeof(WCHAR);
        memcpy(ptr, qs->lpszContext, sz);

        ptr = ptr + sz;
        totalsz += sz;
    }
    if (qs->lpafpProtocols)
    {
        sz = sizeof(AFPROTOCOLS) * qs->dwNumberOfProtocols;
        memcpy(ptr, qs->lpafpProtocols, sz);
        
        ptr = ptr + sz;
        totalsz += sz;
    }
    if (qs->lpszQueryString)
    {
        sz = (lstrlenW(qs->lpszQueryString) + 1) * sizeof(WCHAR);
        memcpy(ptr, qs->lpszQueryString, sz);
        
        ptr = ptr + sz;
        totalsz += sz;
    }
    if (qs->lpcsaBuffer)
    {
        sz = sizeof(CSADDR_INFO) * qs->dwNumberOfCsAddrs;
        memcpy(ptr, qs->lpcsaBuffer, sz);

        ptr = ptr + sz;
        totalsz += sz;

        for(DWORD i=0; i < qs->dwNumberOfCsAddrs ;i++)
        {
            sz = qs->lpcsaBuffer[i].LocalAddr.iSockaddrLength;
            memcpy(ptr, qs->lpcsaBuffer[i].LocalAddr.lpSockaddr, sz);
            ptr = ptr + sz;
            totalsz += sz;
            
            sz = qs->lpcsaBuffer[i].RemoteAddr.iSockaddrLength;
            memcpy(ptr, qs->lpcsaBuffer[i].RemoteAddr.lpSockaddr, sz);
            ptr = ptr + sz;
            totalsz += sz;
        }
    }

    *size = totalsz;
    return 0;
}

// 
// Function: DeMarshallServiceInfo
//
// Description:
//    This routine is the opposite of MarshallServiceInfo. It takes
//    a buffer which contains the raw data received on a socket and
//    reassembles it into a WSAQUERYSETW structure.
//
int DeMarshallServiceInfo(WSAQUERYSETW *qs, char *buf)
{
    BYTE         *ptr=NULL,
                 *ptrdest=NULL;
    unsigned int  sz=0;

    // Setup pointers to the source and destination buffers 
    //
    ptr = (BYTE *)buf;
    ptrdest = (BYTE *)qs;
    //
    // Copy the first portion of the source into the destination.
    //  This is the WSAQUERYSETW structure
    //
    sz = sizeof(WSAQUERYSETW);
    memcpy(qs, ptr, sz);

    ptr = ptr + sz;
    ptrdest = ptrdest + sz;
    //
    // For each non-NULL member of the WSAQUERYSETW, we need to copy
    //  data from the source into the destination and fix the pointer
    //  to point to the correct memory location
    // 
    if (qs->lpszServiceInstanceName)
    {
        sz = (lstrlenW((WCHAR *)ptr)+1) * sizeof(WCHAR);
        memcpy(ptrdest, ptr, sz);
        qs->lpszServiceInstanceName = (LPWSTR)ptrdest;

        ptr = ptr + sz;
        ptrdest = ptrdest + sz;
    }
    if (qs->lpServiceClassId)
    {
        sz = sizeof(GUID);
        memcpy(ptrdest, ptr, sz);
        qs->lpServiceClassId = (LPGUID)ptrdest;
        
            ptr = ptr + sz;
        ptrdest = ptrdest + sz;
    }
    if (qs->lpVersion)
    {
        sz = sizeof(WSAVERSION);
        memcpy(ptrdest, ptr, sz);
        qs->lpVersion = (WSAVERSION *)ptrdest;

            ptr = ptr + sz;
        ptrdest = ptrdest + sz;
    }
    if (qs->lpszComment)
    {
        sz = (lstrlenW((WCHAR *)ptr)+1) * sizeof(WCHAR);
        memcpy(ptrdest, ptr, sz); 
        qs->lpszComment = (LPWSTR)ptrdest;

        ptr = ptr + sz;
        ptrdest = ptrdest + sz;
    }
    if (qs->lpNSProviderId)
    {
        sz = sizeof(GUID);
        memcpy(ptrdest, ptr, sz);
        qs->lpNSProviderId = (GUID *)ptrdest;

        ptr = ptr + sz;
        ptrdest = ptrdest + sz;
    }
    if (qs->lpszContext)
    {
        sz = (lstrlenW((WCHAR *)ptr) + 1) * sizeof(WCHAR);
        memcpy(ptrdest, ptr, sz);
        qs->lpszContext = (LPWSTR)ptrdest;

        ptr = ptr + sz;
        ptrdest = ptrdest + sz;
    }
    if (qs->lpafpProtocols)
    {
        sz = sizeof(AFPROTOCOLS) * qs->dwNumberOfProtocols;
        memcpy(ptrdest, ptr, sz);
        qs->lpafpProtocols = (AFPROTOCOLS *)ptrdest;

        ptr = ptr + sz;
        ptrdest = ptrdest + sz;
    }
    if (qs->lpszQueryString)
    {
        sz = (lstrlenW((WCHAR *)ptr)+1) * sizeof(WCHAR);
        memcpy(ptrdest, ptr, sz);
        qs->lpszQueryString = (LPWSTR)ptrdest;

        ptr = ptr + sz;
        ptrdest = ptrdest + sz;
    }
    if (qs->lpcsaBuffer)
    {
        sz = sizeof(CSADDR_INFO) * qs->dwNumberOfCsAddrs;
        memcpy(ptrdest, ptr, sz);
        qs->lpcsaBuffer = (CSADDR_INFO *)ptrdest;

        ptr = ptr + sz;
        ptrdest = ptrdest + sz;
        
        for(DWORD i=0; i < qs->dwNumberOfCsAddrs ;i++)
        {
            sz = qs->lpcsaBuffer[i].LocalAddr.iSockaddrLength;
            memcpy(ptrdest, ptr, sz);
            qs->lpcsaBuffer[i].LocalAddr.lpSockaddr = (SOCKADDR *)ptrdest;
            ptr = ptr + sz;
            ptrdest = ptrdest + sz;
            
            sz = qs->lpcsaBuffer[i].RemoteAddr.iSockaddrLength;
            memcpy(ptrdest, ptr, sz);
            qs->lpcsaBuffer[i].RemoteAddr.lpSockaddr = (SOCKADDR *)ptrdest;
            ptr = ptr + sz;
            ptrdest = ptrdest + sz;            
        }
    }

    return 0;
}

//
// Function: readdata
//
// Description:
//    This function performs a blocking read for the given number of 
//    bytes. The bytesread parameter indicates the number of bytes 
//    to be read. Upon completion it is updated to indicate the 
//    actual number of bytes read. However, under normal circumstances
//    this function will not return until the requested number of bytes
//    have been read.
//
int readdata(SOCKET s, char *buffer, int buffersz, int *bytesread)
{
    int        ret, 
               nbytes;
    ULONG      nLeft;

    nLeft = *bytesread; 

    nbytes = 0;
    *bytesread = 0; 
    //
    // Loop until all data has been read. Make sure we don't
    //  exceed the buffer size.
    //
    while ((nLeft > 0) && (nbytes < buffersz))
    {
        ret = recv(s, &buffer[nbytes], buffersz - nbytes, 0);
        if (ret == SOCKET_ERROR)
        {
            printf("recv() failed: %d\n", WSAGetLastError());
            closesocket(s);
            break;
        }
        else if (ret == 0)        // socket has been closed
        {
            closesocket(s);
            break;
        }
        // Update counters
        //
        nbytes += ret;
        nLeft -= ret;
    }
    // Update the byte count
    //
    *bytesread = nbytes;

    return 0;
}

//
// Function: writedata
//
// Description:
//    This function writes the requested number of bytes to the socket.
//    The numwrite parameter indicates how many bytes should be written
//    while byteswritten is an out parameter indicating how many were
//    actually written. Under normal circumstances, the function will
//    not return until all bytes have been written.
//
int writedata(SOCKET s, char *buffer, int numwrite, int *byteswritten)
{
    int        nbytes,
               ret;

    nbytes = 0;
    *byteswritten = 0;
    //
    // Write the data in a loop until all is sent
    //
    while (numwrite > 0)
    {
        ret = send(s, &buffer[nbytes], numwrite, 0);
        if (ret == SOCKET_ERROR)
        {
            printf("send() failed: %d\n", WSAGetLastError());
            return 1;
        }
        else if (ret == 0)
        {
            return 0;
        }
        numwrite -= ret;
        nbytes   += ret;
    }
    // Update bytes written
    //
    *byteswritten = nbytes;

    return 0;
}
