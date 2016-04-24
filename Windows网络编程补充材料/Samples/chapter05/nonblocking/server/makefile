!include <win32.mak>

objs=nbserver.obj resolve.obj

all: nbserver.exe

.cpp.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

nbserver.exe: $(objs) $(common_objs)
    $(link) $(linkdebug) $(conlflags) -out:nbserver.exe $(objs) $(conlibsmt) ws2_32.lib

clean:
    del *.obj
    del *.exe
    del *.pdb
