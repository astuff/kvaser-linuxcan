# Kvaser Linux Canlib library Makefile 
# Copyright (C) 2005 Kvaser AB - support@kvaser.com - www.kvaser.com


include ../config.mak

LIBNAME = libcanlib.so
LIBRARY = $(LIBNAME).1.0.1
SONAME  = $(LIBNAME).1
TARBALL = canlib-1.0.1.tar.gz
RPMSPEC = canlib-1.0.1.spec

CFLAGS  += -Wall -DCANLIB_NAME_STRING=\"$(LIBRARY)\" -O2 -fomit-frame-pointer $(XTRA_CFLAGS) #-DPCMCIA_DEBUG=0
CFLAGS  += -DSPLIT -DLINUX=1 -D_LINUX=1 # maybe remove in time
CFLAGS  += -I. -I../include

# export to sub-makes
export XTRA_CFLAGS
export DEBUGFLAGS

#	-march=pentium -mcpu=pentium -funroll-loops \
#	-fomit-frame-pointer -fforce-mem -fforce-addr  
# Flags for  reentrant, position independent code

LIBCFLAGS = $(CFLAGS) -D_REENTRANT -fPIC
LDFLAGS = -lc -lpthread

#SRCS := $(wildcard *.c)
SRCS := canlib.c 
SRCS += linkedlist.c 
SRCS += VCanFunctions.c
SRCS += ../common/osif_functions_user.c

OBJS := $(patsubst %.c, %.o, $(SRCS))
OTHERDEPS := ../include/canlib.h
DEPS := $(patsubst %.c, %.d, $(SRCS))

.PHONY:	sub all tarball rpm clean examples depend

all:
	@echo "Run make from the parent directory"

#setup:
#	@mkdir $(OUTDIR) $(OUTDIR_DEBUG) -p --parents

sub: $(LIBRARY) examples
#	@./incfile canlib_build_version.h
#	@mv --target-directory=$(OUTDIR) *.o *.d

$(LIBRARY): $(DEPS) $(OTHERDEPS) $(OBJS)
	$(CC) $(CFLAGS) -shared -Wl,-soname,$(SONAME) -o $@ $(OBJS) $(LDFLAGS)
	ln -sf $@ $(LIBNAME)
	ln -sf $@ $(SONAME)

examples:
	$(MAKE) -C examples sub$(DEBUG_EXT) 

sub_debug: CFLAGS += $(KV_XTRA_CFLAGS_DEBUG)
sub_debug: DEBUG_EXT = _debug
sub_debug: sub

#tarball: $(TARBALL)

#$(TARBALL):
#	rm -f canlib_linux
#	ln -s .. canlib_linux
#	tar -zcvf $@ --files-from=canlib_tar_files
#	rm canlib_linux

#rpm:	tarball
#	rpm -bb $(RPMSPEC)

%.o: %.c
	$(CC) $(CFLAGS) $(LIBCFLAGS)  -c -o $@ $< 

%.d: %.c
	$(CC) -M $(CFLAGS) $< > $@ 
	$(CC) -M $(CFLAGS) $< | sed s/\\.o/.d/> $@ 

clean:	
	rm -f $(OBJS) $(LIBRARY) $(DEPS) $(LIBNAME) $(LIBRARY) $(SONAME) $(TARBALL) *~
#	rm -f $(OUTDIR)/*.o $(OUTDIR)/*.d *~
#	rm -f $(OUTDIR_DEBUG)/*.o $(OUTDIR_DEBUG)/*.d *~
	$(MAKE) -C examples clean

depend: $(DEPS)

-include $DEPS	 
