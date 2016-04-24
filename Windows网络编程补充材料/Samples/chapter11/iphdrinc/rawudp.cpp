//
// Sample: Raw IPv4/IPv6 UDP with IP_HDRINCL option
//
// Files:
//      rawudp.cpp    - this file
//      iphdr.h       - IPv4, IPv6, and UDP structure definitions
//      resolve.cpp   - common name resolution routines
//      resolve.h     - header file for common name resolution routines
//
//
// Description:
//    This is a simple app that demonstrates the usage of the 
//    IP_HDRINCL socket option. A raw socket is created of the
//    UDP protocol where we will build our own IP and UDP header
//    that we submit to sendto(). 
//
//    For IPv4 this is fairly simple. Create a raw socket, set the 
//    IP_HDRINCL option, build the IPv4 and UDP headers, and do a
//    sendto. The IPv4 stack will fragment the data as necessary and
//    generally leaves the packet unmodified -- it performs fragmentation
//    and sets the IPv4 ID field.
//
//    For IPv6 its a bit more involved as it does not perform any 
//    fragmentation, you have to do it and build the headers yourself.
//    
//    The IP_HDRINCL option only works on Windows 2000 or greater.
//
// Compile:
//      cl -o rawudp.exe rawudp.cpp resolve.cpp ws2_32.lib
//
// Usage:
//      rawudp.exe [options]
//          -a  4|6    Address family
//          -sa addr   From (sender) port number
//          -sp int    From (sender) IP address
//          -da addr   To (recipient) port number
//          -dp int    To (recipient) IP address
//          -n  int    Number of times to read message
//          -m  str    String message to fill packet data with
//          -p  proto  Protocol value
//          -r  port   Receive raw (SOCK_RAW) datagrams on the given port
//          -rd port   Receive datagram (SOCK_DGRAM) on the given port
//          -t  mtu    MTU size (required for fragmentation)
//          -z  int    Size of message to send
//
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdio.h>
#include <stdlib.h>

#include "resolve.h"

#include "iphdr.h"

//
// Setup some default values 
//
#define DEFAULT_MTU           1496          // default MTU size
#define DEFAULT_TTL           8             // default TTL value

#define MAX_PACKET            65535         // maximum datagram size
#define MAX_PACKET_FRAGMENTS ((MAX_PACKET / DEFAULT_MTU) + 1)

#define DEFAULT_PORT          5150          // default port to send to
#define DEFAULT_COUNT         5             // default number of messages to send
#define DEFAULT_MESSAGE       "This is a test"  // default message

#define FRAGMENT_HEADER_PROTOCOL    44      // protocol value for IPv6 fragmentation header

//
// Global variables
//
char     *gSrcAddress=NULL,          // IP address to send from
         *gDestAddress=NULL,         // IP address to send to
         *gSrcPort=NULL,             // port to send from
         *gDestPort=NULL,            // port to send to
         *gMessage=NULL;             // Message to send as UDP payload
int       gAddressFamily=AF_UNSPEC,
          gSocketType=SOCK_DGRAM,    // Socket type to pass to name resolution routines
          gProtocol=IPPROTO_UDP,     // Protocol value that we're sending
          gSendSize=0,               // Data size of message to send
          gMtuSize=DEFAULT_MTU;      // Maximum transmission unit to use
DWORD     gSendCount;                // number of times to send
BOOL      bSender=TRUE,              // sending or receiving data
          bReadRaw=TRUE;             // Use raw sockets when reading

//
// Function: usage:
//
// Description:
//    Print usage information and exit.
//
void usage(char *progname)
{
    printf("usage: %s [-fp int] [-fi str] [-tp int] [-ti str] [-n int] [-m str]\n"
           "    -a  4|6    Address family\n"
           "    -sa addr   From (sender) port number\n"
           "    -sp int    From (sender) IP address\n"
           "    -da addr   To (recipient) port number\n"
           "    -dp int    To (recipient) IP address\n"
           "    -n  int    Number of times to read message\n"
           "    -m  str    String message to fill packet data with\n"
           "    -p  proto  Protocol value\n"
           "    -r  port   Receive raw (SOCK_RAW) datagrams on the given port\n"
           "    -rd port   Receive datagram (SOCK_DGRAM) on the given port\n"
           "    -t  mtu    MTU size (required for fragmentation)\n"
           "    -z  int    Size of message to send\n",
           progname
           );
    ExitProcess(1);
}

//
// Function: ValidateArgs
//
// Description:
//    Parse the command line arguments and set some global flags to
//    indicate what actions to perform.
//
void ValidateArgs(int argc, char **argv)
{
    int                i;

    gMessage = DEFAULT_MESSAGE;
    for(i=1; i < argc ;i++)
    {
        if ((argv[i][0] == '-') || (argv[i][0] == '/'))
        {
            switch (tolower(argv[i][1]))
            {
                case 'a':                   // Address family
                    if (i+1 > argc)
                        usage(argv[0]);
                    if (argv[i+1][0] == '4')
                        gAddressFamily = AF_INET;
                    else if (argv[i+1][0] == '6')
                        gAddressFamily = AF_INET6;
                    else
                        usage(argv[0]);
                    i++;
                    break;
                case 's':                   // source address
                    if (i+1 > argc)
                        usage(argv[0]);
                    if (tolower(argv[i][2]) == 'a')
                    {
                        gSrcAddress = argv[++i];
                    }
                    else if (tolower(argv[i][2]) == 'p')
                    {
                        gSrcPort = argv[++i];
                    }
                    else
                    {
                        usage(argv[0]);
                        break;
                    }    
                    break;
                case 'd':                   // destination address
                    if (i+1 > argc)
                        usage(argv[0]);
                    if (tolower(argv[i][2]) == 'a')
                    {
                        gDestAddress = argv[++i];
                    }
                    else if (tolower(argv[i][2]) == 'p')
                    {
                        gDestPort = argv[++i];
                    }
                    else
                    {
                        usage(argv[0]);
                        break;
                    }
                    break;
                case 'n':                   // number of times to send message
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gSendCount = atol(argv[++i]);
                    break;                  // String message to copy into payload
                case 'm':
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gMessage = argv[++i];
                    break;
                case 'p':                   // Protocol value
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gProtocol = atoi(argv[++i]);
                    break;
                case 'r':                   // Port to receive data on
                    if (i+1 >= argc)
                        usage(argv[0]);
                    if (strlen(argv[i]) == 3)
                        bReadRaw = FALSE;
                    gSrcPort = argv[++i];
                    bSender = FALSE;
                    break;
                case 't':                   // MTU size
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gMtuSize = atoi(argv[++i]);
                    break;
                case 'z':                   // Send size
                    if (i+1 >= argc)
                        usage(argv[0]);
                    gSendSize = atoi(argv[++i]);
                    break;
                default:
                    usage(argv[0]);
                    break;
            }
        }
    }

    // If no data size was given, initialize it to the message supplied
    if (gSendSize == 0)
    {
        gSendSize = strlen(gMessage);
    }

    return;
}

// 
// Function: checksum
//
// Description:
//    This function calculates the 16-bit one's complement sum
//    for the supplied buffer.
//
USHORT checksum(USHORT *buffer, int size)
{
    unsigned long cksum=0;

    while (size > 1)
    {
        cksum += *buffer++;
        size  -= sizeof(USHORT);   
    }
    if (size)
    {
        cksum += *(UCHAR*)buffer;   
    }
    cksum = (cksum >> 16) + (cksum & 0xffff);
    cksum += (cksum >>16); 

    return (USHORT)(~cksum); 
}

//
// Function: InitIpv4Header
//
// Description:
//    Initialize the IPv4 header with the version, header length,
//    total length, ttl, protocol value, and source and destination
//    addresses.
//
int InitIpv4Header(
    char *buf, 
    SOCKADDR *src, 
    SOCKADDR *dest, 
    int ttl,
    int proto,
    int payloadlen
    )
{
    IPV4_HDR    *v4hdr=NULL;

    v4hdr = (IPV4_HDR *)buf;

    v4hdr->ip_verlen      = (4 << 4) | (sizeof(IPV4_HDR) / sizeof(unsigned long));
    v4hdr->ip_tos         = 0;
    v4hdr->ip_totallength = htons(sizeof(IPV4_HDR) + payloadlen);
    v4hdr->ip_id          = 0;
    v4hdr->ip_offset      = 0;
    v4hdr->ip_ttl         = (unsigned char)ttl;
    v4hdr->ip_protocol    = (unsigned char)proto;
    v4hdr->ip_checksum    = 0;
    v4hdr->ip_srcaddr     = ((SOCKADDR_IN *)src)->sin_addr.s_addr;
    v4hdr->ip_destaddr    = ((SOCKADDR_IN *)dest)->sin_addr.s_addr;

    v4hdr->ip_checksum    = checksum((unsigned short *)v4hdr, sizeof(IPV4_HDR));
    
    return sizeof(IPV4_HDR);
}

//
// Function: InitIpv6Header
//
// Description:
//    Initialize the IPv6 header with the version, payload length, next
//    hop protocol, TTL, and source and destination addresses.
//
int InitIpv6Header(
    char *buf, 
    SOCKADDR *src, 
    SOCKADDR *dest, 
    int ttl,
    int proto,
    int payloadlen
    )
{
    IPV6_HDR    *v6hdr=NULL;

	v6hdr = (IPV6_HDR *)buf;

    // We don't explicitly set the traffic class or flow label fields
    v6hdr->ipv6_vertcflow    = htonl(6 << 28);
    v6hdr->ipv6_payloadlen   = htons((unsigned short)payloadlen);
    v6hdr->ipv6_nexthdr      = (unsigned char)proto;
    v6hdr->ipv6_hoplimit     = (unsigned char)ttl;
    v6hdr->ipv6_srcaddr      = ((SOCKADDR_IN6 *)src)->sin6_addr;
    v6hdr->ipv6_destaddr     = ((SOCKADDR_IN6 *)dest)->sin6_addr;

    return sizeof(IPV6_HDR);
}

//
// Function: InitIpv6FragmentHeader
//
// Description:
//    Initialize the IPv6 fragmentation header. The offset is the offset
//    from the start of the IPv6 total payload (which includes the UDP
//    header along with the data) which is why we add the length of
//    the UDP header if this fragment is not the first fragment. Also,
//    the lastfragment parameter is a boolean value (0 == not the last
//    fragment while 1 == this is the last fragment) which is the opposite
//    value thats supposed to be indicated in the header (i.e. 0 indicates
//    that this fragment is the last fragment).
//
int InitIpv6FragmentHeader(
    char *buf,
    unsigned long offset,
    int nextproto,
    int id,
    int lastfragment
    )
{
    IPV6_FRAGMENT_HDR *frag=NULL;

    frag = (IPV6_FRAGMENT_HDR *)buf;

    // Swap the value of this field
    lastfragment = (lastfragment ? 0 : 1);

    // Account for the size of the UDP header
    if (offset != 0)
        offset += sizeof(UDP_HDR);

    frag->ipv6_frag_nexthdr = (unsigned char)nextproto;
    frag->ipv6_frag_offset  = htons( (unsigned short)(((offset/8) << 3) | lastfragment));
    frag->ipv6_frag_id      = htonl(id);

    return sizeof(IPV6_FRAGMENT_HDR);
}

// 
// Function: InitUdpHeader
//
// Description:
//    Setup the UDP header which is fairly simple. Grab the ports and
//    stick in the total payload length.
//
int InitUdpHeader(
    char *buf, 
    SOCKADDR *src,
    SOCKADDR *dest, 
    int       payloadlen
    )
{
    UDP_HDR *udphdr=NULL;

    udphdr = (UDP_HDR *)buf;

    // Port numbers are already in network byte order
    if (src->sa_family == AF_INET)
    {
        udphdr->src_portno = ((SOCKADDR_IN *)src)->sin_port;
        udphdr->dst_portno = ((SOCKADDR_IN *)dest)->sin_port;
    }
    else if (src->sa_family == AF_INET6)
    {
        udphdr->src_portno = ((SOCKADDR_IN6 *)src)->sin6_port;
        udphdr->dst_portno = ((SOCKADDR_IN6 *)dest)->sin6_port;
    }
    udphdr->udp_length = htons(sizeof(UDP_HDR) + payloadlen);

    return sizeof(UDP_HDR);
}

//
// Function: ComputeUdpPseudoHeaderChecksumV4
//
// Description:
//    Compute the UDP pseudo header checksum. The UDP checksum is based
//    on the following fields:
//       o source IP address
//       o destination IP address
//       o 8-bit zero field
//       o 8-bit protocol field
//       o 16-bit UDP length
//       o 16-bit source port
//       o 16-bit destination port
//       o 16-bit UDP packet length
//       o 16-bit UDP checksum (zero)
//       o UDP payload (padded to the next 16-bit boundary)
//    This routine copies these fields to a temporary buffer and computes
//    the checksum from that.
//
void ComputeUdpPseudoHeaderChecksumV4(
    void    *iphdr,
    UDP_HDR *udphdr,
    char    *payload,
    int      payloadlen
    )
{
    IPV4_HDR     *v4hdr=NULL;
    unsigned long zero=0;
    char          buf[MAX_PACKET],
                 *ptr=NULL;
    int           chksumlen=0,
                  i;
    
    ptr = buf;

    v4hdr = (IPV4_HDR *)iphdr;

    // Include the source and destination IP addresses
    memcpy(ptr, &v4hdr->ip_srcaddr,  sizeof(v4hdr->ip_srcaddr));  
    ptr += sizeof(v4hdr->ip_srcaddr);
    chksumlen += sizeof(v4hdr->ip_srcaddr);

    memcpy(ptr, &v4hdr->ip_destaddr, sizeof(v4hdr->ip_destaddr)); 
    ptr += sizeof(v4hdr->ip_destaddr);
    chksumlen += sizeof(v4hdr->ip_destaddr);
    
    // Include the 8 bit zero field
    memcpy(ptr, &zero, 1);
    ptr++;
    chksumlen += 1;

    // Protocol
    memcpy(ptr, &v4hdr->ip_protocol, sizeof(v4hdr->ip_protocol)); 
    ptr += sizeof(v4hdr->ip_protocol);
    chksumlen += sizeof(v4hdr->ip_protocol);

    // UDP length
    memcpy(ptr, &udphdr->udp_length, sizeof(udphdr->udp_length)); 
    ptr += sizeof(udphdr->udp_length);
    chksumlen += sizeof(udphdr->udp_length);
    
    // UDP source port
    memcpy(ptr, &udphdr->src_portno, sizeof(udphdr->src_portno)); 
    ptr += sizeof(udphdr->src_portno);
    chksumlen += sizeof(udphdr->src_portno);

    // UDP destination port
    memcpy(ptr, &udphdr->dst_portno, sizeof(udphdr->dst_portno)); 
    ptr += sizeof(udphdr->dst_portno);
    chksumlen += sizeof(udphdr->dst_portno);

    // UDP length again
    memcpy(ptr, &udphdr->udp_length, sizeof(udphdr->udp_length)); 
    ptr += sizeof(udphdr->udp_length);
    chksumlen += sizeof(udphdr->udp_length);
   
    // 16-bit UDP checksum, zero 
    memcpy(ptr, &zero, sizeof(unsigned short));
    ptr += sizeof(unsigned short);
    chksumlen += sizeof(unsigned short);

    // payload
    memcpy(ptr, payload, payloadlen);
    ptr += payloadlen;
    chksumlen += payloadlen;

    // pad to next 16-bit boundary
    for(i=0 ; i < payloadlen%2 ; i++, ptr++)
    {
        printf("pad one byte\n");
        *ptr = 0;
        ptr++;
        chksumlen++;
    }

    // Compute the checksum and put it in the UDP header
    udphdr->udp_checksum = checksum((USHORT *)buf, chksumlen);

    return;
}

void ComputeUdpPseudoHeaderChecksumV6(
    void    *iphdr,
    UDP_HDR *udphdr,
    char    *payload,
    int      payloadlen
    )
{
    IPV6_HDR     *v6hdr=NULL;
    unsigned long length=0;
    char          buf[MAX_PACKET],
                  proto,
                 *ptr=NULL;
    int           chksumlen=0,
                  i;
    
    ptr = buf;

    v6hdr = (IPV6_HDR *)iphdr;

    memcpy(ptr, &v6hdr->ipv6_srcaddr,  sizeof(v6hdr->ipv6_srcaddr));  
    ptr += sizeof(v6hdr->ipv6_srcaddr);
    chksumlen += sizeof(v6hdr->ipv6_srcaddr);

    memcpy(ptr, &v6hdr->ipv6_destaddr,  sizeof(v6hdr->ipv6_destaddr));  
    ptr += sizeof(v6hdr->ipv6_destaddr);
    chksumlen += sizeof(v6hdr->ipv6_destaddr);

    printf("payload length = %d\n", payloadlen);
    length = htonl(payloadlen + sizeof(UDP_HDR));

    memcpy(ptr, &length, sizeof(length));
    ptr += sizeof(length);
    chksumlen += sizeof(length);

    memset(ptr, 0, 3);
    ptr += 3;
    chksumlen +=3;

    proto = IPPROTO_UDP;

    memcpy(ptr, &proto, sizeof(proto));
    ptr += sizeof(proto);
    chksumlen += sizeof(proto);
   
    // UDP source port
    memcpy(ptr, &udphdr->src_portno, sizeof(udphdr->src_portno)); 
    ptr += sizeof(udphdr->src_portno);
    chksumlen += sizeof(udphdr->src_portno);

    // UDP destination port
    memcpy(ptr, &udphdr->dst_portno, sizeof(udphdr->dst_portno)); 
    ptr += sizeof(udphdr->dst_portno);
    chksumlen += sizeof(udphdr->dst_portno);

    // UDP length again
    memcpy(ptr, &udphdr->udp_length, sizeof(udphdr->udp_length)); 
    ptr += sizeof(udphdr->udp_length);
    chksumlen += sizeof(udphdr->udp_length);
   
    // 16-bit UDP checksum, zero 
    memset(ptr, 0, sizeof(unsigned short));
    ptr += sizeof(unsigned short);
    chksumlen += sizeof(unsigned short);

    // payload
    memcpy(ptr, payload, payloadlen);
    ptr += payloadlen;
    chksumlen += payloadlen;

    // pad to next 16-bit boundary
    for(i=0 ; i < payloadlen%2 ; i++, ptr++)
    {
        printf("pad one byte\n");
        *ptr = 0;
        ptr++;
        chksumlen++;
    }

    // Compute the checksum and put it in the UDP header
    udphdr->udp_checksum = checksum((USHORT *)buf, chksumlen);

    return;
}


//
// Function: memfill
//
// Description:
//    Fills a block of memory with a given string pattern.
//
void memfill(
    char *dest,
    int   destlen,
    char *data,
    int   datalen
    )
{
    char *ptr=NULL;
    int   copylen;

    ptr = dest;
    while (destlen > 0)
    {
        copylen = ((destlen > datalen) ? datalen : destlen);
        memcpy(ptr, data, copylen);

        destlen -= copylen;
        ptr += copylen;
    }
    return;
}

//
// Function: PacketizeIpv4
//
// Description:
//    This routine takes the data buffer and packetizes it for IPv4.
//    Since the IPv4 stack takes care of fragmentation for us, this
//    routine simply initializes the IPv4 and UDP headers. The data
//    is returned in an array of WSABUF structures.
//
WSABUF *PacketizeIpv4(
    struct addrinfo *src,
    struct addrinfo *dest,
    char *payload, 
    int payloadlen
    )
{
    static WSABUF Packets[MAX_PACKET_FRAGMENTS];
    int           iphdrlen,
                  udphdrlen;

    // Allocate memory for the packet
    Packets[0].buf = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IPV4_HDR) + sizeof(UDP_HDR) + payloadlen);
    if (Packets[0].buf == NULL)
    {
        fprintf(stderr, "PacetizeV4: HeapAlloc failed: %d\n", GetLastError());
        ExitProcess(-1);
    }
    Packets[0].len = sizeof(IPV4_HDR) + sizeof(UDP_HDR) + payloadlen;

    // Initialize the v4 header
    iphdrlen = InitIpv4Header(
            Packets[0].buf, 
            src->ai_addr, 
            dest->ai_addr, 
            DEFAULT_TTL, 
            gProtocol, 
            payloadlen
            );
 
    // Initialize the UDP header
    udphdrlen = InitUdpHeader(
           &Packets[0].buf[iphdrlen], 
            src->ai_addr, 
            dest->ai_addr, 
            payloadlen
            );

    // Compute the UDP checksum
    ComputeUdpPseudoHeaderChecksumV4(
            Packets[0].buf, 
            (UDP_HDR *)&Packets[0].buf[iphdrlen], 
            payload, 
            payloadlen
            );

    // Copy the payload to the end of the header
    memcpy(&Packets[0].buf[iphdrlen + udphdrlen], payload, payloadlen);

    // Zero out the next WSABUF structure which indicates the end of
    //    the packets -- caller must free the buffers
    Packets[1].buf = NULL;
    Packets[1].len = 0;

    return Packets;
}

//
// Function: PacketizeIpv6
//
// Description:
//    This routine fragments data payload with the appropriate IPv6
//    headers. The individual fragments are returned via an array of
//    WSABUF structures. Each structure is a separate fragment of the
//    whole message. The end of the fragments is indicated by a WSABUF
//    entry with a NULL buffer pointer.
//
WSABUF *PacketizeIpv6(
    struct addrinfo *src,
    struct addrinfo *dest,
    char *payload, 
    int payloadlen
    )
{
    static WSABUF Packets[MAX_PACKET_FRAGMENTS];
    static ULONG  fragid=1;
    int           offset=0,        // offset into payload 
                  datalen,         // length of the payload
                  hdrlen,          // length of the header(s)
                  fragment,        // is this a fragment?
                  lastfragment,    // is this the last fragment?
                  iphdrlen,        // length of ip header 
                  udphdrlen,       // length of the udp header
                  plushdrs,        // IPv6 length field includes encapsulated headers
                  numpackets=0,    // number of fragments
                  originalpayload;

    originalpayload = payloadlen;
    do
    {
        // Compute the size of this fragment
        lastfragment = 0;
        fragment = 0;
        if ((payloadlen > gMtuSize) && (numpackets == 0))
        {
            // Data needs to be fragmented, this is the first packet
            hdrlen  = sizeof(IPV6_HDR) + sizeof(UDP_HDR) + sizeof(IPV6_FRAGMENT_HDR);
            datalen = gMtuSize - hdrlen;
            plushdrs = sizeof(UDP_HDR) + sizeof(IPV6_FRAGMENT_HDR);
            fragment = 1;

            printf("Require fragmentation: FIRST packet\n");
        }
        else if ((payloadlen > gMtuSize) && (numpackets > 0))
        {
            // Data needs to be fragmented, this is packet number > 0
            hdrlen = sizeof(IPV6_HDR) + sizeof(IPV6_FRAGMENT_HDR);
            datalen = gMtuSize - hdrlen;
            fragment = 1;
            plushdrs = sizeof(IPV6_FRAGMENT_HDR);

            printf("Require fragmentation: packet number > 0\n");
        }
        else if (numpackets == 0)
        {
            // Data doesn't need to be fragmented
            hdrlen = sizeof(IPV6_HDR) + sizeof(UDP_HDR);
            datalen = payloadlen;
            fragment = 0;
            plushdrs = sizeof(UDP_HDR);

            printf("No fragmentation required\n");
        }
        else
        {
            // This is the last fragment
            hdrlen = sizeof(IPV6_HDR) + sizeof(IPV6_FRAGMENT_HDR);
            datalen = payloadlen;
            fragment = 1;
            plushdrs = sizeof(IPV6_FRAGMENT_HDR);
            lastfragment = 1;

            printf("Require fragmentation: Last packet\n");
        }

        // Build packet
 
        // Allocate buffer for this fragment
        Packets[numpackets].buf = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, hdrlen + datalen);
        if (Packets[numpackets].buf == NULL)
        {
            fprintf(stderr, "PacketizeV6: HeapAlloc failed: %d\n", GetLastError());
            ExitProcess(-1);
        }
        Packets[numpackets].len = hdrlen + datalen;

        // Initialize the V6 header, if we have to fragment the next header field of
        //    the v6 header is that of the fragmentation header. Also the payload
        //    length includes the headers (UDP + fragmentation) and the payload itself.
        iphdrlen = InitIpv6Header(
                Packets[numpackets].buf, 
                src->ai_addr, 
                dest->ai_addr, 
                DEFAULT_TTL, 
                (fragment ? FRAGMENT_HEADER_PROTOCOL : gProtocol),
                datalen + plushdrs
                );

        // Build the fragmentation header if necessary
        if (fragment)
        {
            iphdrlen += InitIpv6FragmentHeader(
                   &Packets[numpackets].buf[iphdrlen],
                    offset,         // offset from start of packet
                    gProtocol,
                    fragid,
                    lastfragment
                    );
        }

        // The first fragment includes the UDP header, subsequent fragments don't
        if (numpackets == 0)
        {
            udphdrlen = InitUdpHeader(
                   &Packets[numpackets].buf[iphdrlen], 
                    src->ai_addr, 
                    dest->ai_addr, 
                    originalpayload //payloadlen
                    );

            // Compute the checksum
            ComputeUdpPseudoHeaderChecksumV6(
                    Packets[numpackets].buf, 
                    (UDP_HDR *)&Packets[numpackets].buf[iphdrlen], 
                    payload, 
                    payloadlen);
        }
        else
        {
            udphdrlen = 0;
        }

        // Copy the payload into this fragment
        memcpy(&Packets[numpackets].buf[iphdrlen + udphdrlen], &payload[offset], datalen);

        // Adjust our counters
        payloadlen = payloadlen - datalen;
        offset += datalen;
        numpackets++;

    } while (payloadlen > 0);

    fragid++;

    // Mark the next WSABUF entry with NULL and zero to indicate end of fragments
    Packets[numpackets].buf = NULL;
    Packets[numpackets].len = 0;

    return Packets;
}

// 
// Function: main
//
// Description:
//    First, parse command line arguments and load Winsock. Then 
//    create the raw socket and then set the IP_HDRINCL option.
//    Following this assemble the IP and UDP packet headers by
//    assigning the correct values and calculating the checksums.
//    Then fill in the data and send to its destination.
//
int _cdecl main(int argc, char **argv)
{
    WSADATA            wsd;
    SOCKET             s;
    DWORD              bytes;
    WSABUF            *wbuf=NULL;
    struct addrinfo   *ressrc=NULL,
                      *resdest=NULL,
                      *resbind=NULL;
    int                packets,
                       rc,
                       i, j;

    // Parse command line arguments and print them out
    ValidateArgs(argc, argv);

    srand(GetTickCount());

    if (WSAStartup(MAKEWORD(2,2), &wsd) != 0)
    {
        printf("WSAStartup() failed: %d\n", GetLastError());
        return -1;
    }

    // Convert the source and destination addresses/ports
    ressrc = ResolveAddress(gSrcAddress, gSrcPort, gAddressFamily, gSocketType, gProtocol);
    if (ressrc == NULL)
    {
        fprintf(stderr, "Unable to resolve address '%s' and port '%s'\n", 
                gSrcAddress, gSrcPort);
        return -1;
    }

    if (bSender)
    {
        resdest = ResolveAddress(gDestAddress, gDestPort, ressrc->ai_family, ressrc->ai_socktype, ressrc->ai_protocol);
        if (resdest == NULL)
        {
            fprintf(stderr, "Unable to resolve address '%s' and port '%s'\n", 
                    gDestAddress, gDestPort);
            return -1;
        }
    }

    //  Creating a raw socket

    //  BUG - For IPv6 if we create the raw socket with IPPROTO_UDP then the Ipv6
    //  stack will thow away our IPv6 and UDP headers and put "valid" ones in their
    //  place. As a workaround, create the socket with a protocol value of an 
    //  unhandled protocol. Of course the IPv6 header should still indicate that 
    //  the encapsulated protocol is UDP.
  
    if (bSender)
        s = socket(ressrc->ai_family, SOCK_RAW, ((ressrc->ai_family == AF_INET6) ? 3 : ressrc->ai_protocol));
    else if (!bSender && bReadRaw)
        s = socket(ressrc->ai_family, SOCK_RAW, ressrc->ai_protocol);
    else
        s = socket(ressrc->ai_family, SOCK_DGRAM, ressrc->ai_protocol);
    if (s == INVALID_SOCKET)
    {
        fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
        return -1;
    }

    if (bSender)
    {
        char   *payload=NULL;
        int     optlevel,
                option,
                optval;

        payload = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, gSendSize);
        if (payload == NULL)
        {
            fprintf(stderr, "HeapAlloc failed: %d\n", GetLastError());
            return -1;
        }
        memfill(payload, gSendSize, gMessage, strlen(gMessage));

        // Enable the IP header include option 
        optval = 1;
        if (ressrc->ai_family == AF_INET)
        {
            optlevel = IPPROTO_IP;
            option   = IP_HDRINCL;
        }
        else if (ressrc->ai_family == AF_INET6)
        {
            optlevel = IPPROTO_IPV6;
            option   = IPV6_HDRINCL;
        }
        rc = setsockopt(s, optlevel, option, (char *)&optval, sizeof(optval));
        if (rc == SOCKET_ERROR)
        {
            fprintf(stderr, "setsockopt: IP_HDRINCL failed: %d\n", WSAGetLastError());
            return -1;
        }

        // Packetize and/or perform necessary fragmentation on data
        if (ressrc->ai_family == AF_INET)
        {
            wbuf = PacketizeIpv4(
                ressrc,
                resdest,
                payload,
                gSendSize
                );
        }
        else if (ressrc->ai_family == AF_INET6)
        {
            wbuf = PacketizeIpv6(
                ressrc,
                resdest,
                payload,
                gSendSize
                );
        }
        // Count how many packets there are
        i=0;
        packets=0;
        while (wbuf[i].buf)
        {
            printf("packet %d buf 0x%p len %d\n",
                    i, wbuf[i].buf, wbuf[i].len);
            packets++;
            i++;
        }

        // Apparently, this SOCKADDR_IN structure makes no difference.
        // Whatever we put as the destination IP addr in the IP
        // header is what goes. Specifying a different dest in remote
        // will be ignored.
        for(i=0; i < (int)gSendCount ;i++)
        {
            for(j=0; j < packets ;j++)
            {
                rc = sendto(
                        s,
                        wbuf[j].buf,
                        wbuf[j].len,
                        0,
                        resdest->ai_addr,
                        resdest->ai_addrlen
                        );
                bytes = rc;
                if (rc == SOCKET_ERROR)
                {
                    printf("sendto() failed: %d\n", WSAGetLastError());
                    break;
                }
                else
                {
                    printf("sent %d bytes\n", bytes);
                }
            }
        }

        // Free the packet buffers
        for(i=0; i < packets ;i++)
        {
            HeapFree(GetProcessHeap(), 0, wbuf[i].buf);
        }
    }
    else
    {
        SOCKADDR_STORAGE    safrom;
        char                buf[MAX_PACKET];
        int                 fromlen;

        rc = bind(
                s, 
                ressrc->ai_addr,
                ressrc->ai_addrlen
                );
        if (rc == SOCKET_ERROR)
        {
            fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
            return -1;
        }

        printf("binding to: ");
        PrintAddress(ressrc->ai_addr, ressrc->ai_addrlen);
        printf("\n");

        while (1)
        {
            fromlen = sizeof(safrom);
            rc = recvfrom(
                    s, 
                    buf, 
                    MAX_PACKET, 
                    0, 
                    (SOCKADDR *)&safrom, 
                   &fromlen
                   );
            if (rc == SOCKET_ERROR)
            {
                fprintf(stderr, "recvfrom failed: %d\n", WSAGetLastError());
                break;
            }
            printf("Read %d bytes from ", rc);
            PrintAddress((SOCKADDR *)&safrom, fromlen);
            printf("\n");
        }
    }

    closesocket(s) ;
    WSACleanup() ;

    return 0;
}
