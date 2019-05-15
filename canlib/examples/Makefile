# Kvaser Linux Canlib
# Examples Makefile
# Copyright (C) 2003 Kvaser AB - support@kvaser.com - www.kvaser.com

CC = gcc
CFLAGS = -Wall -O2  -D_REENTRANT $(XTRA_CFLAGS) -I../../include
LDFLAGS = -L.. -lcanlib -lpthread 
OBJS = 	busparms readTimerTest canmonitor writeloop cancount simplewrite listChannels

# Default target
sub:	$(OBJS)	
#	@mv --target-directory=$(OUTDIR) $(OBJS)

#setup:
#	@mkdir $(OUTDIR) $(OUTDIR_DEBUG) -p --parents

sub_debug: CFLAGS += $(DEBUGFLAGS)
sub_debug: sub


all:	sub

clean:	
	rm -f $(OBJS) *.o *~
#	rm -f $(CURDIR)/$(OUTDIR)/*