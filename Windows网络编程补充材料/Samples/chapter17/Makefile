# Nmake macros for building Windows 32-Bit apps
TARGETOS=BOTH
#APPVER=5.0

!include <win32.mak>


objs_astat=astat.obj nbcommon.obj

objs_cbnbsvr=cbnbsvr.obj nbcommon.obj

objs_evnbsvr=evnbsvr.obj nbcommon.obj

objs_findname=findname.obj nbcommon.obj

objs_nbclient=nbclient.obj nbcommon.obj

objs_nbdgram=nbdgram.obj nbcommon.obj

objs_nbproto=nbproto.obj

netlibs=netapi32.lib user32.lib

all: astat.exe cbnbsvr.exe evnbsvr.exe findname.exe nbclient.exe nbdgram.exe nbproto.exe

# Update the resource if necessary

# Update the object file if necessary

.c.obj: 
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.c

# Update the executable file if necessary, and if so, add the resource back in.

astat.exe: $(objs_astat) $(common_objs) 
    $(link) $(linkdebug) $(conlflags) -out:astat.exe $(objs_astat) $(conlibsmt) $(netlibs)

cbnbsvr.exe: $(objs_cbnbsvr) $(common_objs) 
    $(link) $(linkdebug) $(conlflags) -out:cbnbsvr.exe $(objs_cbnbsvr) $(conlibsmt) $(netlibs)

evnbsvr.exe: $(objs_evnbsvr) $(common_objs) 
    $(link) $(linkdebug) $(conlflags) -out:evnbsvr.exe $(objs_evnbsvr) $(conlibsmt) $(netlibs)

findname.exe: $(objs_findname) $(common_objs) 
    $(link) $(linkdebug) $(conlflags) -out:findname.exe $(objs_findname) $(conlibsmt) $(netlibs)

nbclient.exe: $(objs_nbclient) $(common_objs) 
    $(link) $(linkdebug) $(conlflags) -out:nbclient.exe $(objs_nbclient) $(conlibsmt) $(netlibs)

nbdgram.exe: $(objs_nbdgram) $(common_objs) 
    $(link) $(linkdebug) $(conlflags) -out:nbdgram.exe $(objs_nbdgram) $(conlibsmt) $(netlibs)

nbproto.exe: $(objs_nbproto) $(common_objs) 
    $(link) $(linkdebug) $(conlflags) -out:nbproto.exe $(objs_nbproto) $(conlibsmt) ws2_32.lib

clean:
	del  *.obj
	del  *.exe
    del  *.pdb
