!include <win32.mak>

objs=mcastws2.obj resolve.obj

all: mcastws2.exe

.c.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

mcastws2.exe: $(objs) $(common_objs)
    $(link) $(linkdebug) $(conlflags) -out:mcastws2.exe $(objs) $(conlibsmt) ws2_32.lib

clean:
    del *.obj
    del *.exe
    del *.pdb
