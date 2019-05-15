#!/bin/sh

# Kvaser CAN driver                     
# pcicanII.sh - start/stop pcicanII and create/delete inodes  
# Copyright (C) 2003 Kvaser AB - support@kvaser.com - www.kvaser.com  

#     
# test kernel version
#     
kernel_ver=`uname -r |awk -F . '{print $2}'` 

case $kernel_ver in
   "6") kv_module_install=modprobe
        ;;
   *)   kv_module_install=insmod
        ;;
esac

#
# install
#
case "$1" in
    start)
         /sbin/$kv_module_install kvpcicanII || exit 1
   nrchan=`cat /proc/pcicanII | grep 'total channels' | awk '{print $3}'`
   major=`cat /proc/devices | grep 'pcicanII' | awk '{print $1}'`

   rm -f /dev/pcicanII*
   for (( minor=0 ; minor<$nrchan; minor++ )) ; do
       mknod /dev/pcicanII$minor c $major $minor
   done
   ;;
    stop)
         /sbin/rmmod kvpcicanII || exit 1
   rm -f /dev/pcicanII*
   ;;
    *)
         printf "Usage: %s {start|stop}\n" $0
esac

exit 0 
