!include <win32.mak>

objs=asyncserver.obj resolve.obj

all: asyncserver.exe

.cpp.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $*.cpp

asyncserver.exe: $(objs) $(common_objs)
    $(link) $(linkdebug) $(conlflags) -out:asyncserver.exe $(objs) $(conlibsmt) ws2_32.lib user32.lib gdi32.lib

clean:
    del *.obj
    del *.exe
    del *.pdb
