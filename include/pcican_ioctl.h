#ifndef _PCICAN_IOCTL_H
#define _PCICAN_IOCTL_H

/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

/*
** Description:
** Pcican driver ioctl commands for Linux
**
*/

#include <linux/ioctl.h>

#define PCICAN_IOC_MAGIC 'p'

#define PCICAN_IOC_TEST              _IO(PCICAN_IOC_MAGIC,4)


#endif /* _PCICAN_IOCTL_H */









