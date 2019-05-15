#
#             Copyright 2017 by Kvaser AB, Molndal, Sweden
#                         http://www.kvaser.com
#
#  This software is dual licensed under the following two licenses:
#  BSD-new and GPLv2. You may use either one. See the included
#  COPYING file for details.
#
#  License: BSD-new
#  ==============================================================================
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
#      * Redistributions of source code must retain the above copyright
#        notice, this list of conditions and the following disclaimer.
#      * Redistributions in binary form must reproduce the above copyright
#        notice, this list of conditions and the following disclaimer in the
#        documentation and/or other materials provided with the distribution.
#      * Neither the name of the <organization> nor the
#        names of its contributors may be used to endorse or promote products
#        derived from this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
#  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
#  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
#  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
#  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  POSSIBILITY OF SUCH DAMAGE.
#
#
#  License: GPLv2
#  ==============================================================================
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
#
#
#  IMPORTANT NOTICE:
#  ==============================================================================
#  This source code is made available for free, as an open license, by Kvaser AB,
#  for use with its applications. Kvaser AB does not accept any liability
#  whatsoever for any third party patent or other immaterial property rights
#  violations that may result from any usage of this source code, regardless of
#  the combination of source code and various applications that it can be used
#  in, or with.
#
#  -----------------------------------------------------------------------------
#

DEPMOD=`which depmod`
UDEVCTRL=`which udevcontrol`
UDEVADM=`which udevadm`

GCC_MAJ_VERSION_GTEQ_4 := $(shell expr `gcc -dumpversion | cut -f1 -d.` \>= 4)
GCC_MIN_VERSION_GTEQ_8 := $(shell expr `gcc -dumpversion | cut -f2 -d.` \>= 8)

# alter lines below to change default debug level
ifndef KV_VCANOSIF_DEBUG_LEVEL
export KV_VCANOSIF_DEBUG_LEVEL = 1
endif

ifndef KV_PCICAN_DEBUG_LEVEL
export KV_PCICAN_DEBUG_LEVEL    = 1
endif

ifndef KV_USBCAN_DEBUG_LEVEL
export KV_USBCAN_DEBUG_LEVEL    = 1
endif

ifndef KV_PCICAN2_DEBUG_LEVEL
export KV_PCICAN2_DEBUG_LEVEL  = 1
endif

ifndef KV_LEAF_DEBUG_LEVEL
export KV_LEAF_DEBUG_LEVEL      = 1
endif

ifndef KV_MHYDRA_DEBUG_LEVEL
export KV_MHYDRA_DEBUG_LEVEL    = 1
endif

ifndef KV_VIRTUAL_DEBUG_LEVEL
export KV_VIRTUAL_DEBUG_LEVEL   = 1
endif

ifndef KV_PCIEFD_DEBUG_LEVEL
export KV_PCIEFD_DEBUG_LEVEL = 1
endif

#---------------------------------------------------------------------------
# Specific debug flags
KV_PCICAN_ON   += -DPCICAN_DEBUG=$(KV_PCICAN_DEBUG_LEVEL)
KV_USBCAN_ON   += -DUSBCAN_DEBUG=$(KV_USBCAN_DEBUG_LEVEL)
KV_PCICAN2_ON  += -DPCICAN2_DEBUG=$(KV_PCICAN2_DEBUG_LEVEL)
KV_LEAF_ON     += -DLEAF_DEBUG=$(KV_LEAF_DEBUG_LEVEL)
KV_MHYDRA_ON   += -DMHYDRA_DEBUG=$(KV_MHYDRA_DEBUG_LEVEL)
KV_VIRTUAL_ON  += -DVIRTUAL_DEBUG=$(KV_VIRTUAL_DEBUG_LEVEL)
KV_PCIEFD_ON   += -DPCIEFD_DEBUG=$(KV_PCIEFD_DEBUG_LEVEL)
KV_VCANOSIF_ON += -DVCANOSIF_DEBUG=$(KV_VCANOSIF_DEBUG_LEVEL)
KV_MAGISYNC_ON += -DMAGISYNC_DEBUG=1

KV_DEBUGFLAGS  = -D_DEBUG=1 -DDEBUG=1 $(KV_PCICAN_ON) $(KV_USBCAN_ON) $(KV_PCICAN2_ON) $(KV_LEAF_ON) $(KV_MHYDRA_ON) $(KV_VIRTUAL_ON) $(KV_PCIEFD_ON) $(KV_VCANOSIF_ON) $(KV_MAGISYNC_ON)
KV_NDEBUGFLAGS = -D_DEBUG=0 -DDEBUG=0

#----------------------------------------
# Select kernel source folder
KDIR ?= /lib/modules/`uname -r`/build
KV_KERNEL_SRC_DIR := $(KDIR)

#---------------------------------------------------------------------------
# export these flags to compilation
KV_XTRA_COMMON_FLAGS = -DLINUX=1 $(foreach INC,$(INCLUDES),-I$(INC)) -Werror -Wno-date-time -Wall \
                       -Wclobbered -Wempty-body -Wignored-qualifiers -Wmissing-parameter-type \
                       -Wold-style-declaration -Woverride-init -Wtype-limits \
                       -Wuninitialized

# gcc 4.6 does not allow initializing a struct field with '{ 0 }'
# gcc 4.7.2 is reporting false positive warnings
ifeq ($(GCC_MAJ_VERSION_GTEQ_4),1)
ifeq ($(GCC_MIN_VERSION_GTEQ_8),1)
  KV_XTRA_COMMON_FLAGS += -Wmissing-field-initializers
ifeq ($(ARCH),arm) # On ARM, ignore missing field initializers in <kv_module_name>.mod.c
  CFLAGS_$(KV_MODULE_NAME).mod.o += -Wno-missing-field-initializers
endif
endif
endif

export KV_XTRA_CFLAGS       = $(KV_XTRA_COMMON_FLAGS) $(KV_NDEBUGFLAGS) -DWIN32=0
export KV_XTRA_CFLAGS_DEBUG = $(KV_XTRA_COMMON_FLAGS) $(KV_DEBUGFLAGS)  -DWIN32=0

# obj files
OBJS := $(patsubst %.c,%.o,$(SRCS))

ifeq ($(KV_DEBUG_ON),1)
  export EXTRA_CFLAGS=$(KV_XTRA_CFLAGS_DEBUG)
  IS_DEBUG=Debug: $(KV_DEBUGFLAGS)
else
  export EXTRA_CFLAGS=$(KV_XTRA_CFLAGS)
endif


#------------------------------------------------------
obj-m := $(KV_MODULE_NAME).o
$(KV_MODULE_NAME)-objs := $(OBJS)

KBUILD_EXTRA_SYMBOLS = $(KBUILD_EXTMOD)/../common/Module.symvers

CHECK_LOGFILE = checklog.txt

.PHONY: kv_module install clean check

kv_module:
	@echo --------------------------------------------------------------------
	@echo "building $(KV_MODULE_NAME) $(IS_DEBUG)"
	@echo "Kernel src:" $(KV_KERNEL_SRC_DIR)
	$(MAKE) -C $(KV_KERNEL_SRC_DIR) SUBDIRS=$(PWD) modules
	@echo --------------------------------------------------------------------

install:
	@echo --------------------------------------------------------------------
	@echo "You need to manually install by running 'sudo ./installscript.sh'"
	@echo --------------------------------------------------------------------

check:
# Install cppcheck with 'sudo apt-get install cppcheck'
	@echo --------------------------------------------------------------------
ifeq ($(CHECK_SUPPRESS),)
		cppcheck -I ../include/ -I ../../tmp  -I /usr/include -I /usr/include/linux --enable=all --suppress=toomanyconfigs . 2> $(CHECK_LOGFILE)
else
		cppcheck -I ../include/ -I ../../tmp  -I /usr/include -I /usr/include/linux --enable=all --suppressions-list=$(CHECK_SUPPRESS) --suppress=toomanyconfigs . 2> $(CHECK_LOGFILE)
endif
	@cat $(CHECK_LOGFILE)
	@echo --------------------------------------------------------------------

clean:
	@echo --------------------------------------------------------------------
	@echo "Cleaning $(KV_MODULE_NAME)" $(IS_DEBUG)
	rm -f $(foreach suffix, o mod.o ko mod.c, $(KV_MODULE_NAME).$(suffix))                \
	      $(foreach suffix, o.cmd mod.o.cmd ko.cmd, .$(KV_MODULE_NAME).$(suffix))         \
	      modules.order Module.symvers new_modules.conf                                   \
	      $(SRCS:%.c=%.o)                                                                 \
	      $(join $(dir $(SRCS)),$(addprefix .,$(notdir $(patsubst %.c,%.o.cmd,$(SRCS))))) \
	      $(join $(dir $(SRCS)),$(addprefix .,$(notdir $(patsubst %.c,%.o.d,$(SRCS)))))
	rm -rf .tmp_versions
	@echo --------------------------------------------------------------------


