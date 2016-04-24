!include <win32.mak>

objs=bserver.obj resolve.obj

all: bserver.exe

.cpp.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

bserver.exe: $(objs) $(common_objs)
    $(link) $(linkdebug) $(conlflags) -out:bserver.exe $(objs) $(conlibsmt) ws2_32.lib

clean:
    del *.obj
    del *.exe
    del *.pdb
