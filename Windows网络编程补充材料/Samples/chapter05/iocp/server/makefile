!include <win32.mak>

objs=iocpserver.obj resolve.obj

all: iocpserver.exe

.cpp.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

iocpserver.exe: $(objs) $(common_objs)
    $(link) $(linkdebug) $(conlflags) -out:iocpserver.exe $(objs) $(conlibsmt) ws2_32.lib

clean:
    del *.obj
    del *.exe
    del *.pdb
