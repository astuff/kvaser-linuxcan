#!/bin/sh

DEPMOD=`which depmod`
UDEVCTRL=`which udevcontrol`
UDEVADM=`which udevadm`
UDEVD=`which udevd`

install -D -m 700 mhydra.ko /lib/modules/`uname -r`/kernel/drivers/usb/misc/mhydra.ko
install -m 700 mhydra.sh /usr/sbin/
install -m 644 ../10-kvaser.rules /etc/udev/rules.d

if [ -z $UDEVD ] ; then
  $UDEVADM control --reload-rules ;
else 
  if [ `udevd --version` -lt 128 ] ; then
    $UDEVCTRL reload_rules ;
  else
    $UDEVADM control --reload-rules ;
  fi
fi

$DEPMOD -a
if [ "$?" -ne 0 ] ; then
  echo Failed to execute $DEPMOD -a
fi
