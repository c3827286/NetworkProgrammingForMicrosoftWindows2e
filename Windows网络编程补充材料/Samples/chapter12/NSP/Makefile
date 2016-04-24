!include <win32.mak>

.cpp.obj:
	$(cc) $(cdebug) -c -W3 -DWIN32 -LDd -D_WIN32 -DDEBUG $(cvarsdll) $<

DLL_FILES= mynsp.cpp \
           nspsvc.cpp \
           nspguid.cpp \
           printobj.cpp

OBJS= $(DLL_FILES:.cpp=.obj)

INST_FILES= nspguid.cpp \
            nspinstall.cpp

INST_OBJS= $(INST_FILES:.cpp=.obj)

SVC_FILES= mynspsvc.cpp \
           nspsvc.cpp \
           nspguid.cpp \
           printobj.cpp \
           mynsp.cpp

SVC_OBJS= $(SVC_FILES:.cpp=.obj)

RNR_FILES= nspguid.cpp \
           rnrcs.cpp
           

RNR_OBJS= $(RNR_FILES:.cpp=.obj)

all: mynsp.dll mynspsvc.exe rnrcs.exe nspinstall.exe

mynsp.dll: $(OBJS)
	$(link) $(linkdebug) $(dlllflags) -map -def:mynsp.def  \
	-out:$*.dll $(OBJS) $(guilibsdll) ws2_32.lib
        copy $*.dll %SYSTEMROOT%\System32

mynspsvc.exe: $(SVC_OBJS)
	$(link) $(linkdebug) $(conlflags) -out:$*.exe $(SVC_OBJS) $(conlibs) ws2_32.lib

rnrcs.exe: $(RNR_OBJS)
	$(link) $(linkdebug) $(conlflags) -out:$*.exe $(RNR_OBJS) $(conlibs) ws2_32.lib user32.lib

nspinstall.exe: $(INST_OBJS)
	$(link) $(linkdebug) $(conlflags) -out:$*.exe $(INST_OBJS) $(conlibs) ws2_32.lib

clean:
    -del *.obj
	-del *.dll
	-del *.pch
	-del *.exp
    -del *.pdb
    -del *.lib
    -del *.map
    -del *.exe
