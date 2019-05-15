/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

//////////////////////////////////////////////////////////////////////////////////
// FILE: memQ.c                                /
//////////////////////////////////////////////////////////////////////////////////
// memQ  --  Structs and functions for manipulating memQ             /
//////////////////////////////////////////////////////////////////////////////////

#include <asm/io.h>

#include "helios_cmds.h"
#include "memq.h"

/////////////////////////////////////////////

#define MEM_Q_TX_BUFFER       1
#define MEM_Q_RX_BUFFER       2

/////////////////////////////////////////////////////////////////////////
//help fkt for Tx/Rx- FreeSpace(...)
/////////////////////////////////////////////////////////////////////////

static int AvailableSpace(unsigned int cmdLen, unsigned int rAddr,  
                          unsigned int wAddr,  unsigned int bufStart, 
                          unsigned int bufSize) 
{
    int remaining;
  //cmdLen in bytes
    remaining = (((bufSize - wAddr) + (rAddr - bufStart)) - cmdLen);
    return remaining >= 0;
}

////////////////////////////////////////////////////////////////////////////
//returns true if there is enough room for cmd in Tx-memQ, else false
////////////////////////////////////////////////////////////////////////////
int TxAvailableSpace(PciCanIICardData *ci, unsigned int cmdLen) 
{
  unsigned long hwp, crp;
  int tmp;

  hwp = readl((void *)(ci->baseAddr + DPRAM_HOST_WRITE_PTR));  
  crp = readl((void *)(ci->baseAddr + DPRAM_M16C_READ_PTR));    
    tmp = AvailableSpace(cmdLen,
                         (unsigned long) (ci->baseAddr + crp),
                         (unsigned long) (ci->baseAddr + hwp),
                         (unsigned long) (ci->baseAddr + DPRAM_TX_BUF_START),
                         DPRAM_TX_BUF_SIZE);


    return tmp;
}


//////////////////////////////////////////////////////////////////////////

typedef struct _tmp_context {
    PciCanIICardData * ci;
    heliosCmd * cmd;
    int res;
} TMP_CONTEXT;


int QCmd(PciCanIICardData *ci, heliosCmd *cmd) 
{
    int           i;
    unsigned long p;
    unsigned long hwp, crp;
    unsigned long *tmp;
    unsigned long addr = ci->baseAddr;

    if (!TxAvailableSpace(ci, cmd->head.cmdLen))
        return MEM_Q_FULL;

    hwp = readl((void *)(addr + DPRAM_HOST_WRITE_PTR));    
    crp = readl((void *)(addr + DPRAM_M16C_READ_PTR));    

    p = addr + hwp;
    tmp = (unsigned long*) cmd;

    for (i=0; i<cmd->head.cmdLen; i += 4) {
        writel(*tmp++, (void *)p);
        p += 4;
    }

    p = hwp + cmd->head.cmdLen;
 
    if ((p + MAX_CMD_LEN) > (DPRAM_TX_BUF_START + DPRAM_TX_BUF_SIZE)) 
        p = DPRAM_TX_BUF_START;

    writel(p, (void *)(addr + DPRAM_HOST_WRITE_PTR));

    return MEM_Q_SUCCESS;
}


/////////////////////////////////////////////////////////////////////
int GetCmdFromQ(PciCanIICardData *ci, heliosCmd* cmdPtr) 
{
    unsigned long p;
    unsigned long hrp, cwp;
    unsigned long *tmp;
    int empty;
    unsigned long addr = ci->baseAddr;

    hrp = readl((void *)(addr + DPRAM_HOST_READ_PTR));    
    cwp = readl((void *)(addr + DPRAM_M16C_WRITE_PTR));    

    empty = (hrp == cwp);

    if (!empty) {
        int len;

        p = addr + hrp;
        tmp = (unsigned long*)cmdPtr;
        *tmp++ = readl((void *)p);
        len = cmdPtr->head.cmdLen - 4;
        p += 4;

        while (len > 0) {
            *tmp++ = readl((void *)p);
            len -= 4;
            p += 4;
        }

      p = hrp + cmdPtr->head.cmdLen;

      if ((p + MAX_CMD_LEN) > (DPRAM_RX_BUF_START + DPRAM_RX_BUF_SIZE)) 
          p = DPRAM_RX_BUF_START;

      writel(p, (void *)(addr + DPRAM_HOST_READ_PTR));

      if (empty) {
          printk("<1> GetCmdFromQ - // maybe in some way...qqq\n");
          //maybe in some way...qqq
      }

      return MEM_Q_SUCCESS;
    }
    else {
        //printk("<1> GetCmDFromQ - MEM_Q_EMPTY\n");
        return MEM_Q_EMPTY;
    }
}
