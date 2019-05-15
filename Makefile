# Main Makefile for the Kvaser Linux drivers.

# include setting from these files
include ./settings.mak
include ./config.mak



#----------------------------------------
# included in build
#----------------------------------------
USERLIBS  += canlib  

#---------------------------------
SUBDIRS   = $(USERLIBS) $(DRIVERS)

#---------------------------------
INSTALLS  = $(patsubst %,%_install,$(DRIVERS))


#-----------------------------------------------------
# Choose kernel

ifeq ($(KV_KERNEL_VER), 2_4)
	# explicitly 2.4
	KV_BUILD_KERNEL   = 2_4
	KV_KERNEL_SRC_DIR=$(KERNEL_SOURCE_DIR)
else 
  # explicitly 2.6
	ifeq ($(KV_KERNEL_VER), 2_6)
		KV_BUILD_KERNEL   =2_6
		KV_KERNEL_SRC_DIR=$(KERNEL_SOURCE_DIR)
	else
		# default
		KV_BUILD_KERNEL   =$(KV_DEFAULT_KERNEL)
		KV_KERNEL_SRC_DIR =$(KV_DEFAULT_KERNEL_SRC_PATH)
	endif
endif


#-----------------------------------------------------
# select sub makefile
ifeq ($(KV_BUILD_KERNEL),2_6)
	KV_MAKEFILE_EXT     =   _26
	KV_MAKEFLAGS_LAP    =   -C $(KV_KERNEL_SRC_DIR) SUBDIRS=$(PWD)/lapcan modules KV_DEBUG_ON=$(DEBUG_ON)
	KV_MAKEFLAGS_USB    =   -C $(KV_KERNEL_SRC_DIR) SUBDIRS=$(PWD)/usbcanII modules KV_DEBUG_ON=$(DEBUG_ON)
	KV_MAKEFLAGS_PCI    =   -C $(KV_KERNEL_SRC_DIR) SUBDIRS=$(PWD)/pcican modules KV_DEBUG_ON=$(DEBUG_ON)
	KV_MAKEFLAGS_PCI2   =   -C $(KV_KERNEL_SRC_DIR) SUBDIRS=$(PWD)/pcicanII modules KV_DEBUG_ON=$(DEBUG_ON)
	KV_INST_EXT         =   _26
else
	KV_MAKEFILE_EXT     =   _24 
	KV_MAKEFLAGS_LAP    = 	-C lapcan sub KV_DEBUG_ON=$(DEBUG_ON)
	KV_MAKEFLAGS_PCI    = 	-C pcican sub KV_DEBUG_ON=$(DEBUG_ON)
	KV_MAKEFLAGS_PCI2   = 	-C pcicanII sub KV_DEBUG_ON=$(DEBUG_ON)
	KV_MAKEFLAGS_USB    =   -C usbcanII sub KV_DEBUG_ON=$(DEBUG_ON)
	KV_INST_EXT         = 
endif


# Includes
INCLUDES := $(CURDIR)/include/


# debug
ifeq ($(BUILD_DEBUG),1)
    DEBUG_ON=1
    IS_DEBUG=DEBUG
else
	DEBUG_ON=0
	IS_DEBUG=
endif

ifeq ($(DEBUG_ON),1)

else
	DEBUG_ON=0
endif


# these macros are exported to sub-makes
export KV_KERNEL_SRC_DIR 
export INCLUDES


#---------------------------------------------------------------------------
# RULES

# should be here because otherwise make thinks it is made because we
# have a dir named debug
.PHONY: debug $(SUBDIRS)


all:  $(SUBDIRS)

canlib:
	@echo --------------------------------------------------------------------
	@echo "building CANLIB" $(IS_DEBUG)
	@echo --------------------------------------------------------------------
	cd canlib
	cp ./canlib/Makefile$(KV_MAKEFILE_EXT) ./canlib/Makefile -f
	cd ..
	$(MAKE) -C canlib sub$(DEBUG_EXT)

lapcan:
	@echo --------------------------------------------------------------------
	@echo "building LAPcan/LAPcanII" $(IS_DEBUG) 
	@echo "Kernel src:" $(KERNEL_SOURCE_DIR)  
	@echo --------------------------------------------------------------------
	
	cd lapcan
	cp ./lapcan/Makefile$(KV_MAKEFILE_EXT) ./lapcan/Makefile -f
	$(MAKE) $(KV_MAKEFLAGS_LAP)
	cd ..

pcican:
	@echo --------------------------------------------------------------------
	@echo "building PCIcan" $(IS_DEBUG)
	@echo "Kernel src:" $(KERNEL_SOURCE_DIR)  
	@echo --------------------------------------------------------------------
	cd ./pcican
	cp ./pcican/Makefile$(KV_MAKEFILE_EXT) ./pcican/Makefile -f
	$(MAKE) $(KV_MAKEFLAGS_PCI)
	cd ..

pcicanII:
	@echo --------------------------------------------------------------------
	@echo "building PCIcanII" $(IS_DEBUG)
	@echo "Kernel src:" $(KERNEL_SOURCE_DIR)  
	@echo --------------------------------------------------------------------
	cd ./pcicanII
	cp ./pcicanII/Makefile$(KV_MAKEFILE_EXT) ./pcicanII/Makefile -f
	$(MAKE) $(KV_MAKEFLAGS_PCI2)
	cd ..

usbcanII:
	@echo --------------------------------------------------------------------
	@echo "building USBcanII" $(IS_DEBUG)
	@echo "Kernel src:" $(KERNEL_SOURCE_DIR)  
	@echo --------------------------------------------------------------------
	cd ./usbcanII
	cp ./usbcanII/Makefile$(KV_MAKEFILE_EXT) ./usbcanII/Makefile -f
	$(MAKE) $(KV_MAKEFLAGS_USB)
	cd ..

debug: 
	@echo -----------------------------------
	@echo "Building DEBUG drivers"
	@echo -----------------------------------
	$(MAKE) DEBUG_ON=1 DEBUG_EXT=_debug

debug_install: 
	@echo -----------------------------------
	@echo "Building DEBUG drivers"
	@echo -----------------------------------
	$(MAKE) DEBUG_ON=1 DEBUG_EXT=_debug
	@ . installscript_canlib.sh
	@ . installscript_pcican$(KV_INST_EXT).sh
	@ . installscript_lapcan$(KV_INST_EXT).sh
	@ . installscript_pcicanII$(KV_INST_EXT).sh	
	@ . installscript_usbcan$(KV_INST_EXT).sh	
	
canlib_install: canlib
	@echo ----------------------------------
	@echo "installing CANLIB"
	@echo ----------------------------------
	@ . installscript_canlib.sh

pcican_install: canlib_install pcican
	@echo ----------------------------------
	@echo "installing PCIcan"
	@echo ----------------------------------
	@ . installscript_pcican$(KV_INST_EXT).sh

pcicanII_install: canlib_install  pcicanII
	@echo ----------------------------------
	@echo "installing PCIcanII"
	@echo ----------------------------------
	@ . installscript_pcicanII$(KV_INST_EXT).sh

lapcan_install: canlib_install lapcan
	@echo ----------------------------------
	@echo "installing LAPcan/LAPcanII"
	@echo ----------------------------------
	@ . installscript_lapcan$(KV_INST_EXT).sh

usbcan_install: canlib_install usbcanII 
	@echo ----------------------------------
	@echo "installing USBcanII"
	@echo ----------------------------------
	@ . installscript_usbcan$(KV_INST_EXT).sh

usbcanII_install: canlib_install usbcanII 
	@echo ----------------------------------
	@echo "installing USBcanII"
	@echo ----------------------------------
	@ . installscript_usbcan$(KV_INST_EXT).sh
	
install: $(INSTALLS)


clean:
	@for dir in $(SUBDIRS) ; do $(MAKE) -C $$dir clean -f Makefile$(KV_MAKEFILE_EXT); done

	
	
	
	
	
	
	
	
	
	
	
	
