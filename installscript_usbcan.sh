#!/bin/sh

#install -m 600 usbcanII/usbcanII.ko /lib/modules/`uname -r`/kernel/drivers/usb/misc/
#install -m 700 usbcanII/usbcan.sh /usr/sbin/
#install -m 777 usbcanII/usbcanII /etc/hotplug/usb/
#grep -v usbcan /etc/modules.conf >new_modules.conf
#echo alias usbcan usbcanII >> new_modules.conf
#echo "#" > /etc/hotplug/usb.usermap
#echo "# Supported Kvaser USBcanII CAN interfaces" >> /etc/hotplug/usbcanII.usermap
#echo "#" >> /etc/hotplug/usbcanII.usermap
#echo "usbcanII              0x0003       0x0bfd   0x0004    0x0000       0x0000       0x00         0x00            0x00            0x00            0x00               0x00               0x0" >> /etc/hotplug/usbcanII.usermap
#echo "usbcanII              0x0003       0x0bfd   0x0002    0x0000       0x0000       0x00         0x00            0x00            0x00            0x00               0x00               0x0" >> /etc/hotplug/usbcanII.usermap
#echo "usbcanII              0x0003       0x0bfd   0x0005    0x0000       0x0000       0x00         0x00            0x00            0x00            0x00               0x00               0x0" >> /etc/hotplug/usbcanII.usermap
#echo install usbcanII /usr/sbin/usbcan.sh start >>new_modules.conf
#echo remove usbcanII /usr/sbin/usbcan.sh stop >>new_modules.conf
#cat new_modules.conf > /etc/modules.conf


echo "***************************************************"
echo "USBcanII currently not supported under kernel < 2.6"
echo "***************************************************"
