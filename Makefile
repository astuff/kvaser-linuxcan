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

# Main Makefile for the Kvaser linuxcan.

#----------------------------------------
# included in build
#----------------------------------------
USERLIBS  += canlib
USERLIBS  += linlib

DRIVERS   += common
DRIVERS   += leaf
DRIVERS   += mhydra
DRIVERS   += usbcanII
DRIVERS   += virtualcan
# KV_NO_PCI should be set on targets that do not have PCI, such as RaspberryPi.
ifndef KV_NO_PCI
DRIVERS   += pcican
DRIVERS   += pcican2
DRIVERS   += pciefd
endif

#---------------------------------
# Debug levels are defined in config.mak
KV_DEBUG_ON ?= 0
export KV_DEBUG_ON

#---------------------------------
SUBDIRS   = $(USERLIBS) $(DRIVERS)

reverse=$(if $(1),$(call reverse,$(wordlist 2,$(words $(1)),$(1)))) $(firstword $(1))

KV_KERNEL_VERSION ?= `uname -r`
KDIR ?= /lib/modules/$(KV_KERNEL_VERSION)/build
define print_versions
	echo '$1 building linuxcan v'`sed -n 's/^version=//g; s/_/./g; s/\([[:digit:]]\+\.[[:digit:]]\+\.[[:digit:]]\+\).\(beta\)\?/\1 \2/p' moduleinfo.txt`
	echo '  User    : '$(USER)
	echo '  System  : '`uname -a`
	echo '  CC      : '$(CC)
	echo '  CC ver. : '`$(CC) -dumpfullversion -dumpversion`
	echo '  KDIR    : '$(KDIR)
	echo '  Kernel  : '$(KV_KERNEL_VERSION)
	echo ''
endef

KV_DKMS_MODULE = $(shell sed -n 's/^PACKAGE_NAME="\(.*\)"/\1/p' dkms/dkms.conf)
KV_DKMS_VERSION = $(shell sed -n 's/^PACKAGE_VERSION="\(.*\)"/\1/p' dkms/dkms.conf)
KV_DKMS_TARBALL = $(KV_DKMS_MODULE)-$(KV_DKMS_VERSION)-source-only.dkms.tar.gz
KV_DKMS_TMP = kv_dkms_tmp
KV_DKMS_TMP_SRC = $(KV_DKMS_TMP)/src_tmp
KV_DKMS_TMP_SRC_LINUXCAN = $(KV_DKMS_TMP_SRC)/$(KV_DKMS_MODULE)-$(KV_DKMS_VERSION)
KV_DKMS_TMP_DKMSTREE = $(KV_DKMS_TMP)/dkmstree_tmp

# Select if the kernel modules should only be installed or both installed and loaded:
KV_DKMS_INSTALL_TARGET = load
# KV_DKMS_INSTALL_TARGET = install

#---------------------------------------------------------------------------
# RULES
.PHONY: print_versions_start canlib linlib common leaf mhydra pcican pcican2 usbcanII virtualcan pciefd kvflash install uninstall clean check load dkms dkms_install dkms_load

all: print_versions_start $(SUBDIRS)
	@echo
	@$(call print_versions, Done)
	@$(call check_for_kvaser_usb_devices)
	@$(call check_for_secure_boot)

print_versions_start:
	@$(call print_versions, Start)
	@$(call check_for_kvaser_usb_devices)
	@$(call check_for_secure_boot)

canlib:
	$(MAKE) -C canlib examples

linlib: canlib
	$(MAKE) -C linlib

common:
	@cd ./common; $(MAKE) kv_module

pcican: common
	@cd ./pcican; $(MAKE) kv_module

pcican2: common
	@cd ./pcican2; $(MAKE) kv_module

usbcanII: common
	@cd ./usbcanII; $(MAKE) kv_module

leaf: common
	@cd ./leaf; $(MAKE) kv_module

mhydra: common
	@cd ./mhydra; $(MAKE) kv_module

virtualcan: common
	@cd ./virtualcan; $(MAKE) kv_module

pciefd: common
	@cd ./pciefd; $(MAKE) kv_module

kvflash: canlib
	$(MAKE) -C kvflash

install:
	@for dir in $(DRIVERS) ; do cd $$dir; echo Installing $$dir;./installscript.sh || exit 1; cd ..; done
	$(MAKE) -C canlib install
	$(MAKE) -C linlib install

uninstall:
	@for dir in $(call reverse,$(DRIVERS)) ; do cd $$dir; echo Uninstalling $$dir;./uninstallscript.sh || exit 1; cd ..; done
	$(MAKE) -C canlib uninstall
	$(MAKE) -C linlib uninstall
	rm -f /etc/udev/rules.d/10-kvaser.rules
	rm -f /etc/modules-load.d/kvaser.conf

purge:
	@for dir in $(call reverse,$(DRIVERS)) ; do cd $$dir; echo Uninstalling $$dir;./uninstallscript.sh -p || exit 1; cd ..; done
	$(MAKE) -C canlib uninstall
	$(MAKE) -C linlib uninstall
	rm -f /etc/udev/rules.d/10-kvaser.rules
	rm -f /etc/modules-load.d/kvaser.conf

load:
	@for dir in $(DRIVERS) ; do cd $$dir; echo Installing $$dir;./installscript.sh load || exit 1; cd ..; done
	$(MAKE) -C canlib install
	$(MAKE) -C linlib install

check:
	$(MAKE) -C canlib check
	@for dir in $(DRIVERS) ; do cd $$dir; $(MAKE) check; cd ..; done

clean:
	@for dir in $(SUBDIRS) ; do cd $$dir; $(MAKE) clean; cd ..; done
	rm -f modules.order Module.symvers $(KV_DKMS_TARBALL)
	rm -rf .tmp_versions $(KV_DKMS_TMP)
	find . -name "checklog.txt"|xargs rm -f


define check_for_kvaser_usb_devices
	if lsusb -d 0bfd: > /dev/null ; then \
		echo '*****************************************************'; \
		echo 'WARNING: Found connected Kvaser USB device(s)!'; \
		echo '         Unplug them before installing the drivers.'; \
		echo '*****************************************************'; \
		echo ''; \
	fi
endef

HOSTNAME ?= $(shell uname -n)
EFI_SYS_PATH ?= /sys/firmware/efi
EFI_SECUREBOOT_PATH ?= $(EFI_SYS_PATH)/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c
define check_for_secure_boot
	if test -d $(EFI_SYS_PATH) ; then \
		if which mokutil > /dev/null 2>&1 ; then \
			if mokutil --sb-state | grep --silent 'SecureBoot enabled' ; then \
				echo '*****************************************************'; \
				echo 'WARNING: Secure Boot is enabled on <$(HOSTNAME)>!'; \
				echo '         When Secure Boot is enabled, driver modules'; \
				echo '         need to be signed with a valid private key' ; \
				echo '         in order to be loaded by the kernel.'; \
				echo '*****************************************************'; \
				echo ''; \
			fi \
		elif test -f $(EFI_SECUREBOOT_PATH) ; then \
			if od --skip-bytes=4 --read-bytes=1 -An -t u1 $(EFI_SECUREBOOT_PATH) | grep --silent 1 ; then \
				echo '*****************************************************'; \
				echo 'WARNING: EFI is used on <$(HOSTNAME)>!'; \
				echo '         It looks like Secure Boot is enabled!'; \
				echo '         When Secure Boot is enabled, driver modules'; \
				echo '         need to be signed with a valid private key' ; \
				echo '         in order to be loaded by the kernel.'; \
				echo '*****************************************************'; \
				echo ''; \
			else \
				echo '*****************************************************'; \
				echo 'WARNING: EFI is used on <$(HOSTNAME)>!'; \
				echo '         It looks like Secure Boot is disabled.'; \
				echo '         When Secure Boot is enabled, driver modules'; \
				echo '         need to be signed with a valid private key' ; \
				echo '         in order to be loaded by the kernel.'; \
				echo '*****************************************************'; \
				echo ''; \
			fi \
		else \
			echo '*****************************************************'; \
			echo 'WARNING: EFI is used on <$(HOSTNAME)>!'; \
			echo '         Not able to determine whether Secure Boot is'; \
			echo '         enabled or disabled.'; \
			echo '         When Secure Boot is enabled, driver modules'; \
			echo '         need to be signed with a valid private key' ; \
			echo '         in order to be loaded by the kernel.'; \
			echo '*****************************************************'; \
			echo ''; \
		fi \
	fi
endef

$(KV_DKMS_TARBALL):
	@echo Build DKMS tarball, with install target: $(KV_DKMS_INSTALL_TARGET)
	rm -rf $(KV_DKMS_TMP)
	mkdir -p $(KV_DKMS_TMP)/src_tmp/$(KV_DKMS_MODULE)-$(KV_DKMS_VERSION)
	mkdir -p $(KV_DKMS_TMP_DKMSTREE)
	@for dir in $(DRIVERS) ; do cp -r $$dir $(KV_DKMS_TMP_SRC_LINUXCAN)/. ; done
ifdef KV_NO_PCI
	# make the non-pci dkms.conf
	cp dkms/dkms-no-pci.conf $(KV_DKMS_TMP_SRC_LINUXCAN)/dkms.conf
else
	cp dkms/dkms.conf $(KV_DKMS_TMP_SRC_LINUXCAN)/dkms.conf
endif
	cp dkms/Makefile 10-kvaser.rules dkms/kv_dkms_script.sh moduleinfo.txt config.mak README COPYING COPYING.BSD COPYING.GPL $(KV_DKMS_TMP_SRC_LINUXCAN)/.
	cp -r include common $(KV_DKMS_TMP_SRC_LINUXCAN)/.
	sed -i 's/^KV_INSTALL_TARGET=none/KV_INSTALL_TARGET=$(KV_DKMS_INSTALL_TARGET)/g' $(KV_DKMS_TMP_SRC_LINUXCAN)/dkms.conf
	fakeroot dkms add --verbose --sourcetree "$(PWD)/$(KV_DKMS_TMP_SRC)" --dkmstree $(KV_DKMS_TMP_DKMSTREE) $(KV_DKMS_MODULE)/$(KV_DKMS_VERSION)
	dkms mktarball --verbose --dkmstree $(KV_DKMS_TMP_DKMSTREE) --source-only $(KV_DKMS_MODULE)/$(KV_DKMS_VERSION)
	cp $(KV_DKMS_TMP_DKMSTREE)/$(KV_DKMS_MODULE)/$(KV_DKMS_VERSION)/tarball/$(KV_DKMS_TARBALL) .
	rm -rf $(KV_DKMS_TMP)

dkms: print_versions_start $(USERLIBS) $(KV_DKMS_TARBALL)
	@$(call print_versions, Done)
	@$(call check_for_kvaser_usb_devices)
	@$(call check_for_secure_boot)

dkms_install dkms_load: $(KV_DKMS_TARBALL)
	$(MAKE) -C canlib install
	$(MAKE) -C linlib install
	dkms status $(KV_DKMS_MODULE) | cut -d',' -f 2 | cut -d':' -f 1 | xargs -I theversion dkms remove $(KV_DKMS_MODULE)/theversion --all
	dkms add $(KV_DKMS_TARBALL)
	dkms install $(KV_DKMS_MODULE)/$(KV_DKMS_VERSION) -k $(KV_KERNEL_VERSION)
	dkms status

dkms_uninstall:
	$(MAKE) -C canlib uninstall
	$(MAKE) -C linlib uninstall
	dkms status
	-dkms remove $(KV_DKMS_MODULE)/$(KV_DKMS_VERSION) --all
	rm -f /etc/udev/rules.d/10-kvaser.rules
	rm -f /etc/modules-load.d/kvaser.conf
	rm -rf /usr/src/$(KV_DKMS_MODULE)-$(KV_DKMS_VERSION)
	dkms status
