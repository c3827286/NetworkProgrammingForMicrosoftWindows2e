// Module: qosudp.c
//
// Description:
//    This is a description.
//
// Compile:
//    cl -o qosudp.exe qosudp.c provider.c ws2_32.lib 
//
// Command Line Parameters/Options
//

#include <winsock2.h>
#include <windows.h>
#include <qos.h>
#include <qossp.h>

#include "provider.h"

#include <stdio.h>
#include <stdlib.h>

#define QOS_BUFFER_SZ       16000 // Default buffer size for SIO_GET_QOS
#define DATA_BUFFER_SZ       2048 // Send/Recv buffer size

QOS  sendQos;

const FLOWSPEC flowspec_notraffic = {QOS_NOT_SPECIFIED,
                                     QOS_NOT_SPECIFIED,
                                     QOS_NOT_SPECIFIED,
                                     QOS_NOT_SPECIFIED,
                                     QOS_NOT_SPECIFIED,
                                     SERVICETYPE_NOTRAFFIC,
                                     QOS_NOT_SPECIFIED,
                                     QOS_NOT_SPECIFIED};

const FLOWSPEC flowspec_TC = {8000,
                              DATA_BUFFER_SZ,
                              17000,
                              QOS_NOT_SPECIFIED,
                              QOS_NOT_SPECIFIED,
                              SERVICETYPE_CONTROLLEDLOAD | SERVICE_NO_QOS_SIGNALING,
                              340,
                              340};

//
// Function: main
//
// Description:
//
void main(int argc, char **argv)
{
   WSADATA           wsd;
   WSAPROTOCOL_INFO *pinfo=NULL;
   SOCKET            s;
   SOCKADDR_IN       local;
   SOCKADDR_IN       receiver;
   WSABUF            wbuf;
   DWORD             dwBytes;
   QOS              *lpqos=NULL;
   char              sndbuf[DATA_BUFFER_SZ];
   int               Ret;
   QOS_DESTADDR      qosdest;


   if (argc < 2)
   {
      printf("Usage: %s <receiver IP address>\n", argv[0]);
      return;
   }

   if ((Ret = WSAStartup(MAKEWORD(2,2), &wsd)) != 0)
   {
      printf("Unable to load Winsock: %d\n", Ret);
      return;
   }

   pinfo = FindProtocolInfo(AF_INET, SOCK_DGRAM, IPPROTO_UDP,
                            XP1_QOS_SUPPORTED);
   if (!pinfo)
   {
      printf("unable to find suitable provider!\n");
      return;
   }
   printf("Provider returned: %s\n", pinfo->szProtocol); 
   
   if ((s = WSASocket(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, 
       FROM_PROTOCOL_INFO, pinfo, 0, WSA_FLAG_OVERLAPPED)) 
       == INVALID_SOCKET)
   {
      printf("WSASocket() failed: %d\n", WSAGetLastError());
      return;
   }

   // No matter when we decide to set QOS, the socket should be
   // bound locally (even if its to INADDR_ANY). This is required
   // if you're not using WSAConnect which does an implicit bind,
   // but we'll do it anyway for simplicity.
   //
   local.sin_family = AF_INET;
   local.sin_port = 0;
   local.sin_addr.s_addr = htonl(INADDR_ANY);

   if (bind(s, (SOCKADDR *)&local, sizeof(local)) == SOCKET_ERROR)
   {
      printf("bind() failed: %d\n", WSAGetLastError());
      return;
   }

   receiver.sin_family = AF_INET;
   receiver.sin_port = htons(5150);
   receiver.sin_addr.s_addr = inet_addr(argv[1]);

   qosdest.ObjectHdr.ObjectType = QOS_OBJECT_DESTADDR;
   qosdest.ObjectHdr.ObjectLength = sizeof(QOS_DESTADDR);
   qosdest.SocketAddress = (SOCKADDR *)&receiver;
   qosdest.SocketAddressLength = sizeof(receiver);

   sendQos.SendingFlowspec = flowspec_TC;
   sendQos.ReceivingFlowspec =  flowspec_notraffic;
   sendQos.ProviderSpecific.buf = NULL;
   sendQos.ProviderSpecific.len = 0;

   lpqos = &sendQos;
   lpqos->ProviderSpecific.buf = (char *)&qosdest;
   lpqos->ProviderSpecific.len = sizeof(qosdest);

   if (WSAIoctl(s, SIO_SET_QOS, lpqos, sizeof(QOS) + sizeof(QOS_DESTADDR), 
       NULL, 0, &dwBytes, NULL, NULL) == SOCKET_ERROR)
   {
      printf("WSAIoctl(SIO_SET_QOS) failed: %d\n", WSAGetLastError());
      return;
   }

   memset(sndbuf, '$', DATA_BUFFER_SZ);
   wbuf.buf = sndbuf;
   wbuf.len = DATA_BUFFER_SZ - 1;

   //
   // Send data continuously to a receiver
   //
   while (1)
   {
      if (WSASendTo(s, &wbuf, 1, &dwBytes, 0, (SOCKADDR *)&receiver, 
          sizeof(receiver), NULL, NULL) == SOCKET_ERROR)
      {
         if (WSAGetLastError() == WSAEWOULDBLOCK)
            continue;

         printf("WSASendTo() failed: %d\n", WSAGetLastError());
         return;
      }
   }

   closesocket(s);

   WSACleanup();
   return;
}
