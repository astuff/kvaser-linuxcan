#!/bin/sh

LIBNAME=libcanlib.so
LIBRARY=$LIBNAME.1.0.1
SONAME=$LIBNAME.1

rm -f /usr/lib/$LIBNAME
rm -f /usr/lib/$SONAME
install  -m 755 canlib/$LIBRARY /usr/lib/
ln -s $LIBRARY /usr/lib/$LIBNAME
ln -s $LIBRARY /usr/lib/$SONAME

/sbin/ldconfig
install -m 644 include/canlib.h /usr/include
install -m 644 include/canstat.h /usr/include

mkdir -p /usr/doc/canlib
cp -r doc/HTMLhelp /usr/doc/canlib

# Examples
cp -r canlib/examples /usr/doc/canlib


