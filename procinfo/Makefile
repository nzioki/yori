
BINARIES=procinfo.exe

!INCLUDE "..\config\common.mk"

!IF $(PDB)==1
LINKPDB=/Pdb:procinfo.pdb
!ENDIF

CFLAGS=$(CFLAGS) -DPROCINFO_VER_MAJOR=$(PROCINFO_VER_MAJOR) -DPROCINFO_VER_MINOR=$(PROCINFO_VER_MINOR)

BIN_OBJS=\
	 procinfo.obj         \

MOD_OBJS=\
	 mod_proc.obj     \

compile: $(BIN_OBJS) builtins.lib

procinfo.exe: $(BIN_OBJS) $(YORILIB) $(YORICRT)
	@echo $@
	@$(LINK) $(LDFLAGS) -entry:$(YENTRY) $(BIN_OBJS) $(LIBS) $(CRTLIB) $(YORILIB) -version:$(PROCINFO_VER_MAJOR).$(PROCINFO_VER_MINOR) $(LINKPDB) -out:$@

mod_proc.obj: procinfo.c
	@echo $@
	@$(CC) -c -DYORI_BUILTIN=1 $(CFLAGS) -Fo$@ procinfo.c

builtins.lib: $(MOD_OBJS)
	@echo $@
	@$(LIB32) $(LIBFLAGS) $(MOD_OBJS) -out:$@

