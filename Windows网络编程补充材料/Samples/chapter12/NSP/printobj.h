#ifndef _PRINT_OBJ_H_
#define _PRINT_OBJ_H_

extern "C" 
{

void PrintBytes(BYTE *bytes, int count);
void PrintGuid(GUID *guid);

void PrintServiceClass(WSASERVICECLASSINFOW *sc);
void PrintQuery(WSAQUERYSETW *qs);

}

#endif
