!include <win32.mak>

objs=mcastws1.obj resolve.obj

all: mcastws1.exe

.c.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

mcastws1.exe: $(objs) $(common_objs)
    $(link) $(linkdebug) $(conlflags) -out:mcastws1.exe $(objs) $(conlibsmt) ws2_32.lib

clean:
    del *.obj
    del *.exe
    del *.pdb
