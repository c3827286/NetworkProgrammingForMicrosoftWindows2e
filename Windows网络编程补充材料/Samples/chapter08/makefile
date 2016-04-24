# Nmake macros for building Windows 32-Bit apps
TARGETOS=BOTH

!include <win32.mak>


enumns_objs=enumns.obj

dnsquery_objs=dnsquery.obj

nlaquery_objs=nlaquery.obj

rnrcs_objs=rnrcs.obj

all: enumns.exe dnsquery.exe nlaquery.exe rnrcs.exe

# Update the resource if necessary

# Update the object file if necessary

.cpp.obj: 
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

# Update the executable file if necessary, and if so, add the resource back in.

enumns.exe: $(enumns_objs) $(common_objs) 
    $(link) $(linkdebug) $(conlflags) -out:enumns.exe $(enumns_objs) $(conlibsmt) ws2_32.lib ole32.lib

dnsquery.exe: $(dnsquery_objs) $(common_objs) 
    $(link) $(linkdebug) $(conlflags) -out:dnsquery.exe $(dnsquery_objs) $(conlibsmt) ws2_32.lib

nlaquery.exe: $(nlaquery_objs) $(common_objs) 
    $(link) $(linkdebug) $(conlflags) -out:nlaquery.exe $(nlaquery_objs) $(conlibsmt) ws2_32.lib

rnrcs.exe: $(rnrcs_objs) $(common_objs) 
    $(link) $(linkdebug) $(conlflags) -out:rnrcs.exe $(rnrcs_objs) $(conlibsmt) ws2_32.lib ole32.lib user32.lib


clean:
	del *.obj
	del *.exe
