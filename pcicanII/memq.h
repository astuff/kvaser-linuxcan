/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

#ifndef _MEMQ_H_
#define _MEMQ_H_

#define MEM_Q_FULL        1
#define MEM_Q_EMPTY       2
#define MEM_Q_SUCCESS     0

#include "PciCanIIHwIf.h"

int TxAvailableSpace(PciCanIICardData *ci, unsigned int cmdLen);
int QCmd(PciCanIICardData *ci, heliosCmd *cmd);
int GetCmdFromQ(PciCanIICardData *ci, heliosCmd* cmdPtr) ;

#endif
