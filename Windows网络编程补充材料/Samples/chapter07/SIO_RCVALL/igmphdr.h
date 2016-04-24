//
// Sample: Contains header definitions for the IGMP protocol
// 
// Files:
//      igmphdr.h       - this file
//      
// Description:
//      This file contains header definitions for the IGMP protocol which
//      is used by the SIO_RCVALL sample to parse and print the header
//      contents
//
// Compile:
//      See rcvall.cpp
//
// Usage:
//      See rcvall.cpp
//
#ifndef _IGMP_HDR_H_
#define _IGMP_HDR_H_

#define MULTICAST_ALL_SYSTEMS       "224.0.0.1"

// IP protocol number
#define IP_PROTOCOL_IGMP            0x02

// IGMP message types v1
#define IGMP_MEMBERSHIP_QUERY       0x11
#define IGMP_MEMBERSHIP_REPORT      0x12
// IGMP message types v2
#define IGMP_MEMBERSHIP_REPORT_V2   0x16
#define IGMP_LEAVE_GROUP            0x17
// IGMP message types v3
#define IGMP_MEMBERSHIP_REPORT_V3   0x22


// IGMP v1 and v2 header
typedef struct _igmphdr
{
    UCHAR       version_type;
    UCHAR       max_resp_time;      // zero for v1
    USHORT      checksum;
    ULONG       group_addr;
} igmphdr, IGMP_HDR;

typedef struct _igmphdr_query_v3
{
    UCHAR       type;
    UCHAR       max_resp_time;
    USHORT      checksum;
    ULONG       group_addr;
    USHORT      reserved;
    USHORT      num_sources;
} igmphdr_query_v3, IGMP_QUERY_HDRV3;

typedef struct _igmp_group_record
{
    UCHAR       type;
    UCHAR       aux_data_len;
    USHORT      num_sources;
    ULONG       group_addr;
} igmp_group_record, IGMP_GROUP_RECORD;

typedef struct _igmphdr_report_v3
{
    UCHAR       type;
    UCHAR       reserved1;
    USHORT      checksum;
    USHORT      reserved2;
    USHORT      num_records;
} igmphdr_report_v3, IGMP_REPORT_HDRV3;

#define IGMP_RECORD_MODE_IS_INCLUDE         0x01
#define IGMP_RECORD_MODE_IS_EXCLUDE         0x02
#define IGMP_RECORD_CHANGE_TO_INCLUDE_MODE  0x03
#define IGMP_RECORD_CHANGE_TO_EXCLUDE_MODE  0x04
#define IGMP_RECORD_ALLOW_NEW_SOURCES       0x05
#define IGMP_RECORD_BLOCK_OLD_SOURCES       0x06

typedef struct _group_record
{
    UCHAR       record_type;
    UCHAR       aux_data_len;
    USHORT      num_sources;
    ULONG       group_addr;
} group_record;

#endif
