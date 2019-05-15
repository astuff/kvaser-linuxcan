/*
**             Copyright 2012-2016 by Kvaser AB, Molndal, Sweden
**                        http://www.kvaser.com
**
** This software is dual licensed under the following two licenses:
** BSD-new and GPLv2. You may use either one. See the included
** COPYING file for details.
**
** License: BSD-new
** ===============================================================================
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of the <organization> nor the
**       names of its contributors may be used to endorse or promote products
**       derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
** WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
** DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
** (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
** LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
** ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
**
** License: GPLv2
** ===============================================================================
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**
** ---------------------------------------------------------------------------
**/

// Kvaser CAN driver module for Linux
// Hardware independent parts
//


//--------------------------------------------------
// NOTE! module_versioning HAVE to be included first
#  include "module_versioning.h"
//--------------------------------------------------

#  include <linux/pci.h>
#  include <linux/kernel.h>
#  include <linux/init.h>
#  include <linux/sched.h>
#  include <linux/ptrace.h>
#  include <linux/slab.h>
#  include <linux/string.h>
#  include <linux/timer.h>
#  include <linux/workqueue.h>
#  include <linux/interrupt.h>
#  include <linux/delay.h>
#  include <linux/ioport.h>
#  include <linux/proc_fs.h>
#  include <linux/seq_file.h>
#  include <asm/io.h>
#  if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
#     include <asm/system.h>
#  endif
#  include <asm/bitops.h>
#  include <asm/uaccess.h>
#  include <asm/atomic.h>

// Kvaser definitions
#include "vcanevt.h"
#include "vcan_ioctl.h"
#include "kcan_ioctl.h"
#  include "hwnames.h"
#include "osif_functions_kernel.h"
#include "queue.h"
#include "VCanOsIf.h"
#include "debug.h"
#include "softsync.h"


   MODULE_LICENSE("Dual BSD/GPL");
   MODULE_AUTHOR("KVASER");

#  if defined VCANOSIF_DEBUG
      static int debug_levell = VCANOSIF_DEBUG;
      MODULE_PARM_DESC(debug_levell, "Common debug level");
      module_param(debug_levell, int, 0644);
#     define DEBUGPRINT(n, arg)    if (debug_levell >= (n)) { DEBUGOUT(n, arg); }
#  else
#     define DEBUGPRINT(n, args...)
#  endif

static long calc_timeout (struct timeval *start, unsigned long wanted_timeout);
static uint32_t read_specific (VCanOpenFileNode *fileNodePtr, VCAN_EVENT *msg);

//======================================================================
// File operations...
//======================================================================

static int          vCanClose(struct inode *inode, struct file *filp);
#if !defined(HAVE_UNLOCKED_IOCTL)
static int          vCanIOCtl(struct inode *inode, struct file *filp,
                              unsigned int cmd, unsigned long arg);
#endif
static long         vCanIOCtl_unlocked(struct file *filp,
                                       unsigned int cmd, unsigned long arg);
static int          vCanOpen(struct inode *inode, struct file *filp);
static unsigned int vCanPoll(struct file *filp, poll_table *wait);

struct file_operations fops = {
  .poll    = vCanPoll,
  .open    = vCanOpen,
  .release = vCanClose,
#if defined(HAVE_UNLOCKED_IOCTL)
  .unlocked_ioctl = vCanIOCtl_unlocked,
#else
  .ioctl   = vCanIOCtl,
#endif
#if defined(HAVE_COMPAT_IOCTL)
  .compat_ioctl   = vCanIOCtl_unlocked,
#endif
             // Fills rest with NULL entries
};


//======================================================================
// Time
//======================================================================

int vCanTime (VCanCardData *vCard, unsigned long *time)
{
  struct timeval tv;

  os_if_do_get_time_of_day(&tv);

  tv.tv_usec -= vCard->driverData->startTime.tv_usec;
  tv.tv_sec  -= vCard->driverData->startTime.tv_sec;

  *time = (unsigned long)tv.tv_usec / (unsigned long)vCard->usPerTick +
          (1000000ul / (unsigned long)vCard->usPerTick) * (unsigned long)tv.tv_sec;

  return VCAN_STAT_OK;
}
EXPORT_SYMBOL(vCanTime);

//======================================================================
//  Calculate queue length
//======================================================================

unsigned long getQLen (unsigned long head, unsigned long tail,
                       unsigned long size)
{
  return (head < tail ? head - tail + size : head - tail);
}

//======================================================================
//  Discard send queue
//======================================================================

int vCanFlushSendBuffer (VCanChanData *chd)
{
  queue_reinit(&chd->txChanQueue);

  return VCAN_STAT_OK;
}
EXPORT_SYMBOL(vCanFlushSendBuffer);

//======================================================================
//  Discard recieve queue
//======================================================================

int vCanFlushReceiveBuffer (VCanOpenFileNode *fileNodePtr)
{
  fileNodePtr->rcv.bufTail = 0;
  fileNodePtr->rcv.bufHead = 0;

  return VCAN_STAT_OK;
}

//======================================================================
//  Pop rx queue
//======================================================================
int vCanPopReceiveBuffer (VCanReceiveData *rcv)
{
  do {
    if (rcv->bufTail >= rcv->size - 1) {
      rcv->bufTail = 0;
    }
    else {
      rcv->bufTail++;
    }
  } while ((rcv->valid[rcv->bufTail] == 0) && (rcv->bufTail != rcv->bufHead));

  if ((rcv->bufTail % 10) == 0) {
    DEBUGPRINT(4, (TXT("RXpop(%d)\n"), rcv->bufTail));
  }


  return VCAN_STAT_OK;
}

//======================================================================
//  Push rx queue
//======================================================================
int vCanPushReceiveBuffer (VCanReceiveData *rcv)
{
  int wasEmpty;

  rcv->valid[rcv->bufHead] = 1;

  wasEmpty = rcv->bufTail == rcv->bufHead;
  if (rcv->bufHead >= rcv->size - 1) {
    rcv->bufHead = 0;
  }
  else {
    rcv->bufHead++;
  }

  if ((rcv->bufHead % 10) == 0) {
    DEBUGPRINT(4, (TXT("RXpush(%d)\n"), rcv->bufHead));
  }
  
  // Wake up if the queue was empty BEFORE
  if (wasEmpty) {
    os_if_wake_up_interruptible(&rcv->rxWaitQ);
  }
  
  return VCAN_STAT_OK;
}

//======================================================================
//  Deliver to receive queue
//======================================================================

int vCanDispatchEvent (VCanChanData *chd, VCAN_EVENT *e)
{
  VCanOpenFileNode *fileNodePtr;
  unsigned long    openLock_irqFlags;
  unsigned long    rcvLock_irqFlags;
  long             objbuf_mask;
  int              queue_length;

  // Update and notify readers
  // Needs to be _irqsave since some drivers call from ISR:s.
  os_if_spin_lock_irqsave(&chd->openLock, &openLock_irqFlags);
  for (fileNodePtr = chd->openFileList; fileNodePtr != NULL;
       fileNodePtr = fileNodePtr->next) {
    unsigned short int msg_flags = e->tagData.msg.flags;
    // Event filter
    if (!(e->tag & fileNodePtr->filter.eventMask)) {
      continue;
    }
    if (e->tag == V_RECEIVE_MSG) {
      // Update bus statistics      
      if (msg_flags & VCAN_MSG_FLAG_ERROR_FRAME) {
        if (!(msg_flags & VCAN_MSG_FLAG_TX_START)) {
          chd->busStats.bitCount += 16;          
          chd->busStats.errFrame++;
        }
      }
      else {
        if (!(msg_flags & VCAN_MSG_FLAG_TX_START)) {
          chd->busStats.bitCount += (e->tagData.msg.dlc > 8 ? 8 : e->tagData.msg.dlc) * 10;                      
          if (msg_flags & VCAN_EXT_MSG_ID) {
            chd->busStats.bitCount += 70;
            if (msg_flags & VCAN_MSG_FLAG_REMOTE_FRAME) {
              chd->busStats.extRemote++;
            }
            else {
              chd->busStats.extData++;
            }
          }
          else {
            chd->busStats.bitCount += 50;
            if (msg_flags & VCAN_MSG_FLAG_REMOTE_FRAME) {
              chd->busStats.stdRemote++;
            }
            else {
              chd->busStats.stdData++;
            }
          }          
        }
      }
    }
    if (e->tag == V_RECEIVE_MSG && msg_flags & VCAN_MSG_FLAG_TXACK) {
      // Skip if we sent it ourselves and we don't want the ack
      if (e->transId == fileNodePtr->transId && !fileNodePtr->modeTx) {
        DEBUGPRINT(2, (TXT("TXACK Skipped since we sent it ourselves and we don't want the ack!\n")));
        continue;
      }
      if (e->transId != fileNodePtr->transId) {
        // Don't receive on virtual busses if explicitly denied.
        if (fileNodePtr->modeNoTxEcho) {
          continue;
        }
        // Other receivers (virtual bus extension) should not see the TXACK.
        msg_flags &= ~VCAN_MSG_FLAG_TXACK;
        
        // dont report any SSM NACK's on other handles (on same channel). 
        if (msg_flags & VCAN_MSG_FLAG_SSM_NACK) continue;
      }
    }
    if (e->tag == V_RECEIVE_MSG && msg_flags & VCAN_MSG_FLAG_TXRQ) {
      // Receive only if we sent it and we want the tx request
      if (e->transId != fileNodePtr->transId || !fileNodePtr->modeTxRq) {
        continue;
      }
    }
    // CAN filter
    if (e->tag == V_RECEIVE_MSG || e->tag == V_TRANSMIT_MSG) {
      unsigned int id = e->tagData.msg.id & ~VCAN_EXT_MSG_ID;
      if ((e->tagData.msg.id & VCAN_EXT_MSG_ID) == 0) {

        // Standard message
        if ((fileNodePtr->filter.stdId ^ id) & fileNodePtr->filter.stdMask) {
          continue;
        }
      }

      else {
        // Extended message
        if ((fileNodePtr->filter.extId ^ id) & fileNodePtr->filter.extMask) {
          continue;
        }
      }

      if (msg_flags & VCAN_MSG_FLAG_OVERRUN) {
        fileNodePtr->overruns++;
      }

      //
      // Check against the object buffers, if any.
      //
      if (!(msg_flags & (VCAN_MSG_FLAG_TXRQ | VCAN_MSG_FLAG_TXACK)) &&
          ((objbuf_mask = objbuf_filter_match(fileNodePtr->objbuf,
                                              e->tagData.msg.id,
                                              msg_flags)) != 0)) {
        // This is something that matched the code/mask for at least one buffer,
        // and it's *not* a TXRQ or a TXACK.
#ifdef __arm__
        unsigned int rd;
        unsigned int new_rd;
        do {
          rd = atomic_read(&fileNodePtr->objbufActive);
          new_rd = rd | objbuf_mask;
        } while (atomic_cmpxchg(&fileNodePtr->objbufActive, rd, new_rd) != rd);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)
        atomic_set_mask(objbuf_mask, &fileNodePtr->objbufActive);
#else
        atomic_or(objbuf_mask, &fileNodePtr->objbufActive);
#endif
        os_if_queue_task_not_default_queue(fileNodePtr->objbufTaskQ,
                                           &fileNodePtr->objbufWork);
      }
    }

    os_if_spin_lock_irqsave(&fileNodePtr->rcv.rcvLock, &rcvLock_irqFlags);
    memcpy(&(fileNodePtr->rcv.fileRcvBuffer[fileNodePtr->rcv.bufHead]),
           e, sizeof(VCAN_EVENT));
    vCanPushReceiveBuffer(&fileNodePtr->rcv);
    queue_length = getQLen(fileNodePtr->rcv.bufHead,
                           fileNodePtr->rcv.bufTail,
                           fileNodePtr->rcv.size);

    DEBUGPRINT(3, (TXT("Number of packets in receive queue: %d\n"), queue_length));

    if (queue_length == 0) {
      // The buffer is full
      fileNodePtr->overruns++;
      DEBUGPRINT(2, (TXT("File node overrun\n")));
      // Mark message
      vCanPopReceiveBuffer(&fileNodePtr->rcv);
      fileNodePtr->rcv.fileRcvBuffer[fileNodePtr->rcv.bufTail].tagData.msg.flags |= VCAN_MSG_FLAG_OVERRUN;
    }
    os_if_spin_unlock_irqrestore(&fileNodePtr->rcv.rcvLock, rcvLock_irqFlags);
  }
  os_if_spin_unlock_irqrestore(&chd->openLock, openLock_irqFlags);

  return 0;
}
EXPORT_SYMBOL(vCanDispatchEvent);


//======================================================================
//    Open - File operation
//======================================================================

int vCanOpen (struct inode *inode, struct file *filp)
{
  VCanOpenFileNode *openFileNodePtr;
  VCanDriverData *driverData;
  VCanCardData *cardData = NULL;
  VCanChanData *chanData = NULL;
  unsigned long irqFlags;
  int minorNr;
  int channelNr;

  driverData = container_of(inode->i_cdev, VCanDriverData, cdev);
  minorNr = MINOR(inode->i_rdev);

  DEBUGPRINT(2, (TXT("VCanOpen minor %d major (%d)\n"),
                 minorNr, MAJOR(inode->i_rdev)));

  // Make sure seeks do not work on the file
  filp->f_mode &= ~(FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);

  os_if_spin_lock(&driverData->canCardsLock);
  if (driverData->canCards == NULL) {
    os_if_spin_unlock(&driverData->canCardsLock);
    DEBUGPRINT(1, (TXT("NO CAN cards available at this point. \n")));
    return -ENODEV;
  }

  // Find the right minor inode number
  for (cardData = driverData->canCards; cardData != NULL; cardData = cardData->next) {
    for (channelNr = 0; channelNr < cardData->nrChannels; channelNr++) {
      if (minorNr == cardData->chanData[channelNr]->minorNr) {
        chanData = cardData->chanData[channelNr];
        break;
      }
    }
  }
  os_if_spin_unlock(&driverData->canCardsLock);
  if (chanData == NULL) {
    DEBUGPRINT(1, (TXT("FAILURE: Unable to open minor %d major (%d)\n"),
                   minorNr, MAJOR(inode->i_rdev)));
    return -ENODEV;
  }

  // Allocate memory and zero the whole struct
  openFileNodePtr = os_if_kernel_malloc(sizeof(VCanOpenFileNode));
  if (openFileNodePtr == NULL) {
    return -ENOMEM;
  }
  memset(openFileNodePtr, 0, sizeof(VCanOpenFileNode));

  os_if_init_sema(&openFileNodePtr->ioctl_mutex);
  os_if_up_sema(&openFileNodePtr->ioctl_mutex);

  openFileNodePtr->rcv.size = sizeof(openFileNodePtr->rcv.fileRcvBuffer) / sizeof(openFileNodePtr->rcv.fileRcvBuffer[0]);

    // Init wait queue
  os_if_init_waitqueue_head(&(openFileNodePtr->rcv.rxWaitQ));

  vCanFlushReceiveBuffer(openFileNodePtr);
  openFileNodePtr->filp               = filp;
  openFileNodePtr->chanData           = chanData;
  openFileNodePtr->writeIsBlock       = 1;
  openFileNodePtr->readIsBlock        = 1;
  openFileNodePtr->modeTx             = 0;
  openFileNodePtr->modeTxRq           = 0;
  openFileNodePtr->modeNoTxEcho       = 0;
  openFileNodePtr->writeTimeout       = -1;
  openFileNodePtr->readTimeout        = -1;
  openFileNodePtr->transId            = atomic_read(&chanData->chanId);
  atomic_add(1, &chanData->chanId);
  openFileNodePtr->filter.eventMask   = ~0;
  openFileNodePtr->overruns           = 0;
  os_if_spin_lock_init(&(openFileNodePtr->rcv.rcvLock));

  // Insert this node first in list of "opens"
  os_if_spin_lock_irqsave(&chanData->openLock, &irqFlags);
  openFileNodePtr->chanNr             = -1;
  openFileNodePtr->channelLocked      = 0;
  openFileNodePtr->channelOpen        = 0;
  atomic_inc(&chanData->fileOpenCount);
  openFileNodePtr->next  = chanData->openFileList;
  chanData->openFileList = openFileNodePtr;
  os_if_spin_unlock_irqrestore(&chanData->openLock, irqFlags);

  // We want a pointer to the node as private_data
  filp->private_data = openFileNodePtr;

  return 0;
}



/*======================================================================*/
/*    Release - File operation                                          */
/*======================================================================*/

int vCanClose (struct inode *inode, struct file *filp)
{
  VCanOpenFileNode **openPtrPtr;
  VCanOpenFileNode *fileNodePtr;
  VCanChanData *chanData;
  unsigned long irqFlags;
  VCanHWInterface *hwIf;

  fileNodePtr = filp->private_data;
  chanData    = fileNodePtr->chanData;

  os_if_spin_lock_irqsave(&chanData->openLock, &irqFlags);
  // Find the open file node
  openPtrPtr = &chanData->openFileList;
  for(; *openPtrPtr != NULL; openPtrPtr = &((*openPtrPtr)->next)) {
    if ((*openPtrPtr)->filp == filp) {
      break;
    }
  }
  // We did not find anything?
  if (*openPtrPtr == NULL) {
    os_if_spin_unlock_irqrestore(&chanData->openLock, irqFlags);
    return -EBADF;
  }
  // openPtrPtr now points to the next-pointer that points to the correct node
  if (fileNodePtr != *openPtrPtr) {
    DEBUGPRINT(1, (TXT("VCanClose - not same fileNodePtr: %p vs %p\n"),
                   fileNodePtr, *openPtrPtr));
  }
  fileNodePtr = *openPtrPtr;

  fileNodePtr->chanNr = -1;
  fileNodePtr->channelLocked = 0;
  fileNodePtr->channelOpen   = 0;

  // Remove node
  *openPtrPtr = (*openPtrPtr)->next;

  os_if_spin_unlock_irqrestore(&chanData->openLock, irqFlags);

  os_if_delete_sema(&fileNodePtr->ioctl_mutex);
  
  hwIf = chanData->vCard->driverData->hwIf;
  if (atomic_read(&chanData->fileOpenCount) == 1) {
    hwIf->busOff(chanData);
    if (fileNodePtr->objbuf) {
      // Driver-implemented auto response buffers.
      objbuf_shutdown(fileNodePtr);
      os_if_kernel_free(fileNodePtr->objbuf);
      DEBUGPRINT(2, (TXT("Driver objbuf handling shut down.\n")));
    }
    if (hwIf->objbufFree) {
      if (chanData->vCard->card_flags & DEVHND_CARD_AUTO_RESP_OBJBUFS) {
        // Firmware-implemented auto response buffers
        hwIf->objbufFree(chanData, OBJBUF_TYPE_AUTO_RESPONSE, -1);
      }
      if (chanData->vCard->card_flags & DEVHND_CARD_AUTO_TX_OBJBUFS) {
        // Firmware-implemented periodic transmit buffers
        hwIf->objbufFree(chanData, OBJBUF_TYPE_PERIODIC_TX, -1);
      }
    }
  }

  if (fileNodePtr != NULL) {
    os_if_kernel_free(fileNodePtr);
    fileNodePtr = NULL;
  }

  atomic_dec(&chanData->fileOpenCount);

  DEBUGPRINT(2, (TXT("VCanClose minor %d major (%d)\n"),
                 chanData->minorNr, MAJOR(inode->i_rdev)));

  return 0;
}


//======================================================================
// Returns whether the transmit queue on a specific channel is full
//======================================================================

int txQFull (VCanChanData *chd)
{
  return queue_full(&chd->txChanQueue);
}


//======================================================================
// Returns whether the transmit queue on a specific channel is empty
//======================================================================

int txQEmpty (VCanChanData *chd)
{
  return queue_empty(&chd->txChanQueue);
}

//======================================================================
// Returns whether the receive queue on a specific channel is empty
//======================================================================

int rxQEmpty (VCanOpenFileNode *fileNodePtr)
{
  int           retval;
  unsigned long rcvLock_irqFlags;

  os_if_spin_lock_irqsave (&fileNodePtr->rcv.rcvLock, &rcvLock_irqFlags);
  retval = (getQLen(fileNodePtr->rcv.bufHead, fileNodePtr->rcv.bufTail, fileNodePtr->rcv.size) == 0);
  os_if_spin_unlock_irqrestore (&fileNodePtr->rcv.rcvLock, rcvLock_irqFlags);
  return retval;
}

//======================================================================
//  IOCtl - File operation
//======================================================================



static int ioctl (VCanOpenFileNode *fileNodePtr,
                  unsigned int ioctl_cmd, unsigned long arg)
{
  VCanChanData      *chd;
  unsigned long     timeLeft;
  unsigned long     timeout;
  OS_IF_WAITQUEUE   wait;
  int               ret;
  int               vStat = VCAN_STAT_OK;
  int               chanNr;
  unsigned long     irqFlags;
  int               tmp;
  VCanHWInterface   *hwIf;

  chd = fileNodePtr->chanData;
  hwIf = chd->vCard->driverData->hwIf;

  switch (ioctl_cmd) {
  //------------------------------------------------------------------
    case VCAN_IOC_SENDMSG:
      ArgPtrIn(sizeof(CAN_MSG));
      if (!fileNodePtr->writeIsBlock && txQFull(chd)) {
        DEBUGPRINT(2, (TXT("VCAN_IOC_SENDMSG - returning -EAGAIN\n")));
        return -EAGAIN;
      }
      
      if (chd->vCard->card_flags & DEVHND_CARD_REFUSE_TO_USE_CAN) {
        DEBUGPRINT(2, (TXT("VCAN_IOC_SENDMSG Refuse to run. returning -EACCES\n")));
        return -EACCES;        
      }

      os_if_init_waitqueue_entry(&wait);
      queue_add_wait_for_space(&chd->txChanQueue, &wait);

      while (1) {
        os_if_set_task_interruptible();

        if (txQFull(chd)) {
          if (fileNodePtr->writeTimeout != -1) {
            if (os_if_wait_for_event_timeout(fileNodePtr->writeTimeout,
                                             &wait) == 0) {
              // Transmit timed out
              queue_remove_wait_for_space(&chd->txChanQueue, &wait);
              DEBUGPRINT(2, (TXT("VCAN_IOC_SENDMSG - returning -EAGAIN 2\n")));
              return -EAGAIN;
            }

          } else {
            os_if_wait_for_event(queue_space_event(&chd->txChanQueue));
          }
          if (signal_pending(current)) {
            // Sleep was interrupted by signal
            queue_remove_wait_for_space(&chd->txChanQueue, &wait);
            return -ERESTARTSYS;
          }
        }
        else {
          CAN_MSG *bufMsgPtr;
          CAN_MSG message;
          int queuePos;

          // The copy from user memory can sleep, so it must
          // not be done while holding the queue lock.
          // This means an extra memcpy() from a stack buffer,
          // but that can only be avoided by changing the queue
          // buffer "allocation" method (queue_back/push).
          if (os_if_set_user_data(&message, (CAN_MSG *)arg, sizeof(CAN_MSG))) {
            DEBUGPRINT(2, (TXT("VCAN_IOC_SENDMSG - returning -EFAULT\n")));
            return -EFAULT;
          }

          queuePos = queue_back(&chd->txChanQueue);
          if (queuePos < 0) {
            queue_release(&chd->txChanQueue);
            continue;
          }
          bufMsgPtr = &chd->txChanBuffer[queuePos];

          os_if_set_task_running();
          queue_remove_wait_for_space(&chd->txChanQueue, &wait);

          memcpy(bufMsgPtr, &message, sizeof(CAN_MSG));

          // This is for keeping track of the originating fileNode
          bufMsgPtr->user_data = fileNodePtr->transId;
          bufMsgPtr->flags &= ~(VCAN_MSG_FLAG_TX_NOTIFY | VCAN_MSG_FLAG_TX_START);
          if (fileNodePtr->modeTx || (atomic_read(&chd->fileOpenCount) > 1)) {
            bufMsgPtr->flags |= VCAN_MSG_FLAG_TX_NOTIFY;
          }
          if (fileNodePtr->modeTxRq) {
            bufMsgPtr->flags |= VCAN_MSG_FLAG_TX_START;
          }

          queue_push(&chd->txChanQueue);

          // Exit loop
          break;
        }
      }
      hwIf->requestSend(chd->vCard, chd);
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_RECVMSG:
    {
        unsigned long rcvLock_irqFlags;

        ArgPtrOut(sizeof(VCAN_EVENT));
        if (!fileNodePtr->readIsBlock && rxQEmpty(fileNodePtr)) {
          DEBUGPRINT(3, (TXT("VCAN_IOC_RECVMSG - returning -EAGAIN, line = %d\n"),
                         __LINE__));
          DEBUGPRINT(3, (TXT("head = %d, tail = %d, size = %d, ")
                         TXT2("readIsBlock = %d\n"),
                         fileNodePtr->rcv.bufHead, fileNodePtr->rcv.bufTail,
                         fileNodePtr->rcv.size, fileNodePtr->readIsBlock));
          return -EAGAIN;
        }
        
        os_if_init_waitqueue_entry(&wait);
        os_if_add_wait_queue(&fileNodePtr->rcv.rxWaitQ, &wait);
        while(1) {
          os_if_set_task_interruptible();
          
          if (rxQEmpty(fileNodePtr)) {
            if (fileNodePtr->readTimeout != -1) {

              if (os_if_wait_for_event_timeout(fileNodePtr->readTimeout,
                                               &wait) == 0) {
                // Receive timed out
                os_if_remove_wait_queue(&fileNodePtr->rcv.rxWaitQ, &wait);
                // Reset when finished
                return -EAGAIN;
              }
            }
            else {
              os_if_wait_for_event(&fileNodePtr->rcv.rxWaitQ);
            }

            if (signal_pending(current)) {
              // Sleep was interrupted by signal
              os_if_remove_wait_queue(&fileNodePtr->rcv.rxWaitQ, &wait);
              DEBUGPRINT(4, (TXT("VCAN_IOC_RECVMSG - returning -ERESTARTSYS, "
                                 "line = %d\n"), __LINE__));
              return -ERESTARTSYS;
            }
          }
          // We have events in Q
          else {
            VCAN_EVENT msg;

            os_if_set_task_running();
            os_if_remove_wait_queue(&fileNodePtr->rcv.rxWaitQ, &wait);
            
            //don't copy data data to userspace within a spinlock
            os_if_spin_lock_irqsave(&fileNodePtr->rcv.rcvLock, &rcvLock_irqFlags);
            msg = fileNodePtr->rcv.fileRcvBuffer[fileNodePtr->rcv.bufTail];
            vCanPopReceiveBuffer(&fileNodePtr->rcv);
            os_if_spin_unlock_irqrestore(&fileNodePtr->rcv.rcvLock, rcvLock_irqFlags);

            copy_to_user_ret((VCAN_EVENT *)arg, &msg, sizeof(VCAN_EVENT), -EFAULT);
            break; // Exit loop
          }
        }
        break;
    }
    case VCAN_IOC_RECVMSG_SPECIFIC:
    {
      long          timeout = 0;
      unsigned long rcvLock_irqFlags;
      struct timeval start;

      ArgPtrOut(sizeof(VCAN_EVENT));

      os_if_do_get_time_of_day (&start);

      while (1) {
        if (timeout) {
          msleep (timeout);
        }

        if (signal_pending(current)) {
          // Sleep was interrupted by signal
          DEBUGPRINT(4, (TXT("VCAN_IOC_RECVMSG_SPECIFIC - returning -ERESTARTSYS, ""line = %d\n"), __LINE__));
          return -ERESTARTSYS;
        }

        {
          uint32_t   found = 0;
          VCAN_EVENT msg;
          
          if (!rxQEmpty (fileNodePtr)) {
            os_if_spin_lock_irqsave (&fileNodePtr->rcv.rcvLock, &rcvLock_irqFlags);
            found = read_specific (fileNodePtr, &msg);
            os_if_spin_unlock_irqrestore (&fileNodePtr->rcv.rcvLock, rcvLock_irqFlags);
          }

          if (found) {
            copy_to_user_ret((VCAN_EVENT *)arg, &msg, sizeof(VCAN_EVENT), -EFAULT);
            break;         
          } else {
            timeout = calc_timeout (&start, fileNodePtr->read_specific.timeout);
            if (timeout == 0) {
              if (fileNodePtr->read_specific.skip == READ_SPECIFIC_NO_SKIP) {
                return -ETIMEDOUT;
              } else {
                return -EAGAIN;
              }
            }
          }
        }
      }
      break;
    }

   

    //------------------------------------------------------------------
    case VCAN_IOC_BUS_ON:
      {
        unsigned long ttime;
        unsigned long rcvLock_irqFlags;

        DEBUGPRINT(3, (TXT("VCAN_IOC_BUS_ON\n")));
        if (chd->vCard->card_flags & DEVHND_CARD_REFUSE_TO_USE_CAN) {
          DEBUGPRINT(2, (TXT("VCAN_IOC_BUS_ON Refuse to run. returning -EACCES\n")));
          return -EACCES;        
        }
        
        os_if_spin_lock_irqsave(&fileNodePtr->rcv.rcvLock, &rcvLock_irqFlags);
        vCanFlushReceiveBuffer(fileNodePtr);
        os_if_spin_unlock_irqrestore(&fileNodePtr->rcv.rcvLock, rcvLock_irqFlags);

        vStat = hwIf->flushSendBuffer(chd);
        memset(&chd->busStats, 0, sizeof(chd->busStats));
        vCanTime(chd->vCard, &ttime);
        chd->busStats.timestamp = (__u32) ttime;
        if (vStat == VCAN_STAT_OK) {
          vStat = hwIf->busOn(chd);
        }
        // Make synchronous?
      }
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_BUS_OFF:
      DEBUGPRINT(3, (TXT("VCAN_IOC_BUS_OFF\n")));
      vStat = hwIf->busOff(chd);
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_SET_BITRATE:
      {
        VCanBusParams busParams;
        ArgPtrOut(sizeof(VCanBusParams));   // Check first, to make sure
        ArgPtrIn(sizeof(VCanBusParams));
        
        if (chd->vCard->card_flags & DEVHND_CARD_REFUSE_TO_USE_CAN) {
          DEBUGPRINT(2, (TXT("VCAN_IOC_SET_BITRATE Refuse to run. returning -EACCES\n")));
          return -EACCES;        
        }
        
        copy_from_user_ret(&busParams, (VCanBusParams *)arg,
                           sizeof(VCanBusParams), -EFAULT);

        vStat = hwIf->setBusParams(chd, &busParams);
        if (vStat != VCAN_STAT_OK) {
          DEBUGPRINT(4, (TXT("hwIf->setBusParams(...) = %d\n"), vStat));
        } else {
          VCanBusParams busParamsSet;
          vStat = hwIf->getBusParams(chd, &busParamsSet);
          // Indicate that something went wrong by setting freq to 0
          if (vStat == VCAN_STAT_BAD_PARAMETER) {
            DEBUGPRINT(4, (TXT("Some bus parameter bad\n")));
            busParams.freq = 0;
          } else if (vStat != VCAN_STAT_OK) {
            DEBUGPRINT(4, (TXT("hwIf->getBusParams(...) = %d\n"), vStat));
          } else {
            if (busParamsSet.freq != busParams.freq) {
              DEBUGPRINT(2, (TXT("Bad freq: %d vs %d\n"),
                             busParamsSet.freq, busParams.freq));
              busParams.freq = 0;
            }
            if (busParams.freq > 1000000) {
              DEBUGPRINT(2, (TXT("Too high freq: %d\n"), busParams.freq));
            }
            if (busParamsSet.sjw != busParams.sjw) {
              DEBUGPRINT(2, (TXT("Bad sjw: %d vs %d\n"),
                             busParamsSet.sjw, busParams.sjw));
              busParams.freq = 0;
            }
            if (busParamsSet.tseg1 != busParams.tseg1) {
              DEBUGPRINT(2, (TXT("Bad tseg1: %d vs %d\n"),
                             busParamsSet.tseg1, busParams.tseg1));
              busParams.freq = 0;
            }
            if (busParamsSet.tseg2 != busParams.tseg2) {
              DEBUGPRINT(2, (TXT("Bad tseg2: %d vs %d\n"),
                             busParamsSet.tseg2, busParams.tseg2));
              busParams.freq = 0;
            }
          }
          if (busParams.freq == 0) {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
          else {
            chd->busStats.bitTime100ns = 10000000 / busParams.freq;
          }
        }

        ArgPtrOut(sizeof(VCanBusParams));
        copy_to_user_ret((VCanBusParams *)arg, &busParams,
                         sizeof(VCanBusParams), -EFAULT);
      }
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_BITRATE:
      ArgPtrOut(sizeof(VCanBusParams));
      {
        VCanBusParams busParams;
        vStat = hwIf->getBusParams(chd, &busParams);
        copy_to_user_ret((VCanBusParams *)arg, &busParams,
                         sizeof(VCanBusParams), -EFAULT);
      }
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_SET_OUTPUT_MODE:
      ArgIntIn;
      vStat = hwIf->setOutputMode(chd, arg);
      if (VCAN_STAT_OK == vStat) {
        chd->driverMode = arg;
      }
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_OUTPUT_MODE:
      ArgPtrOut(sizeof(unsigned int));
      put_user_ret(chd->driverMode, (unsigned int *)arg, -EFAULT);
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_SET_MSG_FILTER:
      ArgPtrIn(sizeof(VCanMsgFilter));
      copy_from_user_ret(&(fileNodePtr->filter), (VCanMsgFilter *)arg,
                         sizeof(VCanMsgFilter), -EFAULT);
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_MSG_FILTER:
      ArgPtrOut(sizeof(VCanMsgFilter));
      copy_to_user_ret((VCanMsgFilter *)arg, &(fileNodePtr->filter),
                       sizeof(VCanMsgFilter), -EFAULT);
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_OPEN_TRANSP:
      // This is really a NOP. Implemented to simplify CANlib.
      os_if_get_int(&chanNr, (int *)arg);
      ret = os_if_set_int(chanNr, (int *)arg);
      if (ret) {
        DEBUGPRINT(4, (TXT("VCAN_IOC_OPEN_TRANSP - returning -EFAULT\n")));
        return -EFAULT;
      }
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_OPEN_CHAN:
      os_if_get_int(&chanNr, (int *)arg);
      os_if_spin_lock_irqsave(&chd->openLock, &irqFlags);
      {
        VCanOpenFileNode *tmpFnp;
        for (tmpFnp = chd->openFileList; tmpFnp && !tmpFnp->channelLocked;
             tmpFnp = tmpFnp->next) {
          /* Empty loop */
        }
        // Was channel not locked (i.e. not opened exclusive) before?
        if (!tmpFnp) {
          fileNodePtr->channelOpen = 1;
          fileNodePtr->chanNr = chanNr;
        }
        os_if_spin_unlock_irqrestore(&chd->openLock, irqFlags);
        if (tmpFnp) {
          ret = os_if_set_int(-1, (int *)arg);
        } else {
          ret = os_if_set_int(chanNr, (int *)arg);
        }
      }
      if (ret) {
        DEBUGPRINT(4, (TXT("VCAN_IOC_OPEN_CHAN - returning -EFAULT\n")));
        return -EFAULT;
      }
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_OPEN_EXCL:
      os_if_get_int(&chanNr, (int *)arg);
      os_if_spin_lock_irqsave(&chd->openLock, &irqFlags);
      {
        VCanOpenFileNode *tmpFnp;
        // Return -1 if channel is opened by someone else
        for (tmpFnp = chd->openFileList; tmpFnp && !tmpFnp->channelOpen;
             tmpFnp = tmpFnp->next) {
          /* Empty loop */
        }
        // Was channel not opened before?
        if (!tmpFnp) {
          fileNodePtr->channelOpen = 1;
          fileNodePtr->channelLocked = 1;
          fileNodePtr->chanNr = chanNr;
        }
        os_if_spin_unlock_irqrestore(&chd->openLock, irqFlags);
        if (tmpFnp) {
          ret = os_if_set_int(-1, (int *)arg);
        } else {
          ret = os_if_set_int(chanNr, (int *)arg);
        }
      }
      if (ret) {
        DEBUGPRINT(4, (TXT("VCAN_IOC_OPEN_EXCL - returning -EFAULT\n")));
        return -EFAULT;
      }
      break;

    //------------------------------------------------------------------
    case VCAN_IOC_WAIT_EMPTY:
      ArgPtrIn(sizeof(unsigned long));
      get_user_long_ret(&timeout, (unsigned long *)arg, -EFAULT);
      timeLeft = -1;
      set_bit(0, &chd->waitEmpty);

      timeLeft = os_if_wait_event_interruptible_timeout(chd->flushQ,
          txQEmpty(chd) && hwIf->txAvailable(chd), timeout);
      clear_bit(0, &chd->waitEmpty);

      if (timeLeft == OS_IF_TIMEOUT) {
        /*
        DEBUGPRINT(2, (TXT("VCAN_IOC_WAIT_EMPTY- EAGAIN (TxQLen = %ld, TxAvail = %ld\n"),
                       queue_length(&chd->txChanQueue),
                       hwIf->txQLen(chd)));
                       */
        return -EAGAIN;
      }
      break;
    //------------------------------------------------------------------
# define VCARD chd->vCard
    case VCAN_IOC_GET_NRCHANNELS:
      ArgPtrOut(sizeof(int));
      put_user_ret(VCARD->nrChannels, (int *)arg, -EFAULT);
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_SERIAL:
      ArgPtrOut(8);
      put_user_ret(VCARD->serialNumber, (int *)arg, -EFAULT);
      put_user_ret(0, ((int *)arg) + 1, -EFAULT);
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_FIRMWARE_REV:
      ArgPtrOut(8);
      tmp = (VCARD->firmwareVersionMajor << 16) |
             VCARD->firmwareVersionMinor;
      put_user_ret(tmp, ((int *)arg) + 1, -EFAULT);
      put_user_ret(VCARD->firmwareVersionBuild, ((int *)arg), -EFAULT);
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_EAN:
      ArgPtrOut(8);
      put_user_ret(((int *)VCARD->ean)[0], (int *)arg, -EFAULT);
      put_user_ret(((int *)VCARD->ean)[1], ((int *)arg) + 1, -EFAULT);
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_HARDWARE_REV:
      ArgPtrOut(8);
      tmp = (VCARD->hwRevisionMajor << 16) |
             VCARD->hwRevisionMinor;
      put_user_ret(tmp, ((int *)arg), -EFAULT);
      put_user_ret(0, ((int *)arg) + 1, -EFAULT);
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_CARD_TYPE:
      ArgPtrOut(sizeof(int));
      put_user_ret(VCARD->hw_type, (int *)arg, -EFAULT);
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_CHAN_CAP:
      ArgPtrOut(sizeof(int));
      put_user_ret(chd->capabilities, (int *)arg, -EFAULT);
      break;
    case VCAN_IOC_GET_CHAN_CAP_MASK:
      ArgPtrOut(sizeof(int));
      put_user_ret(chd->capabilities_mask, (int *)arg, -EFAULT);
      break;
   case VCAN_IOC_GET_MAX_BITRATE:
      ArgPtrOut(sizeof(uint32_t));
      put_user_ret(VCARD->current_max_bitrate, (uint32_t *)arg, -EFAULT);
      break;
#undef VCARD
    //------------------------------------------------------------------
    case VCAN_IOC_FLUSH_RCVBUFFER:
      {
        unsigned long    rcvLock_irqFlags;
        os_if_spin_lock_irqsave(&fileNodePtr->rcv.rcvLock, &rcvLock_irqFlags);
        vCanFlushReceiveBuffer(fileNodePtr);
        os_if_spin_unlock_irqrestore(&fileNodePtr->rcv.rcvLock, rcvLock_irqFlags);
        break;
      }
    //------------------------------------------------------------------
    case VCAN_IOC_FLUSH_SENDBUFFER:
      vStat = hwIf->flushSendBuffer(chd);
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_SET_WRITE_BLOCK:
      ArgIntIn;
      fileNodePtr->writeIsBlock = (unsigned char)arg;
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_SET_READ_BLOCK:
      ArgIntIn;
      fileNodePtr->readIsBlock = (unsigned char)arg;
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_SET_WRITE_TIMEOUT:
      ArgIntIn;
      fileNodePtr->writeTimeout = arg;
      break;
   //------------------------------------------------------------------
    case VCAN_IOC_SET_READ_SPECIFIC:
       ArgPtrIn(sizeof(VCanReadSpecific));
       copy_from_user_ret(&fileNodePtr->read_specific, (VCanReadSpecific *)arg,
                          sizeof(VCanReadSpecific), -EFAULT);
       break;
    //------------------------------------------------------------------
    case VCAN_IOC_SET_READ_TIMEOUT:
      ArgIntIn;
      fileNodePtr->readTimeout = arg;
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_READ_TIMER:
      ArgPtrOut(sizeof(unsigned long));
      {
        unsigned long time;
        vStat = hwIf->getTime(chd->vCard, &time);
        if (vStat == VCAN_STAT_OK) {
          copy_to_user_ret((unsigned long *)arg, &time, sizeof(unsigned long), -EFAULT);
        }
      }
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_TX_ERR:
      ArgPtrOut(sizeof(int));
      put_user_ret(hwIf->getTxErr(chd), (int *)arg, -EFAULT);
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_RX_ERR:
      ArgPtrOut(sizeof(int));
      put_user_ret(hwIf->getRxErr(chd), (int *)arg, -EFAULT);
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_OVER_ERR:
      ArgPtrOut(sizeof(int));
      put_user_ret(fileNodePtr->overruns, (int *)arg, -EFAULT);
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_RESET_OVERRUN_COUNT:
      fileNodePtr->overruns = 0;
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_RX_QUEUE_LEVEL:
      ArgPtrOut(sizeof(int));
      {
        int           ql;
        unsigned long rcvLock_irqFlags;

        os_if_spin_lock_irqsave(&fileNodePtr->rcv.rcvLock, &rcvLock_irqFlags);
        ql = getQLen(fileNodePtr->rcv.bufHead, fileNodePtr->rcv.bufTail,
                     fileNodePtr->rcv.size);
        os_if_spin_unlock_irqrestore(&fileNodePtr->rcv.rcvLock, rcvLock_irqFlags);
        put_user_ret(ql, (int *)arg, -EFAULT);
      }
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_TX_QUEUE_LEVEL:
      ArgPtrOut(sizeof(int));
      {
        int ql = queue_length(&chd->txChanQueue) + hwIf->txQLen(chd);
        put_user_ret(ql, (int *)arg, -EFAULT);
      }
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_CHIP_STATE:
      ArgPtrOut(sizeof(int));
      {
        hwIf->requestChipState(chd);
        put_user_ret(chd->chipState.state, (int *)arg, -EFAULT);
      }
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_GET_TXACK:
      ArgPtrOut(sizeof(int));
      {
        DEBUGPRINT(3, (TXT("KVASER VCAN_IOC_GET_TXACK, was %d\n"), fileNodePtr->modeTx));
        put_user_ret(fileNodePtr->modeTx, (int *)arg, -EFAULT);
        break;
      }
    //------------------------------------------------------------------
    case VCAN_IOC_SET_TXACK:
      ArgPtrIn(sizeof(int));
      {
        DEBUGPRINT(3, (TXT("KVASER Try to set VCAN_IOC_SET_TXACK to %d, was %d\n"),
                       *(int *)arg, fileNodePtr->modeTx));
        if (*(int *)arg >= 0 && *(int *)arg <= 2) {
          fileNodePtr->modeTx = *(int *)arg;
          DEBUGPRINT(3, (TXT("KVASER Managed to set VCAN_IOC_SET_TXACK to %d\n"),
                         fileNodePtr->modeTx));
        }
        else {
          return -EFAULT;
        }
      }
      break;
    //------------------------------------------------------------------
    case VCAN_IOC_SET_TXRQ:
      ArgPtrIn(sizeof(int));
      {
        DEBUGPRINT(3, (TXT("KVASER Try to set VCAN_IOC_SET_TXRQ to %d, was %d\n"),
                       *(int *)arg, fileNodePtr->modeTxRq));
        if (*(int *)arg >= 0 && *(int *)arg <= 2) {
          fileNodePtr->modeTxRq = *(int *)arg;
          DEBUGPRINT(3, (TXT("KVASER Managed to set VCAN_IOC_SET_TXRQ to %d\n"),
                         fileNodePtr->modeTxRq));
        }
        else {
          return -EFAULT;
        }
      }
      break;
    //------------------------------------------------------------------  
    case VCAN_IOC_SET_TXECHO:
      ArgPtrIn(sizeof(char));
      {
        int flag = *(unsigned char *)arg;
        DEBUGPRINT(3, (TXT("KVASER Try to set VCAN_IOC_SET_TXECHO to %d, was %d\n"),
                       flag, !fileNodePtr->modeNoTxEcho));
        if (flag >= 0 && flag <= 2) {
          fileNodePtr->modeNoTxEcho = !flag;
          DEBUGPRINT(3, (TXT("KVASER Managed to set VCAN_IOC_SET_TXECHO to %d\n"),
                         !fileNodePtr->modeNoTxEcho));
        }
        else {
          return -EFAULT;
        }
      }
      break;
    //------------------------------------------------------------------
    case KCAN_IOCTL_OBJBUF_FREE_ALL:
      if (fileNodePtr->objbuf) {
        // Driver-implemented auto response buffers.
        int i;
        for (i = 0; i < MAX_OBJECT_BUFFERS; i++) {
          fileNodePtr->objbuf[i].in_use = 0;
        }
      }
      if (hwIf->objbufFree) {
        if (chd->vCard->card_flags & DEVHND_CARD_AUTO_RESP_OBJBUFS) {
          // Firmware-implemented auto response buffers
          vStat = hwIf->objbufFree(chd, OBJBUF_TYPE_AUTO_RESPONSE, -1);
        }
        if (chd->vCard->card_flags & DEVHND_CARD_AUTO_TX_OBJBUFS) {
          // Firmware-implemented periodic transmit buffers
          vStat = hwIf->objbufFree(chd, OBJBUF_TYPE_PERIODIC_TX, -1);
        }
      }
      break;
    //------------------------------------------------------------------
    case KCAN_IOCTL_TX_INTERVAL:
      {
        uint32_t tmp = *(uint32_t*)arg;
        vStat = hwIf->tx_interval(chd, &tmp);
        copy_to_user_ret((uint32_t*)arg, &tmp, sizeof(uint32_t), -EFAULT);
      }
      break;
    case KCAN_IOCTL_SET_BRLIMIT:
      {
        if (*(uint32_t*)arg == 0) {
          chd->vCard->current_max_bitrate = chd->vCard->default_max_bitrate;
        } else {
          copy_from_user_ret (&chd->vCard->current_max_bitrate, (uint32_t*)arg, sizeof(uint32_t), -EFAULT);
        }
      }
      break;
    case KCAN_IOCTL_OBJBUF_ALLOCATE:
      {
        KCanObjbufAdminData io;
        ArgPtrOut(sizeof(KCanObjbufAdminData));   // Check first, to make sure
        ArgPtrIn(sizeof(KCanObjbufAdminData));
        copy_from_user_ret(&io, (KCanObjbufAdminData *)arg,
                           sizeof(KCanObjbufAdminData), -EFAULT);

        DEBUGPRINT(2, (TXT("ObjBuf Allocate type=%d card_flags=0x%x\n"),
                       io.type, chd->vCard->card_flags));
        
        vStat = VCAN_STAT_NO_RESOURCES;
        if (io.type == OBJBUF_TYPE_AUTO_RESPONSE) {
          if (chd->vCard->card_flags & DEVHND_CARD_AUTO_RESP_OBJBUFS) {
            // Firmware-implemented auto response buffers
            if (hwIf->objbufAlloc) {
              vStat = hwIf->objbufAlloc(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                       &io.buffer_number);
            }
          } else {
            // Driver-implemented auto response buffers
            int i;
            if (!fileNodePtr->objbuf) {
              fileNodePtr->objbuf = os_if_kernel_malloc(sizeof(OBJECT_BUFFER) *
                                                        MAX_OBJECT_BUFFERS);
              if (!fileNodePtr->objbuf) {
                vStat = VCAN_STAT_NO_MEMORY;
              } else {
                memset(fileNodePtr->objbuf, 0,
                       sizeof(OBJECT_BUFFER) * MAX_OBJECT_BUFFERS);
                objbuf_init(fileNodePtr);
              }
            }
            if (fileNodePtr->objbuf) {
              vStat = VCAN_STAT_NO_MEMORY;
              for (i = 0; i < MAX_OBJECT_BUFFERS; i++) {
                if (!fileNodePtr->objbuf[i].in_use) {
                  io.buffer_number              = i | OBJBUF_DRIVER_MARKER;
                  fileNodePtr->objbuf[i].in_use = 1;
                  vStat                         = VCAN_STAT_OK;
                  break;
                }
              }
            }
          }
        }
        else if (io.type == OBJBUF_TYPE_PERIODIC_TX) {
          if (chd->vCard->card_flags & DEVHND_CARD_AUTO_TX_OBJBUFS) {
            // Firmware-implemented periodic transmit buffers
            if (hwIf->objbufAlloc) {
              vStat = hwIf->objbufAlloc(chd, OBJBUF_TYPE_PERIODIC_TX,
                                       &io.buffer_number);
            }
          }
        }

        DEBUGPRINT(2, (TXT("ObjBuf Allocate got nr=%x stat=0x%x\n"),
                       io.buffer_number, vStat));

        ArgPtrOut(sizeof(KCanObjbufAdminData));
        copy_to_user_ret((KCanObjbufAdminData *)arg, &io,
                         sizeof(KCanObjbufAdminData), -EFAULT);
      }
      break;
    //------------------------------------------------------------------
    case KCAN_IOCTL_OBJBUF_FREE:
      {
        KCanObjbufAdminData io;
        ArgPtrIn(sizeof(KCanObjbufAdminData));
        copy_from_user_ret(&io, (KCanObjbufAdminData *)arg,
                           sizeof(KCanObjbufAdminData), -EFAULT);

        DEBUGPRINT(2, (TXT("ObjBuf Free nr=%x card_flags=0x%x\n"),
                       io.buffer_number, chd->vCard->card_flags));
        
        if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
          int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;
          if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS)) {
            fileNodePtr->objbuf[buffer_number].in_use = 0;
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else if (hwIf->objbufExists && hwIf->objbufFree) {
          if (hwIf->objbufExists(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                io.buffer_number)) {
            vStat = hwIf->objbufFree(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                    io.buffer_number);
          }
          else if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX,
                                     io.buffer_number)) {
            vStat = hwIf->objbufFree(chd, OBJBUF_TYPE_PERIODIC_TX,
                                    io.buffer_number);
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else {
          vStat = VCAN_STAT_NO_RESOURCES;
        }
      }
      break;
    //------------------------------------------------------------------
    case KCAN_IOCTL_OBJBUF_WRITE:
      {
        KCanObjbufBufferData io;
        ArgPtrIn(sizeof(KCanObjbufBufferData));
        copy_from_user_ret(&io, (KCanObjbufBufferData *)arg,
                           sizeof(KCanObjbufBufferData), -EFAULT);

        DEBUGPRINT(2, (TXT("ObjBuf Write nr=%x card_flags=0x%x\n"),
                       io.buffer_number, chd->vCard->card_flags));
        
        if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
          int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;
          if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS) &&
              (fileNodePtr->objbuf[buffer_number].in_use)) {
            fileNodePtr->objbuf[buffer_number].msg.tag    = V_TRANSMIT_MSG;
            fileNodePtr->objbuf[buffer_number].msg.channel_index =
              (unsigned char)fileNodePtr->chanNr;
            fileNodePtr->objbuf[buffer_number].msg.id     = io.id;
            fileNodePtr->objbuf[buffer_number].msg.flags  =
              (unsigned char)io.flags;
            fileNodePtr->objbuf[buffer_number].msg.length =
              (unsigned char)io.dlc;
            memcpy(fileNodePtr->objbuf[buffer_number].msg.data, io.data, sizeof(io.data));
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else if (hwIf->objbufExists && hwIf->objbufWrite) {
          if (hwIf->objbufExists(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                io.buffer_number)) {
            vStat = hwIf->objbufWrite(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                     io.buffer_number,
                                     io.id, io.flags, io.dlc, io.data);
          }
          else if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX,
                                     io.buffer_number)) {
            vStat = hwIf->objbufWrite(chd, OBJBUF_TYPE_PERIODIC_TX,
                                     io.buffer_number,
                                     io.id, io.flags, io.dlc, io.data);
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else {
          vStat = VCAN_STAT_NO_RESOURCES;
        }
      }
      break;

    //------------------------------------------------------------------ 
    case KCAN_IOCTL_OBJBUF_SET_FILTER:
      {
        KCanObjbufAdminData io;
        ArgPtrIn(sizeof(KCanObjbufAdminData));
        copy_from_user_ret(&io, (KCanObjbufAdminData *)arg,
                           sizeof(KCanObjbufAdminData), -EFAULT);

        DEBUGPRINT(2, (TXT("ObjBuf SetFilter nr=%x card_flags=0x%x\n"),
                       io.buffer_number, chd->vCard->card_flags));

        if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
          int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;
          if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS) &&
              (fileNodePtr->objbuf[buffer_number].in_use))
          {
            fileNodePtr->objbuf[buffer_number].acc_code = io.acc_code;
            fileNodePtr->objbuf[buffer_number].acc_mask = io.acc_mask;
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else if (hwIf->objbufExists && hwIf->objbufSetFilter) {
          if (hwIf->objbufExists(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                io.buffer_number)) {
            vStat = hwIf->objbufSetFilter(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                         io.buffer_number,
                                         io.acc_code, io.acc_mask);
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else {
          vStat = VCAN_STAT_NO_RESOURCES;
        }
      }
      break;
    //------------------------------------------------------------------
    case KCAN_IOCTL_OBJBUF_SET_FLAGS:
      {
        KCanObjbufAdminData io;
        ArgPtrIn(sizeof(KCanObjbufAdminData));
        copy_from_user_ret(&io, (KCanObjbufAdminData *)arg,
                           sizeof(KCanObjbufAdminData), -EFAULT);
        
        DEBUGPRINT(2, (TXT("ObjBuf SetFlags nr=%x card_flags=0x%x\n"),
                       io.buffer_number, chd->vCard->card_flags));

        if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
          int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;
          if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS) &&
              (fileNodePtr->objbuf[buffer_number].in_use))
          {
            fileNodePtr->objbuf[buffer_number].flags = io.flags;
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else if (hwIf->objbufExists && hwIf->objbufSetFlags) {
          if (hwIf->objbufExists(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                io.buffer_number)) {
            vStat = hwIf->objbufSetFlags(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                        io.buffer_number,
                                        io.flags);
          } else if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX,
                                       io.buffer_number)) {
            vStat = hwIf->objbufSetFlags(chd, OBJBUF_TYPE_PERIODIC_TX,
                                        io.buffer_number,
                                        io.flags);
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else {
          vStat = VCAN_STAT_NO_RESOURCES;
        }
      }
      break;
    //------------------------------------------------------------------
    case KCAN_IOCTL_OBJBUF_ENABLE:
      {
        KCanObjbufAdminData io;
        ArgPtrIn(sizeof(KCanObjbufAdminData));
        copy_from_user_ret(&io, (KCanObjbufAdminData *)arg,
                           sizeof(KCanObjbufAdminData), -EFAULT);

        DEBUGPRINT(2, (TXT("ObjBuf Enable nr=%x card_flags=0x%x\n"),
                       io.buffer_number, chd->vCard->card_flags));
        
        if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
          int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;
          if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS) &&
              (fileNodePtr->objbuf[buffer_number].in_use))
          {
            fileNodePtr->objbuf[buffer_number].active = 1;
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else if (hwIf->objbufExists && hwIf->objbufEnable) {
          if (hwIf->objbufExists(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                io.buffer_number)) {
            vStat = hwIf->objbufEnable(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                      io.buffer_number, 1);
          } else if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX,
                                       io.buffer_number)) {
            vStat = hwIf->objbufEnable(chd, OBJBUF_TYPE_PERIODIC_TX,
                                      io.buffer_number, 1);
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else {
          vStat = VCAN_STAT_NO_RESOURCES;
        }
      }
      break;
    //------------------------------------------------------------------
    case KCAN_IOCTL_OBJBUF_DISABLE:
      {
        KCanObjbufAdminData io;
        ArgPtrIn(sizeof(KCanObjbufAdminData));
        copy_from_user_ret(&io, (KCanObjbufAdminData *)arg,
                           sizeof(KCanObjbufAdminData), -EFAULT);

        DEBUGPRINT(2, (TXT("ObjBuf Disable nr=%x card_flags=0x%x\n"),
                       io.buffer_number, chd->vCard->card_flags));

        if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
          int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;
          if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS) &&
              (fileNodePtr->objbuf[buffer_number].in_use))
          {
            fileNodePtr->objbuf[buffer_number].active = 0;
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else if (hwIf->objbufExists && hwIf->objbufEnable) {
          if (hwIf->objbufExists(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                io.buffer_number)) {
            vStat = hwIf->objbufEnable(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                      io.buffer_number, 0);
          } else if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX,
                                       io.buffer_number)) {
            vStat = hwIf->objbufEnable(chd, OBJBUF_TYPE_PERIODIC_TX,
                                      io.buffer_number, 0);
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else {
          vStat = VCAN_STAT_NO_RESOURCES;
        }
      }
      break;
    //------------------------------------------------------------------
    case KCAN_IOCTL_OBJBUF_SET_PERIOD:
      {
        KCanObjbufAdminData io;
        ArgPtrIn(sizeof(KCanObjbufAdminData));
        copy_from_user_ret(&io, (KCanObjbufAdminData *)arg,
                           sizeof(KCanObjbufAdminData), -EFAULT);

        DEBUGPRINT(2, (TXT("ObjBuf SetPeriod nr=%x card_flags=0x%x\n"),
                       io.buffer_number, chd->vCard->card_flags));

        if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
          int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;
          if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS) &&
              (fileNodePtr->objbuf[buffer_number].in_use))
          {
            // Driver-implemented auto tx buffers are not implemented.
            // fileNodePtr->objbuf[buffer_number].period = io.period;
            vStat = VCAN_STAT_NO_RESOURCES;   // Not implemented
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else if (hwIf->objbufExists && hwIf->objbufSetPeriod) {
          if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX,
                                io.buffer_number)) {
            vStat = hwIf->objbufSetPeriod(chd, OBJBUF_TYPE_PERIODIC_TX,
                                         io.buffer_number,
                                         io.period);
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else {
          vStat = VCAN_STAT_NO_RESOURCES;   // Not implemented
        }
      }
      break;
    //------------------------------------------------------------------
    case KCAN_IOCTL_OBJBUF_SET_MSG_COUNT:
      {
        KCanObjbufAdminData io;
        ArgPtrIn(sizeof(KCanObjbufAdminData));
        copy_from_user_ret(&io, (KCanObjbufAdminData *)arg,
                           sizeof(KCanObjbufAdminData), -EFAULT);

        DEBUGPRINT(2, (TXT("ObjBuf SetMsgCount nr=%x card_flags=0x%x\n"),
                       io.buffer_number, chd->vCard->card_flags));

        if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
          int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;
          if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS) &&
              (fileNodePtr->objbuf[buffer_number].in_use))
          {
            // Driver-implemented auto tx buffers are not implemented.
            // fileNodePtr->objbuf[buffer_number].period = io.period;
            vStat = VCAN_STAT_NO_RESOURCES;   // Not implemented
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else if (hwIf->objbufExists && hwIf->objbufSetMsgCount) {
          if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX,
                                io.buffer_number)) {
            vStat = hwIf->objbufSetMsgCount(chd, OBJBUF_TYPE_PERIODIC_TX,
                                           io.buffer_number,
                                           io.period);
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else {
          vStat = VCAN_STAT_NO_RESOURCES;   // Not implemented
        }
      }
      break;
    //------------------------------------------------------------------
    case KCAN_IOCTL_OBJBUF_SEND_BURST:
      {
        KCanObjbufAdminData io;
        ArgPtrIn(sizeof(KCanObjbufAdminData));
        copy_from_user_ret(&io, (KCanObjbufAdminData *)arg,
                           sizeof(KCanObjbufAdminData), -EFAULT);

        DEBUGPRINT(2, (TXT("ObjBuf SendBurst nr=%x card_flags=0x%x\n"),
                       io.buffer_number, chd->vCard->card_flags));

        if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
          int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;
          if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS) &&
              (fileNodePtr->objbuf[buffer_number].in_use))
          {
            // Driver-implemented auto tx buffers are not implemented.
            vStat = VCAN_STAT_NO_RESOURCES;   // Not implemented
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else if (hwIf->objbufExists && hwIf->objbufSendBurst) {
          if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX,
                                io.buffer_number)) {
            vStat = hwIf->objbufSendBurst(chd, OBJBUF_TYPE_PERIODIC_TX,
                                         io.buffer_number,
                                         io.period);
          } else {
            vStat = VCAN_STAT_BAD_PARAMETER;
          }
        } else {
          vStat = VCAN_STAT_NO_RESOURCES;   // Not implemented
        }
      }
      break;

  //------------------------------------------------------------------
  case KCAN_IOCTL_CANFD:
    {
      KCAN_IOCTL_CANFD_T fd_data;
      unsigned int openHandles = 0;
      unsigned char canfd_mode = OPEN_AS_CAN;
      unsigned int req_caps = 0;
      ArgPtrIn(sizeof(KCAN_IOCTL_CANFD_T));
      copy_from_user_ret(&fd_data, (KCAN_IOCTL_CANFD_T *)arg,
                           sizeof(KCAN_IOCTL_CANFD_T), -EFAULT);
      
      // Count the number of open handles to this channel
      os_if_spin_lock_irqsave(&chd->openLock, &irqFlags);
      {
        VCanOpenFileNode *tmpFnp;
        for (tmpFnp = chd->openFileList; tmpFnp; tmpFnp = tmpFnp->next) {
          openHandles++;
        }
      }
      os_if_spin_unlock_irqrestore(&chd->openLock, irqFlags);

      if (fd_data.action == CAN_CANFD_SET) 
        canfd_mode = fd_data.fd;
      else 
        canfd_mode = chd->canFdMode;
      fd_data.status = CAN_CANFD_NOT_IMPLEMENTED;      
      switch (canfd_mode) {
        case OPEN_AS_CANFD_NONISO:
          req_caps |= VCAN_CHANNEL_CAP_CANFD_NONISO;
          //intentional fall-through          
        case OPEN_AS_CANFD_ISO:
          req_caps |= VCAN_CHANNEL_CAP_CANFD;
          //intentional fall-through
        case OPEN_AS_CAN: {
          if (fd_data.action == CAN_CANFD_SET) {
            fd_data.status = CAN_CANFD_FAILURE;
            if ((chd->capabilities & req_caps) == req_caps) {
              // 1 since we have to be open to be here.
              if (1 == openHandles) {
                chd->canFdMode = fd_data.fd;
                fd_data.status = CAN_CANFD_SUCCESS;
              }
              else {
                if (fd_data.fd == chd->canFdMode) {
                  fd_data.status = CAN_CANFD_SUCCESS;
                }
                else {
                  fd_data.status = CAN_CANFD_MISMATCH;
                }
              }
            }
          }
          else if (fd_data.action == CAN_CANFD_READ) {
            fd_data.reply = chd->canFdMode;
            fd_data.status = CAN_CANFD_SUCCESS;
          }
        }
      }
      ArgPtrOut(sizeof(KCAN_IOCTL_CANFD_T));
      copy_to_user_ret((KCAN_IOCTL_CANFD_T *)arg, &fd_data,
      sizeof(KCAN_IOCTL_CANFD_T), -EFAULT);
    }
    break;


  //------------------------------------------------------------------
  case VCAN_IOC_REQ_BUS_STATS:
    {
      if (hwIf->reqBusStats) {
        vStat = hwIf->reqBusStats(chd);
      }
      else {
        __u32 curt, difft, btime;
        unsigned long ttime;
        vCanTime(chd->vCard, &ttime);
        curt = (__u32) ttime;
        difft = curt - chd->busStats.timestamp;
        btime = chd->busStats.bitTime100ns;
        if (!btime) {
          VCanBusParams busParams;
          vStat = VCAN_STAT_FAIL;
          if (hwIf->getBusParams) {
            vStat = hwIf->getBusParams(chd, &busParams);
          }
          if (vStat != VCAN_STAT_OK) {
            break;
          }
          btime = 10000000 / busParams.freq;
          chd->busStats.bitTime100ns = btime;
        }
        else {
          vStat = VCAN_STAT_OK;
        }
        if (difft != 0) {
          chd->busStats.busLoad = (chd->busStats.bitCount * btime) / difft * 100;
        }
        else {
          chd->busStats.busLoad = 0;
        }
        if (chd->busStats.busLoad > 10000) {
          chd->busStats.busLoad = 10000;
        }
        chd->busStats.bitCount = 0;
        chd->busStats.timestamp = curt;
      }
    }
    break;

  //------------------------------------------------------------------
  case VCAN_IOC_GET_BUS_STATS:
    {
      ArgPtrOut(sizeof(VCanBusStatistics));
      vStat = VCAN_STAT_OK;
      copy_to_user_ret((void *)arg, &chd->busStats,
                         sizeof(VCanBusStatistics), -EFAULT);
      memset(&chd->busStats, 0, sizeof(chd->busStats));
    }
    break;


  case KCAN_IOCTL_GET_CUST_CHANNEL_NAME:
    {
      KCAN_IOCTL_GET_CUST_CHANNEL_NAME_T *custChannelName = (KCAN_IOCTL_GET_CUST_CHANNEL_NAME_T*)arg;
      if (hwIf->getCustChannelName == NULL) {
        return -EINVAL;
      }
      vStat = hwIf->getCustChannelName(chd, custChannelName->data, sizeof(custChannelName->data), 
                                       &custChannelName->status);
    }
    break;
    
    //------------------------------------------------------------------
    default:
      DEBUGPRINT(1, (TXT("vCanIOCtrl UNKNOWN VCAN_IOC!!!: %d\n"), ioctl_cmd));
      return -EAGAIN;
  }
  //------------------------------------------------------------------

  switch (vStat) {
  case VCAN_STAT_OK:
    return 0;
  case VCAN_STAT_FAIL:
    return -EIO;
  case VCAN_STAT_TIMEOUT:
    return -EAGAIN;
  case VCAN_STAT_NO_DEVICE:
    return -ENODEV;
  case VCAN_STAT_NO_RESOURCES:
    return -EAGAIN;
  case VCAN_STAT_NO_MEMORY:
    return -ENOMEM;
  case VCAN_STAT_SIGNALED:
    return -ERESTARTSYS;
  case VCAN_STAT_BAD_PARAMETER:
    return -EINVAL;
  default:
    return -EIO;
  }
}


#  if !defined(HAVE_UNLOCKED_IOCTL)

int vCanIOCtl (struct inode *inode, struct file *filp,
               unsigned int ioctl_cmd, unsigned long arg)
{
  VCanOpenFileNode  *fileNodePtr = filp->private_data;

  return ioctl(fileNodePtr, ioctl_cmd, arg);
}

#  else

long vCanIOCtl_unlocked (struct file *filp,
                         unsigned int ioctl_cmd, unsigned long arg)
{
  VCanOpenFileNode  *fileNodePtr = filp->private_data;
  int               ret;

  // Use semaphore to enforce mutual exclusion
  // for a specific file descriptor.
  os_if_down_sema(&fileNodePtr->ioctl_mutex);
  ret = ioctl(fileNodePtr, ioctl_cmd, arg);
  os_if_up_sema(&fileNodePtr->ioctl_mutex);

  return ret;
}
#  endif



//======================================================================
//  Poll - File operation
//======================================================================

unsigned int vCanPoll (struct file *filp, poll_table *wait)
{
  VCanOpenFileNode  *fileNodePtr = filp->private_data;
  VCanChanData      *chd;
  int full = 0;
  unsigned int mask = 0;

  // Use semaphore to enforce mutual exclusion
  // for a specific file descriptor.
  os_if_down_sema(&fileNodePtr->ioctl_mutex);

  chd = fileNodePtr->chanData;

  full = txQFull(chd);

  // Add the channel wait queues to the poll
  poll_wait(filp, queue_space_event(&chd->txChanQueue), wait);
  poll_wait(filp, &fileNodePtr->rcv.rxWaitQ, wait);

  if (!rxQEmpty(fileNodePtr)) {
    // Readable
    mask |= POLLIN | POLLRDNORM;
    DEBUGPRINT(4, (TXT("vCanPoll: Channel %d readable\n"), fileNodePtr->chanNr));
  }

  if (!full) {
    // Writable
    mask |= POLLOUT | POLLWRNORM;
    DEBUGPRINT(4, (TXT("vCanPoll: Channel %d writable\n"), fileNodePtr->chanNr));
  }

  os_if_up_sema(&fileNodePtr->ioctl_mutex);

  return mask;
}

//======================================================================
// Init common data structures for one card
//======================================================================
int vCanInitData (VCanCardData *vCard)
{
  unsigned int  chNr;
  int           minorsUsed = 0;
  int           minor;
  VCanCardData *cardData   = vCard->driverData->canCards;
  VCanChanData *chanData;

  /* Build bitmap for used minor numbers */
  while (NULL != cardData) {
    /* Only interested in other cards */
    if (cardData != vCard) {
      for (chNr = 0; chNr < cardData->nrChannels; chNr++) {
        chanData = cardData->chanData[chNr];
        minorsUsed |= 1 << chanData->minorNr;
      }
    }
    cardData = cardData->next;
  }
  DEBUGPRINT(4, (TXT("vCanInitCardData: minorsUsed 0x%x \n"), minorsUsed));

  vCard->usPerTick = 10; // Currently, a tick is 10us long

  for (chNr = 0; chNr < vCard->nrChannels; chNr++) {
    VCanChanData *vChd = vCard->chanData[chNr];

    vCard->driverData->noOfDevices++;
    DEBUGPRINT(4, (TXT("vCanInitCardData: noOfDevices %d\n"), vCard->driverData->noOfDevices));

    for (minor = 0; minor < vCard->driverData->noOfDevices; minor++) {
      DEBUGPRINT(4, (TXT("vCanInitCardData: mask 0x%x, minor %d\n"),
                     1 << minor, minor));
      if (!(minorsUsed & (1 << minor))) {
        /* Found a free minor number */
        vChd->minorNr = minor;
        minorsUsed |= 1 << minor;
        break;
      }
    }

    // Init waitqueues
    os_if_init_waitqueue_head(&(vChd->flushQ));
    queue_init(&vChd->txChanQueue, TX_CHAN_BUF_SIZE);

    os_if_spin_lock_init(&(vChd->openLock));
    os_if_init_atomic_bit(&(vChd->waitEmpty));

    atomic_set(&vChd->chanId, 1);

    // vCard points back to card
    vChd->vCard = vCard;
  }

  return 0;
}
EXPORT_SYMBOL(vCanInitData);

//======================================================================
// Proc open
//======================================================================

static int kvaser_proc_open(struct inode* inode, struct file* file)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)
  VCanHWInterface *hwIf = PDE(inode)->data;
#else
  VCanHWInterface *hwIf = PDE_DATA(inode);
#endif
  return single_open(file, hwIf->procRead, NULL);
}


//======================================================================
// Module init
//======================================================================

static const struct file_operations kvaser_proc_fops = {
    .open = kvaser_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = seq_release,
};

int vCanInit (VCanDriverData *driverData, unsigned max_channels)
{
  VCanHWInterface *hwIf;
  int result;
  dev_t devno;

  // Initialise card and data structures
  hwIf = driverData->hwIf;
  memset(driverData, 0, sizeof(VCanDriverData));

  os_if_spin_lock_init(&driverData->canCardsLock);

  result = hwIf->initAllDevices();
  if (result == -ENODEV) {
    DEBUGPRINT(1, (TXT("No Kvaser %s cards found!\n"), driverData->deviceName));
    return -1;
  } else if (result != 0) {
    DEBUGPRINT(1, (TXT("Error (%d) initializing Kvaser %s driver!\n"), result,
                   driverData->deviceName));
    return -1;
  }

  if (!proc_create_data(driverData->deviceName,
                              0,             // default mode
                              NULL,          // parent dir
                              &kvaser_proc_fops,
                              hwIf           // client data
                              )) {
    DEBUGPRINT(1, (TXT("Error creating proc read entry!\n")));
    hwIf->closeAllDevices();
    return -1;
  }

  // Register driver for device
  result = alloc_chrdev_region(&devno, 0, max_channels,
                               driverData->deviceName);
  if (result < 0) {
    DEBUGPRINT(1, ("alloc_chrdev_region(%u, %s) error = %d\n",
                   max_channels, driverData->deviceName, result));
    hwIf->closeAllDevices();
    return -1;
  }

  cdev_init(&driverData->cdev, &fops);
  driverData->hwIf = hwIf;
  driverData->cdev.owner = THIS_MODULE;

  result = cdev_add(&driverData->cdev, devno, max_channels);
  if (result < 0) {
    DEBUGPRINT(1, ("cdev_add() error = %d\n", result));
    hwIf->closeAllDevices();
    return -1;
  }


  os_if_do_get_time_of_day(&driverData->startTime);

  return 0;
}
EXPORT_SYMBOL(vCanInit);


//======================================================================
// Module shutdown
//======================================================================
void vCanCleanup (VCanDriverData *driverData)
{
  if (driverData->cdev.dev > 0) {
    DEBUGPRINT(2, ("unregister chrdev_region (%s) major=%d count=%d\n",
                   driverData->deviceName, MAJOR(driverData->cdev.dev),
                   driverData->cdev.count));
    cdev_del(&driverData->cdev);
    unregister_chrdev_region(driverData->cdev.dev, driverData->cdev.count);
  }
  remove_proc_entry(driverData->deviceName, NULL /* parent dir */);
  driverData->hwIf->closeAllDevices();
  os_if_spin_lock_remove(&driverData->canCardsLock);
}
EXPORT_SYMBOL(vCanCleanup);
//======================================================================

INIT int init_module (void)
{
  return 0;
}

EXIT void cleanup_module (void)
{
}

static long calc_timeout (struct timeval *start, unsigned long wanted_timeout) {
  long           retval;
  struct timeval stop;

  if ((long)wanted_timeout == -1) {//wait until match
    retval = 2;
  } else {
    unsigned long dt_ms;
    os_if_do_get_time_of_day (&stop);
    dt_ms = (stop.tv_sec - start->tv_sec) * 1000 + (long)((stop.tv_usec - start->tv_usec + 500) / 1000);
    if (dt_ms < wanted_timeout) {
      retval = wanted_timeout - dt_ms;
      if (retval > 2) { //poll every 2 ms
        retval = 2;
      }
    } else {
      retval = 0;
    }
  }
  return retval;
}

//looks for message with a specific id
static uint32_t read_specific (VCanOpenFileNode *fileNodePtr, VCAN_EVENT *msg) {
  int                index = fileNodePtr->rcv.bufTail;
  uint32_t           found = 0;
  unsigned short int flags = 0;

  do {
    if (fileNodePtr->rcv.valid[index]) {
      if (fileNodePtr->rcv.fileRcvBuffer[index].tagData.msg.id == fileNodePtr->read_specific.id) {
        if (fileNodePtr->read_specific.skip == READ_SPECIFIC_SKIP_MATCHING) {
          fileNodePtr->rcv.valid[index] = 0;
          if (index == fileNodePtr->rcv.bufTail) {
            vCanPopReceiveBuffer (&fileNodePtr->rcv);
          } 
        }
        found = 1;
        break;
      } else {//save all flags so we can set OVERRUN properly
        flags |= fileNodePtr->rcv.fileRcvBuffer[index].tagData.msg.flags;
      }
    }
    index++;
    if (index >= fileNodePtr->rcv.size) {
      index = 0;
    }
  } while (index != fileNodePtr->rcv.bufHead);

  if (found) {
    if (fileNodePtr->read_specific.skip == READ_SPECIFIC_SKIP_PRECEEDING) {
      fileNodePtr->rcv.bufTail = index;
      vCanPopReceiveBuffer (&fileNodePtr->rcv);
    }

    flags = flags & VCAN_MSG_FLAG_OVERRUN;
    *msg = fileNodePtr->rcv.fileRcvBuffer[index];
    msg->tagData.msg.flags |= flags;
  }
  return found;
}
