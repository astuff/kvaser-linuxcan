#!/bin/sh

install -m 600 pcicanII/kvpcicanII.ko /lib/modules/`uname -r`/kernel/drivers/char/
install -m 700 pcicanII/pcicanII.sh /usr/sbin/
grep -v pcicanII /etc/modules.conf >new_modules.conf
echo alias pcicanII kvpcicanII >> new_modules.conf
echo install kvpcicanII /usr/sbin/pcicanII.sh start >>new_modules.conf
echo remove kvpcicanII /usr/sbin/pcicanII.sh stop >>new_modules.conf

cat new_modules.conf > /etc/modules.conf
