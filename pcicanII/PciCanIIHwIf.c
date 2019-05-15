/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

//
// Kvaser CAN driver PCIcan hardware specific parts                    
// PCIcan functions                                                    
//

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#if LINUX_2_6
#   include <linux/workqueue.h>
#else
#   include <linux/tqueue.h>
#endif
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>

#include <asm/io.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>

// Module versioning 
#define EXPORT_SYMTAB
#include <linux/module.h>

// retrieve the CONFIG_* macros 
#include <linux/autoconf.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#   define MODVERSIONS
#endif

#ifdef MODVERSIONS
#   if LINUX_2_6
#      include <config/modversions.h>
#   else
#      include <linux/modversions.h>
#   endif
#endif

// Kvaser definitions
#include "helios_cmds.h"
#include "VCanOsIf.h"
#include "PciCanIIHwIf.h"
#include "osif_kernel.h"
#include "osif_functions_kernel.h"
#include "memq.h"
#include "hwnames.h"

//
// If you do not define PCICANII_DEBUG at all, all the debug code will be
// left out.  If you compile with PCICAN_DEBUG=0, the debug code will
// be present but disabled -- but it can then be enabled for specific
// modules at load time with a 'debug_level=#' option to insmod.
// i.e. >insmod kvpcicanII debug_level=#
//

#ifdef PCICANII_DEBUG
static int debug_level = PCICANII_DEBUG;
MODULE_PARM(debug_level, "i");
#   define DEBUGPRINT(n, args...) if (debug_level>=(n)) printk("<" #n ">" args)
#else
#   define DEBUGPRINT(n, args...)
#endif

// Bits in the CxSTRH register in the M16C.
#define M16C_BUS_RESET    0x01    // Chip is in Reset state
#define M16C_BUS_ERROR    0x10    // Chip has seen a bus error
#define M16C_BUS_PASSIVE  0x20    // Chip is error passive
#define M16C_BUS_OFF      0x40    // Chip is bus off


//======================================================================
// HW function pointers                                                 
//======================================================================
#if LINUX_2_6
VCanHWInterface hwIf = {
    .initAllDevices   =  pciCanInitAllDevices,    
    .setBusParams     =  pciCanSetBusParams,     
    .getBusParams     =  pciCanGetBusParams,     
    .setOutputMode    =  pciCanSetOutputMode,    
    .setTranceiverMode=  pciCanSetTranceiverMode,
    .busOn            =  pciCanBusOn,            
    .busOff           =  pciCanBusOff,           
    .txAvailable      =  pciCanInSync,      
    .transmitMessage  =  pciCanTransmitMessage,  
    .procRead         =  pciCanProcRead,         
    .closeAllDevices  =  pciCanCloseAllDevices,  
    .getTime          =  pciCanTime,
    .flushSendBuffer  =  pciCanFlushSendBuffer, 
    .getTxErr         =  pciCanGetTxErr,
    .getRxErr         =  pciCanGetRxErr,
    .rxQLen           =  pciCanRxQLen,
    .txQLen           =  pciCanTxQLen,
    .requestChipState =  pciCanRequestChipState,
    .requestSend      =  pciCanRequestSend
};
#elif LINUX
VCanHWInterface hwIf = {
     initAllDevices:     pciCanInitAllDevices,    
     setBusParams:       pciCanSetBusParams,     
     getBusParams:       pciCanGetBusParams,     
     setOutputMode:      pciCanSetOutputMode,    
     setTranceiverMode:  pciCanSetTranceiverMode,
     busOn:              pciCanBusOn,            
     busOff:             pciCanBusOff,           
     txAvailable:        pciCanInSync,      
     transmitMessage:    pciCanTransmitMessage,  
     procRead:           pciCanProcRead,         
     closeAllDevices:    pciCanCloseAllDevices,  
     getTime:            pciCanTime,
     flushSendBuffer:    pciCanFlushSendBuffer, 
     getTxErr:           pciCanGetTxErr,
     getRxErr:           pciCanGetRxErr,
     rxQLen:             pciCanRxQLen,
     txQLen:             pciCanTxQLen,
     requestChipState:   pciCanRequestChipState,
     requestSend:        pciCanRequestSend
};
#endif

const char *device_name = DEVICE_NAME_STRING;

static int           pciCanTxAvailable (VCanChanData *vChd);
static unsigned long pciCanTimeStamp(VCanCardData *vCard, unsigned long timeLo);


//======================================================================
// Find out length of helios command                                    
//======================================================================
OS_IF_INLINE int getCmdLen (heliosCmd *cmd)
{
    return cmd->head.cmdLen;
}

//======================================================================
// Copy a helios command                                                
//======================================================================
OS_IF_INLINE void copyCmd (heliosCmd *cmd_to, heliosCmd *cmd_from)
{
    int cmdLen;
    unsigned char *from, *to;

    from = (unsigned char*) cmd_from;
    to = (unsigned char*) cmd_to;

    for (cmdLen = getCmdLen(cmd_from); cmdLen > 0; cmdLen--) {
        *to++ = *from++;
    }
    return;
}

//======================================================================
//    getTransId                                                        
//======================================================================
OS_IF_INLINE int getTransId (heliosCmd *cmd)
{
    if (cmd->head.cmdNo > CMD_TX_EXT_MESSAGE) {

        // any of the commands
        return cmd->getBusparamsReq.transId;
    }
    else {
        DEBUGPRINT(1, "WARNING wont give a correct transid\n");
        return 0;   
    }
}

//======================================================================
// /proc read function                                                  
//======================================================================
int pciCanProcRead (char *buf, char **start, off_t offset,
                    int count, int *eof, void *data)
{
    int len = 0;
    len += sprintf(buf+len,"\ntotal channels %d\n", driverData.minorNr);
    *eof = 1;
    return len;
}


//======================================================================
//  All acks received?                                                 
//======================================================================
int pciCanInSync (VCanChanData *vChd)
{
    PciCanIIChanData *hChd = vChd->hwChanData;
    return (atomic_read(&hChd->outstanding_tx) == 0);
} // pciCanInSync 

//======================================================================
//  Can we send now?                                                    
//======================================================================
static int pciCanTxAvailable (VCanChanData *vChd)
{
    PciCanIIChanData *hChd = vChd->hwChanData;
    return (atomic_read(&hChd->outstanding_tx) < HELIOS_MAX_OUTSTANDING_TX);

} // pciCanInSync 

//======================================================================
// Find out some info about the H/W                                                    
//======================================================================
int pciCanProbe (VCanCardData *vCd)
{
    PciCanIICardData *hCd = vCd->hwCardData;
    int chan, i;

    if(hCd->baseAddr == 0) {
        DEBUGPRINT(1,"<1> pcicanProbe card_present = 0\n");
        vCd->cardPresent = 0;
        return -1;
    }
   
    chan = 0;
    for (i = 0; i < MAX_CHANNELS; i++) {
        VCanChanData *vChd = vCd->chanData[i];

        if(vChd != NULL) {
            vChd->channel = i;
            chan++;
        }
    }

    if (chan == 0) {
        vCd->cardPresent = 0;
        return -1;
    }
    vCd->nrChannels = chan;
    vCd->cardPresent = 1;

    return 0;
} // pciCanProbe 

//======================================================================
// Enable bus error interrupts, and reset the                           
// counters which keep track of the error rate                          
//======================================================================
#if 0
static void pciCanResetErrorCounter (VCanChanData *vChd)
{
    PciCanIICardData *hCard = vChd->vCard->hwCardData;
    heliosCmd cmd;

    if(!vChd->vCard->cardPresent) return;

    cmd.head.cmdNo                = CMD_RESET_ERROR_COUNTER;
    cmd.resetErrorCounter.cmdLen  = sizeof(cmdResetErrorCounter);
    cmd.resetErrorCounter.channel = (unsigned char)vChd->channel;

    os_if_irq_disable(&vCard->memQLock);
    if (QCmd(hCard, &cmd) != MEM_Q_SUCCESS) {
        os_if_irq_enable(&vCard->memQLock);
        DEBUGPRINT(1,"ERROR----- pciCanResetErrorCounter----------\n");
    }
    os_if_irq_enable(&vCard->memQLock);

    vChd->errorCount = 0;
    vChd->errorTime = hwIf.getTime(vChd->vCard); // 10us?
} // pciCanResetErrorCounter 
#endif

//======================================================================
//  Set bit timing                                                      
//======================================================================
int pciCanSetBusParams (VCanChanData *vChd, VCanBusParams *par)
{
    
    PciCanIICardData *hCard = vChd->vCard->hwCardData;
    PciCanIIChanData *hChd  = vChd->hwChanData;
    VCanCardData     *vCard = vChd->vCard;
    heliosCmd cmd;
    unsigned long tmp;

    cmd.setBusparamsReq.cmdNo   = CMD_SET_BUSPARAMS_REQ;
    cmd.setBusparamsReq.cmdLen  = sizeof(cmdSetBusparamsReq);
    cmd.setBusparamsReq.bitRate = (unsigned long) par->freq;
    cmd.setBusparamsReq.sjw     = (unsigned char) par->sjw;
    cmd.setBusparamsReq.tseg1   = (unsigned char) par->tseg1;
    cmd.setBusparamsReq.tseg2   = (unsigned char) par->tseg2;
    cmd.setBusparamsReq.channel = (unsigned char)vChd->channel;
    cmd.setBusparamsReq.transId = (unsigned char)vChd->channel;
    cmd.setBusparamsReq.noSamp  = 1; // always 1

    // Check bus parameters
    tmp = par->freq * (par->tseg1 + par->tseg2 + 1);
    if (tmp == 0) {
        return -1;
    }
    if ((8000000/tmp) > 16) {
        return -1;
    }
    
    // store locally since getBusParams not correct
    hChd->freq    = (unsigned long) par->freq;
    hChd->sjw     = (unsigned char) par->sjw;
    hChd->tseg1   = (unsigned char) par->tseg1;
    hChd->tseg2   = (unsigned char) par->tseg2;
    hChd->samples = 1; // always 1

   // DEBUGPRINT(1, "pciCanSetBusParams: chan = %d,
      //freq = %d, sjw = %d, tseg1 = %d, tseg2 = %d, samples = %d (always 1)\n"
      //,vChd->channel, par->freq, par->sjw, par->tseg1, par->tseg2, 1);

    os_if_irq_disable(&vCard->memQLock);
    if (QCmd(hCard, &cmd) != MEM_Q_SUCCESS) {
        DEBUGPRINT(1,"ERROR----- pciCanSetBusParams----------\n");
        os_if_irq_enable(&vCard->memQLock);
        return -1;
    }
    os_if_irq_enable(&vCard->memQLock);

    return 0;
} // pciCanSetBusParams 


//======================================================================
//  Get bit timing                                                      
//======================================================================
int pciCanGetBusParams (VCanChanData *vChd, VCanBusParams *par)
{
    // do not send cmdGetBusparamsReq command
    // possible bug in firmware
    
    PciCanIIChanData *hChd  = vChd->hwChanData;
    par->sjw     = hChd->sjw;
    par->samp3   = hChd->samples; // always 1
    par->tseg1   = hChd->tseg1;
    par->tseg2   = hChd->tseg2;
    par->freq    = hChd->freq;

    return 0;
} // pciCanGetBusParams 


//======================================================================
//  Set silent or normal mode                                           
//======================================================================
int pciCanSetOutputMode (VCanChanData *vChd, int silent)
{
    PciCanIICardData *hCard = vChd->vCard->hwCardData;
    VCanCardData     *vCard = vChd->vCard;
    heliosCmd cmd;

    cmd.setDrivermodeReq.cmdNo    = CMD_SET_DRIVERMODE_REQ;
    cmd.setDrivermodeReq.cmdLen   = sizeof(cmdSetDrivermodeReq);
    cmd.setDrivermodeReq.channel  = (unsigned char)vChd->channel;
    cmd.setDrivermodeReq.driverMode = silent? DRIVERMODE_SILENT : DRIVERMODE_NORMAL;

    os_if_irq_disable(&vCard->memQLock);
    if (QCmd(hCard, &cmd) != MEM_Q_SUCCESS) {
        DEBUGPRINT(1,"ERROR----- pciCanSetOutputMode----------\n");
        os_if_irq_enable(&vCard->memQLock);
        return -1;
    }
    os_if_irq_enable(&vCard->memQLock);

    return 0;
} // pciCanSetOutputMode 


//======================================================================
//  Line mode                                                           
//======================================================================
int pciCanSetTranceiverMode (VCanChanData *vChd, int linemode, int resnet)
{
    vChd->lineMode = linemode;
    // qqq
    return 0;
} // pciCanSetTranceiverMode 


//======================================================================
//  Query chip status                                                   
//======================================================================
int pciCanRequestChipState (VCanChanData *vChd)
{
    heliosCmd cmd;
    heliosCmd resp;
    int ret;

    cmd.head.cmdNo              = CMD_GET_CHIP_STATE_REQ;
    cmd.getChipStateReq.cmdLen  = sizeof(cmdGetChipStateReq);
    cmd.getChipStateReq.channel = (unsigned char)vChd->channel;
    cmd.getChipStateReq.transId = (unsigned char)vChd->channel;

    ret = pciCanWaitResponse(vChd->vCard, (heliosCmd*)&cmd,
    (heliosCmd*)&resp, CMD_CHIP_STATE_EVENT, cmd.getChipStateReq.transId);
    if (ret) return ret;

    return 0;
} // pciCanRequestChipState 


//======================================================================
//  Go bus on                                                           
//======================================================================
int pciCanBusOn (VCanChanData *vChd)
{
    heliosCmd cmd;
    heliosCmd resp;
    int ret = 0;
    PciCanIIChanData *hChd = vChd->hwChanData;

    cmd.head.cmdNo            = CMD_START_CHIP_REQ;
    cmd.startChipReq.cmdLen   = sizeof(cmdStartChipReq);
    cmd.startChipReq.channel  = (unsigned char)vChd->channel;
    cmd.startChipReq.transId  = (unsigned char)vChd->channel;
  
    ret = pciCanWaitResponse(vChd->vCard, (heliosCmd*)&cmd, (heliosCmd*)&resp, CMD_START_CHIP_RESP, cmd.startChipReq.transId);
    if (ret) return ret;

    memset (hChd->current_tx_message, 0, sizeof (hChd->current_tx_message));
    atomic_set(&hChd->outstanding_tx, 0);
    atomic_set(&vChd->transId, 1);
    vChd->overrun = 0;         // qqq overrun not used
    vChd->isOnBus = 1;
    
    pciCanRequestChipState(vChd);
    
    return 0;
} // pciCanBusOn 


//======================================================================
//  Go bus off                                                          
//======================================================================
int pciCanBusOff (VCanChanData *vChd)
{  
    PciCanIIChanData *hChd  = vChd->hwChanData;
    heliosCmd cmd;
    heliosCmd resp;
    int ret;

    cmd.head.cmdNo          = CMD_STOP_CHIP_REQ;
    cmd.stopChipReq.cmdLen  = sizeof(cmdStopChipReq);
    cmd.stopChipReq.channel = (unsigned char)vChd->channel;
    cmd.stopChipReq.transId = (unsigned char)vChd->channel;
    
    ret = pciCanWaitResponse(vChd->vCard, (heliosCmd*)&cmd, (heliosCmd*)&resp, CMD_STOP_CHIP_RESP, cmd.stopChipReq.transId);
    if (ret) return ret;
    
    memset(hChd->current_tx_message, 0, sizeof (hChd->current_tx_message));
    atomic_set(&hChd->outstanding_tx, 0);
    atomic_set(&vChd->transId, 1);

    vChd->isOnBus = 0;
    vChd->chipState.state = CHIPSTAT_BUSOFF;
    vChd->overrun = 0;         // qqq overrun not used

    pciCanRequestChipState(vChd);
    
    return 0;
} // pciCanBusOff 


//======================================================================
//  Reset card                                                      
//======================================================================
int pciCanResetCard (VCanCardData *vCard)
{
    PciCanIICardData *hCard = vCard->hwCardData;
    heliosCmd         cmd;
    unsigned long     tmp;
    unsigned long     addr;

    DEBUGPRINT(1, "pciCanResetCard\n");

    if(!vCard->cardPresent) return -1;

    addr = hCard->baseAddr;

    // Disable interrupts from card
    tmp = readl((void *)(addr + DPRAM_INTERRUPT_REG));
    tmp |= DPRAM_INTERRUPT_DISABLE;
    writel(tmp, (void *)(addr + DPRAM_INTERRUPT_REG));
    
    cmd.head.cmdNo          = CMD_RESET_CARD_REQ;
    cmd.resetCardReq.cmdLen = sizeof(cmd.resetCardReq);

    os_if_irq_disable(&vCard->memQLock);
    if (QCmd(hCard, &cmd) != MEM_Q_SUCCESS) {
        DEBUGPRINT(1,"<1> Error: In QCmd\n");
        os_if_irq_enable(&vCard->memQLock);
        return -1;
    }
    os_if_irq_enable(&vCard->memQLock);
    
    return 0;
} // pciCanResetCard

//======================================================================
// Get time
//======================================================================
unsigned long pciCanTime(VCanCardData *vCard)
{
    PciCanIICardData *hCard = vCard->hwCardData;
    heliosCmd cmd;
    heliosCmd resp;
    int ret;
    
    memset(&cmd, 0, sizeof(cmd));
    cmd.readClockReq.cmdNo      = CMD_READ_CLOCK_REQ;
    cmd.readClockReq.cmdLen     = sizeof(cmdReadClockReq);
    cmd.readClockReq.flags      = 0;
    cmd.readClockReq.transId    = 0;
    
    ret = pciCanWaitResponse(vCard, (heliosCmd*)&cmd, (heliosCmd*)&resp, CMD_READ_CLOCK_RESP, cmd.readClockReq.transId);
    if (ret) return ret;

    return hCard->recClock/PCICANII_TICKS_PER_MS;
}

//======================================================================
// get timestamp
//======================================================================
static unsigned long pciCanTimeStamp(VCanCardData *vCard, unsigned long timeLo)
{
    unsigned long    ret;
    PciCanIICardData *hCd = vCard->hwCardData;
    
    os_if_irq_disable(&hCd->timeHi_lock);
    ret = (vCard->timeHi + timeLo)/PCICANII_TICKS_PER_MS;
    os_if_irq_enable(&hCd->timeHi_lock);
    
    return ret;
}

//======================================================================
//  Interrupt handling functions                                        
//======================================================================
static void pciCanReceiveIsr (VCanCardData *vCard)
{
    VCAN_EVENT e;
    PciCanIICardData  *hCd     = vCard->hwCardData;
    unsigned int      loopMax  = 1000;
    heliosCmd         cmd;
        
    // Reading clears interrupt flag

    os_if_irq_disable(&vCard->memQLock);
    while(GetCmdFromQ(hCd, &cmd) == MEM_Q_SUCCESS) {
    os_if_irq_enable(&vCard->memQLock);
        // A loop counter as a safetly measure.
        if (--loopMax == 0) {
          DEBUGPRINT(1, "pciCanReceiverIsr: Loop counter as a safetly measure!!!\n");
          return;
        }

        switch (cmd.head.cmdNo) {

            case CMD_RX_STD_MESSAGE:
            {
                char dlc;
                unsigned char flags;
                unsigned int chan = cmd.rxCanMessage.channel;


                if (chan < (unsigned)vCard->nrChannels) {
                    VCanChanData *vChd = vCard->chanData[cmd.rxCanMessage.channel];
                    e.tag               = V_RECEIVE_MSG;
                    e.transId           = 0;
                    e.timeStamp         = pciCanTimeStamp(vCard, cmd.rxCanMessage.time);
                    e.tagData.msg.id    =  (cmd.rxCanMessage.rawMessage[0] & 0x1F) << 6;
                    e.tagData.msg.id    += (cmd.rxCanMessage.rawMessage[1] & 0x3F);
                    flags = cmd.rxCanMessage.flags;
                    e.tagData.msg.flags = 0;
                    if (flags & MSGFLAG_OVERRUN) 
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_OVERRUN;
                    if (flags & MSGFLAG_REMOTE_FRAME) 
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_REMOTE_FRAME;
                    if (flags & MSGFLAG_ERROR_FRAME) 
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_ERROR_FRAME;
                    if (flags & MSGFLAG_TX)
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_TXACK;
                    if (flags & MSGFLAG_TXRQ)
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_TXRQ;
                    
                    dlc = cmd.rxCanMessage.rawMessage[5] & 0x0F;
                    e.tagData.msg.dlc = dlc;
                    memcpy(e.tagData.msg.data, &cmd.rxCanMessage.rawMessage[6], 8);

                    vCanDispatchEvent(vChd, &e);
                } else {
                  DEBUGPRINT(1, "QQQ: CMD_RX_STD_MESSAGE, dlc = %d flags = %x, chan = %d\n",
                      cmd.rxCanMessage.rawMessage[5] & 0x0F,
                      cmd.rxCanMessage.flags,chan);
                }
                break;
            }

            case CMD_RX_EXT_MESSAGE:
            {
                char dlc;
                unsigned char flags;
                unsigned int chan = cmd.rxCanMessage.channel;

                if (chan < (unsigned)vCard->nrChannels) {
                    VCanChanData *vChd  = vCard->chanData[cmd.rxCanMessage.channel];
                    e.tag               = V_RECEIVE_MSG;
                    e.transId           = 0;
                    e.timeStamp         = pciCanTimeStamp(vCard, cmd.rxCanMessage.time);
                    e.tagData.msg.id    =  (cmd.rxCanMessage.rawMessage[0] & 0x1F) << 24;
                    e.tagData.msg.id    += (cmd.rxCanMessage.rawMessage[1] & 0x3F) << 18;
                    e.tagData.msg.id    += (cmd.rxCanMessage.rawMessage[2] & 0x0F) << 14;
                    e.tagData.msg.id    += (cmd.rxCanMessage.rawMessage[3] & 0xFF) <<  6;
                    e.tagData.msg.id    += (cmd.rxCanMessage.rawMessage[4] & 0x3F);
                    e.tagData.msg.id    += EXT_MSG;
                    flags = cmd.rxCanMessage.flags;
                    e.tagData.msg.flags = 0;
                    if (flags & MSGFLAG_OVERRUN) 
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_OVERRUN;
                    if (flags & MSGFLAG_REMOTE_FRAME) 
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_REMOTE_FRAME;
                    if (flags & MSGFLAG_ERROR_FRAME) 
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_ERROR_FRAME;
                    if (flags & MSGFLAG_TX)
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_TXACK;
                    if (flags & MSGFLAG_TXRQ)
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_TXRQ;
                    dlc = cmd.rxCanMessage.rawMessage[5] & 0x0F;
                    e.tagData.msg.dlc = dlc;
                    memcpy(e.tagData.msg.data, &cmd.rxCanMessage.rawMessage[6], 8);

                    vCanDispatchEvent(vChd, &e);
                } 
                else 
                {
                  DEBUGPRINT(1, "QQQ: CMD_RX_EXT_MESSAGE, dlc = %d flags = %x, chan = %d\n",
                      cmd.rxCanMessage.rawMessage[5] & 0x0F,
                      cmd.rxCanMessage.flags,chan);
                }
                break;
            }

            case CMD_TX_ACKNOWLEDGE:
            {
                // A TxAck - handle it the manual way, just as in the old PCIcan.
                // Send a tx ack. qqq review this
                     
                unsigned int transId;
                unsigned int chan = cmd.txAck.channel;
                /*
                DEBUGPRINT(1, 
                           "CMD_TX_ACKNOWLEDGE: chan(%d), time(%d), transId(%x)\n",
                           chan,
                           cmd.txAck.time,
                           cmd.txAck.transId
                          );
                */

                if (chan < (unsigned)vCard->nrChannels) {

                    VCanChanData     *vChd = vCard->chanData[cmd.txAck.channel];
                    PciCanIIChanData *hChd = vChd->hwChanData;


                    /*DEBUGPRINT(1, 
                               "ACK: ch%d, tId(%x) o:%d\n",
                               chan,
                               cmd.txAck.transId,
                               atomic_read(&hChd->outstanding_tx)
                              );*/
                    
                    transId = cmd.txAck.transId;
                    if ((transId == 0) || (transId > HELIOS_MAX_OUTSTANDING_TX)) {
                        DEBUGPRINT(1,"CMD_TX_ACKNOWLEDGE chan %d ERROR transid %d\n", chan, transId);
                        break;
                    }

                    if (atomic_read(&hChd->outstanding_tx) > 0) {

                        atomic_sub(1, &hChd->outstanding_tx);

                        // wake up those who are waiting for all
                        // sending to finish

                        // is anyone waiting for this ack?
                        if((atomic_read(&hChd->outstanding_tx) == 0) && atomic_read(&vChd->waitEmpty)) {
                            if(getQLen(atomic_read(&vChd->txChanBufHead), atomic_read(&vChd->txChanBufTail), TX_CHAN_BUF_SIZE) == 0)
                            {
                                atomic_set(&vChd->waitEmpty, 0);
                                os_if_wake_up_interruptible(&vChd->flushQ);
                            }
                        }
                        
                        if(getQLen(atomic_read(&vChd->txChanBufHead), atomic_read(&vChd->txChanBufTail), TX_CHAN_BUF_SIZE) != 0)
                        {
                            pciCanRequestSend(vCard, vChd);
                        } 
                    }
                    else
                    {
                      DEBUGPRINT(1, "chan >= vCard->nrChannels\n");
                    }
                    
                    if (hChd->current_tx_message[transId - 1].flags & VCAN_MSG_FLAG_TXACK) {
                        VCAN_EVENT *e = (VCAN_EVENT*) &hChd->current_tx_message[transId - 1];
                        e->tag = V_RECEIVE_MSG;
                        e->timeStamp = pciCanTimeStamp(vCard, cmd.txAck.time);
                        e->tagData.msg.flags &= ~VCAN_MSG_FLAG_TXRQ;

                        vCanDispatchEvent(vChd, e);
                    }
                }
                else {
                  DEBUGPRINT(1, "QQQ: CMD_TX_ACKNOWLEDGE, chan = %d\n", chan);
                }
                break;
            }

            case CMD_TX_REQUEST:
            {
                unsigned int transId;
                unsigned int chan      = cmd.txRequest.channel;
                VCanChanData     *vChd = vCard->chanData[cmd.txRequest.channel];
                PciCanIIChanData *hChd = vChd->hwChanData;
                DEBUGPRINT(1, "CMD_TX_REQUEST, chan = %d, cmd.txRequest.transId = %d ",chan, cmd.txRequest.transId);
                if (chan < (unsigned)vCard->nrChannels) {
                    // A TxReq. Take the current tx message, modify it to a
                    // receive message and send it back.
                    transId = cmd.txRequest.transId;
                    if ((transId == 0) || (transId > HELIOS_MAX_OUTSTANDING_TX)) {
                        DEBUGPRINT(1,"CMD_TX_REQUEST chan %d ERROR transid to high %d\n", chan, transId);
                        break;
                    }

                    if (hChd->current_tx_message[transId - 1].flags & VCAN_MSG_FLAG_TXRQ)
                    {
                        VCAN_EVENT *e          = (VCAN_EVENT*) &hChd->current_tx_message[transId - 1];
                        VCAN_EVENT tmp         = *e;
                        tmp.tag                =  V_RECEIVE_MSG;
                        tmp.timeStamp          = pciCanTimeStamp(vCard, cmd.txRequest.time);
                        tmp.tagData.msg.flags &=  ~VCAN_MSG_FLAG_TXACK;

                        vCanDispatchEvent(vChd, e);
                    }
                }                
                break;
            }

            case CMD_GET_BUSPARAMS_RESP:
            {
                DEBUGPRINT(1,"CMD_GET_BUSPARAMS_RESP\n");
                // qqq not used since bug in firmware
                break;
            }

            case CMD_GET_DRIVERMODE_RESP:
                DEBUGPRINT(1,"CMD_GET_DRIVERMODE_RESP\n");
                break;

            case CMD_START_CHIP_RESP:
                DEBUGPRINT(1,"CMD_START_CHIP_RESP chan %d\n", cmd.startChipResp.channel);

                break;

            case CMD_STOP_CHIP_RESP:
                DEBUGPRINT(1,"CMD_STOP_CHIP_RESP ch %d\n", cmd.stopChipResp.channel);
                break;

            case CMD_CHIP_STATE_EVENT:
            {                    
                unsigned int chan  = cmd.chipStateEvent.channel;
                VCanChanData *vChd = vCard->chanData[chan];

                if (chan < (unsigned)vCard->nrChannels) {
                    vChd->txErrorCounter = cmd.chipStateEvent.txErrorCounter;
                    vChd->rxErrorCounter = cmd.chipStateEvent.rxErrorCounter;
                }

                // ".busStatus" is the contents of the CnSTRH register.
                switch (cmd.chipStateEvent.busStatus & (M16C_BUS_PASSIVE|M16C_BUS_OFF)) {
                    case 0:
                        vChd->chipState.state = CHIPSTAT_ERROR_ACTIVE;
                        break;
                    case M16C_BUS_PASSIVE:
                        vChd->chipState.state = CHIPSTAT_ERROR_PASSIVE|CHIPSTAT_ERROR_WARNING;
                        break;
                    case M16C_BUS_OFF:
                        vChd->chipState.state = CHIPSTAT_BUSOFF;
                        break;
                    case (M16C_BUS_PASSIVE|M16C_BUS_OFF):
                        vChd->chipState.state = CHIPSTAT_BUSOFF|CHIPSTAT_ERROR_PASSIVE|CHIPSTAT_ERROR_WARNING;
                        break;
                }
                // Reset is treated like bus-off
                if (cmd.chipStateEvent.busStatus & M16C_BUS_RESET) {
                    vChd->chipState.state = CHIPSTAT_BUSOFF;
                    vChd->txErrorCounter = 0;
                    vChd->rxErrorCounter = 0;
                }
                
                e.tag                =  V_CHIP_STATE;
                e.timeStamp          = pciCanTimeStamp(vCard, cmd.chipStateEvent.time);
                e.transId            = 0;
                e.tagData.chipState.busStatus      = (unsigned char) vChd->chipState.state;
                e.tagData.chipState.txErrorCounter = (unsigned char) vChd->txErrorCounter;
                e.tagData.chipState.rxErrorCounter = (unsigned char) vChd->rxErrorCounter;
                vCanDispatchEvent(vChd, &e);
                
                break;
            }

            case CMD_CLOCK_OVERFLOW_EVENT:
                os_if_irq_disable(&hCd->timeHi_lock);
                vCard->timeHi = cmd.clockOverflowEvent.currentTime & 0xFFFF0000;
                os_if_irq_enable(&hCd->timeHi_lock);
                break;

            case CMD_READ_CLOCK_RESP:
            {
                DEBUGPRINT(1, "CMD_READ_CLOCK_RESP\n");
                hCd->recClock = (cmd.readClockResp.time[1] << 16) + cmd.readClockResp.time[0];
                os_if_irq_disable(&hCd->timeHi_lock);
                vCard->timeHi = cmd.readClockResp.time[1] << 16;
                os_if_irq_enable(&hCd->timeHi_lock);
                break;
            }

            case CMD_GET_CARD_INFO_RESP:
            {
                unsigned int chan;
                chan = cmd.getCardInfoResp.channelCount;
                DEBUGPRINT(1, "CMD_GET_CARD_INFO_RESP chan = %d\n",chan);
                if (!hCd->initDone) {
                    vCard->nrChannels = chan;
                }
                memcpy(hCd->ean_code, &cmd.getCardInfoResp.EAN[0], 8);
                vCard->serialNumber = cmd.getCardInfoResp.serialNumberLow; 
                hCd->hardware_revision_major = cmd.getCardInfoResp.hwRevision >> 4;
                hCd->hardware_revision_minor = cmd.getCardInfoResp.hwRevision & 0x0F;

                if(hCd->isWaiting) {
                    os_if_wake_up_interruptible(&hCd->waitHwInfo);
                }
                hCd->receivedHwInfo = 1;
                break;
            }

            case CMD_GET_SOFTWARE_INFO_RESP:
            {
                vCard->firmwareVersionMajor = cmd.getSoftwareInfoResp.applicationVersion >> 24;
                vCard->firmwareVersionMinor = (cmd.getSoftwareInfoResp.applicationVersion >> 16) & 0xFF;
                vCard->firmwareVersionBuild = (cmd.getSoftwareInfoResp.applicationVersion & 0xFFFF);

                if(hCd->isWaiting) {
                    os_if_wake_up_interruptible(&hCd->waitSwInfo);
                }
                hCd->receivedSwInfo = 1;

                DEBUGPRINT(1,"PCIcanII firmware version %d.%d.%d\n", (int)cmd.getSoftwareInfoResp.applicationVersion >> 24,
                           (int)(cmd.getSoftwareInfoResp.applicationVersion >> 16) & 0xFF, (int)cmd.getSoftwareInfoResp.applicationVersion & 0xFFFF);
                
                break;
            }

            // qqq not done
            case CMD_GET_TRANSCEIVER_INFO_RESP:
            {  
                unsigned int chan = cmd.getTransceiverInfoResp.channel;
                VCanChanData *vChd = vCard->chanData[chan];
                DEBUGPRINT(1, "CMD_GET_TRANSCEIVER_INFO_RESP chan = %d\n",chan);
                if (chan < (unsigned)vCard->nrChannels) {
                    vChd = vCard->chanData[chan];
                    vChd->transType = cmd.getTransceiverInfoResp.transceiverType;
                }
                // wake up
                break;
            }

            case CMD_CAN_ERROR_EVENT:
            {
                int errorCounterChanged;
                // first channel
                VCanChanData *vChd = vCard->chanData[0];
                   
                // Known problem: if the error counters of both channels
                // are max then there is no way of knowing which channel got an errorframe

                // It's an error frame if any of our error counters has
                // increased..
                errorCounterChanged =  (cmd.canErrorEvent.txErrorCounterCh0 > vChd->txErrorCounter);
                errorCounterChanged |= (cmd.canErrorEvent.rxErrorCounterCh0 > vChd->rxErrorCounter);
                
                // It's also an error frame if we have seen a bus error while
                // the other channel hasn't seen any bus errors at all.
                errorCounterChanged |= ((cmd.canErrorEvent.busStatusCh0 & M16C_BUS_ERROR) &&
                                        !(cmd.canErrorEvent.busStatusCh1 & M16C_BUS_ERROR));

                vChd->txErrorCounter = cmd.canErrorEvent.txErrorCounterCh0;
                vChd->rxErrorCounter = cmd.canErrorEvent.rxErrorCounterCh0;

                switch (cmd.canErrorEvent.busStatusCh0 & (M16C_BUS_PASSIVE|M16C_BUS_OFF)) {
                    case 0:
                        vChd->chipState.state = CHIPSTAT_ERROR_ACTIVE;
                        break;

                    case M16C_BUS_PASSIVE:
                        vChd->chipState.state = CHIPSTAT_ERROR_PASSIVE|CHIPSTAT_ERROR_WARNING;
                        break;

                    case M16C_BUS_OFF:
                        vChd->chipState.state = CHIPSTAT_BUSOFF;
                        errorCounterChanged = 0;
                        break;

                    case (M16C_BUS_PASSIVE|M16C_BUS_OFF):
                        vChd->chipState.state = CHIPSTAT_BUSOFF|CHIPSTAT_ERROR_PASSIVE|CHIPSTAT_ERROR_WARNING;
                        errorCounterChanged = 0;
                        break;

                    default:
                        break;
                }

                // Reset is treated like bus-off
                if (cmd.canErrorEvent.busStatusCh0 & M16C_BUS_RESET) {
                    vChd->chipState.state = CHIPSTAT_BUSOFF;
                    vChd->txErrorCounter = 0;
                    vChd->rxErrorCounter = 0;
                    errorCounterChanged = 0;
                }

                e.tag = V_CHIP_STATE;
                e.timeStamp = pciCanTimeStamp(vCard, cmd.canErrorEvent.time);
                e.transId                           = 0;
                e.tagData.chipState.busStatus       = vChd->chipState.state; // qqq really chipState?
                e.tagData.chipState.txErrorCounter  = vChd->txErrorCounter;
                e.tagData.chipState.rxErrorCounter  = vChd->rxErrorCounter;
                vCanDispatchEvent(vChd, &e);

                if (errorCounterChanged) {
                  e.tag               = V_RECEIVE_MSG;
                  e.transId           = 0;
                  e.timeStamp         = pciCanTimeStamp(vCard, cmd.canErrorEvent.time);
                  e.tagData.msg.id    = 0;
                  e.tagData.msg.flags = VCAN_MSG_FLAG_ERROR_FRAME;
                  e.tagData.msg.dlc   = 0;
                  vCanDispatchEvent(vChd, &e);
                }

                // next channel
                if ((unsigned)vCard->nrChannels > 0) {

                    VCanChanData *vChd = vCard->chanData[1];

                    // It's an error frame if any of our error counters has
                    // increased..
                    errorCounterChanged =  (cmd.canErrorEvent.txErrorCounterCh1 > vChd->txErrorCounter);
                    errorCounterChanged |= (cmd.canErrorEvent.rxErrorCounterCh1 > vChd->rxErrorCounter);

                    // It's also an error frame if we have seen a bus error while
                    // the other channel hasn't seen any bus errors at all.
                    errorCounterChanged |= ((cmd.canErrorEvent.busStatusCh1 & M16C_BUS_ERROR) &&
                                            !(cmd.canErrorEvent.busStatusCh0 & M16C_BUS_ERROR));

                    vChd->txErrorCounter = cmd.canErrorEvent.txErrorCounterCh1;
                    vChd->rxErrorCounter = cmd.canErrorEvent.rxErrorCounterCh1;

                    switch (cmd.canErrorEvent.busStatusCh1 & (M16C_BUS_PASSIVE|M16C_BUS_OFF)) {
                        case 0:
                            vChd->chipState.state = CHIPSTAT_ERROR_ACTIVE;
                            break;

                        case M16C_BUS_PASSIVE:
                            vChd->chipState.state = CHIPSTAT_ERROR_PASSIVE|CHIPSTAT_ERROR_WARNING;
                            break;

                        case M16C_BUS_OFF:
                            vChd->chipState.state = CHIPSTAT_BUSOFF;
                            errorCounterChanged = 0;
                            break;

                        case (M16C_BUS_PASSIVE|M16C_BUS_OFF):
                            vChd->chipState.state = CHIPSTAT_BUSOFF|CHIPSTAT_ERROR_PASSIVE|CHIPSTAT_ERROR_WARNING;
                            errorCounterChanged = 0;
                            break;

                        default:
                            break;
                    }

                    // Reset is treated like bus-off
                    if (cmd.canErrorEvent.busStatusCh1 & M16C_BUS_RESET) {
                        vChd->chipState.state = CHIPSTAT_BUSOFF;
                        vChd->txErrorCounter = 0;
                        vChd->rxErrorCounter = 0;                            
                        errorCounterChanged = 0;
                    }

                    e.tag = V_CHIP_STATE;
                    e.timeStamp = pciCanTimeStamp(vCard, cmd.canErrorEvent.time);
                    e.transId                           = 0;
                    e.tagData.chipState.busStatus       = vChd->chipState.state;
                    e.tagData.chipState.txErrorCounter  = vChd->txErrorCounter;
                    e.tagData.chipState.rxErrorCounter  = vChd->rxErrorCounter;
                    vCanDispatchEvent(vChd, &e);

                    if (errorCounterChanged) {
                      e.tag               = V_RECEIVE_MSG;
                      e.transId           = 0;
                      e.timeStamp         = pciCanTimeStamp(vCard, cmd.canErrorEvent.time);
                      e.tagData.msg.id    = 0;
                      e.tagData.msg.flags = VCAN_MSG_FLAG_ERROR_FRAME;
                      e.tagData.msg.dlc   = 0;
                      vCanDispatchEvent(vChd, &e);
                    }
                }
                break;
            }

            case CMD_ERROR_EVENT:
            {
#ifdef PCICANII_DEBUG             
              VCanChanData *vChd = vCard->chanData[0];
              DEBUGPRINT(1, "CMD_ERROR_EVENT, chan = %d\n", vChd->channel);
#endif              
              break;
            }
            case 0:
                // this means we have read corrupted data
                DEBUGPRINT(1,"ERROR: Corrupt data. QQQ\n");   
                return;

            default:
                DEBUGPRINT(1,"Unknown command %d received. QQQ\n", cmd.head.cmdNo);
                break;
        }
        
        
        if(cmd.head.cmdNo > CMD_TX_EXT_MESSAGE) {
            // Copy command and wakeup those who are waiting for this reply 
            struct list_head *currHead, *tmpHead;
            PciCanIIWaitNode *currNode;
            unsigned long flags;
            read_lock_irqsave(&hCd->replyWaitListLock, flags);
            list_for_each_safe(currHead, tmpHead, &hCd->replyWaitList) {
                currNode = list_entry(currHead, PciCanIIWaitNode, list); 
                if (currNode->cmdNr == cmd.head.cmdNo && getTransId(&cmd) == currNode->transId) {
                    copyCmd(currNode->replyPtr, &cmd);
                    os_if_up_sema(&currNode->waitSemaphore);                       
                }
            }
            read_unlock_irqrestore(&hCd->replyWaitListLock, flags);
        }
    }
    os_if_irq_enable(&vCard->memQLock);
} // pciCanReceiveIsr 


//======================================================================
//  Main ISR                                                            
//======================================================================
OS_IF_INTR_HANDLER pciCanInterrupt(int irq, void *dev_id, struct pt_regs *regs)
{
    VCanCardData      *vCard   = (VCanCardData*) dev_id;
    PciCanIICardData  *hCd     = vCard->hwCardData;
    unsigned long     tmp;
    unsigned int      loopMax  = 1000;
    int               handled  = 0;

    tmp = readl((void *)(hCd->baseAddr + DPRAM_INTERRUPT_REG));

    while ((tmp & DPRAM_INTERRUPT_ACTIVE) != 0) {
        writel(tmp | DPRAM_INTERRUPT_ACK, (void *)(hCd->baseAddr + DPRAM_INTERRUPT_REG));
        // this readl was added 051208 because otherwise we cannot be
        // sure that the first writel isn't overwritten by the next one.
        readl((void *)(hCd->baseAddr + DPRAM_INTERRUPT_REG));
        writel(tmp & ~DPRAM_INTERRUPT_ACK, (void *)(hCd->baseAddr + DPRAM_INTERRUPT_REG));

        tmp = readl((void *)(hCd->baseAddr + DPRAM_INTERRUPT_REG));
        handled = 1;

        if (--loopMax == 0) {
            // Kill the card.
            DEBUGPRINT(1,"<1>Channel runaway.\n");
            pciCanResetCard(vCard);         
            return IRQ_HANDLED;
        }
        pciCanReceiveIsr(vCard);
        tmp = readl((void *)(hCd->baseAddr + DPRAM_INTERRUPT_REG));
    }

    return IRQ_HANDLED;
} // pciCanInterrupt 


//======================================================================
//  Sends a can message                                                      
//======================================================================
int pciCanTransmitMessage (VCanChanData *vChd,
                                  CAN_MSG *m)
{
    PciCanIICardData *hCard = vChd->vCard->hwCardData;
    PciCanIIChanData *hChd  = vChd->hwChanData;
    VCanCardData     *vCard = vChd->vCard;
    heliosCmd         msg;

    
    // Save a copy of the message.
    hChd->current_tx_message[atomic_read(&vChd->transId) -1 ] = *m;

    msg.txCanMessage.cmdLen       = sizeof(cmdTxCanMessage);
    msg.txCanMessage.channel      = (unsigned char)vChd->channel;
    msg.txCanMessage.transId      = (unsigned char) atomic_read(&vChd->transId);
    
    if (m->id & VCAN_EXT_MSG_ID) { // Extended CAN 
        msg.txCanMessage.cmdNo         = CMD_TX_EXT_MESSAGE;
        msg.txCanMessage.rawMessage[0] = (unsigned char) ((m->id >> 24) & 0x1F);
        msg.txCanMessage.rawMessage[1] = (unsigned char) ((m->id >> 18) & 0x3F);
        msg.txCanMessage.rawMessage[2] = (unsigned char) ((m->id >> 14) & 0x0F);
        msg.txCanMessage.rawMessage[3] = (unsigned char) ((m->id >>  6) & 0xFF);
        msg.txCanMessage.rawMessage[4] = (unsigned char) ((m->id      ) & 0x3F);
    }
    else { // Standard CAN 
        msg.txCanMessage.cmdNo         = CMD_TX_STD_MESSAGE;
        msg.txCanMessage.rawMessage[0] = (unsigned char) ((m->id >>  6) & 0x1F);
        msg.txCanMessage.rawMessage[1] = (unsigned char) ((m->id      ) & 0x3F);
    }
    msg.txCanMessage.rawMessage[5] = m->length & 0x0F;
    memcpy(&msg.txCanMessage.rawMessage[6], m->data, 8);

    if((atomic_read(&vChd->transId)+1) > HELIOS_MAX_OUTSTANDING_TX) {
      atomic_set(&vChd->transId,1);
    }
    else {
      atomic_add(1, &vChd->transId);
    }
    atomic_add(1, &hChd->outstanding_tx);
    
    msg.txCanMessage.flags = m->flags & (VCAN_MSG_FLAG_TX_NOTIFY|
                                         VCAN_MSG_FLAG_TX_START|
                                         VCAN_MSG_FLAG_ERROR_FRAME|
                                         VCAN_MSG_FLAG_REMOTE_FRAME);
    os_if_irq_disable(&vCard->memQLock);
    if (QCmd(hCard, &msg) != MEM_Q_SUCCESS) {
        DEBUGPRINT(1,"*********MEMQ error can msg******** \n");
        os_if_irq_enable(&vCard->memQLock);
        return -1;
    }
    os_if_irq_enable(&vCard->memQLock);
    /*
    // dispatch message??
    if (m->flags & VCAN_MSG_FLAG_TXRQ) {
        DEBUGPRINT(1,"Msg with VCAN_MSG_FLAG_TXRQ\n");
    }
    
    if (msg.txCanMessage.flags & VCAN_MSG_FLAG_ERROR_FRAME) {
      DEBUGPRINT(1, "pciCanTransmitMessage: Transmit ERROR FRAME\n");
    }
    */
    
    return 0;
} // pciCanTransmitMessage 


//======================================================================
//  Read transmit error counter                                         
//======================================================================
int pciCanGetTxErr(VCanChanData *vChd) 
{
    pciCanRequestChipState(vChd);
    return vChd->txErrorCounter;
}


//======================================================================
//  Read transmit error counter                                         
//======================================================================
int pciCanGetRxErr(VCanChanData *vChd) 
{
    pciCanRequestChipState(vChd);
    return vChd->rxErrorCounter;
}


//======================================================================
//  Read receive queue length in hardware/firmware                     
//======================================================================
unsigned long pciCanRxQLen(VCanChanData *vChd) 
{
    return getQLen(atomic_read(&vChd->txChanBufHead), atomic_read(&vChd->txChanBufTail), TX_CHAN_BUF_SIZE);
}


//======================================================================
//  Read transmit queue length in hardware/firmware                     
//======================================================================
unsigned long pciCanTxQLen(VCanChanData *vChd) 
{
    PciCanIIChanData *hChd  = vChd->hwChanData;
    return atomic_read(&hChd->outstanding_tx);
}

//======================================================================
//  Clear send buffer on card
//======================================================================
int pciCanFlushSendBuffer (VCanChanData *vChan)
{
    PciCanIIChanData *hChd  = vChan->hwChanData;
    PciCanIICardData *hCard = vChan->vCard->hwCardData;
    VCanCardData     *vCard = vChan->vCard;
    heliosCmd cmd;

    // The card must be present! 
    if (!vCard->cardPresent) {
        DEBUGPRINT(1,"ERROR: The card must be present!\n");
        return -1;
    }

    cmd.head.cmdNo         = CMD_FLUSH_QUEUE;
    cmd.flushQueue.cmdLen  = sizeof (cmd.flushQueue);
    cmd.flushQueue.channel = (unsigned char)vChan->channel;
    cmd.flushQueue.flags   = 0;

    os_if_irq_disable(&vCard->memQLock);
    if (QCmd(hCard, &cmd) != MEM_Q_SUCCESS) {
        DEBUGPRINT(1, "ERROR: pciCanFlushSendBuffer\n");
        os_if_irq_enable(&vCard->memQLock);
        return -1;
    }
    os_if_irq_enable(&vCard->memQLock);

    atomic_set(&hChd->outstanding_tx, 0);
    atomic_set(&vChan->transId, 1);
    
    atomic_set(&vChan->txChanBufHead, 0);
    atomic_set(&vChan->txChanBufTail, 0);

    return 0;
}

//======================================================================
//  Initialize H/W                                                      
//======================================================================
int pciCanInitHW (VCanCardData *vCard)
{
    VCanChanData      *vChd;
    PciCanIIChanData  *hChd;
    PciCanIICardData  *hCard = vCard->hwCardData;
    unsigned long     tmp;
    unsigned long     addr;
    int               timeOut = 0;
    heliosCmd         cmd;
    int               i;

    // The card must be present! 
    if (!vCard->cardPresent) {
        DEBUGPRINT(1,"Error: The card must be present!\n");
        return -1;
    }

    addr = hCard->baseAddr;
    if (!addr) {
        DEBUGPRINT(1,"Error: In address!\n");
        return -1;
    }

    os_if_init_waitqueue_head(&hCard->waitHwInfo);
    os_if_init_waitqueue_head(&hCard->waitSwInfo);
    hCard->isWaiting = 0;
    
    // enable interrupts from card
    tmp = readl((void *)(addr + DPRAM_INTERRUPT_REG));
    tmp &= ~(DPRAM_INTERRUPT_DISABLE | DPRAM_INTERRUPT_ACK);
    writel(tmp, (void *)(addr + DPRAM_INTERRUPT_REG));

    // Reset card. We should get a CMD_GET_CARD_INFO_RESP and
    // a CMD_GET_SOFTWARE_INFO_RESP back.
    cmd.head.cmdNo          = CMD_RESET_CARD_REQ;
    cmd.resetCardReq.cmdLen = sizeof(cmd.resetCardReq);

    os_if_irq_disable(&vCard->memQLock);
    if (QCmd(hCard, &cmd) != MEM_Q_SUCCESS) {
        DEBUGPRINT(1,"<1> Error: In QCmd\n");
        os_if_irq_enable(&vCard->memQLock);
        return -1;
    }
    os_if_irq_enable(&vCard->memQLock);
    
    hCard->isWaiting = 1;
    timeOut = os_if_wait_event_interruptible_timeout(hCard->waitHwInfo, hCard->receivedHwInfo, 1000);
    if(!timeOut) {
        DEBUGPRINT(1,"no HW wakeup\n");
        hCard->isWaiting = 0;
        hCard->initDone = 0;
        return -1;
    }

    timeOut = os_if_wait_event_interruptible_timeout(hCard->waitHwInfo, hCard->receivedSwInfo, 1000);
    if(!timeOut) {
        DEBUGPRINT(1,"no SW wakeup\n");
        hCard->isWaiting = 0;
        hCard->initDone = 0;
        return -1;
    }
    hCard->isWaiting = 0;
    
    // for every chan
    for (i = 0; i < vCard->nrChannels; i++) {
        vChd = vCard->chanData[i];
        hChd = vChd->hwChanData;

        cmd.resetChipReq.cmdNo   = CMD_RESET_CHIP_REQ;
        cmd.resetChipReq.cmdLen  = sizeof(cmd.resetChipReq);
        cmd.resetChipReq.channel = (unsigned char)vChd->channel;

        os_if_irq_disable(&vCard->memQLock);
        if (QCmd(hCard, &cmd) != MEM_Q_SUCCESS) {
            DEBUGPRINT(1,"Error: In QCmd\n");
            os_if_irq_enable(&vCard->memQLock);
            return -1;            
        }
        os_if_irq_enable(&vCard->memQLock);
    }
    hCard->initDone = 1;
    
    return 0;
} 


//======================================================================
//  Find out addresses for one card                                     
//======================================================================
static int readPCIAddresses(struct pci_dev *dev, VCanCardData *vCard)
{  
    PciCanIICardData *hCd = vCard->hwCardData;
    int i;

    u32 addresses[] = {
        PCI_BASE_ADDRESS_0,
        PCI_BASE_ADDRESS_1,
        PCI_BASE_ADDRESS_2,
        PCI_BASE_ADDRESS_3,
        PCI_BASE_ADDRESS_4,
        PCI_BASE_ADDRESS_5,
        0
    };

    /*
     * Base addresses specify locations in memory or I/O space.
     * Decoded size can be determined by writing a value of 
     * 0xffffffff to the register, and reading it back.  Only 
     * 1 bits are decoded.
     */

    for(i = 0;addresses[i];i++) {
        u32 curr,mask;

        pci_read_config_dword(dev,addresses[i],&curr);
        pci_write_config_dword(dev,addresses[i],~0);
        pci_read_config_dword(dev,addresses[i],&mask);
        pci_write_config_dword(dev,addresses[i],curr);
        if(!mask)
            continue;

        // curr holds location in mem space
        if(curr & PCI_BASE_ADDRESS_SPACE_IO) {
            curr &= PCI_BASE_ADDRESS_IO_MASK;
        }
        else {
            curr &= PCI_BASE_ADDRESS_MEM_MASK;
        }

        // mask holds the size
        if(mask & PCI_BASE_ADDRESS_SPACE_IO) {
            mask &= PCI_BASE_ADDRESS_IO_MASK;
        }
        else {
            mask &= PCI_BASE_ADDRESS_MEM_MASK;
        }

        switch(i){
            case 0:
                hCd->baseAddr = (unsigned long)ioremap(curr, DPRAM_MEMMAP_SIZE);
                //DEBUGPRINT(1,"<1> We got an base adress! 0x%x\n", hCd->baseAddr);
                vCard->irq = dev->irq;
                break;
            default:
                
                DEBUGPRINT(1,"<1> ERROR to many io-windows\n");
                break;
        }      
    }
    if (pci_enable_device(dev)){
        DEBUGPRINT(1,"<1> enable device failed\n");
        return 0;
    }
    else {
        return 1;
    }
}    


//======================================================================
// Request send                                                         
//======================================================================
int pciCanRequestSend (VCanCardData *vCard, VCanChanData *vChan)
{
    PciCanIIChanData *hChan = vChan->hwChanData;
    if (pciCanTxAvailable(vChan)){
        os_if_queue_task(&hChan->txTaskQ);
    }
    return 0;
}


//======================================================================
//  Process send Q - This function is called from the immediate queue   
//======================================================================
void pciCanSend (void *void_chanData)
{
    VCanChanData *chd = (VCanChanData*) void_chanData;

    if(!chd->isOnBus) {
        return;
    }
    
    // Send Messages 
    if (getQLen(atomic_read(&chd->txChanBufHead), atomic_read(&chd->txChanBufTail), TX_CHAN_BUF_SIZE) != 0) {
        pciCanTransmitMessage(chd, &(chd->txChanBuffer[atomic_read(&chd->txChanBufTail)]));
        atomic_add(1, &chd->txChanBufTail);

        if ((atomic_read(&chd->txChanBufTail)) >= TX_CHAN_BUF_SIZE)
            atomic_set(&chd->txChanBufTail, 0);

        os_if_wake_up_interruptible(&chd->txChanWaitQ);
    }

    return;
}

//======================================================================
//  Timeout handler for the waitResponse below                         
//======================================================================
void responseTimeout(unsigned long voidWaitNode)
{
#if LINUX
    PciCanIIWaitNode *waitNode = (PciCanIIWaitNode*) voidWaitNode;
    waitNode->timedOut = 1;
    os_if_up_sema(&waitNode->waitSemaphore);
    return;
#else
#           pragma message("responseTimeout...")
    return;
#endif
}

//======================================================================
// Send out a command and wait for a response with timeout
//======================================================================
int pciCanWaitResponse(VCanCardData *vCard, heliosCmd *cmd, heliosCmd *replyPtr, unsigned char cmdNr, unsigned char transId)
{

    PciCanIICardData *hCard = vCard->hwCardData;
    PciCanIIWaitNode waitNode;
    unsigned long flags = 0;
    struct timer_list waitTimer;

    os_if_init_sema(&waitNode.waitSemaphore);

    waitNode.replyPtr = replyPtr;
    waitNode.cmdNr = cmdNr;
    waitNode.transId = transId;
    waitNode.timedOut = 0;

    // Add to card's list of expected responses 
    spin_lock_irqsave(&hCard->replyWaitListSpinLock, flags);
    list_add(&waitNode.list, &hCard->replyWaitList);
    spin_unlock_irqrestore(&hCard->replyWaitListSpinLock, flags);

    os_if_irq_disable(&vCard->memQLock);
    if (QCmd(hCard, cmd) != MEM_Q_SUCCESS) {
        os_if_irq_enable(&vCard->memQLock);
        DEBUGPRINT(1,"ERROR----- pciCanGetBusParams----------\n");
        write_lock_irqsave(&hCard->replyWaitListLock, flags);
        list_del(&waitNode.list);
        write_unlock_irqrestore(&hCard->replyWaitListLock, flags);
        return -1;
    }
    os_if_irq_enable(&vCard->memQLock);

    init_timer(&waitTimer);
    waitTimer.function = responseTimeout;
    waitTimer.data = (unsigned long) &waitNode;
    waitTimer.expires = jiffies + (PCICANII_CMD_RESP_WAIT_TIME * HZ)/1000;
    add_timer(&waitTimer);

    os_if_down_sema(&waitNode.waitSemaphore);

    // Now we either got a response or a timeout
    spin_lock_irqsave(&hCard->replyWaitListSpinLock, flags);
    list_del(&waitNode.list);
    spin_unlock_irqrestore(&hCard->replyWaitListSpinLock, flags);
    del_timer_sync(&waitTimer);

    if (waitNode.timedOut) {
        DEBUGPRINT(1, "pciCanWaitResponse: return VCAN_STAT_TIMEOUT\n");
        return VCAN_STAT_TIMEOUT;
    }

    return VCAN_STAT_OK;
}

//======================================================================
//  Initialize H/W specific data                                        
//======================================================================
int pciCanInitData (VCanCardData *vCard)
{
    VCanChanData *vChd;
    PciCanIICardData *hCd = vCard->hwCardData;
    int chNr;
    vCanInitData(vCard);
    for (chNr = 0; chNr < vCard->nrChannels; chNr++){
        PciCanIIChanData *hChd = vCard->chanData[chNr]->hwChanData;
        os_if_init_task(&hChd->txTaskQ, pciCanSend, vCard->chanData[chNr]);
        vChd = vCard->chanData[chNr];
        memset (hChd->current_tx_message, 0, sizeof (hChd->current_tx_message));
        atomic_set(&hChd->outstanding_tx, 0);
        atomic_set(&vChd->transId, 1);
        vChd->overrun        = 0;        // qqq overrun not used
        vChd->errorCount     = 0;
        vChd->errorTime      = 0;
    }

    INIT_LIST_HEAD(&hCd->replyWaitList);
    hCd->replyWaitListLock = RW_LOCK_UNLOCKED;
    hCd->timeHi_lock       = SPIN_LOCK_UNLOCKED;
    
    return 0;
}


//======================================================================
// Initialize the HW for one card                                       
//======================================================================
int pciCanInitOne(struct pci_dev *dev)
{
    // Helper struct for allocation 
    typedef struct {
        VCanChanData *dataPtrArray[MAX_CHANNELS];
        VCanChanData vChd[MAX_CHANNELS];
        PciCanIIChanData hChd[MAX_CHANNELS];
    } ChanHelperStruct;

    ChanHelperStruct *chs;
    int chNr;
    VCanCardData *vCard;
    
    // Allocate data area for this card 
    vCard  = kmalloc(sizeof(VCanCardData) + sizeof(PciCanIICardData), GFP_KERNEL);
    if (!vCard) goto card_alloc_err;
    memset(vCard, 0, sizeof(VCanCardData) + sizeof(PciCanIICardData));

    // hwCardData is directly after VCanCardData 
    vCard->hwCardData = vCard + 1;    

    // Allocate memory for n channels 
    chs = kmalloc(sizeof(ChanHelperStruct), GFP_KERNEL);
    if (!chs) goto chan_alloc_err;
    memset(chs, 0, sizeof(ChanHelperStruct));

    // Init array and hwChanData 
    for (chNr = 0; chNr < MAX_CHANNELS; chNr++){
        chs->dataPtrArray[chNr] = &chs->vChd[chNr];
        chs->vChd[chNr].hwChanData = &chs->hChd[chNr];
    }
    vCard->chanData = chs->dataPtrArray;

    // Get PCI controller, SJA1000 base and Xilinx addresses 
    if (!readPCIAddresses(dev, vCard)) {
        DEBUGPRINT(1,"<1>readPCIAddresses failed");
        goto pci_err;
    }
    
    // Find out type of card i.e. N/O channels etc 
    if (pciCanProbe(vCard)) {
        DEBUGPRINT(1,"<1>pciCanProbe failed");
        goto probe_err;
    }
    
    // Init channels 
    pciCanInitData(vCard);

    os_if_spin_lock(&canCardsLock);
    // Insert into list of cards 
    vCard->next = canCards;
    canCards = vCard;
    os_if_spin_unlock(&canCardsLock);
    
    // ISR 
    request_irq(vCard->irq, pciCanInterrupt, SA_SHIRQ, "Kvaser PCIcanII", vCard);
    // Init h/w  & enable interrupts in PCI Interface 
    if (pciCanInitHW (vCard)) {
        DEBUGPRINT(1,"<1> pciCanInitHW failed\n");
        goto intr_err;
    }

    
    return 1;

intr_err:
    free_irq(vCard->irq, vCard);
    kfree(vCard->chanData);
chan_alloc_err:
probe_err:
pci_err:
    kfree(vCard);
card_alloc_err:
    return 0;
} // pciCanInitOne 


//======================================================================
// Find and initialize all cards                                        
//======================================================================
int pciCanInitAllDevices(void)
{
    struct pci_dev *dev = NULL;
    int found;
    
    for (found=0; found < PCICAN_MAX_DEV;) {
        dev = pci_find_device(PCICANII_VENDOR, PCICANII_ID, dev);

        if (!dev) {
            // No more PCIcanII
            break;
        }
        // Initialize card 
        found += pciCanInitOne(dev);
    }
    
    // We need to find at least one 
    return  (found == 0) ? -ENODEV : 0;
} // pciCanInitAllDevices 


//======================================================================
// Shut down and free resources before unloading driver                 
//======================================================================
int pciCanCloseAllDevices(void)
{
    VCanCardData *vCard;

    os_if_spin_lock(&canCardsLock);
    vCard = canCards;
    while (vCard) {
        DEBUGPRINT(1, "pciCanCloseAllDevices\n");
        free_irq(vCard->irq, vCard);
        vCard = canCards->next;
        kfree(canCards->chanData);
        kfree(canCards);
        canCards = vCard;
    }
    os_if_spin_unlock(&canCardsLock);
    return 0;
} // pciCanCloseAllDevices 
