
! include <ntwin32.mak>

# define these if you want a debug version
#PDB = ipconfig.pdb
#CDEBUG= -Zi -Fd"$(PDB)"
#LDEBUG = -debug:full

LIBS = kernel32.lib libcmt.lib wsock32.lib ws2_32.lib iphlpapi.lib

.cpp.obj:
	cl $(CDEBUG) -W3 -MT  -c $<

all:ipconfigv6.exe 

ipconfigv6.exe:ipconfigv6.obj
	link -nodefaultlib ipconfigv6.obj $(LIBS) $(LDEBUG) \
#
# uncomment the line below for a debug build
#	-pdb:$(PDB)  \
	-out:ipconfigv6.exe

clean:
	-del *.obj

cleanall:clean
	-del *.exe
