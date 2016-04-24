//
// Sample: Function definitions for QOS print routines
// 
// Files:
//      printqos.h      - this file
//      
// Description:
//      This file contains function prototypes for support routines
//      that print various QOS structure. These functions are 
//      implemented in printqos.cpp
//
#ifndef _PRINTQOS_H_
#define _PRINTQOS_H_

#define SERVICETYPE_STR_LEN        256

// QOS and FLOWSPEC print functions
void  PrintQos(QOS *pqos);
void  PrintFlowspec(FLOWSPEC *pflow, int indent);
void  PrintProviderSpecific(WSABUF *provider, int indent);
char *GetServiceTypeStr(SERVICETYPE type);

// Provider specific object functions
void PrintRsvpStatus     (RSVP_STATUS_INFO *status, int indent);
void PrintRsvpResv       (RSVP_RESERVE_INFO *reserve, int indent);
void PrintRsvpAdspec     (RSVP_ADSPEC *adspec, int indent);
void PrintRsvpPolicy     (RSVP_POLICY_INFO *policy, int indent);

void PrintQosSDMode      (QOS_SD_MODE *sd, int indent);
void PrintQosDestAddr    (QOS_DESTADDR *dest, int indent);

void PrintAdGeneralParams(AD_GENERAL_PARAMS *params, int indent);


#endif
