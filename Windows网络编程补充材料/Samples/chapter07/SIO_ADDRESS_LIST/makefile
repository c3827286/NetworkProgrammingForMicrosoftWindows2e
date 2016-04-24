!include <win32.mak>

objs=addrquery.obj resolve.obj

all: addrquery.exe

.c.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

addrquery.exe: $(objs) $(common_objs)
    $(link) $(linkdebug) $(conlflags) -out:addrquery.exe $(objs) $(conlibsmt) ws2_32.lib

clean:
    del *.obj
    del *.exe
