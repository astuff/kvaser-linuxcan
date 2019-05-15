#!/bin/sh

DEPMOD=`which depmod`

install -m 600 kvpcicanII.ko /lib/modules/`uname -r`/kernel/drivers/char/
install -m 700 pcicanII.sh /usr/sbin/

if [ -f /etc/modprobe.conf ] ; then
  # CentOS/Redhat/RHEL/Fedora Linux...
  CONF=/etc/modprobe.conf
else
  # Debian/Ubuntu Linux
  CONF=/etc/modprobe.d/kvaser.conf
  if [ ! -f $CONF ] ; then
    touch $CONF
  fi
fi

grep -v pcicanII $CONF                                     > newconf
echo "alias     pcicanII     kvpcicanII"                  >> newconf
echo "install   kvpcicanII   /usr/sbin/pcicanII.sh start" >> newconf
echo "remove    kvpcicanII   /usr/sbin/pcicanII.sh stop"  >> newconf

cat newconf > $CONF
rm newconf

$DEPMOD -a
if [ "$?" -ne 0 ] ; then
    echo Failed to execute $DEPMOD -a
fi
