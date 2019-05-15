/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

// Kvaser CAN driver module for Linux
// Hardware independent parts
//

#if LINUX

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#   if LINUX_2_6
#       include <linux/workqueue.h>
#   else
#       include <linux/tqueue.h>
#   endif
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

#else // win32
#   include "linuxErrors.h"
#   include "debug_file.h"
#endif // end win32

// Kvaser definitions 
#include "vcanevt.h"
#include "vcan_ioctl.h"
#include "hwnames.h"
#include "osif_functions_kernel.h"
#include "VCanOsIf.h"


#if LINUX
#   if LINUX_2_6
        MODULE_LICENSE("GPL");
#   else
        MODULE_LICENSE("GPL");
        EXPORT_NO_SYMBOLS;
#   endif
#endif
        
#if LINUX
#  if defined PCMCIA_DEBUG || defined PCICAN_DEBUG
#      if PCMCIA_DEBUG > PCICAN_DEBUG
           static int debug_levell = PCMCIA_DEBUG;
#       else
           static int debug_levell = PCICAN_DEBUG;
#     endif
      MODULE_PARM(debug_levell, "i");
#     define DEBUGPRINT(n, args...) if (debug_levell>=(n)) printk("<" #n ">" args)
#  else
#     define DEBUGPRINT(n, args...)
#  endif
#else // wince
#  ifdef PCMCIA_DEBUG 
#         pragma message("DEBUG")
#         define DEBUGPRINT(x)    fprintf x
          FILE                    *g_out;
#  else 
#         pragma warning(disable:4002)
#         define DEBUGPRINT(a) 
#  endif
#endif // LINUX
          
VCanDriverData driverData;
VCanCardData   *canCards = NULL;
OS_IF_LOCK     canCardsLock = SPIN_LOCK_UNLOCKED;

//======================================================================
// file operations...                                                   
//======================================================================
#if LINUX
struct file_operations fops = {
llseek:    NULL,        
read:      NULL,        
write:     NULL,        
readdir:   NULL,        
poll:      NULL,        
ioctl:     vCanIOCtl, 
mmap:      NULL,        
open:      vCanOpen,  
flush:     NULL,        
release:   vCanClose, 
fsync:     NULL        
           // Fills rest with NULL entries
};
#else
#           pragma message("qqq")
#endif




//======================================================================
// Time                                                                 
//======================================================================

unsigned long vCanTime(VCanCardData *vCard)
{
    struct timeval tv;
    os_if_do_get_time_of_day(&tv);

    tv.tv_usec -= driverData.startTime.tv_usec;
    tv.tv_sec  -= driverData.startTime.tv_sec;
    
    return tv.tv_usec / (long)vCard->usPerTick + (1000000 / vCard->usPerTick) * tv.tv_sec;
}

//======================================================================
//  Calculate queue length                                              
//======================================================================

unsigned long getQLen(unsigned long head, unsigned long tail,
                                   unsigned long size)
{
    return (head < tail ? head - tail + size : head - tail);
}

//======================================================================
//  Discard send queue                                                  
//======================================================================

int vCanFlushSendBuffer (VCanChanData *chd)
{
    atomic_set(&chd->txChanBufHead, 0);
    atomic_set(&chd->txChanBufTail, 0);
   
    return VCAN_STAT_OK;
}

//======================================================================
//  Deliver to receive queue                                            
//======================================================================

int vCanDispatchEvent(VCanChanData *chd, VCAN_EVENT *e)
{ 
    VCanOpenFileNode *fileNodePtr;
    int rcvQLen;
    
    // Update and notify readers 
    for (fileNodePtr = chd->openFileList; fileNodePtr != NULL; fileNodePtr = fileNodePtr->next){    
        // Event filter 
        if (!(e->tag & fileNodePtr->filter.eventMask)) continue; 
        if (e->tag == V_RECEIVE_MSG && ((CAN_MSG*)e)->flags & VCAN_MSG_FLAG_TXACK){
            // Skip if we sent it ourselves and we dont want the ack
            if (e->transId == fileNodePtr->transId && !fileNodePtr->modeTx) {
                DEBUGPRINT(1, "TXACK Skipped since we sent it ourselves and we don't want the ack!\n");
                continue;
            }
        }
        if (e->tag == V_TRANSMIT_MSG && ((CAN_MSG*)e)->flags & VCAN_MSG_FLAG_TXRQ){
            // Receive only if we sent it and we want the tx request 
            if (e->transId != fileNodePtr->transId || !fileNodePtr->modeTxRq) continue;
        }
        // CAN filter 
        if (e->tag == V_RECEIVE_MSG || e->tag == V_TRANSMIT_MSG){
            unsigned int id = e->tagData.msg.id & ~VCAN_EXT_MSG_ID;   
            if ((e->tagData.msg.id & VCAN_EXT_MSG_ID) == 0) {
   
                // Standard message 
                if ((fileNodePtr->filter.stdId  ^ id) & fileNodePtr->filter.stdMask) {
                    continue;
                }
            }
            
            else {
                // Extended message
                if ((fileNodePtr->filter.extId  ^ id) & fileNodePtr->filter.extMask) {
          continue;
        }
            }
            // Filter on message flags ; note inverting 
            // if (!((fileNodePtr->filter.msgFlags ^ e->tagData.msg.flags) & fileNodePtr->filter.flagsMask)) continue;
            
            if (e->tagData.msg.flags & VCAN_MSG_FLAG_OVERRUN) fileNodePtr->overruns++;
        }
        rcvQLen = getQLen(fileNodePtr->rcvBufHead, fileNodePtr->rcvBufTail, FILE_RCV_BUF_SIZE);
        if (rcvQLen >= FILE_RCV_BUF_SIZE - 1){
            // The buffer is full, ignore new message
            fileNodePtr->overruns++;
            
            // Mark message that arrived before this one 
            {
                //int i = FILE_RCV_BUF_SIZE - 1, head = fileNodePtr->rcvBufHead;
                int i;
                int head = fileNodePtr->rcvBufHead;
                for (i = FILE_RCV_BUF_SIZE - 1; i; --i){
                    head = (head == 0 ? FILE_RCV_BUF_SIZE - 1 : head - 1);
                    if (fileNodePtr->fileRcvBuffer[head].tag == V_RECEIVE_MSG) break;
                }
                if (i) fileNodePtr->fileRcvBuffer[head].tagData.msg.flags |= VCAN_MSG_FLAG_OVERRUN;   
            }
            
        }
        // insert into buffer
        else {
            memcpy(&(fileNodePtr->fileRcvBuffer[fileNodePtr->rcvBufHead]), e, sizeof(VCAN_EVENT));
            if (++fileNodePtr->rcvBufHead >= FILE_RCV_BUF_SIZE) fileNodePtr->rcvBufHead = 0;

            // Wake up if the queue was empty BEFORE 
            if (rcvQLen == 0) {
                os_if_wake_up_interruptible(&fileNodePtr->rxWaitQ);
      }
        }
    }
    return 0;
}


//======================================================================
//    Open - File operation                                             
//======================================================================

int vCanOpen(struct inode *inode, struct file *filp)
{
#if LINUX
    struct VCanOpenFileNode *openFileNodePtr;
    VCanCardData *cardData = NULL;
    VCanChanData *chanData = NULL;
    
    int minorNr;
    int channelNr;

    minorNr = MINOR(inode->i_rdev);

    DEBUGPRINT(1, "VCanOpen minor %d major (%d)\n", minorNr, MAJOR(inode->i_rdev));

    os_if_spin_lock(&canCardsLock);
    if (canCards == NULL) {
        return -ENODEV;
        os_if_spin_unlock(&canCardsLock);
    }
    
    // Find the right minor inode number
    for (cardData = canCards; cardData != NULL; cardData = cardData->next){
        for (channelNr = 0; channelNr < cardData->nrChannels; channelNr++){
            if (minorNr == cardData->chanData[channelNr]->minorNr){
                chanData = cardData->chanData[channelNr];
                break;
            }
        }
    }
    os_if_spin_unlock(&canCardsLock);
    if (chanData == NULL) return -ENODEV;
    
    // Allocate memory and zero the whole struct
    openFileNodePtr = kmalloc(sizeof(VCanOpenFileNode), GFP_KERNEL);
    if (openFileNodePtr == NULL) return -ENOMEM;
    memset(openFileNodePtr, 0, sizeof(VCanOpenFileNode));
  
    // Init wait queue
    os_if_init_waitqueue_head(&(openFileNodePtr->rxWaitQ));

    
    openFileNodePtr->rcvBufTail         = 0;
    openFileNodePtr->rcvBufHead         = 0;
    openFileNodePtr->filp               = filp;
    openFileNodePtr->chanData           = chanData;
    openFileNodePtr->writeIsBlock       = 1;
    openFileNodePtr->readIsBlock        = 1;
    openFileNodePtr->modeTx             = 0;
    openFileNodePtr->modeTxRq           = 0;
    openFileNodePtr->writeTimeout       = -1;
    openFileNodePtr->readTimeout        = -1;
    openFileNodePtr->transId            = atomic_read(&chanData->transId);
    atomic_add(1, &chanData->transId);
    openFileNodePtr->filter.eventMask   = ~0;
    openFileNodePtr->overruns           = 0;
    
    // Insert this node first in list of "opens"
    os_if_spin_lock(&chanData->openLock);
    openFileNodePtr->chanNr             = -1;
    openFileNodePtr->channelLocked       = 0;
    openFileNodePtr->channelOpen         = 0;
    chanData->fileOpenCount++;
    openFileNodePtr->next  = chanData->openFileList;
    chanData->openFileList = openFileNodePtr;
    os_if_spin_unlock(&chanData->openLock);
  
    // We want a pointer to the node as private_data
    filp->private_data = openFileNodePtr;

    // dummy for 2.6
    OS_IF_MODE_INC_USE_COUNT;

    // qqq should this be called?
    /*
    if (!try_module_get(THIS_MODULE)) {
        printk("<1> try_module_get failed...");
        return NULL;
    }*/
    
    return 0;
#else
    return 0;
#endif
}


/*======================================================================*/
/*    Release - File operation                                          */
/*======================================================================*/

int vCanClose (struct inode *inode, struct file *filp) {
#if LINUX
    struct VCanOpenFileNode **openPtrPtr;
    struct VCanOpenFileNode *fileNodePtr;
    VCanChanData *chanData;
  
    fileNodePtr = filp->private_data;
    chanData = fileNodePtr->chanData;
    
    os_if_spin_lock(&chanData->openLock);
    // Find the open file node 
    openPtrPtr = &chanData->openFileList;
    for(;*openPtrPtr != NULL; openPtrPtr = &((*openPtrPtr)->next)) {
        if ((*openPtrPtr)->filp == filp) break;
    }
    // We did not find anything? 
    if (*openPtrPtr == NULL) {
        os_if_spin_unlock(&chanData->openLock); 
        return -EBADF;
    }
    // openPtrPtr now points to the next-pointer that points to the correct node
    fileNodePtr = *openPtrPtr; 
    
    if (fileNodePtr->chanNr != -1 ) { 
        chanData->fileOpenCount--;
        fileNodePtr->chanNr = -1;
        fileNodePtr->channelLocked = 0;
        fileNodePtr->channelOpen   = 0;
    }
    
    // Remove node
    *openPtrPtr = (*openPtrPtr)->next;

    os_if_spin_unlock(&chanData->openLock);

    if(fileNodePtr != NULL) {
        kfree(fileNodePtr);
        fileNodePtr = NULL;
    }

    // should this be here or up?
    if (!chanData->fileOpenCount) {
      hwIf.busOff(chanData);
    }

    // dummy for 2.6
    OS_IF_MODE_DEC_USE_COUNT;

    // qqq should this be called?
    //module_put(THIS_MODULE);
    return 0;
#else
    return 0;
#           pragma message("qqq vCanClose not implemented...")
#endif
}

//======================================================================
// Returns whether the transmit queue on a specific channel is full     
//======================================================================

int txQFull(VCanChanData *chd)
{   
    return (getQLen(atomic_read(&chd->txChanBufHead), atomic_read(&chd->txChanBufTail),
        TX_CHAN_BUF_SIZE) >= TX_CHAN_BUF_SIZE - 1);
}


//======================================================================
// Returns whether the transmit queue on a specific channel is empty    
//======================================================================

int txQEmpty(VCanChanData *chd) 
{
    return (getQLen(atomic_read(&chd->txChanBufHead), atomic_read(&chd->txChanBufTail), TX_CHAN_BUF_SIZE) == 0);
}

//======================================================================
// Returns whether the receive queue on a specific channel is empty     
//======================================================================

int rxQEmpty(VCanOpenFileNode *fileNodePtr) 
{
    return (getQLen(fileNodePtr->rcvBufHead, fileNodePtr->rcvBufTail, FILE_RCV_BUF_SIZE) == 0);
}


//======================================================================
//  IOCtl - File operation                                              
//  This function is not reentrant with the same file descriptor!       
//======================================================================

int vCanIOCtl (struct inode *inode, struct file *filp, 
               unsigned int ioctl_cmd, unsigned long arg)
{
    VCanOpenFileNode  *fileNodePtr;
    VCanChanData      *chd;
    unsigned long     timeLeft;
    unsigned long     timeout;
    OS_IF_WAITQUEUE   wait;
    int               ret;
    int               vStat;
    int               chanNr;
    
#if LINUX
    fileNodePtr = filp->private_data;
    chd = fileNodePtr->chanData;
#else
#           pragma message("QQQ vCanIOCtl")
#endif


    switch (ioctl_cmd) {
        //------------------------------------------------------------------
    case VCAN_IOC_SENDMSG:
        if (!fileNodePtr->writeIsBlock && txQFull(chd)) {
            DEBUGPRINT(1, "VCAN_IOC_SENDMSG - returning -EAGAIN\n");
            return -EAGAIN;
        }
        
        os_if_init_waitqueue_entry(&wait);
        os_if_add_wait_queue(&chd->txChanWaitQ, &wait);
        
        while(1) {
            os_if_set_task_interruptible();            

            if (fileNodePtr->writeTimeout == -1 && txQFull(chd)) {
              os_if_wait_for_event(chd->txChanWaitQ);
#if LINUX
              if (signal_pending(current)) {
                // Sleep was interrupted by signal
                os_if_remove_wait_queue(&chd->txChanWaitQ, &wait);
                DEBUGPRINT(1, "VCAN_IOC_SENDMSG - returning -ERESTARTSYS\n");
                return -ERESTARTSYS;
              }
#endif
            }
            else if(fileNodePtr->writeTimeout != -1 && !txQEmpty(chd)) {
              if (os_if_wait_for_event_timeout(1 + fileNodePtr->writeTimeout * HZ/1000, &wait) == 0) {
                // Transmit timed out
                os_if_remove_wait_queue(&chd->txChanWaitQ, &wait);
                DEBUGPRINT(1, "VCAN_IOC_SENDMSG - returning -EAGAIN 2\n");
                return -EAGAIN;
              }
#if LINUX
              if (signal_pending(current)) {
                // Sleep was interrupted by signal
                os_if_remove_wait_queue(&chd->txChanWaitQ, &wait);
                DEBUGPRINT(1, "VCAN_IOC_SENDMSG - returning -ERESTARTSYS 2\n");
                return -ERESTARTSYS;
              }
#endif
            }
            else {
                
                CAN_MSG *bufMsgPtr = &(chd->txChanBuffer[atomic_read(&chd->txChanBufHead)]);
                
                os_if_set_task_running();
                os_if_remove_wait_queue(&chd->txChanWaitQ, &wait);

                if (os_if_set_user_data(bufMsgPtr, (CAN_MSG*)arg, sizeof(CAN_MSG))) {
                    DEBUGPRINT(1, "VCAN_IOC_SENDMSG - returning -EFAULT\n");
                    return -EFAULT;
                }

                // This is for keeping track of the originating fileNode
                bufMsgPtr->user_data = fileNodePtr->transId;
                bufMsgPtr->flags &= ~(VCAN_MSG_FLAG_TX_NOTIFY | VCAN_MSG_FLAG_TX_START);
                if (fileNodePtr->modeTx || (chd->fileOpenCount > 1))
                    bufMsgPtr->flags |= VCAN_MSG_FLAG_TX_NOTIFY;
                if (fileNodePtr->modeTxRq)
                    bufMsgPtr->flags |= VCAN_MSG_FLAG_TX_START;
               
                if (atomic_read(&chd->txChanBufHead) >= (TX_CHAN_BUF_SIZE - 1))
                    atomic_set(&chd->txChanBufHead, 0);
                else
                    atomic_add(1, &chd->txChanBufHead);
        
                // Exit loop
                break;
            }
        }
        hwIf.requestSend(chd->vCard, chd);
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_RECVMSG:
        if (!fileNodePtr->readIsBlock && rxQEmpty(fileNodePtr)) return -EAGAIN;

        os_if_init_waitqueue_entry(&wait);
        os_if_add_wait_queue(&fileNodePtr->rxWaitQ, &wait);
        while(1) {
            os_if_set_task_interruptible();

            if (rxQEmpty(fileNodePtr)) {
                
                if (fileNodePtr->readTimeout != -1){
                    if (os_if_wait_for_event_timeout(1 + fileNodePtr->readTimeout * HZ/1000, &wait) == 0) {
                    // Receive timed out
                        os_if_remove_wait_queue(&fileNodePtr->rxWaitQ, &wait);
                        // reset when finished
                        return -EAGAIN;
                    }
                }
                else {
                    os_if_wait_for_event(fileNodePtr->rxWaitQ);
                }
#if LINUX
                if (signal_pending(current)) {
                    // Sleep was interrupted by signal
                    os_if_remove_wait_queue(&fileNodePtr->rxWaitQ, &wait);
                    return -ERESTARTSYS;
                }
#endif          
            }
            // we have events in Q
            else {
                os_if_set_task_running();
                os_if_remove_wait_queue(&fileNodePtr->rxWaitQ, &wait);
                copy_to_user_ret((VCAN_EVENT*) arg, &(fileNodePtr->fileRcvBuffer[fileNodePtr->rcvBufTail]), sizeof(VCAN_EVENT), -EFAULT);
                if (++(fileNodePtr->rcvBufTail) >= FILE_RCV_BUF_SIZE) {
                    fileNodePtr->rcvBufTail = 0;
                }
                // Exit loop
                break;
            }
        }
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_BUS_ON:
        DEBUGPRINT(1, "VCAN_IOC_BUS_ON\n");
        fileNodePtr->rcvBufHead = 0;
        fileNodePtr->rcvBufTail = 0;
        vStat = hwIf.flushSendBuffer(chd);
        vStat = hwIf.busOn(chd); 
        // make synchronous? qqq
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_BUS_OFF:
        DEBUGPRINT(1, "VCAN_IOC_BUS_OFF\n");
        vStat = hwIf.busOff(chd);
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_SET_BITRATE:
        {
            VCanBusParams busParams;
            copy_from_user_ret(&busParams, (VCanBusParams*)arg, sizeof(VCanBusParams), -EFAULT);
            if (hwIf.setBusParams(chd, &busParams)) {
                
                // Indicate that something went wrong by setting freq to 0
                busParams.freq = 0;
            } else {
                vStat = hwIf.getBusParams(chd, &busParams);
            }
            copy_to_user_ret((VCanBusParams*)arg, &busParams, sizeof(VCanBusParams), -EFAULT);
        }
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_GET_BITRATE:
        {
            VCanBusParams busParams;
            vStat = hwIf.getBusParams(chd, &busParams);        
            copy_to_user_ret((VCanBusParams*)arg, &busParams, sizeof(VCanBusParams), -EFAULT);
        }
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_SET_OUTPUT_MODE:
        vStat = hwIf.setOutputMode(chd, arg);
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_SET_MSG_FILTER:
        copy_from_user_ret(&(fileNodePtr->filter), (VCanMsgFilter*) arg, sizeof(VCanMsgFilter), -EFAULT);
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_GET_MSG_FILTER:
        copy_to_user_ret((VCanMsgFilter*) arg, &(fileNodePtr->filter), sizeof(VCanMsgFilter), -EFAULT);
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_OPEN_CHAN:
        
        os_if_get_int(&chanNr, (int*) arg);
        os_if_spin_lock(&chd->openLock);
        {
            VCanOpenFileNode *tmpFnp;
            for (tmpFnp = chd->openFileList; tmpFnp && !tmpFnp->channelLocked; tmpFnp = tmpFnp->next);
            // This channel is locked (i.e opened exclusive)
            if (tmpFnp) {
                ret = os_if_set_int(-1, (int*) arg);
                chd->fileOpenCount--;
                
            } else {
                ret = os_if_set_int(chanNr, (int*) arg);
                fileNodePtr->channelOpen = 1;
                fileNodePtr->chanNr = chanNr; 
            }
        }
        os_if_spin_unlock(&chd->openLock);
        if (ret) return -EFAULT;
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_OPEN_EXCL:
        
        os_if_get_int(&chanNr, (int*) arg);
        os_if_spin_lock(&chd->openLock );
        {
            VCanOpenFileNode *tmpFnp;
            // Return -1 if channel is opened by someone else
            for (tmpFnp = chd->openFileList; tmpFnp && !tmpFnp->channelOpen; tmpFnp = tmpFnp->next);
            // This channel is already opened
            if (tmpFnp) {
                ret = os_if_set_int(-1, (int*) arg);
                chd->fileOpenCount--;
            } else {
                ret = os_if_set_int(chanNr, (int*) arg);
                fileNodePtr->channelOpen = 1;
                fileNodePtr->channelLocked = 1;
                fileNodePtr->chanNr = chanNr;              
            }
        }
        os_if_spin_unlock(&chd->openLock );
        if (ret) return -EFAULT;
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_WAIT_EMPTY:
#if LINUX
       
        get_user_long_ret(&timeout, (unsigned long*) arg, -EFAULT);
        timeLeft       = -1;        
        atomic_set(&chd->waitEmpty, 1);
        
#   if LINUX_2_6
        timeLeft = wait_event_interruptible_timeout(chd->flushQ,       
            (getQLen(atomic_read(&chd->txChanBufHead), atomic_read(&chd->txChanBufTail), TX_CHAN_BUF_SIZE) == 0) &&
            ((chd->chipState.state != CHIPSTAT_BUSOFF) && hwIf.txAvailable(chd))
            , 1 + timeout * HZ / 1000);
#    else
        timeLeft = os_if_wait_event_interruptible_timeout(chd->flushQ,
            (getQLen(atomic_read(&chd->txChanBufHead), atomic_read(&chd->txChanBufTail), TX_CHAN_BUF_SIZE) == 0) &&
            ((chd->chipState.state != CHIPSTAT_BUSOFF) && hwIf.txAvailable(chd))
            , 1 + timeout * HZ / 1000);
#    endif
        if (atomic_read(&chd->waitEmpty)) {
            atomic_set(&chd->waitEmpty, 0);
        }
        
        if (timeLeft == OS_IF_TIMEOUT) {
            DEBUGPRINT(1, "VCAN_IOC_WAIT_EMPTY- EAGAIN\n");
            return -EAGAIN;
        }
#endif
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_FLUSH_RCVBUFFER:
        fileNodePtr->rcvBufHead = 0;
        fileNodePtr->rcvBufTail = 0;
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_FLUSH_SENDBUFFER:
        vStat = hwIf.flushSendBuffer(chd);
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_SET_WRITE_BLOCK:
        fileNodePtr->writeIsBlock = arg;
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_SET_READ_BLOCK:
        fileNodePtr->readIsBlock = arg;
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_SET_WRITE_TIMEOUT:
        fileNodePtr->writeTimeout = arg;
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_SET_READ_TIMEOUT:
        fileNodePtr->readTimeout = arg;
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_READ_TIMER:
        put_user_ret(hwIf.getTime(chd->vCard), (int*) arg, -EFAULT);
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_GET_TX_ERR:
        put_user_ret(hwIf.getTxErr(chd), (int*) arg, -EFAULT);
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_GET_RX_ERR:
        put_user_ret(hwIf.getRxErr(chd), (int*) arg, -EFAULT);  
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_GET_OVER_ERR:
        put_user_ret(fileNodePtr->overruns, (int*) arg, -EFAULT); 
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_GET_RX_QUEUE_LEVEL:
        {
            int ql = getQLen(fileNodePtr->rcvBufHead, fileNodePtr->rcvBufTail, FILE_RCV_BUF_SIZE) + hwIf.rxQLen(chd);
            put_user_ret(ql, (int*) arg, -EFAULT);  
        }
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_GET_TX_QUEUE_LEVEL:
        {
            int ql = getQLen(atomic_read(&chd->txChanBufHead), atomic_read(&chd->txChanBufTail), TX_CHAN_BUF_SIZE) + hwIf.txQLen(chd);
            put_user_ret(ql, (int*) arg, -EFAULT);  
        }
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_GET_CHIP_STATE:
        {
            hwIf.requestChipState(chd);
            put_user_ret(chd->chipState.state, (int*) arg, -EFAULT);  
        }
        break;
        //------------------------------------------------------------------
    case VCAN_IOC_GET_TXACK:
        {
          DEBUGPRINT(1, "KVASER VCAN_IOC_GET_TXACK, was %d\n", fileNodePtr->modeTx);
          
          put_user_ret(fileNodePtr->modeTx, (int*) arg, -EFAULT);

          break;
        }
    case VCAN_IOC_SET_TXACK:
        {
          DEBUGPRINT(1, "KVASER Try to set VCAN_IOC_SET_TXACK to %d, was %d\n", *(int *)arg, fileNodePtr->modeTx);
          if (*(int *)arg >= 0 && *(int *)arg <= 2) {
            fileNodePtr->modeTx = *(int *)arg;
            DEBUGPRINT(1, "KVASER Managed to set VCAN_IOC_SET_TXACK to %d\n", fileNodePtr->modeTx);
          }
          else {
            return -EFAULT;
          }

          break;
        }
    // WARNING! IT IS NOT RECOMMENDED TO USE THIS IOCTL
    // (TEMP_IOCHARDRESET).
    // IT IS A SPECIAL SOLUTION FOR A CUMSTOMER AND WE TAKE NO
    // RESPONSIBILITY FOR THE FUNCTIONALITY.

#   if !LINUX_2_6    
    case TEMP_IOCHARDRESET:
    {
        while(MOD_IN_USE) {
            MOD_DEC_USE_COUNT;
        }

        MOD_INC_USE_COUNT; // this is because this is open
        break;
    }
#   endif

    default:
        DEBUGPRINT(1, "vCanIOCtrl UNKNOWN VCAN_IOC!!!\n");
        return -EINVAL;
    }
    //------------------------------------------------------------------
    
    return 0;
}


//======================================================================
// Init common data structures for one card                             
//======================================================================
int vCanInitData(VCanCardData *vCard)
{
    int chNr;    
    vCard->usPerTick = 1000;
    for (chNr = 0; chNr < vCard->nrChannels; chNr++){
        VCanChanData *vChd = vCard->chanData[chNr];

        vChd->minorNr = driverData.minorNr++;        

        // Init waitqueues
        os_if_init_waitqueue_head(&(vChd->flushQ));
        os_if_init_waitqueue_head(&(vChd->txChanWaitQ));

#if LINUX        
        spin_lock_init(&(vChd->openLock));
        spin_lock_init(&(vCard->memQLock));
        
#endif

        // vCard points back to card
        vChd->vCard = vCard;
    }
    return 0;
}

//======================================================================
// Module init                                                          
//======================================================================

// Major device number qqq

int init_module(void)
{
#if LINUX    
    int result;
  
    // Initialize card and data structures 
    memset(&driverData, 0, sizeof(VCanDriverData));
    
    if (hwIf.initAllDevices() == -ENODEV) {
        DEBUGPRINT(1, "No Kvaser %s cards found!", device_name);
        return -1;
    }
    
    // Register driver for device
    // qqq update this to alloc_chrdev_region further on...
    result = register_chrdev(driverData.majorDevNr, device_name, &fops);
    if (result < 0) {
        DEBUGPRINT(1,"register_chrdev(%d, %s, %x) failed, error = %d\n",
            driverData.majorDevNr, device_name, (int) &fops, result);
        hwIf.closeAllDevices();
        return -1;
    }
    
    if (driverData.majorDevNr == 0) driverData.majorDevNr = result;    

    DEBUGPRINT(1,"REGISTER CHRDEV (%s) majordevnr = %d\n", device_name, driverData.majorDevNr);
    do_gettimeofday(&driverData.startTime);
    
    create_proc_read_entry(device_name, 
                           0,             // default mode 
                           NULL,          // parent dir  
                           hwIf.procRead, 
                           NULL          // client data
                          );
#endif
    return 0;
}


//======================================================================
// Module shutdown                                                      
//======================================================================
void cleanup_module(void)
{
#if LINUX
    if (driverData.majorDevNr > 0){
        DEBUGPRINT(1,"UNREGISTER CHRDEV (%s) majordevnr = %d\n", device_name, driverData.majorDevNr);
        unregister_chrdev(driverData.majorDevNr, device_name);
    }
    remove_proc_entry(device_name, NULL /* parent dir*/);
    hwIf.closeAllDevices();
#endif
}
//======================================================================
