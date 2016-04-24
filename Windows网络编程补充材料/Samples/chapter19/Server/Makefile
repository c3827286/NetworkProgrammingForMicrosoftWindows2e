# Nmake macros for building Windows 32-Bit apps
TARGETOS=BOTH
#APPVER=5.0

!include <win32.mak>


objs=server.obj

all: server.exe

# Update the resource if necessary

# Update the object file if necessary

.cpp.obj: 
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

# Update the executable file if necessary, and if so, add the resource back in.

server.exe: $(objs) $(common_objs) 
    $(link) $(linkdebug) $(conlflags) -out:server.exe $(objs) $(conlibsmt)

clean:
	del *.obj
	del *.exe
