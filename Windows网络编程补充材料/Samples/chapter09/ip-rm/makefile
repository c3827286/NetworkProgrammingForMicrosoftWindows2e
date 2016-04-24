!include <win32.mak>

objs=rmcast.obj resolve.obj

all: rmcast.exe

.c.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

rmcast.exe: $(objs) $(common_objs)
    $(link) $(linkdebug) $(conlflags) -out:rmcast.exe $(objs) $(conlibsmt) ws2_32.lib

clean:
    del *.obj
    del *.exe
    del *.pdb
