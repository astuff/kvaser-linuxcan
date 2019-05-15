#!/bin/sh

DEPMOD=`which depmod`

install -m 600 kvpcican.ko /lib/modules/`uname -r`/kernel/drivers/char/
install -m 700 pcican.sh /usr/sbin/

echo Removing SocketCAN for Kvaser devices.
modprobe -r kvaser_pci

echo Blacklisting SocketCAN Kvaser driver to prevent it from auto-loading.

if [ -f /etc/modprobe.conf ] ; then
  # CentOS/Redhat/RHEL/Fedora Linux...
  CONF=/etc/modprobe.conf
  BLACKLIST="alias     kvaser_pci   /dev/null"
else
  # Debian/Ubuntu Linux
  CONF=/etc/modprobe.d/kvaser.conf
  BLACKLIST="blacklist kvaser_pci"
  if [ ! -f $CONF ] ; then
    touch $CONF
  fi
fi

# First, remove any old PCIcan settings.
# The space after pcican in the grep below is needed to not match pcicanII.
grep -v "pcican "       < $CONF                          > newconfx
grep -v "^${BLACKLIST}" < newconfx                       > newconf
rm newconfx

# Add PCIcan.
echo "alias     pcican       kvpcican"                  >> newconf
echo "install   kvpcican     /usr/sbin/pcican.sh start" >> newconf
echo "remove    kvpcican     /usr/sbin/pcican.sh stop"  >> newconf
# Since it conflicts with pcican, we must disable kvaser_pci (SocketCAN).
echo "${BLACKLIST}"                                     >> newconf

cat newconf > $CONF
rm newconf

$DEPMOD -a
if [ "$?" -ne 0 ] ; then
    echo Failed to execute $DEPMOD -a
fi
