!include <win32.mak>

objs=eventserver.obj resolve.obj

all: eventserver.exe

.cpp.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

eventserver.exe: $(objs) $(common_objs)
    $(link) $(linkdebug) $(conlflags) -out:eventserver.exe $(objs) $(conlibsmt) ws2_32.lib

clean:
    del *.obj
    del *.exe
    del *.pdb
