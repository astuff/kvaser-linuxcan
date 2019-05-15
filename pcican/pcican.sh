#!/bin/sh

# Kvaser CAN driver                     
# pcican.sh - start/stop pcican and create/delete inodes  
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
         /sbin/$kv_module_install kvpcican || exit 1
	 nrchan=`cat /proc/pcican | grep 'total channels' | awk '{print $3}'`
	 major=`cat /proc/devices | grep 'pcican' |grep 'pcicanII' -v|awk '{print $1}'`
	 nodes=`ls /dev/ -1|grep pcican|grep pcicanII -v|awk '{print "/dev/"$1 }'`
	 rm -f $nodes

         for (( minor=0 ; minor<$nrchan; minor++ )) ; do
	     mknod /dev/pcican$minor c $major $minor
	 done
	 ;;
    stop)
         /sbin/rmmod kvpcican || exit 1
         nodes=`ls /dev/ -1|grep pcican|grep pcicanII -v|awk '{print "/dev/"$1 }'`
	 rm -f $nodes
         
	 ;;
    *)
         printf "Usage: %s {start|stop}\n" $0
esac

exit 0 