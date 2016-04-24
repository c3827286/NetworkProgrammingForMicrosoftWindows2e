!include <win32.mak>

objs=overserver.obj resolve.obj

all: overserver.exe

.cpp.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

overserver.exe: $(objs) $(common_objs)
    $(link) $(linkdebug) $(conlflags) -out:overserver.exe $(objs) $(conlibsmt) ws2_32.lib

clean:
    del *.obj
    del *.exe
    del *.pdb
