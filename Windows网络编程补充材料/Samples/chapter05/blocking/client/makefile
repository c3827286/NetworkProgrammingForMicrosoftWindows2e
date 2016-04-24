!include <win32.mak>

objs=bclient.obj resolve.obj

all: bclient.exe

.cpp.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

bclient.exe: $(objs) $(common_objs)
    $(link) $(linkdebug) $(conlflags) -out:bclient.exe $(objs) $(conlibsmt) ws2_32.lib

clean:
    del *.obj
    del *.exe
    del *.pdb
