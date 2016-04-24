// Module Name: findname.c
//
// Description:
//    This sample performs an NCBFINDNAME to discover whether a given
//    NetBIOS name is in use on the network. This NetBIOS command is
//    specific to NT 4 and greater.
//
// Compile:
//    cl -o findname findname.c ..\Common\nbcommon.obj netapi32.lib
//
// Command Line Options:
//    findname.exe NAME [16-BYTE]
//
//    NAME    - NetBIOS name to find
//    16-BYTE - Integer value of 16th byte of NetBIOS (in case its
//              non-printable
//
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "nbcommon.h"

#define MAX_SESSIONS	254
#define MAX_NAMES	254

//
// Each response will return a FIND_NAME_HEADER followed by
// a FIND_NAME_BUFFER
//
typedef struct _FIND_NAME_STRUCT {
    FIND_NAME_HEADER	header;
    FIND_NAME_BUFFER	buffers[254];
} FIND_NAME_STRUCT;

//
// Function: PrintFindNameHeader
//
// Description:
//    Print out the characteristics of the NetBIOS name (i.e whether
//    it is a group name or unique name)  
//
void PrintFindNameHeader(FIND_NAME_HEADER *header)
{
    if (header->unique_group == 0)
	printf("\t         Name Type: UNIQUE\n");
    else if (header->unique_group == 1)
	printf("\t         Name Type: GROUP\n");
}

//
// Function: PrintFindNameBuffers
//
// Description:
//    This prints out the location of where the name is registered.
//    Because there is the posibility of more that one computer 
//    registering the name there is an array.  Each FIND_NAME_BUFFER
//    returns the local hosts MAC address as well as the MAC address
//    that has the name registered. Also, per Q137916, performing
//    an NCBFINDNAME on LANAs that correspond to TCP/IP returns
//    unexpected results (i.e. bogus MAC addresses). 
//
void PrintFindNameBuffers(FIND_NAME_BUFFER *buffers, WORD count)
{
    WORD	i;

    for(i=0; i < count ;i++)
    {
        printf("\t       MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
	    buffers[i].destination_addr[0],
	    buffers[i].destination_addr[1],
	    buffers[i].destination_addr[2],
	    buffers[i].destination_addr[3],
	    buffers[i].destination_addr[4],
	    buffers[i].destination_addr[5]);
	printf("\tName registered at: %02X:%02X:%02X:%02X:%02X:\
%02X\n\n",
	    buffers[i].source_addr[0],
	    buffers[i].source_addr[1],
	    buffers[i].source_addr[2],
	    buffers[i].source_addr[3],
	    buffers[i].source_addr[4],
	    buffers[i].source_addr[5]);
    }
}

//
// Function: FindName
//
// Description:
//    This performs the actual find name command.  The last byte is a
//    separate integer parameter as you can't specify special characters
//    as arguments to the command line.
//
int FindName(int lana, char *name, int lastbyte)
{
    NCB		 	ncb;
    FIND_NAME_STRUCT    namestruct;

    ZeroMemory(&ncb, sizeof(NCB));
    ncb.ncb_command = NCBFINDNAME;
    ncb.ncb_lana_num = lana;
    ncb.ncb_buffer = (PUCHAR)&namestruct;
    ncb.ncb_length = sizeof(FIND_NAME_STRUCT);

    memset(ncb.ncb_callname, ' ', NCBNAMSZ); 
    strncpy(ncb.ncb_callname, name, strlen(name));

    ncb.ncb_callname[NCBNAMSZ-1] = lastbyte;

    if (Netbios(&ncb) != NRC_GOODRET)
    {
	printf("Netbios: NCBFINDNAME failed: %d\n", ncb.ncb_retcode);
	return ncb.ncb_retcode;
    }
    PrintFindNameHeader(&namestruct.header);
    if (namestruct.header.node_count > 0)
        PrintFindNameBuffers(namestruct.buffers, 
            namestruct.header.node_count);
    else
        printf("Name not registered on network\n");

    return NRC_GOODRET;
}

//
// Function: main
//
// Description:
//    Initialize the NetBIOS interface, parse the arguments, and issue
//    the find name command. Upon return print the name information.
//
int main(int argc, char **argv)
{
    LANA_ENUM	lenum;
    char        szFindName[NCBNAMSZ+1];
    int         i, 
                iLastChar=(int)' ';
    DWORD       dwNum;

    //
    // Check usage parameters, enumerate LANAs, and reset them
    //
    if ((argc != 2) && (argc != 3))
    {
	printf("usage: findname NAME\n");
	return 0;
    }
    if (argc == 3)
        iLastChar = atoi(argv[2]);
    if (LanaEnum(&lenum) != NRC_GOODRET)
	return 0;
    if (ResetAll(&lenum, (UCHAR)MAX_SESSIONS, (UCHAR)MAX_NAMES,
	    FALSE) != NRC_GOODRET)
	return 0;
    //
    // Format the supplied name to find so we can print it out
    // as it should look
    //
    memset(szFindName, ' ', NCBNAMSZ);
    strncpy(szFindName, argv[1], strlen(argv[1]));
    szFindName[NCBNAMSZ-1] = iLastChar;
    szFindName[NCBNAMSZ] = 0;
    FormatNetbiosName(szFindName, szFindName);
    //
    // Add a name to each name table first..otherwise the FindName
    // fails.
    // 
    for(i=0; i < lenum.length ;i++)
    {
        printf("LANA: %d Searching for name: '%s'\n",
            lenum.lana[i], szFindName);

        AddName(lenum.lana[i], "FINDNAME-TEST", &dwNum);
        FindName(lenum.lana[i], argv[1], iLastChar);
    }
    return 1;
}
