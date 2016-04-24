!include <win32.mak>

objs=mcastv3.obj resolve.obj

all: mcastv3.exe

.c.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

mcastv3.exe: $(objs) $(common_objs)
    $(link) $(linkdebug) $(conlflags) -out:mcastv3.exe $(objs) $(conlibsmt) ws2_32.lib

clean:
    del *.obj
    del *.exe
    del *.pdb
