#
# CONFIG MAKEFILE
#


#-----------------------------
# DEBUGGING INFO ON/OF       |
# change below               |
#-----------------------------
#
# alter lines below to change 
# debug level
export KV_PCMCIA_DEBUG_LEVEL    = 8
export KV_PCICAN_DEBUG_LEVEL    = 8
export KV_USBCAN_DEBUG_LEVEL    = 8
export KV_PCICANII_DEBUG_LEVEL  = 8

# kernel to build for as default i.e. when not specifying
#export KV_DEFAULT_KERNEL           = 2_6
export KV_DEFAULT_KERNEL_SRC_PATH  = /usr/src/linux-`uname -r`
#export KV_DEFAULT_KERNEL_SRC_PATH = /usr/src/linux-2.6.3-7mdk

#---------------------------------------------------------------------------

# comment lines below out 
# to remove debug code
KV_LAPCAN_ON   = -DPCMCIA_DEBUG=$(KV_PCMCIA_DEBUG_LEVEL)
KV_PCICAN_ON   = -DPCICAN_DEBUG=$(KV_PCICAN_DEBUG_LEVEL)
KV_USBCAN_ON   = -DUSBCAN_DEBUG=$(KV_USBCAN_DEBUG_LEVEL)
KV_PCICANII_ON = -DPCICANII_DEBUG=$(KV_PCICANII_DEBUG_LEVEL)

KV_DEBUGFLAGS  = -D_DEBUG=1 -DDEBUG=1 $(KV_LAPCAN_ON) $(KV_PCICAN_ON) $(KV_USBCAN_ON) $(KV_PCICANII_ON)
KV_NDEBUGFLAGS = -D_DEBUG=0 -DDEBUG=0 

#---------------------------------------------------------------------------

# export these flags to compilation
KV_XTRA_COMMON_FLAGS           = -DLINUX=1 -D_LINUX=1 -I$(INCLUDES) 

# kernel 2.4 and older
export KV_XTRA_CFLAGS          = $(KV_XTRA_COMMON_FLAGS) $(KV_NDEBUGFLAGS)
export KV_XTRA_CFLAGS_DEBUG    = $(KV_XTRA_COMMON_FLAGS) $(KV_DEBUGFLAGS) 

#kernel 2.6
export KV_XTRA_CFLAGS_26       = $(KV_XTRA_COMMON_FLAGS) $(KV_NDEBUGFLAGS) -DLINUX_2_6=1
export KV_XTRA_CFLAGS_DEBUG_26 = $(KV_XTRA_COMMON_FLAGS) $(KV_DEBUGFLAGS)  -DLINUX_2_6=1