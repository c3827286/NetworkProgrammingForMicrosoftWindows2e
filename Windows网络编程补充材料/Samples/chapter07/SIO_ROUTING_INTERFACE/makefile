!include <win32.mak>

objs=routequery.obj resolve.obj

all: routequery.exe

.c.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

routequery.exe: $(objs) $(common_objs)
    $(link) $(linkdebug) $(conlflags) -out:routequery.exe $(objs) $(conlibsmt) ws2_32.lib

clean:
    del *.obj
    del *.exe
