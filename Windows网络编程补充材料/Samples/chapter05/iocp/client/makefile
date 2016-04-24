!include <win32.mak>

objs=iocpclient.obj resolve.obj

all: iocpclient.exe

.cpp.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

iocpclient.exe: $(objs) $(common_objs)
    $(link) $(linkdebug) $(conlflags) -out:iocpclient.exe $(objs) $(conlibsmt) ws2_32.lib

clean:
    del *.obj
    del *.exe
    del *.pdb
