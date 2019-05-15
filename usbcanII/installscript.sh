#!/bin/sh

DEPMOD=`which depmod`
UDEVCTRL=`which udevcontrol`
UDEVADM=`which udevadm`

install -D -m 700 usbcanII.ko /lib/modules/`uname -r`/kernel/drivers/usb/misc/usbcanII.ko
install -m 700 usbcanII.sh /usr/sbin/
if [ -d /etc/hotplug ] ; then
  install -m 777 usbcanII/usbcanII /etc/hotplug/usb/ ;
  install -m 644 usbcanII/usbcanII.usermap /etc/hotplug/usbcanII.usermap
fi
install -m 644 ../10-kvaser.rules /etc/udev/rules.d 

if [ `udevd --version` -lt 128 ] ; then
  $UDEVCTRL reload_rules ;
else
  $UDEVADM control --reload-rules ;
fi

$DEPMOD -a
if [ "$?" -ne 0 ] ; then
  echo Failed to execute $DEPMOD -a
fi
