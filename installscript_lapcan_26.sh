#!/bin/sh
# Is PCMCIA configured?
if ( ! grep "PCMCIA" /etc/sysconfig/pcmcia | grep [Yy] ) then
echo "Edit your pcmcia configuration file:"
echo "set PCMCIA = yes and specify your PCIC type"
echo "This line should probably read \"PCIC=i82365\""      
return
fi

/etc/rc.d/init.d/pcmcia stop

# eject card (needed for kernel 2.6 of some reason)
cardctl eject

# start/stop script
if [ ! -a /etc/pcmcia/can -o -a /etc/pcmcia/can.backup ]; then 
    rm -f /etc/pcmcia/can
    cp -f config/can /etc/pcmcia/can 
else 
    mv -f /etc/pcmcia/can /etc/pcmcia/can.backup
    cp -f config/can /etc/pcmcia/can 
fi

# Copy module
install -m 644 lapcan/lapcan_cs.ko /lib/modules/`uname -r`/kernel/drivers/pcmcia/

# insert card
cardctl insert

/sbin/ldconfig

# Edit /etc/pcmcia/config
echo device \"lapcan_cs\" class \"can\" module \"lapcan_cs\" > /etc/pcmcia/config.new
echo card \"Kvaser LAPcan\" manfid 0x01ee, 0x001d bind \"lapcan_cs\" >> /etc/pcmcia/config.new
echo card \"Kvaser LAPcan II\" manfid 0x01ee, 0x0073 bind \"lapcan_cs\" >> /etc/pcmcia/config.new

grep -v lapcan_cs /etc/pcmcia/config >> /etc/pcmcia/config.new
mv /etc/pcmcia/config.new /etc/pcmcia/config

install --m 755 config/can /etc/pcmcia/can

/etc/rc.d/init.d/pcmcia start

