# Nmake macros for building Windows 32-Bit apps
TARGETOS=BOTH

!include <win32.mak>


objs_udp=qosudp.obj provider.obj printqos.obj

objs_tcp=qostcp.obj provider.obj printqos.obj

objs_template=template.obj provider.obj printqos.obj

all: qosudp.exe qostcp.exe template.exe

# Update the resource if necessary

# Update the object file if necessary

.cpp.obj: 
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

# Update the executable file if necessary, and if so, add the resource back in.

qosudp.exe: $(objs_udp) $(common_objs) 
    $(link) $(linkdebug) $(conlflags) -out:qosudp.exe $(objs_udp) $(conlibsmt) ws2_32.lib

qostcp.exe: $(objs_tcp) $(common_objs) 
    $(link) $(linkdebug) $(conlflags) -out:qostcp.exe $(objs_tcp) $(conlibsmt) ws2_32.lib

template.exe: $(objs_template) $(common_objs) 
    $(link) $(linkdebug) $(conlflags) -out:template.exe $(objs_template) $(conlibsmt) ws2_32.lib

clean:
	del *.obj
	del *.exe
	del *.pdb
