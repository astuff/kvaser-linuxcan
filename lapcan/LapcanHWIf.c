/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

//  Kvaser CAN driver for LAPcan

#if LINUX
    // Module versioning 
#   define EXPORT_SYMTAB
#   include <linux/module.h>
#   include <linux/autoconf.h> // retrieve the CONFIG_* macros 
#   if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#       define MODVERSIONS
#   endif
#   ifdef MODVERSIONS
#       if LINUX_2_6
#           include <config/modversions.h>
#       else
#           include <linux/modversions.h>
#       endif
#   endif
#   include <linux/config.h>
#   include <linux/kernel.h>
#   include <linux/init.h>
#   include <linux/sched.h>
#   include <linux/ptrace.h>
#   include <linux/slab.h>
#   include <linux/string.h>
#   include <linux/timer.h>
#   include <linux/spinlock.h>
#   if LINUX_2_6
#       include <linux/workqueue.h>
#       include <linux/completion.h>
#   else
#       include <linux/tqueue.h>
#       include <pcmcia/bus_ops.h>
#   endif
#   include <linux/interrupt.h>
#   include <linux/delay.h>
#   include <linux/ioport.h>
#   include <linux/types.h>
//#   include <asm/io.h>
#   include <asm/system.h>
#   include <asm/bitops.h>
#   include <asm/uaccess.h>
#   include <pcmcia/version.h>
#   include <pcmcia/cs_types.h>
#   include <pcmcia/cs.h>
#   include <pcmcia/cistpl.h>
#   include <pcmcia/cisreg.h>
#   include <pcmcia/ds.h>
#   include <asm/atomic.h>
#else // win32
#   include "linuxErrors.h"
#   include "debug_file.h"
#endif // end win32

// common
#include <tl16564.h>


// Kvaser definitions 
#include "VCanOsIf.h"
#include "LapcanHwIf.h"
#include "osif_functions_kernel.h"
#include "hwnames.h"
#include "osif_kernel.h"
#include "osif_functions_pcmcia.h"

//
// All the PCMCIA modules use PCMCIA_DEBUG to control debugging.  If
// you do not define PCMCIA_DEBUG at all, all the debug code will be
// left out.  If you compile with PCMCIA_DEBUG=0, the debug code will
// be present but disabled -- but it can then be enabled for specific
// modules at load time with a 'pc_debug=#' option to insmod.
//

#if LINUX
#   if LINUX_2_6
        MODULE_LICENSE("GPL");
#   else
        MODULE_LICENSE("GPL");
        EXPORT_NO_SYMBOLS;
#   endif

#   ifdef PCMCIA_DEBUG
        static int pc_debug = PCMCIA_DEBUG;
        MODULE_PARM(pc_debug, "i");
#       define DEBUGPRINT(n, args...) if (pc_debug>=(n)) printk("<" #n ">" args)
#   else
#       define DEBUGPRINT(n, args...)

#   endif
#else
#   ifdef PCMCIA_DEBUG
#       pragma message("DEBUG")
#       define DEBUGPRINT(x)    fprintf x
        FILE         *g_out;
#   else    
#pragma warning(disable:4002)
#       define DEBUGPRINT(a) 
#   endif
#endif // LINUX

const char      *device_name    = DEVICE_NAME_STRING;
spinlock_t      driver_lock     = SPIN_LOCK_UNLOCKED;
rwlock_t        devList_lock    = RW_LOCK_UNLOCKED;
spinlock_t      timeHi_lock     = SPIN_LOCK_UNLOCKED;
spinlock_t      interrupt_lock  = SPIN_LOCK_UNLOCKED;


//======================================================================
// HW function pointers                                                 
//======================================================================

#if LINUX_2_6
VCanHWInterface hwIf = {
    .initAllDevices    = hwIfInitDriver,    
    .setBusParams      = hwIfSetBusParams,     
    .getBusParams      = hwIfGetBusParams,     
    .setOutputMode     = hwIfSetOutputMode,    
    .setTranceiverMode = hwIfSetTranceiverMode,
    .busOn             = hwIfBusOn,            
    .busOff            = hwIfBusOff,           
    .txAvailable       = hwIfTxAvailable,      

    // qqq shouldn't be here!
    .transmitMessage   = hwIfPrepareAndTransmit,  

    .procRead          = hwIfProcRead,         
    .closeAllDevices   = hwIfCloseAllDevices,  
    .getTime           = hwIfTime,
    .flushSendBuffer   = hwIfFlushSendBuffer, 
    .getTxErr          = hwIfGetTxErr,
    .getRxErr          = hwIfGetRxErr,
    .rxQLen            = hwIfHwRxQLen,
    .txQLen            = hwIfHwTxQLen,
    .requestChipState  = hwIfRequestChipState,
    .requestSend       = hwIfRequestSend
};
#elif LINUX
VCanHWInterface hwIf = {
    initAllDevices:      hwIfInitDriver,    
    setBusParams:        hwIfSetBusParams,     
    getBusParams:        hwIfGetBusParams,     
    setOutputMode:       hwIfSetOutputMode,    
    setTranceiverMode:   hwIfSetTranceiverMode,
    busOn:               hwIfBusOn,            
    busOff:              hwIfBusOff,           
    txAvailable:         hwIfTxAvailable,      

    // qqq shouldn't be here!
    transmitMessage:     hwIfPrepareAndTransmit,  

    procRead:            hwIfProcRead,         
    closeAllDevices:     hwIfCloseAllDevices,  
    getTime:             hwIfTime,
    flushSendBuffer:     hwIfFlushSendBuffer, 
    getTxErr:            hwIfGetTxErr,
    getRxErr:            hwIfGetRxErr,
    rxQLen:              hwIfHwRxQLen,
    txQLen:              hwIfHwTxQLen,
    requestChipState:    hwIfRequestChipState,
    requestSend:         hwIfRequestSend
};
#else
VCanHWInterface hwIf = {
    /*initAllDevices:*/     hwIfInitDriver,    
    /*setBusParams:  */     hwIfSetBusParams,     
    /*getBusParams:  */     hwIfGetBusParams,     
    /*setOutputMode: */     hwIfSetOutputMode,    
    /*setTranceiverMode: */ hwIfSetTranceiverMode,
    /*busOn:           */   hwIfBusOn,            
    /*busOff:          */   hwIfBusOff,           
    /*txAvailable:      */  hwIfTxAvailable,      
    /*transmitMessage:  */  hwIfPrepareAndTransmit,  
    /*procRead:         */  hwIfProcRead,         
    /*closeAllDevices:  */  hwIfCloseAllDevices,  
    /*getTime:          */  hwIfTime,
    /*flushSendBuffer:  */  hwIfFlushSendBuffer, 
    /*getTxErr:         */  hwIfGetTxErr,
    /*getRxErr:         */  hwIfGetRxErr,
    /*rxQLen:           */  hwIfHwRxQLen,
    /*txQLen:           */  hwIfHwTxQLen,
    /*requestChipState: */  hwIfRequestChipState,
    /*requestSend:      */  hwIfRequestSend
};
#endif




//======================================================================
// /proc read function                                                  
//======================================================================
int hwIfProcRead (char *buf, char **start, off_t offset, int count, int *eof, void *data)
{
    int len = 0;
    len += sprintf(buf+len,"\ntotal channels %d\n", driverData.minorNr);
    *eof = 1;
    return len;
}


//======================================================================
//  Can we send now?                                                    
//======================================================================
int hwIfTxAvailable (VCanChanData *vChan)
{
    LapcanChanData *hChan = vChan->hwChanData;
    return (atomic_read(&hChan->sentTXflagCount) == atomic_read(&hChan->recvTXflagCount)) && !atomic_read(&hChan->txQChipFull);
} // hwIfTxAvailable 


//======================================================================
// Get time
//======================================================================
unsigned long hwIfTime(VCanCardData *vCard)
{
    int ret;

    cmdReadClockReq cmd;
    cmdReadClockResp resp;

    cmd.cmdLen = CMDLEN(cmd);
    cmd.cmdNo = CMD_READ_CLOCK_REQ;
    cmd.transId = 0;

    ret = hwIfWaitResponse(vCard, (lpcCmd*)&cmd, (lpcCmd*)&resp, CMD_READ_CLOCK_RESP, cmd.transId);
    if (ret) return 0;
    os_if_irq_disable(&timeHi_lock);
    vCard->timeHi = resp.currentTime & 0xffff0000;
    os_if_irq_enable(&timeHi_lock);
    return resp.currentTime/LAPCAN_TICKS_PER_MS;//hwIfTimeStamp(vCard, resp.currentTime & 0xffff);
}


//======================================================================
// get timestamp
//======================================================================
unsigned long hwIfTimeStamp(VCanCardData *vCard, unsigned long timeLo)
{
    unsigned long ret;
    os_if_irq_disable(&timeHi_lock);
    ret = (vCard->timeHi + timeLo)/ LAPCAN_TICKS_PER_MS;
    os_if_irq_enable(&timeHi_lock);
    return ret;
}


//======================================================================
#if LINUX
// Parameters that can be set with 'insmod' 

// Release IO ports after configuration? 
static int free_ports = 0;

// The old way: bit map of interrupts to choose from 
// This means pick from 15, 14, 12, 11, 10, 9, 7, 5, 4, and 3 
static unsigned int irq_mask = 0xdeb8;
// Newer, simpler way of listing specific interrupts 
static int irq_list[4] = { -1 };

MODULE_PARM(free_ports, "i");
MODULE_PARM(irq_mask, "i");
MODULE_PARM(irq_list, "1-4i");
MODULE_AUTHOR("KVASER");
MODULE_DESCRIPTION("LAPcan CAN module.");


#else
#endif
//======================================================================

// qqq static?
#if LINUX
#   if LINUX_2_6
    static OS_IF_DEV_INFO devInfoStruct = {
        .drv = {.name = "lapcan_cs",},
        .attach  = hwIfAttach,
        .detach  = hwIfDetach,
        .owner   = THIS_MODULE,
    };
#else
    static dev_info_t devInfoStruct = DEVICE_NAME_STRING;
#   endif
    static dev_info_t devInfo       = DEVICE_NAME_STRING;
#endif

// List of all lapcan cards(devices) 
static OS_IF_CARD_CONTEXT *devList = NULL;

unsigned char chanMap [CMD__HIGH + 1] = \
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
0, 0, 0, 0, 0, 0, 2, 3, 3, 5, \
3, 3, 0, 0, 3, 0, 3, 3, 3, 3, \
3, 3, 3, 3, 0, 0, 0, 0, 0, 0, \
0, 0, 3, 3, 0, 0, 0, 3, 0, 3, \
3, 0, 3, 3, 3, 0, 0, 0, 3, 3, \
3, 3, 0, 0, 0, 0, 0, 0, 0, 0, \
0, 0, 0, 0, 0, 0, 0, 3, 3, 2, \
3, 3, 0, 0, 0, 3, 0, 0, 0, 3, \
3, 3, 3};

unsigned char flagMap [CMD__HIGH + 1] = \
{2, 2, 0, 0, 0, 0, 0, 0, 0, 0, \
0, 0, 0, 0, 0, 0, 0, 4, 6, 0, \
0, 0, 0, 0, 6, 0, 0, 0, 0, 0, \
0, 6, 0, 0, 0, 0, 0, 0, 0, 0, \
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
0, 0, 0, 6, 5, 0, 0, 0, 0, 0, \
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
4, 0, 0};

#if LINUX
unsigned char transIdMap[CMD__HIGH + 1] = {

    [CMD_RX_STD_MESSAGE] =                        offsetof(cmdRxStdMessage, transId),
    [CMD_RX_EXT_MESSAGE] =                        offsetof(cmdRxStdMessage, transId),
    [CMD_AUTOBAUD_REQ] =                          offsetof(cmdAutobaudReq, transId),                        
    [CMD_AUTOBAUD_RESP] =                         offsetof(cmdAutobaudResp, transId),                      
    [CMD_CANCEL_SCHDL_MSGS_REQ] =                 offsetof(cmdCancelSchdlMsgsReq, transId),               
    [CMD_CANCEL_SCHDL_MSGS_RESP] =                offsetof(cmdCancelSchdlMsgsResp, transId),              
    [CMD_GET_SW_FILTER_REQ] =                     offsetof(cmdGetSwFilterReq, transId),                   
    [CMD_GET_SW_FILTER_RESP] =                    offsetof(cmdGetSwFilterResp, transId),                  
    [CMD_ERROR_EVENT] =                           offsetof(cmdErrorEvent, transId),                        
    [CMD_GET_BUSPARAMS_REQ] =                     offsetof(cmdGetBusparamsReq, transId),                   
    [CMD_GET_BUSPARAMS_RESP] =                    offsetof(cmdGetBusparamsResp, transId),                  
    [CMD_GET_CARD_INFO_REQ] =                     offsetof(cmdGetCardInfoReq, transId),                   
    [CMD_GET_CARD_INFO_RESP] =                    offsetof(cmdGetCardInfoResp, transId),                  
    [CMD_SET_SW_FILTER_REQ] =                     offsetof(cmdSetSwFilterReq, transId),                   
    [CMD_GET_CHIP_STATE_REQ] =                    offsetof(cmdGetChipStateReq, transId),                  
    [CMD_GET_CHIP_STATE_RESP] =                   offsetof(cmdGetChipStateResp, transId),                 
    [CMD_GET_DRIVERMODE_REQ] =                    offsetof(cmdGetDrivermodeReq, transId),                  
    [CMD_GET_DRIVERMODE_RESP] =                   offsetof(cmdGetDrivermodeResp, transId),                 
    [CMD_GET_HW_FILTER_REQ] =                     offsetof(cmdGetHwFilterReq, transId),                   
    [CMD_GET_HW_FILTER_RESP] =                    offsetof(cmdGetHwFilterResp, transId),                  
    [CMD_GET_INTERFACE_INFO_REQ] =                offsetof(cmdGetInterfaceInfoReq, transId),              
    [CMD_GET_INTERFACE_INFO_RESP] =               offsetof(cmdGetInterfaceInfoResp, transId),             
    [CMD_GET_IO_PIN_STATE_REQ] =                  offsetof(cmdGetIoPinStateReq, transId),                
    [CMD_GET_IO_PIN_STATE_RESP] =                 offsetof(cmdGetIoPinStateResp, transId),               
    [CMD_GET_IO_PIN_TRIGGER_REQ] =                offsetof(cmdGetIoPinTriggerReq, transId),              
    [CMD_GET_IO_PIN_TRIGGER_RESP] =               offsetof(cmdGetIoPinTriggerResp, transId),             
    [CMD_GET_GLOBAL_OPTIONS_REQ] =                offsetof(cmdGetGlobalOptionsReq, transId),              
    [CMD_GET_GLOBAL_OPTIONS_RESP] =               offsetof(cmdGetGlobalOptionsResp, transId),             
    [CMD_GET_SOFTWARE_INFO_REQ] =                 offsetof(cmdGetSoftwareInfoReq, transId),               
    [CMD_GET_SOFTWARE_INFO_RESP] =                offsetof(cmdGetSoftwareInfoResp, transId),              
    [CMD_GET_STATISTICS_REQ] =                    offsetof(cmdGetStatisticsReq, transId),                  
    [CMD_GET_STATISTICS_RESP] =                   offsetof(cmdGetStatisticsResp, transId),                 
    [CMD_GET_TIMER_REQ] =                         offsetof(cmdGetTimerReq, transId),                       
    [CMD_GET_TIMER_RESP] =                        offsetof(cmdGetTimerResp, transId),                      
    [CMD_NO_COMMAND] =                            offsetof(cmdNoCommand, transId),                         
    [CMD_RESET_CHIP_REQ] =                        offsetof(cmdResetChipReq, transId),                      
    [CMD_RESET_CARD_REQ] =                        offsetof(cmdResetCardReq, transId),                      
    [CMD_RESET_STATISTICS_REQ] =                  offsetof(cmdResetStatisticsReq, transId),                
    [CMD_SET_BUSPARAMS_REQ] =                     offsetof(cmdSetBusparamsReq, transId),                   
    [CMD_SET_DRIVERMODE_REQ] =                    offsetof(cmdSetDrivermodeReq, transId),                  
    [CMD_SET_HW_FILTER_REQ] =                     offsetof(cmdSetHwFilterReq, transId),                   
    [CMD_SET_IO_PIN_STATE_REQ] =                  offsetof(cmdSetIoPinStateReq, transId),                
    [CMD_SET_IO_PIN_TRIGGER_REQ] =                offsetof(cmdSetIoPinTriggerReq, transId),              
    [CMD_SET_GLOBAL_OPTIONS_REQ] =                offsetof(cmdSetGlobalOptionsReq, transId),              
    [CMD_SET_TIMER_REQ] =                         offsetof(cmdSetTimerReq, transId),                       
    [CMD_START_CHIP_REQ] =                        offsetof(cmdStartChipReq, transId),                      
    [CMD_START_CHIP_RESP] =                       offsetof(cmdStartChipResp, transId),                     
    [CMD_STOP_CHIP_REQ] =                         offsetof(cmdStopChipReq, transId),                       
    [CMD_STOP_CHIP_RESP] =                        offsetof(cmdStopChipResp, transId),                      
    [CMD_TIMER_EVENT] =                           offsetof(cmdTimerEvent, transId),                        
    [CMD_TRIGGER_EVENT] =                         offsetof(cmdTriggerEvent, transId),                      
    [CMD_START_APPLICATION_REQ] =                 offsetof(cmdStartApplicationReq, transId),               
    [CMD_READ_CLOCK_REQ] =                        offsetof(cmdReadClockReq, transId),                      
    [CMD_READ_CLOCK_RESP] =                       offsetof(cmdReadClockResp, transId),                     
    [CMD_RESET_CLOCK] =                           offsetof(cmdResetClock, transId),                        
    [CMD_SET_HEARTBEAT_REQ] =                     offsetof(cmdSetHeartbeatReq, transId),                   
    [CMD_SET_TRANSCEIVER_MODE_REQ] =              offsetof(cmdSetTransceiverModeReq, transId),            
    [CMD_SET_TRANSCEIVER_MODE_RESP] =             offsetof(cmdSetTransceiverModeResp, transId),           
    [CMD_GET_TRANSCEIVER_INFO_REQ] =              offsetof(cmdGetTransceiverInfoReq, transId),            
    [CMD_GET_TRANSCEIVER_INFO_RESP] =             offsetof(cmdGetTransceiverInfoResp, transId),           
    [CMD_READ_PARAMETER_REQ] =                    offsetof(cmdReadParameterReq, transId),                  
    [CMD_READ_PARAMETER_RESP] =                   offsetof(cmdReadParameterResp, transId),                 
    [CMD_DETECT_TRANSCEIVERS] =                   offsetof(cmdDetectTransceivers, transId),                
    [CMD_READ_CLOCK_NOW_REQ] =                    offsetof(cmdReadClockNowReq, transId),                  
    [CMD_READ_CLOCK_NOW_RESP] =                   offsetof(cmdReadClockNowResp, transId),                 
    [CMD_FILTER_MESSAGE] =                        offsetof(cmdFilterMessage, transId),                     
    [CMD_FLUSH_QUEUE] =                           offsetof(cmdFlushQueue, transId),                        
    [CMD_GET_CHIP_STATE2_REQ] =                   offsetof(cmdGetChipState2Req, transId),                 
    [CMD_GET_CHIP_STATE2_RESP] =                  offsetof(cmdGetChipState2Resp, transId)                
};
#else
#           pragma message("qqq ...")
#endif


//======================================================================
// getCmdNr                                                             
//======================================================================
OS_IF_INLINE int getCmdNr (lpcCmd *cmd)
{
    unsigned char *b;  

    b = (unsigned char*) cmd;
    if (b [0] & 0x80) { // Infrequent command:bit 7 is set 
        return b [1] & 0x7f;
    } 
    else {
        return b [0] >> 5;
    }
}


//======================================================================
// Find out length of lapcan command                                    
//======================================================================
OS_IF_INLINE int getCmdLen (lpcCmd *cmd)
{  
    unsigned char *b;

    b = (unsigned char*) cmd;
    if (b [0] & 0x80) { // Infrequent command:bit 7 is set 
        return 1 + (b [0] & 0x7f); // Length is lower 7 bits 
    } 
    else {
        return 1 + (b [0] & 0x1f); // Length is lower 5 bits 
    }
}


//======================================================================
//    getCmdChannel                                                     
//======================================================================
OS_IF_INLINE int getCmdChan (lpcCmd *cmd)
{
    unsigned char *b;
    b = (unsigned char*) cmd;
    if (b [0] & 0x80) { // Infrequent command:bit 7 is set 
        int index = chanMap[getCmdNr(cmd)];
        if (index == 0) return 0;
        else return b[index];
    } 
    else {
        return b[1] >> 4; // channel is upper 4 bits 
    }
}


//======================================================================
//    getTransId                                                        
//======================================================================
OS_IF_INLINE int getTransId (lpcCmd *cmd)
{
#if LINUX
    unsigned char *b;
    int index;
    b = (unsigned char*) cmd;
    index = transIdMap[getCmdNr(cmd)];
    if (index == 0) return 0;
    else return b[index];
#else
#           pragma message("qqq getTransId not implemented...")
    return 1;
#endif
}


//======================================================================
//    Read flags from command                                           
//======================================================================
OS_IF_INLINE unsigned char getFlags (lpcCmd *cmd)
{
    // flagMap[getCmdNr(cmd)] is the position of the flags in the command 
    return ((unsigned char*) cmd) [flagMap [getCmdNr(cmd)] ];
    
}

//======================================================================
// setFlags                                                             
//======================================================================
OS_IF_INLINE void setFlags (lpcCmd *cmd, unsigned char flags)
{   
    ((unsigned char*) cmd) [flagMap [getCmdNr(cmd)] ] = flags;
    return;
}


//======================================================================
// Copy a lapcan command                                                
//======================================================================
OS_IF_INLINE void copyLapcanCmd (lpcCmd *cmd_to, lpcCmd *cmd_from)
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
//  Set bit timing                                                      
//======================================================================
int hwIfSetBusParams (VCanChanData *vChan, VCanBusParams *par)
{
    int ret;
    cmdSetBusparamsReq cmd;

    cmd.cmdLen  = CMDLEN(cmd);
    cmd.cmdNo   = CMD_SET_BUSPARAMS_REQ;
    cmd.transId = vChan->channel;
    cmd.channel = vChan->channel;
    cmd.bitRate = par->freq;
    cmd.tseg1   = par->tseg1;
    cmd.tseg2   = par->tseg2;
    cmd.sjw     = par->sjw;
    cmd.noSamp  = par->samp3;

    ret = hwIfQCmd(vChan->vCard, (lpcCmd*)&cmd, LAPCAN_Q_CMD_WAIT_TIME);
    return ret;

} // hwIfSetBusParams 


//======================================================================
//  Timeout handler for the waitResponse below                         
//======================================================================
void responseTimeout(unsigned long voidWaitNode)
{
#if LINUX
    LapcanWaitNode *waitNode = (LapcanWaitNode*) voidWaitNode;
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
int hwIfWaitResponse(VCanCardData *vCard, lpcCmd *cmd, lpcCmd *replyPtr, unsigned char cmdNr, unsigned char transId)
{
#if LINUX
    LapcanCardData *hCard = vCard->hwCardData;
    int ret;
    LapcanWaitNode waitNode;
    unsigned long flags = 0;
    struct timer_list waitTimer;

    os_if_init_sema(&waitNode.waitSemaphore);
    
    waitNode.replyPtr = replyPtr;
    waitNode.cmdNr = cmdNr;
    waitNode.transId = transId;
    waitNode.timedOut = 0;

    // Add to card's list of expected responses 
    spin_lock_irqsave(&hCard->replyWaitListLock, flags);
    list_add(&waitNode.list, &hCard->replyWaitList);
    spin_unlock_irqrestore(&hCard->replyWaitListLock, flags);

    ret = hwIfQCmd(vCard, (lpcCmd*)cmd, LAPCAN_Q_CMD_WAIT_TIME);
    if (ret != 0) {
        write_lock_irqsave(&hCard->replyWaitListLock, flags);
        list_del(&waitNode.list);
        write_unlock_irqrestore(&hCard->replyWaitListLock, flags);            
        return ret;
    }

    init_timer(&waitTimer);
    waitTimer.function = responseTimeout;
    waitTimer.data = (unsigned long) &waitNode;
    waitTimer.expires = jiffies + (LAPCAN_CMD_RESP_WAIT_TIME * HZ)/1000;
    add_timer(&waitTimer);

    os_if_down_sema(&waitNode.waitSemaphore);
    
    // Now we either got a response or a timeout
    spin_lock_irqsave(&hCard->replyWaitListLock, flags);
    list_del(&waitNode.list);
    spin_unlock_irqrestore(&hCard->replyWaitListLock, flags);
    del_timer_sync(&waitTimer);

    if (waitNode.timedOut) {
        return VCAN_STAT_TIMEOUT;
    }

    return VCAN_STAT_OK;
#else
#           pragma message("hwIfWaitResponse not implemented...")
    return VCAN_STAT_OK;
#endif

} // hwIfWaitResponse 


//======================================================================
//  Get bit timing                                                      
//======================================================================
int hwIfGetBusParams (VCanChanData *vChan, VCanBusParams *par)
{
    int ret;

    cmdGetBusparamsReq cmd;
    cmdGetBusparamsResp resp;

    cmd.cmdLen = CMDLEN(cmd);
    cmd.cmdNo = CMD_GET_BUSPARAMS_REQ;
    cmd.transId = vChan->channel;
    cmd.channel = vChan->channel;

    ret = hwIfWaitResponse(vChan->vCard, (lpcCmd*)&cmd, (lpcCmd*)&resp, CMD_GET_BUSPARAMS_RESP, cmd.transId);
    if (ret) return ret;

    par->sjw   = resp.sjw;
    par->samp3 = resp.noSamp;
    par->tseg1 = resp.tseg1;
    par->tseg2 = resp.tseg2;
    par->freq  = resp.bitRate; 

    return VCAN_STAT_OK;

} // hwIfGetBusParams 


//======================================================================
//  Set silent or normal mode                                           
//======================================================================
int hwIfSetOutputMode (VCanChanData *vChan, int silent)
{
    cmdSetDrivermodeReq cmd;
    int ret;

    cmd.cmdLen = CMDLEN(cmd);
    cmd.cmdNo  = CMD_SET_DRIVERMODE_REQ;
    cmd.transId = vChan->channel;
    cmd.channel = vChan->channel;

    if (silent) cmd.driverMode = DRIVERMODE_SILENT;
    else cmd.driverMode = DRIVERMODE_NORMAL;

    ret = hwIfQCmd(vChan->vCard, (lpcCmd*)&cmd, LAPCAN_Q_CMD_WAIT_TIME);

    return ret;
} // hwIfSetOutputMode 


//======================================================================
//  Line mode                                                           
//======================================================================
int hwIfSetTranceiverMode (VCanChanData *vChan, int linemode, int resnet)
{
    cmdSetTransceiverModeReq cmd;
    int ret;

    cmd.cmdLen = CMDLEN(cmd);
    cmd.cmdNo  = CMD_SET_TRANSCEIVER_MODE_REQ;
    cmd.transId = vChan->channel;
    cmd.channel = vChan->channel;
    cmd.lineMode = linemode;
    cmd.resistorNet = resnet;

    ret = hwIfQCmd(vChan->vCard, (lpcCmd*)&cmd, LAPCAN_Q_CMD_WAIT_TIME);

    return ret;

} // hwIfSetTranceiverMode 


//======================================================================
//  Query chip status                                                   
//======================================================================
int hwIfRequestChipState (VCanChanData *vChan)
{
    int ret;

    cmdGetChipState2Req cmd;
    cmdGetChipState2Resp resp;
    VCAN_EVENT event;

    cmd.cmdLen = CMDLEN(cmd);
    cmd.cmdNo = CMD_GET_CHIP_STATE2_REQ;
    cmd.transId = vChan->channel;
    cmd.channel = vChan->channel;

    ret = hwIfWaitResponse(vChan->vCard, (lpcCmd*)&cmd, (lpcCmd*)&resp, CMD_GET_CHIP_STATE2_RESP, cmd.transId);
    if (ret) return ret;

    vChan->chipState.state = 0;

    if (resp.busStatus & BUSSTAT_BUSOFF) vChan->chipState.state |= CHIPSTAT_BUSOFF;

    if (resp.busStatus & BUSSTAT_ERROR_PASSIVE) vChan->chipState.state |= CHIPSTAT_ERROR_PASSIVE;

    if (resp.busStatus & BUSSTAT_ERROR_WARNING) vChan->chipState.state |= CHIPSTAT_ERROR_WARNING;

    if (resp.busStatus & BUSSTAT_ERROR_ACTIVE) vChan->chipState.state |= CHIPSTAT_ERROR_ACTIVE;

    event.tag = V_CHIP_STATE;
    event.timeStamp = hwIfTimeStamp(vChan->vCard, resp.time);
    event.transId = 0;
    event.tagData.chipState.busStatus = (unsigned char)vChan->chipState.state;
    event.tagData.chipState.txErrorCounter = resp.txErrorCounter;
    event.tagData.chipState.rxErrorCounter = resp.rxErrorCounter ;
    vCanDispatchEvent(vChan, &event);

    return VCAN_STAT_OK;
   
} // hwIfRequestChipState 


//======================================================================
//  Go bus on                                                           
//======================================================================
int hwIfBusOn (VCanChanData *vChan)
{
    int ret;

    cmdStartChipReq cmd;
    cmdStartChipResp resp;

    LapcanCardData *lCard = (LapcanCardData*)vChan->vCard->hwCardData;
    lCard->mainRcvBufHead = 0;
    lCard->mainRcvBufTail = 0;
    
    cmd.cmdLen = CMDLEN(cmd);
    cmd.cmdNo = CMD_START_CHIP_REQ;
    cmd.transId = vChan->channel;
    cmd.channel = vChan->channel;
    
    // qqq do we need a longer timeout here?
    ret = hwIfWaitResponse(vChan->vCard, (lpcCmd*)&cmd, (lpcCmd*)&resp, CMD_START_CHIP_RESP, cmd.transId);
    if (ret) return ret;

    vChan->isOnBus = 1;
    vChan->overrun = 0;

    return VCAN_STAT_OK;

} // hwIfBusOn 


//======================================================================
//  Go bus off                                                          
//======================================================================
int hwIfBusOff (VCanChanData *vChan)
{
    int ret;
    cmdStopChipReq cmd;
    cmdStopChipResp resp;
    LapcanChanData *hChan;

    cmd.cmdLen = CMDLEN(cmd);
    cmd.cmdNo = CMD_STOP_CHIP_REQ;
    cmd.transId = vChan->channel;
    cmd.channel = vChan->channel;

    // empty can-queue otherwise we might send messages even though
    // channel is off bus. 

    hChan  = vChan->hwChanData;
    atomic_set(&hChan->sentTXflagCount, 0);
    atomic_set(&hChan->recvTXflagCount, 0);
    atomic_set(&hChan->txQChipFull, 0);
    atomic_set(&vChan->txChanBufHead, 0);
    atomic_set(&vChan->txChanBufTail, 0);

    DEBUGPRINT(8, "******Bus OFF called on ch %d\n", cmd.channel);
    ret = hwIfWaitResponse(vChan->vCard, (lpcCmd*)&cmd, (lpcCmd*)&resp, CMD_STOP_CHIP_RESP, cmd.transId);
    if (ret) return ret;
    
    vChan->isOnBus = 0;
    vChan->overrun = 0;

    return VCAN_STAT_OK;

} // hwIfBusOff 


//======================================================================
//  Clear send buffer on card                                           
//======================================================================
int hwIfFlushSendBuffer (VCanChanData *vChan)
{
    int ret;
    cmdFlushQueue cmd;

    LapcanChanData *hChan  = vChan->hwChanData;
    atomic_set(&hChan->sentTXflagCount, 0);
    atomic_set(&hChan->recvTXflagCount, 0);
    atomic_set(&hChan->txQChipFull, 0);

    atomic_set(&vChan->txChanBufHead, 0);
    atomic_set(&vChan->txChanBufTail, 0);

    cmd.cmdLen = CMDLEN(cmd);
    cmd.cmdNo = CMD_FLUSH_QUEUE;
    cmd.transId = vChan->channel;
    cmd.channel = vChan->channel;
    cmd.flags = FLUSH_TX_QUEUE;

    ret = hwIfQCmd(vChan->vCard, (lpcCmd*)&cmd, LAPCAN_Q_CMD_WAIT_TIME);

    return ret;
} // hwIfFlushSendBuffer 


//======================================================================
//  Read transmit error counter                                         
//======================================================================
int hwIfGetTxErr(VCanChanData *vChan) 
{
    hwIfRequestChipState(vChan);
    return vChan->txErrorCounter;
}


//======================================================================
//  Read transmit error counter                                         
//======================================================================
int hwIfGetRxErr(VCanChanData *vChan) 
{
    hwIfRequestChipState(vChan);
    return vChan->rxErrorCounter;
}


//======================================================================
//  Read receive queue length in hardware/firmware
//======================================================================
unsigned long hwIfHwRxQLen(VCanChanData *vChan) 
{
    return getQLen(atomic_read(&vChan->txChanBufHead),
        atomic_read(&vChan->txChanBufTail), TX_CHAN_BUF_SIZE);
}


//======================================================================
//  Read transmit queue length in hardware/firmware                     
//======================================================================
unsigned long hwIfHwTxQLen(VCanChanData *vChan) 
{
    LapcanChanData *hChan = vChan->hwChanData;    
    return atomic_read(&hChan->sentTXflagCount) - atomic_read(&hChan->recvTXflagCount);
}


//======================================================================
//  Disable Card                                                        
//======================================================================
int hwIfResetCard (VCanCardData *vCard)
{
    // qqq
    return VCAN_STAT_OK;
} // hwIfResetCard 


//======================================================================
//    hwIfGetStat - Return some statusinformation                     
//======================================================================
#if 0
int hwIfGetStat (CanIfStat *userStatPtr, struct file *filp)
{
    unsigned int      msgCount = 0;
    unsigned int      tmpTail;
    int               channelNr;
    unsigned short    size;
    unsigned char     cmdNr;
    CanIfStat         status;
    VCanOpenFileNode  *fileNodePtr = (VCanOpenFileNode*) (filp->private_data);
    VCanChanData      *vChan = fileNodePtr->chanData;

    copy_from_user_ret(&status, userStatPtr, sizeof(status.statSize), -EFAULT);
    size = status.statSize;
    fileNodePtr = tmpTail = fileNodePtr->rcvBufTail;
    
    while (fileNodePtr->rcvBufHead != tail) {
        cmdNr = getCmdNr(&fileNodePtr -> fileRcvBuffer[tail]);
        if (cmdNr == CMD_RX_STD_MESSAGE || cmdNr == CMD_RX_EXT_MESSAGE) msgCount++;
        tail++;
        if (tail >= FILE_RCV_BUF_SIZE) tail = 0;
    }
    status.rcvQL = msgCount;

    channelNr = fileNodePtr -> chanNr;
    if (channelNr == -1) return -EINVAL;
    status.sendQL = getQLen(ctxPtr->sentTXflagCount [channelNr], 
                            ctxPtr->recvTXflagCount [channelNr],
                            1 << sizeof(int));
    status.overruns = fileNodePtr -> overruns [channelNr];
    copy_to_user_ret(userStatPtr, &status, size, -EFAULT);
    return 0;
}
#endif


//======================================================================
// Request send                                                         
//======================================================================
int hwIfRequestSend (VCanCardData *vCard, VCanChanData *vChan)
{
    LapcanCardData *hCard = vCard->hwCardData;
    if(hCard->fifoCount == 0 && vChan->isOnBus)
    {
        os_if_queue_task(&hCard->txTaskQ);
    }
    return 0;
}


//======================================================================
// Queue  command for delivery                                          
//======================================================================
int hwIfQCmd (VCanCardData *vCard, lpcCmd *cmd, unsigned int timeout)
{
    LapcanCardData *hCard = vCard->hwCardData;
    OS_IF_WAITQUEUE wait;

    os_if_init_waitqueue_entry(&wait);
    os_if_add_wait_queue(&hCard->txCmdWaitQ, &wait);

    // Sleep when buffer is full and timeout > 0
    while(1) {
        if (getQLen((unsigned long)hCard->txCmdBufHead, (unsigned long)hCard->txCmdBufTail, (unsigned long)TX_CMD_BUF_SIZE) >= TX_CMD_BUF_SIZE - 1) {
            // Do we want a timeout ?
            if (timeout == 0){
                os_if_remove_wait_queue(&hCard->txCmdWaitQ, &wait);         
                return -EAGAIN;
            } else {
                os_if_set_task_interruptible();
                
                //
                //if(!signal_pending(current)) {
                //
                if (os_if_wait_for_event_timeout(timeout*HZ/1000, &wait) == 0) {
                    // Sleep was interrupted by timer
                    // set task running?
                    os_if_remove_wait_queue(&hCard->txCmdWaitQ, &wait);         
                    return -EAGAIN;
                }
                //
                /*
                continue;
                }
                os_if_remove_wait_queue(&hCard->txCmdWaitQ, &wait);
                return -ERESTARTSYS;
                */
                //
            }
            //
            
            if(signal_pending(current)){
                os_if_remove_wait_queue(&hCard->txCmdWaitQ, &wait);
                return -ERESTARTSYS;
            }
            continue;
            
            //
        } else {
            // Get a pointer to the right bufferspace 
            // Lock needed for SMP 
            lpcCmd *bufCmdPtr = (lpcCmd*)&hCard->txCmdBuffer[hCard->txCmdBufHead];

            copyLapcanCmd(bufCmdPtr, (lpcCmd*)cmd);
            hCard->txCmdBufHead++;
            if (hCard->txCmdBufHead >= TX_CMD_BUF_SIZE)
                hCard->txCmdBufHead = 0;
            //os_if_set_task_running(); // QQQ
            os_if_remove_wait_queue(&hCard->txCmdWaitQ, &wait);
            
            break;
        }
    }

    if (hCard->fifoCount == 0) {
        os_if_queue_task(&hCard->txTaskQ);
    }
    return VCAN_STAT_OK;
}




//======================================================================
// compose msg and transmit                                         
//======================================================================
int hwIfPrepareAndTransmit(VCanChanData *vChan, CAN_MSG *m)
{
    LapcanChanData *hChan = vChan->hwChanData;
    cmdTxExtMessage tmpMsg;
    cmdTxStdMessage *stdPtr = (cmdTxStdMessage *)(&tmpMsg);
    cmdTxExtMessage *extPtr = &tmpMsg;

    // Extended CAN 
    if (m->id & VCAN_EXT_MSG_ID) { 
        extPtr->cmdAndLen = CMDLEN_TX_EXT_MESSAGE_CMDANDLEN(m->length);
        extPtr->channel_and_dlc = ((vChan->channel) << 4) + m->length;
        extPtr->flags = 0;
        if (m->flags & VCAN_MSG_FLAG_ERROR_FRAME) extPtr->flags |= MSGFLAG_ERROR_FRAME;
        if (m->flags & VCAN_MSG_FLAG_REMOTE_FRAME) extPtr->flags |= MSGFLAG_REMOTE_FRAME;
        if (m->flags & VCAN_MSG_FLAG_TXRQ) extPtr->flags |= MSGFLAG_TXRQ;
        extPtr->flags |= MSGFLAG_TX;
        extPtr->transId = m->user_data;
        extPtr->id = m->id & ~VCAN_EXT_MSG_ID;
        // Copy messagedata 
        memcpy(extPtr->data, m->data, m->length);
    }
    // standard can
    else {

        stdPtr->cmdAndLen = CMDLEN_TX_STD_MESSAGE_CMDANDLEN(m->length);
        stdPtr->channel_and_dlc = ((vChan->channel) << 4) + m->length;
        stdPtr->flags = 0;
        if (m->flags & VCAN_MSG_FLAG_ERROR_FRAME) stdPtr->flags |= MSGFLAG_ERROR_FRAME;
        if (m->flags & VCAN_MSG_FLAG_REMOTE_FRAME) stdPtr->flags |= MSGFLAG_REMOTE_FRAME;
        if (m->flags & VCAN_MSG_FLAG_TXRQ) stdPtr->flags |= MSGFLAG_TXRQ;
        stdPtr->flags |= MSGFLAG_TX;
        stdPtr->transId = m->user_data;
        stdPtr->id = m->id & ~VCAN_EXT_MSG_ID;
        memcpy(stdPtr->data, m->data, m->length);        
    }
    
    hwIfTransmit(vChan->vCard, (lpcCmd*)&tmpMsg);

    atomic_add(1, &hChan->sentTXflagCount);

    return VCAN_STAT_OK;
}


//======================================================================
// Process send Q - This function is called from the immediate queue    
//======================================================================
static void hwIfSend (void *voidVCard)
{
    static int      chanNr;
    int             i;
    VCanCardData    *vCard = voidVCard;
    LapcanCardData  *hCard = vCard->hwCardData;
    VCanChanData    *vChan;
    LapcanChanData  *hChan;

    if (hCard->stop == 1) {
        return;
    }

    
    // Do we have any cmd to send
    if (getQLen(hCard->txCmdBufHead, hCard->txCmdBufTail, TX_CMD_BUF_SIZE) != 0) {

        //DEBUGPRINT(8, "Send cmd nr %d from %d\n", 
        //      getCmdNr(&hCard->txCmdBuffer [hCard->txCmdBufTail]),
        //      hCard->txCmdBufTail);

        hwIfTransmit(vCard, &hCard->txCmdBuffer [hCard->txCmdBufTail]);
        if (++(hCard->txCmdBufTail) >= TX_CMD_BUF_SIZE)
            hCard->txCmdBufTail = 0;
        os_if_wake_up_interruptible(&hCard->txCmdWaitQ);
        // Nothing to do except wait for the card to respond
        return;
    }
    // Process the channel queues (send can-messages)

    for (i = 0; i < vCard->nrChannels; i++){

        // Alternate between channels
        chanNr++;
        if (chanNr >= vCard->nrChannels)
            chanNr = 0; 
        vChan = vCard->chanData[chanNr];
        hChan = vChan->hwChanData;
        // Test if queue is empty or Lapcan has sent "queue high"
        if (getQLen(atomic_read(&vChan->txChanBufHead), atomic_read(&vChan->txChanBufTail),
            TX_CHAN_BUF_SIZE) == 0 || atomic_read(&hChan->txQChipFull)) {
            continue;
        }
        hwIfPrepareAndTransmit(vChan, &(vChan->txChanBuffer[atomic_read(&vChan->txChanBufTail)]));

        // ??? Multiple CPU race when flushing txQ
        atomic_add(1, &vChan->txChanBufTail);
        if ((atomic_read(&vChan->txChanBufTail)) >= TX_CHAN_BUF_SIZE)
            atomic_set(&vChan->txChanBufTail, 0);
        
        os_if_wake_up_interruptible(&vChan->txChanWaitQ);
        return;
    }
    
    // Can't send anything right now
    return;
}


//======================================================================
// HwIf interrupt handler                                             
//======================================================================
OS_IF_INTR_HANDLER hwIfInterrupt (int irq, void *dev_id, struct pt_regs *regs)
{
    unsigned short    lsrAddr;
    unsigned short    fifoAddr;
    unsigned char     iir;
    unsigned char     msr;
    unsigned char     mcr;
    unsigned char     didService;
    int               cmdLen;
    int               i;
    VCanCardData      *vCard;
    LapcanCardData    *hCard; 
    unsigned long     irqFlags = 0;    
    int               maxLoop   = 5000;
    int               oneFlag   = 0;
    
    os_if_irq_save(&driver_lock, irqFlags);
    os_if_spin_lock(&interrupt_lock);
    
    vCard = dev_id;
    hCard = vCard->hwCardData;

    if (hCard->stop == 1) {
        os_if_spin_unlock(&interrupt_lock);
        os_if_irq_restore(&driver_lock, irqFlags);
        return IRQ_HANDLED;
    }
    if (os_if_read_port(hCard->portAddress+TLH_IIR) & 0x30) {
        hCard->stop = 1;
        os_if_spin_unlock(&interrupt_lock);
        os_if_irq_restore(&driver_lock, irqFlags);
        return IRQ_HANDLED;
    }
    
    
    didService = 0;
    lsrAddr    = hCard->portAddress+TLH_LSR;
    fifoAddr   = hCard->portAddress+TLH_FIFO;

    // Read interrupt id. register until cleared
    while (!((iir = os_if_read_port(hCard->portAddress+TLH_IIR))  & IIR_NOINTPEND)) {
        didService = 1;
        //DEBUGPRINT(8, "lapcanInterrupt - iir=%x\n",iir);
        maxLoop--;
        if (maxLoop <= 0) {
            DEBUGPRINT(1," max_loop\n");
            break;
        }

        switch (iir & IIR_INTID_MASK) {
            case INT_RCV_DATA: // Receive standard mode
                if (!(os_if_read_port(lsrAddr) & LSR_DATA_READY)) {
                    oneFlag = 1; // There is one char to read
                }
                
                while (oneFlag || (os_if_read_port(lsrAddr) & LSR_DATA_READY)) {
                    maxLoop--;
                    if (maxLoop <= 0) {
                        DEBUGPRINT(1," max_loop in standard receive\n");
                        hCard->curCmdLen = 0;
                        hCard->curCmdPos = 0;
                        break;
                    }
                    if (hCard->curCmdLen == 0) {
                        // We have a new packet incoming...read first byte to be able to determine
                        // packet length. The packet is read into a circular buffer(mainRcvBuffer)                                                           

                        hCard->mainRcvBuffer[hCard->mainRcvBufHead].b [0] = os_if_read_port(fifoAddr);
                        hCard->curCmdLen = getCmdLen(&hCard->mainRcvBuffer[hCard->mainRcvBufHead]);
                        if (hCard->curCmdLen >= sizeof(lpcCmd)) { // out of sync ?
                            DEBUGPRINT(1,"FATAL: Out of sync, data lost\n");
                            hCard->curCmdLen = 0;
                            hCard->curCmdPos = 0;
                            break;     
                        }
                        // Store in receive buffer at position hCard->mainRcvBufHead
                        hCard->curCmdPos = 1;
                        oneFlag = 0;
                    }

                    // read bytes from fifo
                    while (oneFlag || (os_if_read_port(lsrAddr) & LSR_DATA_READY)) {
                        maxLoop--;
                        if (maxLoop <= 0) {
                            DEBUGPRINT(1," max_loop == 0 in lapcanInterrupt (while222)\n");
                            hCard->curCmdLen = 0;
                            hCard->curCmdPos = 0;
                            break;
                        }

                        oneFlag = 0;
                        // get data to receive buffer
                        hCard->mainRcvBuffer [hCard->mainRcvBufHead].b[hCard->curCmdPos] = 
                            os_if_read_port(fifoAddr);

                        if (++(hCard->curCmdPos) == hCard->curCmdLen) {
                            // We're ready
                            (hCard->mainRcvBufHead)++;
                            if (hCard->mainRcvBufHead >= MAIN_RCV_BUF_SIZE) { 
                                hCard->mainRcvBufHead = 0;
                            }
                            // Handle the message
                            hwIfReceive(vCard);
                            hCard->curCmdLen = 0;
                            break;
                        } // event complete

                        if (hCard->curCmdPos >= sizeof(lpcCmd)) { // out of synch ?
                            DEBUGPRINT(1,"FATAL: Out of sync, data lost\n");
                            hCard->curCmdLen = 0;
                            hCard->curCmdPos = 0;
                            break; // restart
                        }
                    } // while LSR_DATA_READY
                } // while LSR_DATA_READY

                break;
            case INT_TX_HOLD_REG_EMPTY:
                // @@@@ TODO not needed but perhaps conservative?
                //      hCard->fifoCount = 0;
                //printk("<1>TX_HOLD_REG_EMPTY\n");
                break;
            case INT_MODEM_STATUS:
                // Read modem status register / clear interrupt
                msr = os_if_read_port(hCard->portAddress+TLH_MSR);
                if (msr & MSR_DELTA_DSR) {
                    // The subsystem has toggled DSR. i.e. it has read the packet
                    // we sent last time. Enable sending a new one.
                    hCard->fifoCount = 0;
                    if(!os_if_queue_task(&hCard->txTaskQ))
                        ;
                }

                if ((hCard->useDCD) && (msr & MSR_DELTA_DCD)) {
                    // New handshake
                    // Loop. Check if first byte is 0 => packets to read
                    while (!os_if_read_port(fifoAddr)){
                        
                        hCard->mainRcvBuffer[hCard->mainRcvBufHead].b[0] = os_if_read_port(fifoAddr);
                        cmdLen = getCmdLen(&hCard->mainRcvBuffer[hCard->mainRcvBufHead]);
                        // Loop. Read data into buffer
                        for(i = 1; i < cmdLen; i++){
                            hCard->mainRcvBuffer[hCard->mainRcvBufHead].b[i] = os_if_read_port(fifoAddr);
                        }
                        // We have read one packet, increase bufferhead
                        (hCard->mainRcvBufHead)++;
                        if (hCard->mainRcvBufHead >= MAIN_RCV_BUF_SIZE) hCard->mainRcvBufHead = 0;
                        // Handle message
                        hwIfReceive(vCard);
                    }
                    // Toggle OUT2 so the card can send more data
                    os_if_write_port(os_if_read_port(hCard->portAddress+TLH_MCR) ^ MCR_OUT2, hCard->portAddress+TLH_MCR);
                } 
                break;
        }
    }
    if (didService && !hCard->useDCD) {
        // reset RTS for auto-RTS feature
        mcr = os_if_read_port(hCard->portAddress+TLH_MCR);
        if (!(mcr & MCR_RTS)) {
            os_if_write_port(mcr |MCR_RTS, hCard->portAddress+TLH_MCR);
        }
    }
    os_if_irq_restore(&driver_lock, irqFlags);
    os_if_spin_unlock(&interrupt_lock);
    return IRQ_HANDLED;
}


//======================================================================
// Handle incoming lapcan commands                                      
//======================================================================
void hwIfReceive (VCanCardData *vCard)
{
    lpcCmd          *cmdPtr;
    LapcanCardData  *hCard = vCard->hwCardData;
    VCanChanData    *vChan;
    LapcanChanData  *hChan;
    VCAN_EVENT      e;
    unsigned char   *b;
    unsigned char   hasOverrun;
    unsigned char   cmdNo = 0;
    unsigned long     irqFlags = 0; 

    // Concurrent test
    if (os_if_is_rec_busy(0, &hCard->recIsBusy))
    {
        return;
    }
    while (hCard->mainRcvBufHead != hCard->mainRcvBufTail){
        hasOverrun = 0;
        cmdPtr = &(hCard->mainRcvBuffer [hCard->mainRcvBufTail]);
        vChan = vCard->chanData[getCmdChan(cmdPtr)];
        hChan = vChan->hwChanData;
        b = (unsigned char*) cmdPtr;
        
        // Check highest bit
        cmdNo = getCmdNr(cmdPtr);

        // Infrequent command:
        if (b [0] & 0x80) { 
            switch(cmdNo) {
                case CMD_CHIP_STATE_EVENT:
                    if (cmdPtr->chipStateEvent.busStatus & BUSSTAT_BUSOFF){
                        atomic_set(&hChan->txQChipFull, 0);
                        if (hCard->fifoCount == 0) {
                            os_if_queue_task(&hCard->txTaskQ);
                        }
                        
                        atomic_set(&hChan->sentTXflagCount, 0);
                        atomic_set(&hChan->recvTXflagCount, 0);
                    }
                    
                    vChan->rxErrorCounter = cmdPtr->chipStateEvent.rxErrorCounter;
                    vChan->txErrorCounter = cmdPtr->chipStateEvent.txErrorCounter;

                    DEBUGPRINT (8,"CMD_CHIP_STATE_EVENT channel=%d, busStatus=%d,txerr=%d, rxerr=%d , time=%d\n"
                        , cmdPtr->chipStateEvent.channel, cmdPtr->chipStateEvent.busStatus, cmdPtr->chipStateEvent.txErrorCounter,
                        cmdPtr->chipStateEvent.rxErrorCounter, cmdPtr->chipStateEvent.time);
                    break;
                case CMD_GET_CHIP_STATE_RESP:
                    DEBUGPRINT (8,"CMD_CHIP_STATE_RESP channel=%d, busStatus=%d,txerr=%d, rxerr=%d\n"
                        , cmdPtr->getChipStateResp.channel, cmdPtr->getChipStateResp.busStatus,  cmdPtr->getChipStateResp.txErrorCounter,
                        cmdPtr->getChipStateResp.rxErrorCounter);
                    vChan->rxErrorCounter = cmdPtr->getChipStateResp.rxErrorCounter;
                    vChan->txErrorCounter = cmdPtr->getChipStateResp.txErrorCounter;

                case CMD_GET_BUSPARAMS_RESP:
                    DEBUGPRINT (8,"BUSPARAMS RESP chan=%u, bitRate=%lu, tseg1=%u, tseg2=%u, sjw=%u, nosamp=%d\n", 
                        cmdPtr->getBusparamsResp.channel, cmdPtr->getBusparamsResp.bitRate, 
                        cmdPtr->getBusparamsResp.tseg1, cmdPtr->getBusparamsResp.tseg2, 
                        cmdPtr->getBusparamsResp.sjw, cmdPtr->getBusparamsResp.noSamp);
                    break;
                case CMD_GET_CARD_INFO_RESP:
                    DEBUGPRINT (8,"CMD_GET_CARD_INFO_RESP RESP");
                    hCard->hwRevision = cmdPtr->getCardInfoResp.hwInfo & 0x0f; // Revision;
                    hCard->clockUsPerTick = cmdPtr->getCardInfoResp.clockResolution;
                    hCard->clockHz = 1000000 / hCard->clockUsPerTick;
                    hCard->hwInfoIsValid = 1;
                    break;
                case CMD_GET_SOFTWARE_INFO_RESP:
                    DEBUGPRINT (8,"CMD_GET_SOFTWARE_INFO_RESP RESP");         
                    hCard->swRevision =  (cmdPtr->getSoftwareInfoResp.applicationVersion [0] - '0') << 20;
                    hCard->swRevision += (cmdPtr->getSoftwareInfoResp.applicationVersion [1] - '0') << 16;
                    hCard->swRevision += (cmdPtr->getSoftwareInfoResp.applicationVersion [2] - '0') << 12;
                    hCard->swRevision += (cmdPtr->getSoftwareInfoResp.applicationVersion [3] - '0') <<  8;
                    hCard->swRevision += (cmdPtr->getSoftwareInfoResp.applicationVersion [4] - '0') <<  4;
                    hCard->swRevision += (cmdPtr->getSoftwareInfoResp.applicationVersion [5] - '0');
                    if ((cmdPtr->b[0] & 0x7F) > offsetof(cmdGetSoftwareInfoResp, swOptions) - 1) {
                        hCard->swOptions = cmdPtr->getSoftwareInfoResp.swOptions;
                        DEBUGPRINT (5, "swOptions=%u\n", cmdPtr->getSoftwareInfoResp.swOptions);
                    } else {
                        // There's no swOptions field! (The firmware is too old.)
                        hCard->swOptions = 0;
                    }
                    hCard->swInfoIsValid = 1;
                    break;
                case CMD_ERROR_EVENT:
                    if (cmdPtr->errorEvent.errorCode == LAPERR_QUEUE_LEVEL){
                        // Channelnr is in errorEvent.addInfo1
                        hChan = vCard->chanData[cmdPtr->errorEvent.addInfo1]->hwChanData;
                        if (cmdPtr->errorEvent.addInfo2 == TRANSMIT_QUEUE_HIGH){        
                            atomic_set(&hChan->txQChipFull, 1);
                        } 
                        else if (cmdPtr->errorEvent.addInfo2 == TRANSMIT_QUEUE_LOW){
                            if (atomic_read(&hChan->txQChipFull)){
                                // We previously had a full queue, try to send again 
                                if (hCard->fifoCount == 0) {
                                    os_if_queue_task(&hCard->txTaskQ);
                                }
                            }
                            atomic_set(&hChan->txQChipFull, 0);
                        }
                    }
                    break;
                case CMD_CLOCK_OVERFLOW_EVENT:
                    os_if_irq_save(&timeHi_lock, irqFlags);
                    vCard->timeHi = cmdPtr->clockOverflowEvent.currentTime & 0xffff0000;
                    os_if_irq_restore(&timeHi_lock, irqFlags);
                    break;
                case CMD_START_CHIP_RESP:
                    DEBUGPRINT(8,"CMD_START_CHIP_RESP ch %d\n", cmdPtr->startChipResp.channel);
                    break;
                    
                case CMD_STOP_CHIP_RESP:
                  DEBUGPRINT(8,"CMD_STOP_CHIP_RESP ch %d\n", cmdPtr->stopChipResp.channel);
                  break;

                default:
                    //DEBUGPRINT(1,"InFreq Msg cmd=%d, ptr=%d, chan =%d\n", cmdNo, hCard->mainRcvBufTail
                    //      , getCmdChan(cmdPtr));
                    break;
            }

            {
#if LINUX

                // Copy command and wakeup those who are waiting for this reply 
                struct list_head *currHead, *tmpHead;
                LapcanWaitNode *currNode;

                unsigned long flags;
                read_lock_irqsave(&hCard->replyWaitListLock, flags);

                list_for_each_safe(currHead, tmpHead, &hCard->replyWaitList) {
                    currNode = list_entry(currHead, LapcanWaitNode, list); 
                    if (currNode->cmdNr == cmdNo && getTransId(cmdPtr) == currNode->transId) {
                        copyLapcanCmd(currNode->replyPtr, cmdPtr);
                        os_if_up_sema(&currNode->waitSemaphore);                       
                    }
                }
#else
#           pragma message("qqq add stuff here...")
#endif
                
#if LINUX
                read_unlock_irqrestore(&hCard->replyWaitListLock, flags);
#else
#           pragma message("qqq add stuff here...")
#endif
            }
        }
        // frequent command
        else {  
            unsigned long id;
            unsigned char dlc;
            unsigned char flags;
            unsigned char  transId;
            unsigned short timeLo;
            unsigned char *data;
            //int counter = 0;

            // Extract information
            hasOverrun = getFlags(cmdPtr) & MSGFLAG_OVERRUN;
            // Is this an echoed message ?
            if (getFlags(cmdPtr) & MSGFLAG_TX) {
                atomic_add(1, &hChan->recvTXflagCount);
                
                if (atomic_read(&hChan->sentTXflagCount) == atomic_read(&hChan->recvTXflagCount)) {
                    // Wake up those who are waiting for all sending to finish
                    // Are there more in queue?

// qqq this was added to avoid unnecessary wake-ups but for lapcan and
// kernel 2.4 this seems to fail (though very rarely) when calling
// canWriteSync
#ifdef LINUX_2_6
                    if((getQLen(atomic_read(&vChan->txChanBufHead), atomic_read(&vChan->txChanBufTail), TX_CHAN_BUF_SIZE) == 0) && atomic_read(&vChan->waitEmpty))
#else
                    if((getQLen(atomic_read(&vChan->txChanBufHead), atomic_read(&vChan->txChanBufTail), TX_CHAN_BUF_SIZE) == 0))
#endif                        
                    {
                        atomic_set(&vChan->waitEmpty, 0);
                        os_if_wake_up_interruptible(&vChan->flushQ);
                    }
                }
            }

            switch (cmdNo){
                case CMD_RX_STD_MESSAGE:
                    id      = cmdPtr->rxStdMessage.id;
                    dlc     = cmdPtr->rxStdMessage.channel_and_dlc & 0x0F; // Bit 0..3 is dlc 
                    timeLo  = cmdPtr->rxStdMessage.time;
                    flags   = cmdPtr->rxStdMessage.flags;
                    data    = cmdPtr->rxStdMessage.data;
                    transId = cmdPtr->rxStdMessage.transId;
                    break;
                case CMD_RX_EXT_MESSAGE:
                    id      = cmdPtr->rxExtMessage.id | VCAN_EXT_MSG_ID;
                    dlc     = cmdPtr->rxExtMessage.channel_and_dlc & 0x0F; // Bit 0..3 is dlc 
                    timeLo  = cmdPtr->rxExtMessage.time;
                    flags   = cmdPtr->rxExtMessage.flags;
                    data    = cmdPtr->rxExtMessage.data;
                    transId = cmdPtr->rxExtMessage.transId;                    
                    break;
                default:
                    // Unknown command 
                    if (++hCard->mainRcvBufTail >= MAIN_RCV_BUF_SIZE) hCard->mainRcvBufTail = 0;
                    continue;
            }
            
            // Fill in event 
            e.tag = V_RECEIVE_MSG;
            e.transId = transId;
            e.tagData.msg.id = id;
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
            e.tagData.msg.dlc = dlc;
            // never copy more than 8 bytes
            memcpy(e.tagData.msg.data, data, dlc > 8? 8: dlc);
            e.timeStamp = hwIfTimeStamp(vChan->vCard, timeLo);
            vCanDispatchEvent(vChan, &e);            
        }

        if (++hCard->mainRcvBufTail >= MAIN_RCV_BUF_SIZE) { 
            hCard->mainRcvBufTail = 0;
        }        
    }
    os_if_rec_not_busy(0, &hCard->recIsBusy);

    return;
} // hwIfReceive


//======================================================================
//    Check for LAPcan presence                                         
//======================================================================
static int hwIfVerify(VCanCardData *vCard)
{
    LapcanCardData *hCard = vCard->hwCardData;

    if ((os_if_read_port(hCard->portAddress+2) & 0x30) != 0 || // Bit 2.4,2.5 should be zero
          (os_if_read_port(hCard->portAddress+4) & 0xc0) != 0)
    {
        vCard->cardPresent = 0;
        return -1;
    } else {
        vCard->cardPresent = 1;
        return 0;
    }
}


//======================================================================
//    Init datastructures                                               
//======================================================================
static int hwIfInitData(VCanCardData *vCard)
{
#if LINUX
    int             chNr;
    LapcanCardData  *hCard;

    // new
    driver_lock    = SPIN_LOCK_UNLOCKED;
    devList_lock   = RW_LOCK_UNLOCKED;
    timeHi_lock    = SPIN_LOCK_UNLOCKED;
    interrupt_lock = SPIN_LOCK_UNLOCKED;

    hCard = vCard->hwCardData;
    vCard->nrChannels       = 2;
    hCard->portAddress      = hCard->link.io.BasePort1;
    os_if_init_task(&hCard->txTaskQ, &hwIfSend, vCard);

    // Init waitqueues 
    os_if_init_waitqueue_head(&hCard->txCmdWaitQ);
    INIT_LIST_HEAD(&hCard->replyWaitList);

    hCard->replyWaitListLock = RW_LOCK_UNLOCKED;
    vCanInitData(vCard);

    for (chNr = 0; chNr < vCard->nrChannels; chNr++){
        VCanChanData *vChan = vCard->chanData[chNr];
        LapcanChanData *hChd = vChan->hwChanData;
        
        // Init data structures for the channel
        // The dev_node_t structures are initialized
        sprintf(hChd->node.dev_name, "lapcan%d", vChan->minorNr);
        hChd->node.major = driverData.majorDevNr; 
        hChd->node.minor = vChan->minorNr;

        // ...and arranged in a linked list at link.dev
        if (chNr == 0) hCard->link.dev = &hChd->node;
        if (chNr == (vCard->nrChannels - 1)) hChd->node.next = NULL;
        else {
            LapcanChanData *next = vCard->chanData[chNr + 1]->hwChanData;
            hChd->node.next = &next->node;
        }
    }    
    return 0;
#else
    // PCARD struct
    // c_CANRegPtr is the register address
#           pragma message("qqq hwIfInitData not impl...")
#if PCMCIA_DEBUG
    static int ft = 1;
    if (ft) {
        if ((g_out = fopen("debug_lapcan.txt", "wt")) == NULL) {
        }
        PRINTF(("**************************************************\n"));
        PRINTF(("***************  START DRIVER ********************\n"));
        PRINTF(("**************************************************\n"));
        ft = 0;
    }
#endif
    return 0;
#endif
}


//======================================================================
//  Initialize the card                                                 
//======================================================================
static int switchHandshake (VCanCardData *vCard) 
{
    LapcanCardData *hCard = vCard->hwCardData;
    lpcCmd cmd;

    cmd.setGlobalOptionsReq.cmdLen      = (sizeof(cmdSetGlobalOptionsReq)-1) | 0x80;
    cmd.setGlobalOptionsReq.cmdNo       = CMD_SET_GLOBAL_OPTIONS_REQ;
    cmd.setGlobalOptionsReq.optionsMask = GLOPT_USE_DCD_PROTOCOL;
    cmd.setGlobalOptionsReq.optionsCode = GLOPT_USE_DCD_PROTOCOL;
    hwIfTransmit(vCard, &cmd);
    if (waitTransmit(vCard) == 0) {
        hCard->useDCD = 1;
        hCard->useOUT1Toggle = 1;
        // Enable only modem status interrupts
        os_if_write_port(IER_EDSSI, hCard->portAddress+TLH_IER);
        return 0;
    }
    else {
        return -1;
    }
}


//======================================================================
// Report card services errors                                          
//======================================================================
#if LINUX
void cs_error(client_handle_t handle, int func, int ret)
{
    error_info_t err = { func, ret };
    os_if_serv_report_error(handle, &err);
}
#endif


//======================================================================
//  Wait until fifo is avalable                                         
//======================================================================
int waitTransmit (VCanCardData *vCard) 
{
    LapcanCardData *hCard = vCard->hwCardData;
    // jiffies equals nr of timer interrupts since boot 
    unsigned long int timeOut= os_if_get_timeout_time();

    for (;;) {
        if (hCard->fifoCount == 0)
            return 0;
        if (OS_IF_TICK_COUNT >= timeOut) {
            break;
        }
    }
    return -1;
}


//======================================================================
//  Init card & start firmware application                              
//======================================================================
int hwIfInit(VCanCardData *vCard)
{
    LapcanCardData *hCard = vCard->hwCardData;

    lpcCmd cmd;
    long int timeOut;
    int error;
    int i;
    unsigned char transId = 0;

    if (vCard->cardPresent) {
        // There is no way to reset the card, so we start directly by
        // setting up the FIFO.

        os_if_write_port(0x00,hCard->portAddress+TLH_FCR);              // FIFO disable
        os_if_write_port(FCR_FIFO_ENABLE,hCard->portAddress+TLH_FCR);   // FIFO enable

        // Assign the Divisor Latch
        os_if_write_port(LCR_DLAB,hCard->portAddress+TLH_LCR);
        os_if_write_port(64,hCard->portAddress+TLH_DLL);
        os_if_write_port(0,hCard->portAddress+TLH_DLM);

        // Clear the DLAB-bit to get access to the FIFO
        os_if_write_port(0,hCard->portAddress+TLH_LCR); // Clear bit LCR_DLAB
        // Enable and reset the FIFOs (for 64 byte operation), trigger level 1 byte
        os_if_write_port(
             FCR_FIFO_ENABLE|    // FIFO Enable
             FCR_RXFIFO_RESET|   // FIFO Reset
             FCR_TXFIFO_RESET|
             FCR_AUTO_RTS|       // auto-RTS for '564
             FCR_FDEPTH64,hCard->portAddress+TLH_FCR);      // 64 bytes FIFO, triggerlevel 1

        // Assert RTS# and OUT1#.
        os_if_write_port(MCR_RTS,hCard->portAddress+TLH_MCR);

        // Enable all Interrupts
        os_if_write_port(IER_EDSSI | IER_ERBI | IER_ETBEI | IER_ELSI,hCard->portAddress+TLH_IER);

        // for security empty the FIFO, to get an interrupt
        while ((os_if_read_port(hCard->portAddress+TLH_LSR) & 1)) {
            os_if_read_port(hCard->portAddress+TLH_FIFO);
        }

        // Get some information
        if (hCard->isInit)
            return 0;
        
    
        cmd.startApplicationReq.cmdLen        = (sizeof(cmdStartApplicationReq)-1) | 0x80;
        cmd.startApplicationReq.cmdNo         = CMD_START_APPLICATION_REQ;
        cmd.startApplicationReq.application   = APP_SHUFFLE;
        cmd.startApplicationReq.startAddress  = 0L;
        cmd.startApplicationReq.p1            = 0;
        cmd.startApplicationReq.p2            = 0;
        hwIfTransmit(vCard, &cmd);

        // Wait and let other processes run for 1/2 second
        os_if_set_task_interruptible();
        os_if_wait_for_event_timeout_simple(0.5 * HZ);
        

        cmd.getCardInfoReq.cmdLen    = (sizeof(cmdGetCardInfoReq)-1) | 0x80;
        cmd.getCardInfoReq.cmdNo     = CMD_GET_CARD_INFO_REQ;
        cmd.getCardInfoReq.transId   = transId++;
        cmd.getCardInfoReq.dataLevel = 0;
        if (waitTransmit(vCard)) {           
            return -1;
        }
        hwIfTransmit(vCard, &cmd);

        cmd.getSoftwareInfoReq.cmdLen  = (sizeof(cmdGetSoftwareInfoReq)-1) | 0x80;
        cmd.getSoftwareInfoReq.cmdNo   = CMD_GET_SOFTWARE_INFO_REQ;
        cmd.getSoftwareInfoReq.transId = transId++;
        if (waitTransmit(vCard)) {           
            return -1;
        }
        hwIfTransmit(vCard, &cmd);

        error = -1;
        timeOut = OS_IF_TICK_COUNT + 1 * HZ; // 1 sec ; the debug f/w needs some time to answer.
        for (;;) {
            if ((volatile int)(hCard->hwInfoIsValid) &&
                  (volatile int)(hCard->swInfoIsValid)) {
                error = 0;
                break;
            }
#if LINUX
            schedule();
#endif
            if (OS_IF_TICK_COUNT >= timeOut) {
                break;
            }
        }
        if (error) {
            DEBUGPRINT(1," Warning - Not initialized !!!!!!!!\n");        
            return error;
        }

        DEBUGPRINT(1,"  hwInfoIsValid = %u\n",hCard->hwInfoIsValid);
        DEBUGPRINT(1,"  swInfoIsValid = %u\n",hCard->swInfoIsValid);
        DEBUGPRINT(1,"  hwRevision = %u\n",hCard->hwRevision);
        DEBUGPRINT(1,"  swRevision = %u\n",hCard->swRevision);
        DEBUGPRINT(1,"  swOptions = %u\n",hCard->swOptions);
        DEBUGPRINT(1,"  clockResolution = %u\n",hCard->clockUsPerTick);

        // Use the DCD handshaking protocol
        if (hCard->swOptions & SWOPT_CAN_TOGGLE_DCD) {
            DEBUGPRINT(8, "call switchHandshake\n");
            if (switchHandshake(vCard) != 0) {
                return -1; // OK?
            }
        }

        DEBUGPRINT(1,"useDCD = %u\n",hCard->useDCD);
        DEBUGPRINT(1,"useOUT1Toggle = %u\n",hCard->useOUT1Toggle);

        if (!(hCard->swOptions & SWOPT_CAN_TOGGLE_DCD)) {
            DEBUGPRINT(1, "LAPCAN WARNING: Old firmware, please upgrade\n");
        }

        for (i=0;i<=1;i++) {
            DEBUGPRINT(3,"  CMD_SET_DRIVERMODE channel=%d\n",i);
            cmd.setDrivermodeReq.cmdLen = (sizeof(cmdSetDrivermodeReq)-1) | 0x80;
            cmd.setDrivermodeReq.cmdNo  = CMD_SET_DRIVERMODE_REQ;
            cmd.setDrivermodeReq.transId = transId++;
            cmd.setDrivermodeReq.channel = (unsigned char) i;
            cmd.setDrivermodeReq.driverMode = DRIVERMODE_NORMAL;
            if (waitTransmit(vCard)) {
                return -1;
            }
            hwIfTransmit(vCard, &cmd);
        }


        for (i=0;i<=1;i++) {
            DEBUGPRINT(3,"  CMD_RESET_CHIP channel=%d\n",i);
            cmd.resetChipReq.cmdLen = (sizeof(cmdResetChipReq)-1) | 0x80;
            cmd.resetChipReq.cmdNo  = CMD_RESET_CHIP_REQ;
            cmd.resetChipReq.transId = transId++;
            cmd.resetChipReq.channel = (unsigned char) i;
            if (waitTransmit(vCard)) {
                return -1;
            }
            hwIfTransmit(vCard, &cmd);
        }
        hCard->isInit = 1;  

        return 0;
    }
    else {
        return -1; // No card found
        DEBUGPRINT(8, "No card found\n");
    }
}


//======================================================================
// Send a lapcan command to the card                                    
//======================================================================
void hwIfTransmit (VCanCardData *vCard, lpcCmd *msg)
{
    unsigned int    len;
    unsigned int    i;
    unsigned char   *p     = msg->b;
    unsigned char   mcr;
    LapcanCardData  *hCard = vCard->hwCardData;
    unsigned short  addr   = hCard->portAddress+TLH_FIFO;

    if (hCard->fifoCount != 0) {    
        DEBUGPRINT(1, "hwIfTransmit: We shouldn't be here! \n");
        return; // We shouldn't be here! 
    }
    
    hCard->fifoCount = 1;
    len = getCmdLen(msg);

    // the actual sending
    for (i = 0; i < len; i++) {
        os_if_write_port(p [i],addr);
    }

    // we know we sent them...
    //atomic_add(1, &hChan->sentTXflagCount);

    
    // Tell the card there's something to read.
#if LINUX // not needed for wince??
    os_if_irq_disable(&driver_lock); // We dont want any interrupts while toggling 
#endif
    mcr = os_if_read_port(hCard->portAddress+TLH_MCR);
    if (hCard->useOUT1Toggle) {
        // Toggle OUT1#.
        os_if_write_port((unsigned char)(mcr ^ MCR_OUT1),hCard->portAddress+TLH_MCR);
    } 
    else {
        // Pulse OUT1#.
        mcr &= ~MCR_OUT1;
        // Write MCR twice to cope with platforms where the I/O cycles
        // are relatively fast. The CANcardX needs at least 500 ns
        // to safely detect a level change.
        os_if_write_port((unsigned char )(mcr | MCR_OUT1),hCard->portAddress+TLH_MCR);
        os_if_write_port((unsigned char )(mcr | MCR_OUT1),hCard->portAddress+TLH_MCR);
        os_if_write_port(mcr,hCard->portAddress+TLH_MCR);
    }
#if LINUX // not needed for wince??
    os_if_irq_enable(&driver_lock);
#endif
    return;
} 



// 
// qqq got some wierd error with real memset.
static void own_memset(void *vptr, int ch, int siz)
{
  
  int i;
  char *charPtr = (char *)vptr;
  for (i=0;i<siz;i++) {
    *charPtr++ = ch;
  }
} 



//======================================================================
// lapcanAttach() creates an "instance" of the driver, allocating        
// local data structures for one device.  The device is registered       
// with Card Services.                                                   
//                                                                      
// The dev_link structure is initialized, but we don't actually          
// configure the card at this point -- we wait until we receive a        
// card insertion event.                                               
//======================================================================
OS_IF_CARD_CONTEXT* hwIfAttach(void)
{
#if LINUX    
    typedef struct {
        VCanChanData *dataPtrArray[MAX_CHANNELS];
        VCanChanData vChan[MAX_CHANNELS];
        LapcanChanData hChd[MAX_CHANNELS];
    } ChanHelperStruct;

    ChanHelperStruct    *chs;
    VCanCardData        *vCard;
    LapcanCardData      *hCard;
    int                 chNr;
    OS_IF_CARD_CONTEXT  *link;
    client_reg_t        client_reg;
    int                 ret;
    int                 i;
    unsigned long       flags = 0;

    os_if_spin_lock(&canCardsLock);

    // Allocate data area for this card
    vCard  = kmalloc(sizeof(VCanCardData) + sizeof(LapcanCardData), GFP_KERNEL);
    if (!vCard) goto card_alloc_error;
    own_memset(vCard, 0, sizeof(VCanCardData) + sizeof(LapcanCardData));
    
    // hwCardData is directly after VCanCardData
    vCard->hwCardData = vCard + 1;    

    // Allocate memory for n channels
    chs = kmalloc(sizeof(ChanHelperStruct), GFP_KERNEL);
    if (!chs) goto chan_alloc_err;
    own_memset(chs, 0, sizeof(ChanHelperStruct));

    // Init array and hwChanData 
    for (chNr = 0; chNr < MAX_CHANNELS; chNr++){
        chs->dataPtrArray[chNr] = &chs->vChan[chNr];
        chs->vChan[chNr].hwChanData = &chs->hChd[chNr];
        chs->vChan[chNr].channel = chNr;
    }
    vCard->chanData = chs->dataPtrArray;
    hCard = vCard->hwCardData;
    link = &hCard->link; 
    link->priv = vCard; 

    // We want the card data as irq.Instance in the ISR 
    link->irq.Instance = vCard;

#if LINUX_2_6
    // do nothing?
#else
    // Initialize the dev_link_t structure 
    link->release.function = &hwIfRelease;
    link->release.data = (unsigned long)link;
#endif
    // Interrupt setup 
    link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
    link->irq.IRQInfo1 = IRQ_INFO2_VALID|IRQ_LEVEL_ID;
    if (irq_list [0] == -1)
        link->irq.IRQInfo2 = irq_mask;
    else
        for (i = 0; i < 4; i++)
            link-> irq.IRQInfo2 |= 1 << irq_list [i];

    link->irq.Handler = &hwIfInterrupt;

    //
    // General socket configuration defaults can go here.  In this
    // client, we assume very little, and rely on the CIS for almost
    // everything.  In most clients, many details (i.e., number, sizes,
    // and attributes of IO windows) are fixed by the nature of the
    // device, and can be hard-wired here.
    //
    link->conf.Attributes = 0;
    link->conf.Vcc = 50;
    link->conf.IntType = INT_MEMORY_AND_IO;

    os_if_write_lock(&devList_lock, flags);
    // Register with Card Services 
    link->next = devList;
    devList    = link;
    os_if_write_unlock(&devList_lock, flags);

    client_reg.dev_info = &devInfo;
    client_reg.Attributes = INFO_IO_CLIENT | INFO_CARD_SHARE;
    client_reg.EventMask =
                          CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL |
                          CS_EVENT_RESET_PHYSICAL | CS_EVENT_CARD_RESET |
                          CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
    client_reg.event_handler = &hwIfEvent;
    client_reg.Version = 0x0210;
    client_reg.event_callback_args.client_data = link;
    ret = os_if_serv_register_client(&link->handle, &client_reg);
    if (ret != CS_SUCCESS) {
        cs_error(link->handle, RegisterClient, ret);
        os_if_spin_unlock(&canCardsLock);
        hwIfDetach(link);
        return NULL;
    }

    os_if_spin_unlock(&canCardsLock);
    return link;

chan_alloc_err:
    kfree(vCard);
card_alloc_error:
    os_if_spin_unlock(&canCardsLock);
    return NULL;
#else
#           pragma message("attach not implemented yet...") 
    return NULL;
#endif
} // hwIfAttach 


//======================================================================
//    This deletes a driver "instance".  The device is de-registered    
//    with Card Services.  If it has been released, all local data      
//    structures are freed.  Otherwise, the structures will be freed    
//    when the device is released.                                      
//======================================================================
void hwIfDetach (OS_IF_CARD_CONTEXT *link)
{
#if LINUX
    OS_IF_CARD_CONTEXT    **linkp;
    VCanCardData          *vCard;
    VCanCardData          **vCardpp;
    unsigned long flags = 0;    

    os_if_write_lock(&devList_lock, flags);
    vCard      = link->priv;  
    
    // Locate device structure 
    for (linkp = &devList; *linkp; linkp = &(*linkp)->next)
        if (*linkp == link) break;
    
    if (*linkp == NULL) {
        os_if_write_unlock(&devList_lock, flags);
        return;
    }
    os_if_spin_lock(&canCardsLock);
    
    // locate card structure
    for (vCardpp = &canCards; *vCardpp; vCardpp = &(*vCardpp)->next) {
        if (*vCardpp == vCard) break;
    }
    
    
    DEBUGPRINT(1, "<1> vCardpp = %p\n", vCardpp);
    if (*vCardpp == NULL) {
        // since devlist is created before cancards do this
        *linkp = link->next;      
        if(vCard->chanData != NULL) {
            kfree(vCard->chanData);
            vCard->chanData = NULL;
        }
        if(vCard != NULL) {
            kfree(vCard);
            vCard = NULL;
        }
        os_if_spin_unlock(&canCardsLock);
        os_if_write_unlock(&devList_lock, flags);
        return;
    }
    
    //
    // If the device is currently configured and active, we won't
    // actually delete it yet.  Instead, it is marked so that when
    // the release() function is called, that will trigger a proper
    // detach().
    //
    
    if (link->state & DEV_CONFIG) {
        link->state |= DEV_STALE_LINK;
        os_if_spin_unlock(&canCardsLock);
        os_if_write_unlock(&devList_lock, flags);
        return;
    }

    // Unregister device
    // Break the link with Card Services
    if (link->handle) {
        os_if_serv_deregister_client(link->handle);
    }
    
    // Unlink device structure, and free it
    *linkp = link->next;
    *vCardpp = vCard->next;

    if(vCard->chanData != NULL) {
        kfree(vCard->chanData);
        vCard->chanData = NULL;
    }
    if(vCard != NULL) {
        kfree(vCard);
        vCard = NULL;
    }

    os_if_write_unlock(&devList_lock, flags);
    os_if_spin_unlock(&canCardsLock);
    
#if LINUX_2_6
    flush_scheduled_work();
#endif
#else
#           pragma message("detach not implemented yet...") 
#endif
} // hwIfDetach 


//======================================================================
// hwIfConfig() is scheduled to run after a CARD_INSERTION event      
// is received, to configure the PCMCIA socket, and to make the       
// device available to the system.                                    
//======================================================================
#if LINUX

#if LINUX_2_6
/*
#   define CS_CHECK(fn, args...) \
        while ((last_ret=os_if_pcmcia_services(last_fn=(fn),args)!=0)) goto cs_failed

#   define CFG_CHECK(fn, args...) \
        if (os_if_pcmcia_services(fn, args) != 0) goto next_entry
*/
#else

#   define CS_CHECK(fn, args...) \
         while ((last_ret=CardServices(last_fn=(fn),args)!=0)) goto cs_failed
#   define CFG_CHECK(fn, args...) \
         if (CardServices(fn, args) != 0) goto next_entry

#endif

//#endif
#endif




void hwIfConfig(OS_IF_CARD_CONTEXT *link)
{
#if LINUX  
    client_handle_t handle = link->handle;
    VCanCardData *vCard = link->priv;
    tuple_t tuple;
    cisparse_t parse;
    int last_fn = 0;
    int last_ret = 0;
    u_char buf[64];
    config_info_t conf;
    win_req_t req;
    memreq_t map;

    
    // This reads the card's CONFIG tuple to find its configuration
    // registers.
    tuple.DesiredTuple = CISTPL_CONFIG;
    tuple.Attributes = 0;
    tuple.TupleData = buf;
    tuple.TupleDataMax = sizeof(buf);
    tuple.TupleOffset = 0;

#if LINUX_2_6
    
    while ((last_ret=os_if_serv_get_first_tuple(handle, &tuple)!=0))
        goto cs_failed;
    while ((last_ret=os_if_serv_get_tuple_data(handle, &tuple)!=0))
        goto cs_failed;
    while ((last_ret=os_if_serv_parse_tuple(handle, &tuple, &parse)!=0))
        goto cs_failed;
#elif LINUX
    CS_CHECK(GetFirstTuple, handle, &tuple);
    CS_CHECK(GetTupleData, handle, &tuple);
    CS_CHECK(ParseTuple, handle, &tuple, &parse);
#endif

    link->conf.ConfigBase = parse.config.base;
    link->conf.Present = parse.config.rmask [0];

    // Configure card
    link->state |= DEV_CONFIG;

    // Look up the current Vcc
#if LINUX_2_6
    while ((last_ret=os_if_serv_get_configuration_info(handle, &conf) !=0))
        goto cs_failed;
#elif LINUX
    CS_CHECK(GetConfigurationInfo, handle, &conf);
#endif
    link->conf.Vcc = conf.Vcc;

    // In this loop, we scan the CIS for configuration table entries,
    // each of which describes a valid card configuration, including
    // voltage, IO window, memory window, and interrupt settings.

    tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
#if LINUX_2_6
    while ((last_ret=os_if_serv_get_first_tuple(handle, &tuple) !=0))
        goto cs_failed;
#elif LINUX
    CS_CHECK(GetFirstTuple, handle, &tuple);
#endif
    while (1) {
        cistpl_cftable_entry_t dflt = { 0 };
        cistpl_cftable_entry_t *cfg = &(parse.cftable_entry);
        
#if LINUX_2_6
        if (os_if_serv_get_tuple_data(handle, &tuple) != 0) goto next_entry;
        if (os_if_serv_parse_tuple(handle, &tuple, &parse) != 0) goto next_entry;
#elif LINUX
        CFG_CHECK(GetTupleData, handle, &tuple);
        CFG_CHECK(ParseTuple, handle, &tuple, &parse);
#endif
        

        if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
            dflt = *cfg;
        if (cfg->index == 0)
            goto next_entry;
        link->conf.ConfigIndex = cfg->index;

        // Does this card need audio output?
        if (cfg->flags & CISTPL_CFTABLE_AUDIO) {
            link->conf.Attributes |= CONF_ENABLE_SPKR;
            link->conf.Status = CCSR_AUDIO_ENA;
        }

        // Use power settings for Vcc and Vpp if present
        //  Note that the CIS values need to be rescaled
        if (cfg->vcc.present & (1<<CISTPL_POWER_VNOM)) {
            if (conf.Vcc != cfg->vcc.param [CISTPL_POWER_VNOM] / 10000)
                goto next_entry;
        } else if (dflt.vcc.present & (1<<CISTPL_POWER_VNOM)) {
            if (conf.Vcc != dflt.vcc.param[CISTPL_POWER_VNOM]/10000)
                goto next_entry;
        }

        if (cfg->vpp1.present & (1<<CISTPL_POWER_VNOM))
            link->conf.Vpp1 = link->conf.Vpp2 =
                                               cfg->vpp1.param[CISTPL_POWER_VNOM]/10000;
        else if (dflt.vpp1.present & (1<<CISTPL_POWER_VNOM))
            link->conf.Vpp1 = link->conf.Vpp2 =
                                               dflt.vpp1.param[CISTPL_POWER_VNOM]/10000;

        // Do we need to allocate an interrupt?
        if (cfg->irq.IRQInfo1 || dflt.irq.IRQInfo1)
            link->conf.Attributes |= CONF_ENABLE_IRQ;

        // IO window settings
        link->io.NumPorts1 = link->io.NumPorts2 = 0;
        if ((cfg->io.nwin > 0) || (dflt.io.nwin > 0)) {
            cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt.io;
            link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
            if (!(io->flags & CISTPL_IO_8BIT))
                link->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
            if (!(io->flags & CISTPL_IO_16BIT))
                link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
            link->io.IOAddrLines = io->flags & CISTPL_IO_LINES_MASK;
            link->io.BasePort1 = io->win[0].base;
            link->io.NumPorts1 = io->win[0].len;
            if (io->nwin > 1) {
                link->io.Attributes2 = link->io.Attributes1;
                link->io.BasePort2 = io->win[1].base;
                link->io.NumPorts2 = io->win[1].len;
            }
        }

        //printk("port base 0x%x/num port 0x%x\n", link->io.BasePort1, link->io.NumPorts1);

        // This reserves IO space but doesn't actually enable it
#if LINUX_2_6
        if (os_if_serv_request_io(link->handle, &link->io) != 0) {
            printk("<1>request_io FAILED\n");
            goto next_entry;
        }
#else
        CFG_CHECK(RequestIO, link->handle, &link->io);
#endif
        
        //
        // Now set up a common memory window, if needed.  There is room
        // in the dev_link_t structure for one memory window handle,
        // but if the base addresses need to be saved, or if multiple
        // windows are needed, the info should go in the private data
        // structure for this device.

        // Note that the memory window base is a physical address, and
        // needs to be mapped to virtual space with ioremap() before it
        // is used.
        //
        
        if ((cfg->mem.nwin > 0) || (dflt.mem.nwin > 0)) {
            cistpl_mem_t *mem =
                               (cfg->mem.nwin) ? &cfg->mem : &dflt.mem;
            req.Attributes = WIN_DATA_WIDTH_16|WIN_MEMORY_TYPE_CM;
            req.Attributes |= WIN_ENABLE;
            req.Base = mem->win[0].host_addr;
            req.Size = mem->win[0].len;
            req.AccessSpeed = 0;
            link->win = (window_handle_t)link->handle;

            //printk("mem base 0x%x/mem size 0x%x/link->win 0x%x\n", req.Base, req.Size, link->win);

#if LINUX_2_6
            // qqq handle
            if (os_if_serv_request_window(&link->handle, &req, &link->win) != 0) {
                printk("<1>request_window FAILED\n");
                goto next_entry;
            }
#else
            CFG_CHECK(RequestWindow, &link->win, &req);            
#endif

            
            map.Page = 0; map.CardOffset = mem->win[0].card_addr;
#if LINUX_2_6
            if (os_if_serv_map_mem_page(link->win, &map) != 0) {
                printk("<1>map mem page FAILED\n");
                goto next_entry;
            }
#else
            CFG_CHECK(MapMemPage, link->win, &map);
#endif
        }
        // If we got this far, we're cool!
        break;

next_entry:
#if LINUX_2_6
        if (os_if_serv_get_next_tuple(handle, &tuple) != 0) goto next_entry;
#elif LINUX
        CFG_CHECK(GetNextTuple, handle, &tuple);
#endif
    }

    // Allocate an interrupt line.  Note that this does not assign a
    // handler to the interrupt, unless the 'Handler' member of the
    // irq structure is initialized.

    if (link->conf.Attributes & CONF_ENABLE_IRQ)  {
#if LINUX_2_6
        while ((last_ret=os_if_serv_request_irq(link->handle, &link->irq) !=0))
            goto cs_failed;
#else
        CS_CHECK(RequestIRQ, link->handle, &link->irq);
#endif
    }
    // This actually configures the PCMCIA socket -- setting up
    // the I/O windows and the interrupt mapping, and putting the
    // card and host interface into "Memory and IO" mode.
#if LINUX_2_6
    while ((last_ret=os_if_serv_request_configuration(link->handle, &link->conf) !=0))
        goto cs_failed;
#else
    CS_CHECK(RequestConfiguration, link->handle, &link->conf);
#endif
    // We can release the IO port allocations here, if some other
    // driver for the card is going to loaded, and will expect the
    // ports to be available.

    if (free_ports) {
        if (link->io.BasePort1)
            release_region(link->io.BasePort1, link -> io.NumPorts1);
        if (link -> io.BasePort2)
            release_region(link -> io.BasePort2, link -> io.NumPorts2);
    }   

    // Init datastructures for this card
    hwIfInitData(vCard);

    // Initialize the hardware
    if (hwIfVerify(vCard) != 0) goto cs_failed;
    if (hwIfInit(vCard) != 0) goto cs_failed;

    // Insert into list of cards
    os_if_spin_lock(&canCardsLock);
    vCard->next = canCards;
    canCards = vCard;
    os_if_spin_unlock(&canCardsLock);

    // Finally, report what we've done
    printk(KERN_INFO "hwIfConfig");
    if (link -> conf.Vpp1)
        printk(", Vpp %d.%d", link -> conf.Vpp1/10, link -> conf.Vpp1%10);
    if (link -> conf.Attributes & CONF_ENABLE_IRQ)
        printk(", irq %d", link -> irq.AssignedIRQ);
    if (link -> io.NumPorts1)
        printk(", io 0x%04x-0x%04x", link -> io.BasePort1,
               link -> io.BasePort1+link -> io.NumPorts1-1);
    if (link -> io.NumPorts2)
        printk(" & 0x%04x-0x%04x", link -> io.BasePort2,
               link -> io.BasePort2+link -> io.NumPorts2-1);
    if (link -> win)
        printk(", mem 0x%06lx-0x%06lx", req.Base,
               req.Base+req.Size-1);
    link -> state &= ~DEV_CONFIG_PENDING;
    printk("\n");   
    return;
    
cs_failed:
    cs_error(link -> handle, last_fn, last_ret);
    hwIfRelease((unsigned long)link);
#else
#           pragma message("hwIfConfig not imp...")
#endif
} // hwIfConfig 


//======================================================================
// After a card is removed, hwIfRelease() will unregister the         
// device, and release the PCMCIA configuration.  If the device is      
// still open, this will be postponed until it is closed.               
//======================================================================
void hwIfRelease (unsigned long arg)
{
#if LINUX
    OS_IF_CARD_CONTEXT *link;
    unsigned long flags = 0;
    os_if_spin_lock(&canCardsLock);
    os_if_write_lock(&devList_lock, flags);
    
    link = (OS_IF_CARD_CONTEXT*)arg;
    
    // If the device is currently in use, we won't release until it
    // is actually closed, because until then, we can't be sure that
    // no one will try to access the device or its data structures.

    if (link -> open) {
        link -> state |= DEV_STALE_CONFIG;
        os_if_write_unlock(&devList_lock, flags);
        os_if_spin_unlock(&canCardsLock);
        return;
    }

    // Unlink the device chain
    link -> dev = NULL;

    if (link -> win) {
        if(os_if_serv_release_window(link->win) != CS_SUCCESS) {
            printk("<1>release_window FAILED\n");
        }
    }
    if(os_if_serv_release_configuration(link->handle) != CS_SUCCESS) {
        printk("<1>release_configuration FAILED\n");
    }
    
    if (link -> io.NumPorts1) {
        if(os_if_serv_release_io(link->handle, &link->io) != CS_SUCCESS) {
            printk("<1>release_io FAILED\n");
        }
    }
    if (link -> irq.AssignedIRQ) {
        if(os_if_serv_release_irq(link->handle, &link->irq) != CS_SUCCESS) {
            printk("<1>release_irq FAILED\n");
        }
    }
    link -> state &= ~DEV_CONFIG;
    


    if (link -> state & DEV_STALE_LINK) {
        os_if_write_unlock(&devList_lock, flags);
        os_if_spin_unlock(&canCardsLock);
        hwIfDetach(link);
    }
    else {
        os_if_write_unlock(&devList_lock, flags);
        os_if_spin_unlock(&canCardsLock);
    }
    
#endif
} // hwIfRelease 


//======================================================================
// The card status event handler.  Mostly, this schedules other         
// stuff to run after an event is received.                             
//                                                                      
// When a CARD_REMOVAL event is received, we immediately set a          
// private flag to block future accesses to this device.  All the       
// functions that actually access the device should check this flag     
// to make sure the card is still present.                              
//======================================================================
int hwIfEvent (OS_IF_EVENT event, int priority,
              OS_IF_EVENT_PARAM *args)
{
#if LINUX
    unsigned long flags;
    OS_IF_CARD_CONTEXT  *link;
    VCanCardData        *vCard;
    LapcanCardData      *hCard;
    os_if_spin_lock(&interrupt_lock);    
    flags = 0;
    os_if_write_lock(&devList_lock, flags);
    link  = args->client_data;
    vCard = link->priv;
    hCard = vCard->hwCardData;
    
    switch (event) {
        case CS_EVENT_CARD_REMOVAL:
            link->state &= ~DEV_PRESENT;
            if (link->state & DEV_CONFIG) {            
                hCard->stop = 1;

#if LINUX_2_6
                /*
                os_if_init_task(&rel_task, &hwIfRelease, link);
                //rel_task.func = hwIfRelease; 
                //rel_task.data = (unsigned long)link;
                schedule_delayed_work(&rel_task, jiffies + HZ/20);
                */
#elif LINUX
                link->release.expires = jiffies + HZ/20;
                add_timer(&link->release);
#endif
            }
            os_if_write_unlock(&devList_lock, flags);
            break;
        case CS_EVENT_CARD_INSERTION:
            link-> state |= DEV_PRESENT | DEV_CONFIG_PENDING;
            //hCard->bus = args->bus; // qqq removed because i see no
            //use mh
            os_if_write_unlock(&devList_lock, flags);
            hwIfConfig(link);
            break;
        case CS_EVENT_PM_SUSPEND:
            link->state |= DEV_SUSPEND;
            // Fall through...
        case CS_EVENT_RESET_PHYSICAL:
            // Mark the device as stopped, to block IO until later
            hCard->stop = 1;
            if (link->state & DEV_CONFIG) {
                os_if_serv_release_configuration(link->handle);
            }
            os_if_write_unlock(&devList_lock, flags);
            break;
        case CS_EVENT_PM_RESUME:
            link->state &= ~DEV_SUSPEND;
            // Fall through...
        case CS_EVENT_CARD_RESET:
            if (link -> state & DEV_CONFIG) {
                os_if_serv_request_configuration(link -> handle, &link -> conf);
            }
            hCard->stop = 0;
            // We need to restart the card
            os_if_write_unlock(&devList_lock, flags);
            break;
    }
    os_if_spin_unlock(&interrupt_lock);
    
    return 0;
#else
    return 0;
#           pragma message("hwIfEvent not implemented...")
#endif
} // hwIfEvent 




//======================================================================
// Run when driver is loaded                                            
//======================================================================
int hwIfInitDriver(void)
{
#if LINUX
    servinfo_t serv;
    DEBUGPRINT(8, "hwIfInitDriver\n");
    DEBUGPRINT(1, "< "__DATE__ " > < " __TIME__" >\n");
    os_if_serv_get_card_services_info(&serv);
    
    if (serv.Revision != CS_RELEASE_CODE) {
        printk(KERN_NOTICE "lapcan_cs: Card Services release "
               "does not match!\n");
        return -1;
    }
    os_if_register_driver(&devInfoStruct, &hwIfAttach, &hwIfDetach);

    return 0;
#else
#           pragma message("hwifInitDriver not implemented...")
    return 0;
#endif
} // hwIfInitDriver 



//======================================================================
// Run when driver is unloaded                                          
//======================================================================
int hwIfCloseAllDevices(void)
{
    unsigned int flags = 0;
#if LINUX
    os_if_unregister_driver(&devInfoStruct);

    os_if_read_lock(&devList_lock, flags);
    while (devList != NULL) {
        if (devList -> state & DEV_CONFIG) {
            os_if_read_unlock(&devList_lock, flags);
            hwIfRelease((unsigned long)devList);
        }
        else {
            os_if_read_unlock(&devList_lock, flags);
        }
        
        if (devList != NULL) {
            hwIfDetach(devList);
        }
        os_if_read_lock(&devList_lock, flags);
    }
    os_if_read_unlock(&devList_lock, flags);
    
    return 0;
#else
#           pragma message("hwIfCloseAllDevices not implemented...")
    return 0;
#endif
}

//======================================================================


