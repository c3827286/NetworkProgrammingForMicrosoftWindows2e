// Module Name: astat.c
//
// Description:
//    This sample illustrates how to perform a NetBIOS adapter
//    status either as a local or remote call. Depending on how
//    you call the utility will determine what kind of information
//    is returned.  A local call returns only those names in the
//    current process' name table. A remote call will return all
//    names registered on the machine.
//
// Compile:
//    cl -o astat astat.c ..\Common\nbcommon.obj netapi32.lib
//
// Command Line Options:
//    astat.exe [-l][-r]
//    NONE       Just perform a local status which will return
//               only those names added by this process.
//    -l:NAME    NetBIOS name to add to the local NetBIOS
//               name table to make the call "remotely" on the
//               local machine.
//    -r:NAME    NetBIOS name of the remote machine to query 
//    
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "nbcommon.h"

#define MAX_SESSIONS         254
#define MAX_NAMES        254

BOOL    bLocalName=FALSE,            // Are we adding a local name
        bRemoteName=FALSE;           // Are we doing a remote status?
char    szLocalName[NCBNAMSZ+1],     // Local NetBIOS name
        szRemoteName[NCBNAMSZ+1];    // Remote NetBIOS name

//
// We're safe in hardcoding the number of NAME_BUFFER
// messages since the maximum number of names possible
// on a single LANA is 254
//
typedef struct {
    ADAPTER_STATUS  adapter;
    NAME_BUFFER     names[254];
} MESSAGE_BUFFER;

//
// Function: ValidateArgs
//
// Description:
//    Check for various command line options.
//
void ValidateArgs(int argc, char **argv)
{
    int                i;

    for(i=1; i < argc ;i++)
    {
        if ((argv[i][0] == '-') || (argv[i][0] == '/'))
        {
            switch (tolower(argv[i][1]))
            {
                case 'l':        
                    //
                    // fake the status call on this machine to make
                    // it look like it was a remote call
                    //
                    bLocalName = TRUE;
                    strncpy(szLocalName, &argv[i][3], NCBNAMSZ);
                    szLocalName[NCBNAMSZ] = 0;
                    break;
                case 'r':       // do an adapter status on another 
                                // machine
                    bRemoteName = TRUE;
                    strncpy(szRemoteName, &argv[i][3], NCBNAMSZ);
                    szRemoteName[NCBNAMSZ] = 0;
                    break;
                default:
                    printf("usage: astat [/r:LocalName]\
[/l:LocalName]\n");
                    break;
             }
        }
    }
    return;
}

//
// Function: PrintAdapterInfo
//
// Description:
//    This function prints out the adapter info. About the only useful
//    information returned in this structure is:  MAC address, max 
//    datagram size, and max number of sessions. More often that not 
//    the other fields are not set (i.e. there always zero).
//
void PrintAdapterInfo(int lana, ADAPTER_STATUS adapter)
{
    printf("                      LANA: %d\n", lana);
    printf("               MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
        adapter.adapter_address[0], adapter.adapter_address[1],
        adapter.adapter_address[2], adapter.adapter_address[3],
        adapter.adapter_address[4], adapter.adapter_address[5]);
    //
    // Print the version of the NetBIOS specification implemented.
    // This should be 3.0 for current Microsoft Win332 platforms.
    //
    printf("           Netbios Version: %d.%d\n", adapter.rev_major,
        adapter.rev_minor);
    //
    // Print the type of network adapter
    //
    printf("              Adapter Type: ");
    if (adapter.adapter_type == 0xFF)
        printf("Token Ring\n");
    else if (adapter.adapter_type == 0xFE)
        printf("Ethernet\n");
    else
        printf("Unknown\n");
    printf("                  Duration: %d minutes\n", 
        adapter.duration);
    printf("  Num Aborted Trasmissions: %d\n", adapter.xmit_aborts);
    printf("   Num Transmitted Packets: %d\n", adapter.xmit_success);
    printf("      Num Received Packets: %d\n", adapter.recv_success);
    printf("             Num Free NCBs: %d\n", adapter.free_ncbs);
    printf("         Max Datagram Size: %d\n", adapter.max_dgram_size);
    printf("   Number Pending Sessions: %d\n", adapter.pending_sess);
    printf("    Max Number of Sessions: %d\n", adapter.max_cfg_sess);
    printf("Max Size of Session Packet: %d\n", 
        adapter.max_sess_pkt_size);
}

//
// Function: PrintNameInfo
//
// Description:
//    Prints out a NetBIOS name and its related information
//
void PrintNameInfo(NAME_BUFFER *names, int namecount)
{
    char        namebuff[NCBNAMSZ + 1];
    int                i;

    if (namecount == 0)
    { 
        printf("No names in local name table\n\n\n");
        return;
    }
    printf("\nName             Type  Number  Flags\n");
    for(i=0; i < namecount ;i++)
    {
        FormatNetbiosName(names[i].name, namebuff);
        printf("%s <%02x>     %-2d   ", namebuff, 
            names[i].name[NCBNAMSZ-1], names[i].name_num);
 
        if (names[i].name_flags & REGISTERING)
            printf("Registering  ");
        else if (names[i].name_flags & REGISTERED)
            printf("Registered ");
        else if (names[i].name_flags & DEREGISTERED)
            printf("Deregistered ");
        else if (names[i].name_flags & DUPLICATE)
            printf("Duplicate ");
        else if (names[i].name_flags & DUPLICATE_DEREG)
            printf("Duplicate-Deregistered ");
        if (names[i].name_flags & GROUP_NAME)
            printf("Group-Name ");
        printf("\n");
    }
    printf("\n\n");
}

//
// Function: LanaStatus
//
// Description:
//    Perform a LAN adapter status command.
//
int LanaStatus(int lana, char *name)
{
    NCB             ncb;
    MESSAGE_BUFFER  mb;

    ZeroMemory(&mb,  sizeof(MESSAGE_BUFFER));
    ZeroMemory(&ncb,  sizeof(NCB));

    memset(ncb.ncb_callname, ' ', NCBNAMSZ);
    //
    // Check command line options to see if the call is
    // made locally or remotely.
    //
    if ((bLocalName == FALSE) && (bRemoteName == FALSE))
        ncb.ncb_callname[0] = '*';
    else
        strncpy(ncb.ncb_callname, name, strlen(name));

    ncb.ncb_command = NCBASTAT;
    ncb.ncb_buffer  = (UCHAR *)&mb;
    ncb.ncb_length  = sizeof(MESSAGE_BUFFER);
    ncb.ncb_lana_num= lana;

    if (Netbios(&ncb) != NRC_GOODRET)
    {
        printf("Netbios: NCBASTAT failed: %d\n", ncb.ncb_retcode);
        return ncb.ncb_retcode;
    }
    PrintAdapterInfo(lana, mb.adapter);
    PrintNameInfo(mb.names, mb.adapter.name_count);

    return NRC_GOODRET;
}

//
// Function: main
//
// Description:
//    Setup the NetBIOS interface, parse the arguments, and call the
//    adapter status command either locally or remotely depending on
//    the user supplied arguments.
//
int main(int argc, char **argv)
{
    LANA_ENUM   lenum;
    int         i, num;

    ValidateArgs(argc, argv);
    //
    // Make sure both command line flags weren't set
    //
    if (bLocalName && bRemoteName)
    {
        printf("usage: astat [/l:LOCALNAME | /r:REMOTENAME]\n");
        return 1;
    }
    // Enumerate all LANAs and reset each one
    //
    if (LanaEnum(&lenum) != NRC_GOODRET)
        return 1;
    if (ResetAll(&lenum, (UCHAR)MAX_SESSIONS, (UCHAR)MAX_NAMES, 
            FALSE) != NRC_GOODRET)
        return 1;
    //
    // If we're called with a local name we need to add it to
    // the name table.
    //
    if (bRemoteName == FALSE)
    {
        for(i=0; i < lenum.length ;i++)
        {
            if (bLocalName)
                AddName(lenum.lana[i], szLocalName, &num);
            LanaStatus(lenum.lana[i], szLocalName);
        }
    }
    else
    {
        for(i=0; i < lenum.length ;i++)
            LanaStatus(lenum.lana[i], szRemoteName);
    }
    return 0;
}
