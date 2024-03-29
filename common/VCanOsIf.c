/*
**             Copyright 2017 by Kvaser AB, Molndal, Sweden
**                         http://www.kvaser.com
**
** This software is dual licensed under the following two licenses:
** BSD-new and GPLv2. You may use either one. See the included
** COPYING file for details.
**
** License: BSD-new
** ==============================================================================
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
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
** BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
** IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
** POSSIBILITY OF SUCH DAMAGE.
**
**
** License: GPLv2
** ==============================================================================
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
**
**
** IMPORTANT NOTICE:
** ==============================================================================
** This source code is made available for free, as an open license, by Kvaser AB,
** for use with its applications. Kvaser AB does not accept any liability
** whatsoever for any third party patent or other immaterial property rights
** violations that may result from any usage of this source code, regardless of
** the combination of source code and various applications that it can be used
** in, or with.
**
** -----------------------------------------------------------------------------
*/

// Kvaser CAN driver module for Linux
// Hardware independent parts

//--------------------------------------------------
// NOTE! module_versioning HAVE to be included first
#  include "module_versioning.h"
//--------------------------------------------------

#include <linux/wait.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0))
#include <linux/sched/signal.h>
#endif /* KERNEL_VERSION >= 4.11.0 */
#include <asm/io.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
#   include <asm/system.h>
#endif /* KERNEL_VERSION < 3.4.0 */
#include <asm/bitops.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
#include <asm/uaccess.h>
#else
#include <linux/uaccess.h>
#endif /* KERNEL_VERSION < 4.12.0 */
#include <asm/atomic.h>

// Kvaser definitions
#include "canlib_version.h"
#include "vcanevt.h"
#include "vcan_ioctl.h"
#include "kcan_ioctl.h"
#include "hwnames.h"
#include "queue.h"
#include "VCanOsIf.h"
#include "debug.h"
#include "softsync.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0))
#ifndef HAVE_UNLOCKED_IOCTL
#define HAVE_UNLOCKED_IOCTL 1
#endif /* HAVE_UNLOCKED_IOCTL */
#ifndef HAVE_COMPAT_IOCTL
#define HAVE_COMPAT_IOCTL 1
#endif /* HAVE_COMPAT_IOCTL */
#endif /* KERNEL_VERSION >= 5.9.0 */

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("KVASER");
MODULE_DESCRIPTION("Kvaser common module");
MODULE_VERSION(__stringify(CANLIB_MAJOR_VERSION) "."
               __stringify(CANLIB_MINOR_VERSION));

#if defined VCANOSIF_DEBUG
  static int debug_levell = VCANOSIF_DEBUG;
  MODULE_PARM_DESC(debug_levell, "Common debug level");
  module_param(debug_levell, int, 0644);
# define DEBUGPRINT(n, arg)    if (debug_levell >= (n)) { DEBUGOUT(n, arg); }
#else
# define DEBUGPRINT(n, arg)    if ((n) == 1) { DEBUGOUT(n, arg); }
#endif

#ifndef THIS_MODULE
#define THIS_MODULE 0
#endif

# ifdef _LINUX_TIME64_H
static long     calc_timeout        (struct timespec64 *start, unsigned long wanted_timeout);
#else
static long     calc_timeout        (struct timeval *start, unsigned long wanted_timeout);
#endif
static uint32_t read_specific       (VCanOpenFileNode *fileNodePtr, VCanRead *readOpt, VCAN_EVENT *msg);
static int      wait_tx_queue_empty (VCanOpenFileNode *fileNodePtr, unsigned long timeout);

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


static int is_same_card(VCanCardNumberData *data, VCanCardData *vCard)
{
  if (memcmp(vCard->ean, data->ean, sizeof(vCard->ean)) == 0) {
    return vCard->serialNumber == data->serialNumber;
  }
  return 0;
}

static void set_cardnumberdata(unsigned int index, VCanCardData *vCard)
{
  memcpy(vCard->driverData->cardNumbers[index].ean, vCard->ean, sizeof(vCard->driverData->cardNumbers[index].ean));
  vCard->driverData->cardNumbers[index].serialNumber = vCard->serialNumber;
  vCard->driverData->cardNumbers[index].active = 1;
  vCard->cardNumber = index;
}

//======================================================================
// Time
//======================================================================

int vCanTime (VCanCardData *vCard, uint64_t *time)
{
# ifdef _LINUX_TIME64_H
  struct timespec64 tv;
#else
  struct timeval tv;
#endif

  tv = vCanCalc_dt (&vCard->driverData->startTime);
# ifdef _LINUX_TIME64_H
  *time = (uint64_t)(((unsigned long)tv.tv_nsec /1000) / (unsigned long)vCard->usPerTick +
                     (1000000ul / (unsigned long)vCard->usPerTick) * (unsigned long)tv.tv_sec);

  #else
  *time = (uint64_t)((unsigned long)tv.tv_usec / (unsigned long)vCard->usPerTick +
                     (1000000ul / (unsigned long)vCard->usPerTick) * (unsigned long)tv.tv_sec);
#endif
  return VCAN_STAT_OK;
}
EXPORT_SYMBOL(vCanTime);

# ifdef _LINUX_TIME64_H
void kv_do_gettimeofday (struct timespec64 *tv)
{
  ktime_get_real_ts64(tv);
}
#else
void kv_do_gettimeofday (struct timeval *tv)
{
  do_gettimeofday(tv);
}
#endif /* _LINUX_TIME64_H */

EXPORT_SYMBOL(kv_do_gettimeofday);
#ifdef _LINUX_TIME64_H
struct timespec64 vCanCalc_dt (struct timespec64 *start) {
  struct timespec64 stop;

  kv_do_gettimeofday (&stop);

  if (stop.tv_nsec >= start->tv_nsec) {
    stop.tv_nsec -= start->tv_nsec;
    stop.tv_sec  -= start->tv_sec;
  } else {
    stop.tv_nsec = stop.tv_nsec + 1000000000 - start->tv_nsec;
    stop.tv_sec  = stop.tv_sec - start->tv_sec - 1;
  }

  return stop;
}
#else
struct timeval vCanCalc_dt (struct timeval *start) {
  struct timeval stop;

  kv_do_gettimeofday (&stop);

 if (stop.tv_usec >= start->tv_usec) {
    stop.tv_usec -= start->tv_usec;
    stop.tv_sec  -= start->tv_sec;
  } else {
    stop.tv_usec = stop.tv_usec + 1000000 - start->tv_usec;
    stop.tv_sec  = stop.tv_sec - start->tv_sec - 1;
  }

  return stop;
}
#endif
EXPORT_SYMBOL(vCanCalc_dt);

int vCanSupportsBusParamsTq(VCanChanData *chd) {
  if ((chd->capabilities_ex & chd->capabilities_ex_mask & VCAN_CHANNEL_EX_CAP_HAS_BUSPARAMS_TQ) == 0) {
    return 0;
  } else {
    return 1;
  }
}
EXPORT_SYMBOL(vCanSupportsBusParamsTq);

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
  fileNodePtr->rcv->bufTail = 0;
  fileNodePtr->rcv->bufHead = 0;

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
    } else {
      rcv->bufTail++;
    }
  } while ((rcv->valid[rcv->bufTail] == 0) && (rcv->bufTail != rcv->bufHead));

  return VCAN_STAT_OK;
}

//======================================================================
//  Push rx queue
//======================================================================
int vCanPushReceiveBuffer (VCanReceiveData *rcv)
{
  rcv->valid[rcv->bufHead] = 1;

  if (rcv->bufHead >= rcv->size - 1) {
    rcv->bufHead = 0;
  } else {
    rcv->bufHead++;
  }

  wake_up_interruptible(&rcv->rxWaitQ);

  return VCAN_STAT_OK;
}

//======================================================================
//  get card info
//======================================================================
int vCanGetCardInfo(VCanCardData *vCard, VCAN_IOCTL_CARD_INFO *ci)
{
  VCanDriverData *drvData = vCard->driverData;
  ci->serial_number = vCard->serialNumber;
  ci->channel_count = vCard->nrChannels;
  ci->max_bitrate = (unsigned long) vCard->current_max_bitrate;
  strncpy(ci->driver_name, drvData->deviceName, MAX_IOCTL_DRIVER_NAME);

  ci->driver_version_major = CANLIB_MAJOR_VERSION;
  ci->driver_version_minor = CANLIB_MINOR_VERSION;
  ci->driver_version_build = CANLIB_BUILD_VERSION;
  ci->product_version_major = CANLIB_PRODUCT_MAJOR_VERSION;
  ci->product_version_minor = CANLIB_MINOR_VERSION;
  ci->product_version_minor_letter = 0;

  return 0;
}
EXPORT_SYMBOL(vCanGetCardInfo);

//======================================================================
//  get card info 2
//======================================================================
int vCanGetCardInfo2(VCanCardData *vCard, KCAN_IOCTL_CARD_INFO_2 *ci)
{
  memcpy(ci->ean, vCard->ean, sizeof ci->ean);

  return 0;
}
EXPORT_SYMBOL(vCanGetCardInfo2);

//======================================================================
//  card has been removed
//======================================================================
void vCanCardRemoved (VCanChanData *chd)
{
  VCanOpenFileNode *fileNodePtr;
  unsigned long     openLock_irqFlags;
  int i;

  spin_lock_irqsave(&chd->openLock, openLock_irqFlags);
  for (fileNodePtr = chd->openFileList; fileNodePtr != NULL;
       fileNodePtr = fileNodePtr->next) {

    wake_up_interruptible(&chd->flushQ);
    wake_up_interruptible(&fileNodePtr->rcv->rxWaitQ);
    wake_up_interruptible(&fileNodePtr->rcv_text->rxWaitQ);
  }
  spin_unlock_irqrestore(&chd->openLock, openLock_irqFlags);

  if (chd->vCard != NULL && chd->vCard->driverData != NULL) {
    for (i = 0; i < chd->vCard->driverData->maxCardnumber; i++) {
      if (is_same_card(&chd->vCard->driverData->cardNumbers[i], chd->vCard)) {
        chd->vCard->driverData->cardNumbers[i].active = 0;
        break;
      }
    }
  }
}
EXPORT_SYMBOL(vCanCardRemoved);

int vCanDispatchEvent (VCanChanData *chd, VCAN_EVENT *e)
{
  VCanOpenFileNode *fileNodePtr;
  unsigned long    openLock_irqFlags;
  unsigned long    rcvLock_irqFlags;
  long             objbuf_mask;
  int              queue_length;

  // Update and notify readers
  // Needs to be _irqsave since some drivers call from ISR:s.
  spin_lock_irqsave(&chd->openLock, openLock_irqFlags);
  for (fileNodePtr = chd->openFileList; fileNodePtr != NULL;
       fileNodePtr = fileNodePtr->next) {

    unsigned short int msg_flags;

    if (!fileNodePtr->isBusOn && !fileNodePtr->notify) {
      continue;
    }

    if (e->tag == V_CHIP_STATE) {
      fileNodePtr->chip_status.busStatus      = e->tagData.chipState.busStatus;
      fileNodePtr->chip_status.txErrorCounter = e->tagData.chipState.txErrorCounter;
      fileNodePtr->chip_status.rxErrorCounter = e->tagData.chipState.rxErrorCounter;
      if (!fileNodePtr->notify) {
        continue;
      }
    }

    // Event filter
    if (!(e->tag & fileNodePtr->filter.eventMask)) {
      continue;
    }

    msg_flags = e->tagData.msg.flags;

    if (e->tag == V_RECEIVE_MSG) {
      // Update bus statistics
      if (msg_flags & VCAN_MSG_FLAG_ERROR_FRAME) {
        if (!(msg_flags & VCAN_MSG_FLAG_TX_START) &&
            !(msg_flags & VCAN_MSG_FLAG_TXACK)) //pciefd sends txack on error frames
        {
          chd->busStats.bitCount += 16;
          chd->busStats.errFrame++;
        }
      }
      else {
        if (!(msg_flags & VCAN_MSG_FLAG_TX_START)) {
          chd->busStats.bitCount += (e->tagData.msg.dlc > 8 ? 8 : e->tagData.msg.dlc) * 10;
          if (e->tagData.msg.id & VCAN_EXT_MSG_ID) {
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

        // if we want them, enable txack on non sending hnd
        if ((fileNodePtr->modeTx != TXACK_DISABLED) && (fileNodePtr->modeTxLocal)) {
          msg_flags |= VCAN_MSG_FLAG_LOCAL_TXACK;
        }

        // dont report any SSM NACK's on other handles (on same channel).
        if (msg_flags & VCAN_MSG_FLAG_SSM_NACK) {
          continue;
        }
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

        if ((fileNodePtr->filter.stdId == 0xFFFF) && (fileNodePtr->filter.stdMask == 0xFFFF)) {//from windows
          continue;
        } else if ((fileNodePtr->filter.stdId ^ id) & fileNodePtr->filter.stdMask & 0x07FF) {
          continue;
        }
      } else {
        // Extended message
        if ((fileNodePtr->filter.extId ^ id) & fileNodePtr->filter.extMask) {
          continue;
        }
      }

      if (msg_flags & VCAN_MSG_FLAG_OVERRUN) {
        fileNodePtr->overrun.sw++;
        fileNodePtr->overrun.hw++;
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
#if defined(__arm__) || defined(__aarch64__)
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
        queue_work(fileNodePtr->objbufTaskQ,
                   &fileNodePtr->objbufWork);
      }
    }

    spin_lock_irqsave(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);
    memcpy(&(fileNodePtr->rcv->fileRcvBuffer[fileNodePtr->rcv->bufHead]), e, sizeof(VCAN_EVENT));
    fileNodePtr->rcv->fileRcvBuffer[fileNodePtr->rcv->bufHead].tagData.msg.flags = msg_flags;
    fileNodePtr->rcv->fileRcvBuffer[fileNodePtr->rcv->bufHead].timeStamp -= fileNodePtr->time_start_10usec;
    vCanPushReceiveBuffer(fileNodePtr->rcv);
    queue_length = getQLen(fileNodePtr->rcv->bufHead,
                           fileNodePtr->rcv->bufTail,
                           fileNodePtr->rcv->size);

    DEBUGPRINT(3, (TXT("Number of packets in receive queue: %d\n"), queue_length));

    if (queue_length == 0) {
      // The buffer is full
      fileNodePtr->overrun.sw++;
      DEBUGPRINT(2, (TXT("File node overrun\n")));
      // Mark message
      vCanPopReceiveBuffer(fileNodePtr->rcv);
      fileNodePtr->rcv->fileRcvBuffer[fileNodePtr->rcv->bufTail].tagData.msg.flags |= VCAN_MSG_FLAG_OVERRUN;
    }
    spin_unlock_irqrestore(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);
  }
  spin_unlock_irqrestore(&chd->openLock, openLock_irqFlags);

  return 0;
}
EXPORT_SYMBOL(vCanDispatchEvent);


//======================================================================
//  vCanDispatchPrintfEvent
//======================================================================
int vCanDispatchPrintfEvent (VCanCardData *vCard,
                             VCanChanData *vChan,
                             VCAN_EVENT *printf_evHeader,
                             char* data)
{
  unsigned int      datalen = printf_evHeader->tagData.printfMsg.printf_header.datalen;
  unsigned int      pos     = 0;
  VCanOpenFileNode  *fileNodePtr;
  VCAN_EVENT        e;
  unsigned long     openLock_irqFlags;
  unsigned long     rcvTextLock_irqFlags;
  int               queue_length;

  // Update and notify readers
  // Needs to be _irqsave since some drivers call from ISR:s.
  spin_lock_irqsave(&vChan->openLock, openLock_irqFlags);

  for (fileNodePtr = vChan->openFileList; fileNodePtr != NULL;
       fileNodePtr = fileNodePtr->next) {
    unsigned int   slot_mask  = 0;

    // skip if no subscription
    if (printf_evHeader->tagData.printfMsg.printf_header.flags & DM_FLAG_ERROR) {
      if (!fileNodePtr->error_subscriptions_mask) {
        continue;
      }
    }
    else if (printf_evHeader->tagData.printfMsg.printf_header.flags & DM_FLAG_DEBUG) {
      if (!fileNodePtr->debug_subscriptions_mask) {
        continue;
      }
    }
    else if (printf_evHeader->tagData.printfMsg.printf_header.flags & DM_FLAG_PRINTF) {
      slot_mask = 1 << printf_evHeader->tagData.printfMsg.printf_header.slot;
      if (((fileNodePtr->message_subscriptions_mask) & slot_mask) == 0) {
        continue;
      }
    }
    else {
      DEBUGPRINT(1, (TXT("vCanDispatchPrintfEvent() UNKNOWN printf message flag. 0x%x\n"), printf_evHeader->tagData.printfMsg.printf_header.flags));
    }

    spin_lock_irqsave(&fileNodePtr->rcv_text->rcvLock, rcvTextLock_irqFlags);

    if (fileNodePtr->printf_queue_overrun) {
      printf_evHeader->tagData.printfMsg.printf_header.flags |= VCAN_MSG_FLAG_OVERRUN;
      fileNodePtr->printf_queue_overrun = 0;
    }

    // first write header

    memcpy(&(fileNodePtr->rcv_text->fileRcvBuffer[fileNodePtr->rcv_text->bufHead]), printf_evHeader, sizeof(VCAN_EVENT));
    fileNodePtr->rcv_text->fileRcvBuffer[fileNodePtr->rcv_text->bufHead].timeStamp -= fileNodePtr->time_start_10usec;
    vCanPushReceiveBuffer (fileNodePtr->rcv_text); // <==== wake_up_interruptible(&rcv_text->rxWaitQ);

    queue_length = getQLen(fileNodePtr->rcv_text->bufHead,
                           fileNodePtr->rcv_text->bufTail,
                           fileNodePtr->rcv_text->size);

    if (queue_length == 0) {
      // queue full
      fileNodePtr->printf_queue_overrun = 1;

      // Mark message
      vCanPopReceiveBuffer(fileNodePtr->rcv_text);
      fileNodePtr->overrun.sw++;

      spin_unlock_irqrestore(&fileNodePtr->rcv_text->rcvLock, rcvTextLock_irqFlags);

      DEBUGPRINT(1, (TXT("vCanDispatchPrintfEvent() rcv-buffer overrun.\n")));
      continue;
    }

    // set up event
    e.tag = V_PRINTF_MSG;
    e.tagData.printfMsg.type = VCAN_EVT_PRINTF_DATA;

    pos = 0;
    while(pos < datalen) {
      memset(e.tagData.printfMsg.printf_data.payload, 0, sizeof(e.tagData.printfMsg.printf_data.payload));
      // we can still write a full event
      if ((datalen - pos) >= sizeof(e.tagData.printfMsg.printf_data.payload)) {
        memcpy(e.tagData.printfMsg.printf_data.payload, &data[pos], sizeof(e.tagData.printfMsg.printf_data.payload));

        memcpy(&(fileNodePtr->rcv_text->fileRcvBuffer[fileNodePtr->rcv_text->bufHead]), &e, sizeof(VCAN_EVENT));
        fileNodePtr->rcv_text->fileRcvBuffer[fileNodePtr->rcv_text->bufHead].timeStamp -= fileNodePtr->time_start_10usec;
        vCanPushReceiveBuffer (fileNodePtr->rcv_text);
        pos += sizeof(e.tagData.printfMsg.printf_data.payload);
      }
      else { // sent the leftover bytes
        memcpy(e.tagData.printfMsg.printf_data.payload, &data[pos], datalen%sizeof(e.tagData.printfMsg.printf_data.payload));

        memcpy(&(fileNodePtr->rcv_text->fileRcvBuffer[fileNodePtr->rcv_text->bufHead]), &e, sizeof(VCAN_EVENT));
        fileNodePtr->rcv_text->fileRcvBuffer[fileNodePtr->rcv_text->bufHead].timeStamp -= fileNodePtr->time_start_10usec;
        vCanPushReceiveBuffer (fileNodePtr->rcv_text);
        pos += datalen%sizeof(e.tagData.printfMsg.printf_data.payload);
      }

      queue_length = getQLen(fileNodePtr->rcv_text->bufHead,
                             fileNodePtr->rcv_text->bufTail,
                             fileNodePtr->rcv_text->size);

      DEBUGPRINT(3, (TXT("Number of packets in receive queue: %d\n"), queue_length));

      if (queue_length == 0) {
        // The buffer is full
        fileNodePtr->overrun.sw++;
        DEBUGPRINT(2, (TXT("vCanDispatchPrintfEvent() rcv-buffer overrun\n")));
        // Mark message
        vCanPopReceiveBuffer(fileNodePtr->rcv_text);
        fileNodePtr->rcv_text->fileRcvBuffer[fileNodePtr->rcv_text->bufTail].tagData.msg.flags |= VCAN_MSG_FLAG_OVERRUN;
      }
    }

    spin_unlock_irqrestore(&fileNodePtr->rcv_text->rcvLock, rcvTextLock_irqFlags);
  }

  spin_unlock_irqrestore(&vChan->openLock, openLock_irqFlags);

  return 0;
}
EXPORT_SYMBOL(vCanDispatchPrintfEvent);


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

  spin_lock(&driverData->canCardsLock);
  if (driverData->canCards == NULL) {
    spin_unlock(&driverData->canCardsLock);
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
  spin_unlock(&driverData->canCardsLock);
  if (chanData == NULL) {
    DEBUGPRINT(1, (TXT("FAILURE: Unable to open minor %d major (%d)\n"),
                   minorNr, MAJOR(inode->i_rdev)));
    return -ENODEV;
  }

  // Allocate memory and zero the whole struct
  openFileNodePtr = kmalloc(sizeof(VCanOpenFileNode), GFP_KERNEL);
  if (openFileNodePtr == NULL) {
    return -ENOMEM;
  }

  memset(openFileNodePtr, 0, sizeof(VCanOpenFileNode));

  // Allocate receive buffers
  openFileNodePtr->rcv = kmalloc(sizeof(VCanReceiveData), GFP_KERNEL);
  if (openFileNodePtr->rcv == NULL) {
    kfree(openFileNodePtr);
    return -ENOMEM;
  }

  // Allocate receive buffers for printing
  openFileNodePtr->rcv_text = kmalloc(sizeof(VCanReceiveData), GFP_KERNEL);
  if (openFileNodePtr->rcv_text == NULL) {
    kfree(openFileNodePtr->rcv);
    kfree(openFileNodePtr);
    return -ENOMEM;
  }

  memset(openFileNodePtr->rcv, 0, sizeof(VCanReceiveData));
  memset(openFileNodePtr->rcv_text, 0, sizeof(VCanReceiveData));

  init_completion(&openFileNodePtr->ioctl_completion);
  complete(&openFileNodePtr->ioctl_completion);

  openFileNodePtr->rcv->size = sizeof(openFileNodePtr->rcv->fileRcvBuffer) / sizeof(openFileNodePtr->rcv->fileRcvBuffer[0]);
  openFileNodePtr->rcv_text->size = sizeof(openFileNodePtr->rcv_text->fileRcvBuffer) / sizeof(openFileNodePtr->rcv_text->fileRcvBuffer[0]);

  // Init wait queue
  init_waitqueue_head(&(openFileNodePtr->rcv->rxWaitQ));
  init_waitqueue_head(&(openFileNodePtr->rcv_text->rxWaitQ));

  vCanFlushReceiveBuffer(openFileNodePtr);

  openFileNodePtr->filp               = filp;
  openFileNodePtr->chanData           = chanData;
  openFileNodePtr->modeTx             = 0;
  openFileNodePtr->modeTxRq           = 0;
  openFileNodePtr->modeTxLocal        = 0;
  openFileNodePtr->modeNoTxEcho       = 0;
  openFileNodePtr->writeTimeout       = -1;
  openFileNodePtr->transId            = atomic_read(&chanData->chanId);
  atomic_add(1, &chanData->chanId);
  openFileNodePtr->filter.eventMask   = ~0;

  openFileNodePtr->overrun.sw           = 0;
  openFileNodePtr->overrun.hw           = 0;

  openFileNodePtr->isBusOn              = 0;
  openFileNodePtr->notify               = 0;
  openFileNodePtr->init_access          = 0;
  spin_lock_init(&(openFileNodePtr->rcv->rcvLock));
  spin_lock_init(&(openFileNodePtr->rcv_text->rcvLock));

  // Insert this node first in list of "opens"
  spin_lock_irqsave(&chanData->openLock, irqFlags);
  openFileNodePtr->chanNr             = -1;
  openFileNodePtr->channelLocked      = 0;
  openFileNodePtr->channelOpen        = 0;

  openFileNodePtr->chip_status.busStatus      = CHIPSTAT_BUSOFF;
  openFileNodePtr->chip_status.txErrorCounter = 0;
  openFileNodePtr->chip_status.rxErrorCounter = 0;
  openFileNodePtr->time_start_10usec          = 0;

  openFileNodePtr->message_subscriptions_mask = 0;
  openFileNodePtr->debug_subscriptions_mask = 0;
  openFileNodePtr->error_subscriptions_mask = 0;
  openFileNodePtr->printf_queue_overrun = 0;

  atomic_inc(&chanData->fileOpenCount);
  openFileNodePtr->next  = chanData->openFileList;
  chanData->openFileList = openFileNodePtr;

  chanData->driverMode = 0;

  spin_unlock_irqrestore(&chanData->openLock, irqFlags);

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
  VCanChanData      *chanData;
  unsigned long    irqFlags;
  VCanHWInterface  *hwIf;

  fileNodePtr = filp->private_data;
  chanData    = fileNodePtr->chanData;

  spin_lock_irqsave(&chanData->openLock, irqFlags);
  // Find the open file node
  openPtrPtr = &chanData->openFileList;

  for(; *openPtrPtr != NULL; openPtrPtr = &((*openPtrPtr)->next)) {
    if ((*openPtrPtr)->filp == filp) {
      break;
    }
  }
  // We did not find anything?
  if (*openPtrPtr == NULL) {
    spin_unlock_irqrestore(&chanData->openLock, irqFlags);
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

  spin_unlock_irqrestore(&chanData->openLock, irqFlags);

  hwIf = chanData->vCard->driverData->hwIf;
  if (fileNodePtr->isBusOn) {
      // This can happen if close (unistd.h) is called for the file descriptor.
      // i.e. if you exit a canlib application with open handles.
      // However, this should never happen when vCanClose is called from
      // canClose (in canlib). canClose should call busOff, before calling vCanClose.
      DEBUGPRINT(2, (TXT("vCanClose: handle is busOn. This is unexpected if you called canClose.\n")));
      wait_for_completion(&chanData->busOnCountCompletion);
      fileNodePtr->isBusOn = 0;
      chanData->busOnCount--;
      if (chanData->busOnCount == 0) {
        hwIf->busOff(chanData);
      }
      complete(&chanData->busOnCountCompletion);
  }

  // Do driver specific clean up
  if (hwIf->cleanUpHnd) hwIf->cleanUpHnd(chanData);

  if (atomic_read(&chanData->fileOpenCount) == 1) {
    if (fileNodePtr->objbuf) {
      // Driver-implemented auto response buffers.
      objbuf_shutdown(fileNodePtr);
      kfree(fileNodePtr->objbuf);
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
    kfree(fileNodePtr->rcv);
    kfree(fileNodePtr->rcv_text);
    kfree(fileNodePtr);
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
//  IOCtl - File operation
//======================================================================
static int ioctl_return_value (int vStat) {
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
    case VCAN_STAT_NOT_IMPLEMENTED:
      return -ENOSYS;
    default:
      return -EIO;
  }
}

static int ioctl_non_blocking (VCanOpenFileNode *fileNodePtr,
                               unsigned int      ioctl_cmd,
                               unsigned long     arg) {
  int           vStat = VCAN_STAT_OK;
  VCanChanData *vChd  = fileNodePtr->chanData;
  VCanCardData *vCard = vChd->vCard;

  switch (ioctl_cmd) {

    case VCAN_IOC_RECVMSG:
    {
      unsigned long     rcvLock_irqFlags;
      VCAN_IOCTL_READ_T ioctl_read;
      VCAN_EVENT        msg;
      VCanRead          readOpt;

      if (copy_from_user(&ioctl_read, (VCAN_IOCTL_READ_T *)arg,
                         sizeof(VCAN_IOCTL_READ_T))) {
        DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_RECVMSG 1\n")));
        return -EFAULT;
      }
      if (copy_from_user(&readOpt, (VCanRead *)ioctl_read.read,
                         sizeof(VCanRead))) {
        DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_RECVMSG 2\n")));
        return -EFAULT;
      }

      if (readOpt.timeout) {
        if (readOpt.timeout != -1) {
          wait_event_interruptible_timeout (fileNodePtr->rcv->rxWaitQ,
                                            (fileNodePtr->rcv->bufHead != fileNodePtr->rcv->bufTail) || !vCard->cardPresent,
                                            msecs_to_jiffies (readOpt.timeout));
        } else {
          wait_event_interruptible (fileNodePtr->rcv->rxWaitQ,
                                    (fileNodePtr->rcv->bufHead != fileNodePtr->rcv->bufTail) || !vCard->cardPresent);
        }

        if (signal_pending(current)) {
          return -ERESTARTSYS;
        } else if (!vCard->cardPresent) {
          return -ESHUTDOWN;
        }
      }

      spin_lock_irqsave(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);
      if (fileNodePtr->rcv->bufHead == fileNodePtr->rcv->bufTail) {
        spin_unlock_irqrestore(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);
        return -EAGAIN;
      }
      msg = fileNodePtr->rcv->fileRcvBuffer[fileNodePtr->rcv->bufTail];
      vCanPopReceiveBuffer(fileNodePtr->rcv);
      spin_unlock_irqrestore(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);
      copy_to_user_ret((VCAN_EVENT *)ioctl_read.msg, &msg, sizeof(VCAN_EVENT), -EFAULT);
      break;
    }

    case VCAN_IOC_RECVMSG_SYNC:
    {
      unsigned long timeout;
      unsigned long rcvLock_irqFlags;

      get_user(timeout, (int *)arg);

      if (timeout) {
        if (timeout != -1) {
          wait_event_interruptible_timeout (fileNodePtr->rcv->rxWaitQ,
                                            (fileNodePtr->rcv->bufHead != fileNodePtr->rcv->bufTail) || !vCard->cardPresent,
                                            msecs_to_jiffies (timeout));
        } else {
          wait_event_interruptible (fileNodePtr->rcv->rxWaitQ,
                                    (fileNodePtr->rcv->bufHead != fileNodePtr->rcv->bufTail) || !vCard->cardPresent);
        }

        if (signal_pending(current)) {
          return -ERESTARTSYS;
        } else if (!vCard->cardPresent) {
          return -ESHUTDOWN;
        }
      }

      spin_lock_irqsave(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);
      if (fileNodePtr->rcv->bufHead == fileNodePtr->rcv->bufTail) {
        spin_unlock_irqrestore(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);
        return -ETIMEDOUT;
      }
      spin_unlock_irqrestore(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);
      break;
    }

    case VCAN_IOC_RECVMSG_SPECIFIC:
    {
      long              timeout = 0;
      unsigned long     rcvLock_irqFlags;
#ifdef _LINUX_TIME64_H
      struct timespec64 start;
#else
      struct timeval    start;
#endif
      VCAN_IOCTL_READ_T ioctl_read;
      VCanRead          readOpt;

      kv_do_gettimeofday (&start);

      if (copy_from_user(&ioctl_read, (VCAN_IOCTL_READ_T *)arg,
                         sizeof(VCAN_IOCTL_READ_T))) {
        DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_RECVMSG_SPECIFIC 1\n")));
        return -EFAULT;
      }
      if (copy_from_user(&readOpt, (VCanRead *)ioctl_read.read,
                         sizeof(VCanRead))) {
        DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_RECVMSG_SPECIFIC 2\n")));
        return -EFAULT;
      }
      timeout = readOpt.timeout;


      while (1) {
        if (timeout) {
          if (timeout != -1) {
            wait_event_interruptible_timeout (fileNodePtr->rcv->rxWaitQ,
                                              (fileNodePtr->rcv->bufHead != fileNodePtr->rcv->bufTail) || !vCard->cardPresent,
                                              msecs_to_jiffies (timeout));
          } else {
            wait_event_interruptible (fileNodePtr->rcv->rxWaitQ,
                                      (fileNodePtr->rcv->bufHead != fileNodePtr->rcv->bufTail) || !vCard->cardPresent);
          }

          if (signal_pending(current)) {
            return -ERESTARTSYS;
          } else if (!vCard->cardPresent) {
            return -ESHUTDOWN;
          }
        }

        {
          uint32_t   found = 0;
          VCAN_EVENT msg;

          spin_lock_irqsave (&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);
          if (fileNodePtr->rcv->bufHead == fileNodePtr->rcv->bufTail) {
            found = 0;
          } else {
            found = read_specific (fileNodePtr, &readOpt, &msg);
          }
          spin_unlock_irqrestore (&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);

          if (found) {
            copy_to_user_ret((VCAN_EVENT *)ioctl_read.msg, &msg, sizeof(VCAN_EVENT), -EFAULT);
            break;
          } else {
            timeout = calc_timeout (&start, readOpt.timeout);
            if (timeout == 0) {
              if (readOpt.specific.skip == READ_SPECIFIC_NO_SKIP) {
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

    case VCAN_IOC_SENDMSG:
      {
        VCanChanData    *vChd = fileNodePtr->chanData;
        VCanHWInterface *hwIf = vChd->vCard->driverData->hwIf;

        if (!fileNodePtr->isBusOn) {
          DEBUGPRINT(2, (TXT("VCAN_IOC_SENDMSG Handle is not bus on. returning -EAGAIN\n")));
          return -EAGAIN;
        }

        if (vChd->vCard->card_flags & DEVHND_CARD_REFUSE_TO_USE_CAN) {
          DEBUGPRINT(2, (TXT("VCAN_IOC_SENDMSG Refuse to run. returning -EACCES\n")));
          return -EACCES;
        }

        {
          CAN_MSG *bufMsgPtr;
          CAN_MSG message;
          int queuePos;

          // The copy from user memory can sleep, so it must
          // not be done while holding the queue lock.
          // This means an extra memcpy() from a stack buffer,
          // but that can only be avoided by changing the queue
          // buffer "allocation" method (queue_back/push).
          if (copy_from_user(&message, (CAN_MSG *)arg, sizeof(CAN_MSG))) {
            DEBUGPRINT(2, (TXT("VCAN_IOC_SENDMSG - returning -EFAULT\n")));
            return -EFAULT;
          }

          queuePos = queue_back(&vChd->txChanQueue);
          if (queuePos < 0) {
            queue_release(&vChd->txChanQueue);
            return -EAGAIN;
          }
          bufMsgPtr = &vChd->txChanBuffer[queuePos];

          memcpy(bufMsgPtr, &message, sizeof(CAN_MSG));

          // This is for keeping track of the originating fileNode
          bufMsgPtr->user_data = fileNodePtr->transId;
          bufMsgPtr->flags &= ~(VCAN_MSG_FLAG_TX_NOTIFY | VCAN_MSG_FLAG_TX_START);
          if ((fileNodePtr->modeTx==TXACK_OFF || fileNodePtr->modeTx==TXACK_ON) || (atomic_read(&vChd->fileOpenCount) > 1)) {
            bufMsgPtr->flags |= VCAN_MSG_FLAG_TX_NOTIFY;
          }

          if (fileNodePtr->modeTxRq) {
            bufMsgPtr->flags |= VCAN_MSG_FLAG_TX_START;
          }

          queue_push(&vChd->txChanQueue);
        }
        hwIf->requestSend(vChd->vCard, vChd);
        break;
      }

    case KCAN_IOCTL_SCRIPT_GET_TEXT:
      {
      KCAN_IOCTL_SCRIPT_GET_TEXT_T my_arg;
      unsigned long     rcvTextLock_irqFlags;
      VCAN_EVENT msg;

      copy_from_user_ret(&my_arg, (KCAN_IOCTL_SCRIPT_GET_TEXT_T *)arg, sizeof(KCAN_IOCTL_SCRIPT_GET_TEXT_T), -EFAULT);

      if (!vCard->cardPresent) {
        return -ESHUTDOWN;
      }

      spin_lock_irqsave(&fileNodePtr->rcv_text->rcvLock, rcvTextLock_irqFlags);
      if (fileNodePtr->rcv_text->bufHead == fileNodePtr->rcv_text->bufTail) {
        spin_unlock_irqrestore(&fileNodePtr->rcv_text->rcvLock, rcvTextLock_irqFlags);
        return -EAGAIN;
      }
      msg = fileNodePtr->rcv_text->fileRcvBuffer[fileNodePtr->rcv_text->bufTail];
      vCanPopReceiveBuffer(fileNodePtr->rcv_text);
      spin_unlock_irqrestore(&fileNodePtr->rcv_text->rcvLock, rcvTextLock_irqFlags);
      copy_to_user_ret(my_arg.msg, &msg, sizeof(VCAN_EVENT), -EFAULT);
      }
      break;
  }

  return ioctl_return_value(vStat);
}

static inline int kcan_ioctl_script_envvar_control(VCanChanData *chd,
                                                   VCanHWInterface *hwIf,
                                                   int *vStat,
                                                   KCAN_IOCTL_ENVVAR_GET_INFO_T *arg)
{
  KCAN_IOCTL_ENVVAR_GET_INFO_T my_arg;

  if (!hwIf->script_envvar_control) {
    return -ENOSYS;
  }

  copy_from_user_ret(&my_arg, arg, sizeof(KCAN_IOCTL_ENVVAR_GET_INFO_T),
                     -EFAULT);

  *vStat = hwIf->script_envvar_control(chd, &my_arg);
  if (!*vStat) {
    copy_to_user_ret(arg, &my_arg, sizeof(KCAN_IOCTL_ENVVAR_GET_INFO_T),
                     -EFAULT);
  }

  return 0;
}

static inline int kcan_ioctl_script_set_envvar(VCanChanData *chd,
                                               VCanHWInterface *hwIf,
                                               int *vStat,
                                               KCAN_IOCTL_SCRIPT_SET_ENVVAR_T *arg)
{
  KCAN_IOCTL_SCRIPT_SET_ENVVAR_T *my_arg;

  if (!hwIf->script_envvar_put) {
    return -ENOSYS;
  }

  my_arg = kmalloc(sizeof(KCAN_IOCTL_SCRIPT_SET_ENVVAR_T), GFP_KERNEL);
  if (!my_arg) {
    *vStat = VCAN_STAT_NO_MEMORY;
    return 0;
  }

  if (copy_from_user(my_arg, arg, sizeof(KCAN_IOCTL_SCRIPT_SET_ENVVAR_T))) {
    kfree(my_arg);
    return -EFAULT;
  }

  *vStat = hwIf->script_envvar_put(chd, my_arg);
  kfree(my_arg);

  return 0;
}

static inline int kcan_ioctl_script_get_envvar(VCanChanData *chd,
                                               VCanHWInterface *hwIf,
                                               int *vStat,
                                               KCAN_IOCTL_SCRIPT_GET_ENVVAR_T *arg)
{
  KCAN_IOCTL_SCRIPT_GET_ENVVAR_T *my_arg;

  if (!hwIf->script_envvar_get) {
    return -ENOSYS;
  }

  my_arg = kmalloc(sizeof(KCAN_IOCTL_SCRIPT_GET_ENVVAR_T), GFP_KERNEL);
  if (!my_arg) {
    *vStat = VCAN_STAT_NO_MEMORY;
    return 0;
  }

  if (copy_from_user(my_arg, arg, sizeof(KCAN_IOCTL_SCRIPT_GET_ENVVAR_T))) {
    kfree(my_arg);
    return -EFAULT;
  }

  *vStat = hwIf->script_envvar_get(chd, my_arg);

  if (!*vStat) {
    if (copy_to_user(arg, my_arg, sizeof(KCAN_IOCTL_SCRIPT_GET_ENVVAR_T))) {
      kfree(my_arg);
      return -EFAULT;
    }
  }
  kfree(my_arg);

  return 0;
}

static inline int kcan_ioctl_device_messages_subscription(VCanChanData *chd,
                                                          VCanHWInterface *hwIf,
                                                          VCanOpenFileNode *fileNodePtr,
                                                          int *vStat,
                                                          KCAN_IOCTL_DEVICE_MESSAGES_SUBSCRIPTION_T *arg)
{
  KCAN_IOCTL_DEVICE_MESSAGES_SUBSCRIPTION_T input_arg;

  if (hwIf->deviceMessagesSubscription == NULL) {
    return -ENOSYS;
  }

  ArgPtrIn(sizeof(KCAN_IOCTL_DEVICE_MESSAGES_SUBSCRIPTION_T));
  copy_from_user_ret(&input_arg, arg,
                     sizeof(KCAN_IOCTL_DEVICE_MESSAGES_SUBSCRIPTION_T),
                     -EFAULT);

  *vStat = hwIf->deviceMessagesSubscription(fileNodePtr, &input_arg);
  return 0;
}

static inline int vcan_ioctl_bus_on(VCanChanData *chd,
                                    VCanHWInterface *hwIf,
                                    VCanOpenFileNode *fileNodePtr,
                                    int *vStat,
                                    VCanSetBusOn *arg)
{
  uint64_t      ttime;
  unsigned long rcvLock_irqFlags;
  uint64_t      time;
  VCanSetBusOn  my_arg;


  DEBUGPRINT(3, (TXT("VCAN_IOC_BUS_ON\n")));
  if (fileNodePtr->isBusOn) {
    return 0; // Already bus on, do nothing
  }

  if (chd->vCard->card_flags & DEVHND_CARD_REFUSE_TO_USE_CAN) {
    DEBUGPRINT(2, (TXT("VCAN_IOC_BUS_ON Refuse to run. returning -EACCES\n")));
    return -EACCES;
  }

  spin_lock_irqsave(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);
  vCanFlushReceiveBuffer(fileNodePtr);
  spin_unlock_irqrestore(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);

  memset(&chd->busStats, 0, sizeof(chd->busStats));
  vCanTime(chd->vCard, &ttime);
  chd->busStats.timestamp = (__u32) ttime;

  // Important to set this before channel get busOn, otherwise
  // we may miss messasges.
  fileNodePtr->isBusOn                    |= 1;
  fileNodePtr->chip_status.busStatus       = CHIPSTAT_ERROR_ACTIVE;
  fileNodePtr->chip_status.txErrorCounter  = 0;
  fileNodePtr->chip_status.rxErrorCounter  = 0;

  wait_for_completion(&chd->busOnCountCompletion);

  chd->busOnCount++;

  copy_from_user_ret(&my_arg, arg, sizeof(VCanSetBusOn), -EFAULT);
  if (my_arg.reset_time) {
    *vStat = hwIf->getTime(chd->vCard, &time);
    if (*vStat == VCAN_STAT_OK) {
      fileNodePtr->time_start_10usec = time;
    } else {
      my_arg.retval = VCANSETBUSON_FAIL;
      return 0;
    }
  }

  if (chd->busOnCount == 1) {
    *vStat = hwIf->flushSendBuffer(chd);
    if (*vStat == VCAN_STAT_OK) {
      *vStat = hwIf->busOn(chd);
      if (*vStat != VCAN_STAT_OK) {
        fileNodePtr->isBusOn = 0;
        chd->busOnCount--;
        DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_BUS_ON 1 failed with %d\n"),
                           *vStat));
      }
    } else {
      fileNodePtr->isBusOn = 0;
      chd->busOnCount--;
      DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_BUS_ON 2 failed with %d\n"), *vStat));
    }
  }
  complete(&chd->busOnCountCompletion);

  return 0;
}

static inline int vcan_ioctl_bus_off(VCanChanData *chd,
                                     VCanHWInterface *hwIf,
                                     VCanOpenFileNode *fileNodePtr,
                                     int *vStat)
{
  unsigned long rcvLock_irqFlags;

  DEBUGPRINT(3, (TXT("VCAN_IOC_BUS_OFF\n")));
  spin_lock_irqsave(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);
  vCanFlushReceiveBuffer(fileNodePtr);
  spin_unlock_irqrestore(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);

  memset(&chd->busStats, 0, sizeof(chd->busStats));

  if (fileNodePtr->isBusOn) {
    fileNodePtr->chip_status.busStatus = CHIPSTAT_BUSOFF;
    wait_for_completion(&chd->busOnCountCompletion);
    chd->busOnCount--;
    if (chd->busOnCount == 0) {
      *vStat = hwIf->busOff(chd);
      if (*vStat == VCAN_STAT_OK) {
        fileNodePtr->isBusOn = 0;
      } else {
        chd->busOnCount++;
        DEBUGPRINT(2, (TXT("VCAN_IOC_BUS_OFF failed with %d\n"), *vStat));
      }
    } else {
      fileNodePtr->isBusOn = 0;
    }
    complete(&chd->busOnCountCompletion);
  }

  return 0;
}

static inline int vcan_ioctl_set_bitrate(VCanChanData *chd,
                                         VCanHWInterface *hwIf,
                                         VCanOpenFileNode *fileNodePtr,
                                         int *vStat,
                                         VCanSetBusParams *arg)
{
  VCanSetBusParams my_arg;

  if (copy_from_user(&my_arg, arg, sizeof(VCanSetBusParams))) {
    DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_SET_BITRATE 1\n")));
    return -EFAULT;
  }

  if (fileNodePtr->init_access == 0) {
    my_arg.retval = VCANSETBUSPARAMS_NO_INIT_ACCESS;
    if (copy_to_user(arg, &my_arg, sizeof(VCanSetBusParams))) {
      DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_SET_BITRATE 2\n")));
      return -EFAULT;
    }
    return 0;
  }

  if (chd->vCard->card_flags & DEVHND_CARD_REFUSE_TO_USE_CAN) {
    DEBUGPRINT(2, (TXT("ERROR: VCAN_IOC_SET_BITRATE Refuse to run. returning -EACCES\n")));
    return -EACCES;
  }

  *vStat = hwIf->setBusParams(chd, &my_arg.bp);

  if (*vStat != VCAN_STAT_OK) {
    DEBUGPRINT(1, (TXT("ERROR: hwIf->setBusParams(...) = %d\n"), *vStat));
  } else {
    VCanBusParams busParamsSet;
    if (chd->openMode != OPEN_AS_LIN) {
      *vStat = hwIf->getBusParams(chd, &busParamsSet);
      // Indicate that something went wrong by setting freq to 0
      if (*vStat == VCAN_STAT_BAD_PARAMETER) {
        DEBUGPRINT(1, (TXT("ERROR: Some bus parameter bad\n")));
        my_arg.bp.freq = 0;
      } else if (*vStat != VCAN_STAT_OK) {
        DEBUGPRINT(1, (TXT("ERROR: hwIf->getBusParams(...) = %d\n"), *vStat));
      } else if (busParamsSet.freq != my_arg.bp.freq) {
        DEBUGPRINT(1, (TXT("ERROR: Bad freq: %d vs %d\n"),
                       busParamsSet.freq, my_arg.bp.freq));
        my_arg.bp.freq = 0;
      } else if (my_arg.bp.freq > 1000000) {
        DEBUGPRINT(1, (TXT("ERROR: Too high freq: %d\n"), my_arg.bp.freq));
      } else if (busParamsSet.sjw != my_arg.bp.sjw) {
        DEBUGPRINT(1, (TXT("ERROR: Bad sjw: %d vs %d\n"),
                       busParamsSet.sjw, my_arg.bp.sjw));
        my_arg.bp.freq = 0;
      } else if (busParamsSet.tseg1 != my_arg.bp.tseg1) {
        DEBUGPRINT(1, (TXT("ERROR: Bad tseg1: %d vs %d\n"),
                       busParamsSet.tseg1, my_arg.bp.tseg1));
        my_arg.bp.freq = 0;
      } else if (busParamsSet.tseg2 != my_arg.bp.tseg2) {
        DEBUGPRINT(1, (TXT("ERROR: Bad tseg2: %d vs %d\n"),
                       busParamsSet.tseg2, my_arg.bp.tseg2));
        my_arg.bp.freq = 0;
      }

      if (my_arg.bp.freq == 0) {
        *vStat = VCAN_STAT_BAD_PARAMETER;
      } else {
        chd->busStats.bitTime100ns = 10000000 / my_arg.bp.freq;
        my_arg.retval = 0;
        if (copy_to_user(arg, &my_arg, sizeof(VCanSetBusParams))) {
          DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_SET_BITRATE 3\n")));
          return -EFAULT;
        }
      }
    }
  }

  return 0;
}

static inline int vcan_ioctl_get_bitrate(VCanChanData *chd,
                                         VCanHWInterface *hwIf,
                                         int *vStat,
                                         VCanBusParams *arg)
{
  VCanBusParams busParams;
  *vStat = hwIf->getBusParams(chd, &busParams);

  if (*vStat != VCAN_STAT_OK) {
    DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_GET_BITRATE failed. stat = %d\n"),
                       *vStat));
  }

  copy_to_user_ret(arg, &busParams, sizeof(VCanBusParams), -EFAULT);

  return 0;
}

static inline int vcan_ioctl_set_bitrate_tq(VCanChanData *chd,
                                            VCanHWInterface *hwIf,
                                            VCanOpenFileNode *fileNodePtr,
                                            int *vStat,
                                            VCanBusParamsTq *arg)
{
  VCanBusParamsTq my_arg;

  if (vCanSupportsBusParamsTq(chd) == 0) {
    return -ENOSYS;
  }

  if (copy_from_user(&my_arg, arg, sizeof(VCanBusParamsTq))) {
    DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_SET_BITRATE_TQ 1\n")));
    return -EFAULT;
  }

  my_arg.retval_from_driver = 0;

  if (fileNodePtr->init_access == 0) {
    my_arg.retval_from_driver = VCANSETBUSPARAMSTQ_NO_INIT_ACCESS;
  }

  if (chd->vCard->card_flags & DEVHND_CARD_REFUSE_TO_USE_CAN) {
    DEBUGPRINT(2, (TXT("ERROR: VCAN_IOC_SET_BITRATE_TQ Refuse to run. returning -EACCES\n")));
    return -EACCES;
  }

  if (my_arg.retval_from_driver == 0) {
    if (my_arg.data_valid == 0) {
      if (chd->openMode != OPEN_AS_CAN) {
        my_arg.retval_from_driver = VCANSETBUSPARAMSTQ_INVALID_HANDLE;
      }
    } else {
      if ((chd->openMode != OPEN_AS_CANFD_ISO) &&
          (chd->openMode != OPEN_AS_CANFD_NONISO)) {
        my_arg.retval_from_driver = VCANSETBUSPARAMSTQ_INVALID_HANDLE;
      }
    }

    if (my_arg.retval_from_driver == 0) {
      *vStat = hwIf->setBusParamsTq(chd, &my_arg);
    }
  }

  if (*vStat == VCAN_STAT_OK) {
    if (copy_to_user(arg, &my_arg, sizeof(VCanBusParamsTq))) {
      DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_SET_BITRATE_TQ 3\n")));
      return -EFAULT;
    }
  } else {
    DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_SET_BITRATE_TQ 4. stat=%d\n"), *vStat));
  }

  return 0;
}

static inline int vcan_ioctl_get_bitrate_tq(VCanChanData *chd,
                                            VCanHWInterface *hwIf,
                                            int *vStat,
                                            VCanBusParamsTq *arg)
{
  VCanBusParamsTq my_arg;

  if (vCanSupportsBusParamsTq(chd) == 0) {
    return -ENOSYS;
  }

  if (copy_from_user(&my_arg, arg, sizeof(VCanBusParamsTq))) {
    DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_GET_BITRATE 1\n")));
    return -EFAULT;
  }

  my_arg.retval_from_driver = 0;

  if (my_arg.data_valid == 0) {
    if (chd->openMode != OPEN_AS_CAN) {
      my_arg.retval_from_driver = VCANSETBUSPARAMSTQ_INVALID_HANDLE;
    }
  } else {
    if ((chd->openMode != OPEN_AS_CANFD_ISO) && (chd->openMode != OPEN_AS_CANFD_NONISO)) {
      my_arg.retval_from_driver = VCANSETBUSPARAMSTQ_INVALID_HANDLE;
    }
  }

  if (my_arg.retval_from_driver == 0) {
    *vStat = hwIf->getBusParamsTq(chd, &my_arg);
  }

  if (*vStat == VCAN_STAT_OK) {
    if (copy_to_user(arg, &my_arg, sizeof(VCanBusParamsTq))) {
      DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_GET_BITRATE 2\n")));
      return -EFAULT;
    }
  } else {
    DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_GET_BITRATE 3. stat=%d\n"), *vStat));
  }

  return 0;
}
static inline int vcan_ioctl_flash_leds(VCanChanData *chd,
                                        VCanHWInterface *hwIf,
                                        int *vStat,
                                        KCAN_IOCTL_LED_ACTION_I *arg)
{
  KCAN_IOCTL_LED_ACTION_I ledCtrl;

  if (hwIf->flashLeds == NULL) {
    return -ENOSYS;
  }

  ArgPtrIn(sizeof(KCAN_IOCTL_LED_ACTION_I));
  copy_from_user_ret(&ledCtrl, arg, sizeof(KCAN_IOCTL_LED_ACTION_I), -EFAULT);
  *vStat = hwIf->flashLeds(chd, ledCtrl.sub_command, ledCtrl.timeout);

  return 0;
}

static inline int vcan_ioctl_memo_disk_io_fast(VCanChanData *chd,
                                               VCanHWInterface *hwIf,
                                               int *vStat,
                                               KCANY_MEMO_DISK_IO_FAST *arg)
{
  KCANY_MEMO_DISK_IO_FAST *memoBulkMessage;

  if (hwIf->memoGetData == NULL) {
    return -ENOSYS;
  }
  memoBulkMessage = kmalloc(sizeof(KCANY_MEMO_DISK_IO_FAST), GFP_KERNEL);
  if (!memoBulkMessage) {
    *vStat = VCAN_STAT_NO_MEMORY;
    return 0;
  }

  memset(memoBulkMessage, 0, sizeof(KCANY_MEMO_DISK_IO_FAST));
  ArgPtrIn(sizeof(KCANY_MEMO_DISK_IO_FAST));
  if (copy_from_user(memoBulkMessage, arg, sizeof(KCANY_MEMO_DISK_IO_FAST))) {
    kfree(memoBulkMessage);
    return -EFAULT;
  }
  switch (memoBulkMessage->subcommand) {
    case KCANY_MEMO_DISK_IO_FASTREAD_PHYSICAL_SECTOR:
    case KCANY_MEMO_DISK_IO_FASTREAD_LOGICAL_SECTOR:
      *vStat = hwIf->memoGetData(chd,
                                 memoBulkMessage->subcommand,
                                 &memoBulkMessage->buffer,
                                 memoBulkMessage->count * 512,
                                 memoBulkMessage->first_sector,
                                 memoBulkMessage->count,
                                 &memoBulkMessage->status,
                                 &memoBulkMessage->dio_status,
                                 &memoBulkMessage->lio_status, 0);
      if (VCAN_STAT_OK == *vStat) {
        if (copy_to_user(arg, memoBulkMessage,
                         sizeof(KCANY_MEMO_DISK_IO_FAST))) {
          kfree(memoBulkMessage);
          return -EFAULT;
        }
      }
      break;
    default:
      *vStat = VCAN_STAT_BAD_PARAMETER;
      break;
  }
  kfree(memoBulkMessage);

  return 0;
}

static inline int vcan_ioctl_memo_config_mode(VCanChanData *chd,
                                              VCanHWInterface *hwIf,
                                              int *vStat,
                                              KCANY_CONFIG_MODE *arg)
{
  KCANY_CONFIG_MODE config;

  if (hwIf->memoConfigMode == NULL) {
    return -ENOSYS;
  }
  //  if (chd->vCard.is_memorator)

  ArgPtrIn(sizeof(KCANY_CONFIG_MODE));
  copy_from_user_ret(&config, (KCANY_CONFIG_MODE *)arg,
                     sizeof(KCANY_CONFIG_MODE), -EFAULT);
  *vStat = hwIf->memoConfigMode(chd, config.interval);

  return 0;
}

static inline int vcan_ioctl_memo_disk_io(VCanChanData *chd,
                                          VCanHWInterface *hwIf,
                                          int *vStat,
                                          KCANY_MEMO_DISK_IO *arg)
{
  KCANY_MEMO_DISK_IO memoDiskMessage;

  if (hwIf->memoGetData == NULL) {
    return -ENOSYS;
  }

  if (hwIf->memoPutData == NULL) {
    return -ENOSYS;
  }

  ArgPtrIn(sizeof(KCANY_MEMO_DISK_IO));
  copy_from_user_ret(&memoDiskMessage, arg, sizeof(KCANY_MEMO_DISK_IO),
                     -EFAULT);
  switch (memoDiskMessage.subcommand) {
    case KCANY_MEMO_DISK_IO_READ_PHYSICAL_SECTOR:
    case KCANY_MEMO_DISK_IO_READ_LOGICAL_SECTOR:
      *vStat = hwIf->memoGetData(chd, memoDiskMessage.subcommand,
                                memoDiskMessage.buffer,
                                sizeof(memoDiskMessage.buffer),
                                memoDiskMessage.first_sector,
                                memoDiskMessage.count,
                                &memoDiskMessage.status,
                                &memoDiskMessage.dio_status,
                                &memoDiskMessage.lio_status, 0);
      break;

    case KCANY_MEMO_DISK_IO_WRITE_LOGICAL_SECTOR:
    case KCANY_MEMO_DISK_IO_WRITE_PHYSICAL_SECTOR:
    case KCANY_MEMO_DISK_IO_ERASE_LOGICAL_SECTOR:
    case KCANY_MEMO_DISK_IO_ERASE_PHYSICAL_SECTOR:
      *vStat = hwIf->memoPutData(chd, memoDiskMessage.subcommand,
                                memoDiskMessage.buffer,
                                sizeof(memoDiskMessage.buffer),
                                memoDiskMessage.first_sector,
                                memoDiskMessage.count,
                                &memoDiskMessage.status,
                                &memoDiskMessage.dio_status,
                                &memoDiskMessage.lio_status, 0);
      break;
    default:
      *vStat = VCAN_STAT_BAD_PARAMETER;
      break;
  }

  if (VCAN_STAT_OK == *vStat) {
    copy_to_user_ret(arg, &memoDiskMessage,
                     sizeof(KCANY_MEMO_DISK_IO), -EFAULT);
  }

  return 0;
}

static inline int vcan_ioctl_memo_get_data(VCanChanData *chd,
                                           VCanHWInterface *hwIf,
                                           int *vStat,
                                           KCANY_MEMO_INFO *arg)
{
  KCANY_MEMO_INFO *memoInfo;
  unsigned int buflen;

  if (hwIf->memoGetData == NULL) {
    return -ENOSYS;
  }

  memoInfo = kmalloc(sizeof(KCANY_MEMO_INFO), GFP_KERNEL);
  if (!memoInfo) {
    *vStat = VCAN_STAT_NO_MEMORY;
    return 0;
  }
  if (copy_from_user(memoInfo, arg, sizeof(KCANY_MEMO_INFO))) {
    kfree(memoInfo);
    return -EFAULT;
  }

  buflen = memoInfo->buflen;
  // The user don't always set the buflen...
  if (buflen == 0) {
    buflen = sizeof(memoInfo->buffer);
  }
  *vStat = hwIf->memoGetData(chd, memoInfo->subcommand,
                             memoInfo->buffer, buflen,
                             0, 0, &memoInfo->status,
                             &memoInfo->dio_status,
                             &memoInfo->lio_status, memoInfo->timeout);
  if (VCAN_STAT_OK == *vStat) {
    if (copy_to_user(arg, memoInfo, sizeof(KCANY_MEMO_INFO))) {
      kfree(memoInfo);
      return -EFAULT;
    }
  }
  kfree(memoInfo);

  return 0;
}

static inline int vcan_ioctl_memo_put_data(VCanChanData *chd,
                                           VCanHWInterface *hwIf,
                                           int *vStat,
                                           KCANY_MEMO_INFO *arg)
{
  KCANY_MEMO_INFO *memoInfo;

  if (hwIf->memoPutData == NULL) {
    return -ENOSYS;
  }

  memoInfo = kmalloc(sizeof(KCANY_MEMO_INFO), GFP_KERNEL);
  if (!memoInfo) {
    *vStat = VCAN_STAT_NO_MEMORY;
    return 0;
  }
  if (copy_from_user(memoInfo, arg, sizeof(KCANY_MEMO_INFO))) {
    kfree(memoInfo);
    return -EFAULT;
  }

  *vStat = hwIf->memoPutData(chd, memoInfo->subcommand,
                             memoInfo->buffer, memoInfo->buflen,
                             0, 0, &memoInfo->status,
                             &memoInfo->dio_status,
                             &memoInfo->lio_status, memoInfo->timeout);
  if (VCAN_STAT_OK == *vStat) {
    if (copy_to_user(arg, memoInfo, sizeof(KCANY_MEMO_INFO))) {
      kfree(memoInfo);
      return -EFAULT;
    }
  }
  kfree(memoInfo);

  return 0;
}

static inline int vcan_ioctl_set_output_mode(VCanChanData *chd,
                                             VCanHWInterface *hwIf,
                                             VCanOpenFileNode *fileNodePtr,
                                             int *vStat,
                                             VCanSetBusOutputControl *arg)
{
  VCanSetBusOutputControl my_arg;

  if (copy_from_user(&my_arg, arg, sizeof(VCanSetBusOutputControl))) {
    DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_SET_OUTPUT_MODE 1\n")));
    return -EFAULT;
  }

  if (fileNodePtr->init_access == 0) {
    my_arg.retval = VCANSETBUSOUTPUTCONTROL_NO_INIT_ACCESS;
    copy_to_user_ret(arg, &my_arg, sizeof(VCanSetBusOutputControl), -EFAULT);
    return 0;
  }

  *vStat = hwIf->setOutputMode(chd, my_arg.silent);

  if (VCAN_STAT_OK == *vStat) {
    chd->driverMode = my_arg.silent;
  } else {
    DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_SET_OUTPUT_MODE 2\n")));
    return 0;
  }

  my_arg.retval = 0;
  if (copy_to_user(arg, &my_arg, sizeof(VCanSetBusOutputControl))) {
    DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_SET_OUTPUT_MODE 3\n")));
    return -EFAULT;
  }

  return 0;
}

static inline int vcan_ioctl_get_output_mode(VCanChanData *chd,
                                             VCanHWInterface *hwIf,
                                             int *vStat,
                                             int *arg)
{
  if (hwIf->getOutputMode == NULL) {
    // use cached value
    put_user_ret(chd->driverMode, arg, -EFAULT);
  } else {
    // use value from device
    int silent;
    *vStat = hwIf->getOutputMode(chd, &silent);
    if (VCAN_STAT_OK == *vStat) {
      put_user_ret(silent, arg, -EFAULT);
    }
  }

  return 0;
}

static inline int vcan_ioctl_set_device_mode(VCanChanData *chd,
                                             VCanHWInterface *hwIf,
                                             int *vStat,
                                             uint32_t *arg)
{
  uint32_t devicemode;
  if (hwIf->kvDeviceSetMode == NULL) {
    return -ENOSYS;
  }
  ArgPtrIn(sizeof(uint32_t));
  copy_from_user_ret(&devicemode, arg, sizeof(uint32_t), -EFAULT);
  *vStat = hwIf->kvDeviceSetMode(chd, devicemode);
  if (VCAN_STAT_OK == *vStat) {
    DEBUGPRINT(3, (TXT("kvDeviceSetMode: %d\n"), devicemode));
  }

  return 0;
}

static inline int vcan_ioctl_get_device_mode(VCanChanData *chd,
                                             VCanHWInterface *hwIf,
                                             int *vStat,
                                             uint32_t *arg)
{
  uint32_t devicemode;

  if (hwIf->kvDeviceGetMode == NULL) {
    return -ENOSYS;
  }

  memset(&devicemode, 0, sizeof(uint32_t));
  ArgPtrIn(sizeof(uint32_t));
  copy_from_user_ret(&devicemode, arg, sizeof(uint32_t), -EFAULT);
  *vStat = hwIf->kvDeviceGetMode(chd, &devicemode);
  if (VCAN_STAT_OK == *vStat) {
    DEBUGPRINT(3, (TXT("kvDeviceGetMode: %d\n"), devicemode));
    copy_to_user_ret(arg, &devicemode, sizeof(devicemode), -EFAULT);
  }

  return 0;
}

static inline int vcan_ioctl_file_get_count(VCanChanData *chd,
                                            VCanHWInterface *hwIf,
                                            int *vStat,
                                            int *arg)
{
  int count;

  if (hwIf->kvFileGetCount == NULL) {
    return -ENOSYS;
  }
  ArgPtrIn(sizeof(int));
  *vStat = hwIf->kvFileGetCount(chd, &count);
  put_user_ret(count, arg, -EFAULT);

  return 0;
}

static inline int vcan_ioctl_file_get_name(VCanChanData *chd,
                                           VCanHWInterface *hwIf,
                                           int *vStat,
                                           VCAN_FILE_GET_NAME_T *arg)
{
  char name[255]; // We currently only support 8.3 names, but let's be nice
  int len;
  VCAN_FILE_GET_NAME_T input_arg;

  if (hwIf->kvFileGetName == NULL) {
    return -ENOSYS;
  }
  ArgPtrIn(sizeof(VCAN_FILE_GET_NAME_T));
  copy_from_user_ret(&input_arg, (VCAN_FILE_GET_NAME_T *)arg,
                     sizeof(VCAN_FILE_GET_NAME_T), -EFAULT);

  *vStat = hwIf->kvFileGetName(chd, input_arg.fileNo, name, sizeof(name));
  len = sizeof(name);
  len = len < input_arg.namelen ? len : input_arg.namelen;
  copy_to_user_ret(arg->name, name, len, -EFAULT);

  return 0;
}

static inline int kcan_ioctl_script_control(VCanChanData *chd,
                                            VCanHWInterface *hwIf,
                                            int *vStat,
                                            KCAN_IOCTL_SCRIPT_CONTROL_T *arg)
{
  KCAN_IOCTL_SCRIPT_CONTROL_T scriptControl;

  if (hwIf->kvScriptControl == NULL) {
    return -ENOSYS;
  }
  copy_from_user_ret(&scriptControl, arg, sizeof(KCAN_IOCTL_SCRIPT_CONTROL_T),
                     -EFAULT);

  *vStat = hwIf->kvScriptControl(chd, &scriptControl);

  copy_to_user_ret(arg, &scriptControl, sizeof(scriptControl), -EFAULT);

  return 0;
}

static inline int vcan_ioctl_get_bus_param_limits(VCanChanData *chd,
                                                  VCanBusParamLimits *arg)
{
  VCanBusParamLimits my_arg;

  if (vCanSupportsBusParamsTq(chd) == 0) {
    return -ENOSYS;
  }

  if (copy_from_user(&my_arg, arg, sizeof(VCanBusParamLimits))) {
    DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_GET_BUS_PARAM_LIMITS 1\n")));
    return -EFAULT;
  }

  my_arg.version   = 1;
  my_arg.brp_size  = 13;
  my_arg.seg1_size = 9;
  my_arg.seg2_size = 5;
  my_arg.sjw_size  = 4;

  if (copy_to_user(arg, &my_arg, sizeof(VCanBusParamLimits))) {
    DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_GET_BUS_PARAM_LIMITS 2\n")));
    return -EFAULT;
  }

  return 0;
}

static inline int vcan_ioctl_get_clock_info(VCanChanData *chd,
                                            VCanHWInterface *hwIf,
                                            int *vStat,
                                            VCanClockInfo *arg)
{

  uint32_t      freq_mhz;
  VCanClockInfo my_arg;

  if (vCanSupportsBusParamsTq(chd) == 0) {
    return -ENOSYS;
  }

  if (copy_from_user(&my_arg, arg, sizeof(VCanClockInfo))) {
    DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_GET_CLOCK_INFO 1\n")));
    return -EFAULT;
  }

  *vStat = hwIf->kvDeviceGetClockFreqMhz(chd, &freq_mhz);

  if (*vStat == VCAN_STAT_OK) {
    my_arg.version      = 1;
    my_arg.numerator    = freq_mhz;
    my_arg.denominator  = 1;
    my_arg.power_of_ten = 6;
    my_arg.accuracy_ppm = 100;

    if (copy_to_user(arg, &my_arg, sizeof(VCanClockInfo))) {
      DEBUGPRINT(1, (TXT("ERROR: VCAN_IOC_GET_CLOCK_INFO 2\n")));
      return -EFAULT;
    }
  }

  return 0;
}

static inline int vcan_ioctl_open_chan(VCanChanData *chd,
                                       VCanOpenFileNode *fileNodePtr,
                                       VCanOpen *arg)
{
  VCanOpen flags;
  unsigned long irqFlags;
  VCanOpenFileNode *tmpFnp;

  if (copy_from_user(&flags, arg, sizeof(VCanOpen))) {
    DEBUGPRINT(1, (TXT("VCAN_IOC_OPEN_CHAN 1\n")));
    return -EFAULT;
  }

  flags.retval = 0;

  spin_lock_irqsave(&chd->openLock, irqFlags);
  for (tmpFnp = chd->openFileList;
       tmpFnp && (!tmpFnp->channelLocked || flags.override_exclusive);
       tmpFnp = tmpFnp->next) {
    /* Empty loop */
  }

  // Was channel not locked (i.e. not opened exclusive) before?
  if (tmpFnp) {
    flags.retval = VCANOPEN_LOCKED;
  } else {
    fileNodePtr->channelOpen = 1;
    fileNodePtr->chanNr      = flags.chanNr;
  }
  spin_unlock_irqrestore(&chd->openLock, irqFlags);

  if (copy_to_user(arg, &flags, sizeof(VCanOpen))) {
    return -EFAULT;
    DEBUGPRINT(1, (TXT("VCAN_IOC_OPEN_CHAN 2\n")));
  }

  return 0;
}

static inline int vcan_ioctl_open_excl(VCanChanData *chd,
                                       VCanOpenFileNode *fileNodePtr,
                                       VCanOpen *arg)
{
  VCanOpen flags;
  unsigned long irqFlags;
  VCanOpenFileNode *tmpFnp;

  if (copy_from_user(&flags, arg, sizeof(VCanOpen))) {
    DEBUGPRINT(1, (TXT("VCAN_IOC_OPEN_EXCL 1\n")));
    return -EFAULT;
  }

  flags.retval = 0;

  spin_lock_irqsave(&chd->openLock, irqFlags);
  // Return -1 if channel is opened by someone else
  for (tmpFnp = chd->openFileList; tmpFnp && !tmpFnp->channelLocked;
       tmpFnp = tmpFnp->next) {
    /* Empty loop */
  }

  if (tmpFnp) { // channel was locked by another handle
    flags.retval = VCANOPEN_LOCKED;
  } else {
    for (tmpFnp = chd->openFileList; tmpFnp && !tmpFnp->channelOpen;
         tmpFnp = tmpFnp->next) {
      /* Empty loop */
    }
    // Was channel not opened before?
    if (tmpFnp) {
      flags.retval = VCANOPEN_OPENED;
    } else {
      fileNodePtr->channelOpen   = 1;
      fileNodePtr->channelLocked = 1;
      fileNodePtr->chanNr        = flags.chanNr;
    }
  }
  spin_unlock_irqrestore(&chd->openLock, irqFlags);

  if (copy_to_user(arg, &flags, sizeof(VCanOpen))) {
    return -EFAULT;
    DEBUGPRINT(1, (TXT("VCAN_IOC_OPEN_EXCL 2\n")));
  }

  return 0;
}

static inline int vcan_ioctl_open_init_access(VCanChanData *chd,
                                              VCanOpenFileNode *fileNodePtr,
                                              VCanInitAccess *arg)
{
  unsigned long irqFlags;
  VCanInitAccess flags;
  VCanOpenFileNode *tmpFnp;

  if (copy_from_user(&flags, arg, sizeof(VCanInitAccess))) {
    DEBUGPRINT(1, (TXT("VCAN_IOC_OPEN_INIT_ACCESS 1\n")));
    return -EFAULT;
  }

  flags.retval = 0;

  spin_lock_irqsave(&chd->openLock, irqFlags);
  for (tmpFnp = chd->openFileList; tmpFnp && !tmpFnp->init_access;
       tmpFnp = tmpFnp->next) {
    /* Empty loop */
  }

  if (tmpFnp) { // we already have a handle with init access
    if (flags.require_init_access) {
      flags.retval = -1;
    }
  } else {
    if (flags.wants_init_access == 0) {
      if (flags.require_init_access) {
        flags.retval = -1;
      }
    }
    fileNodePtr->init_access = flags.wants_init_access;
  }

  // report back to caller if we got init access or not
  flags.wants_init_access = fileNodePtr->init_access;
  spin_unlock_irqrestore(&chd->openLock, irqFlags);

  if (copy_to_user(arg, &flags, sizeof(VCanInitAccess))) {
    return -EFAULT;
    DEBUGPRINT(1, (TXT("VCAN_IOC_OPEN_INIT_ACCESS 2\n")));
  }

  return 0;
}
static inline int vcan_ioctl_wait_empty(VCanOpenFileNode *fileNodePtr,
                                        unsigned long *arg)
{
  unsigned long timeout;

  ArgPtrIn(sizeof(unsigned long));
  get_user_long_ret(timeout, arg, -EFAULT);
  return wait_tx_queue_empty(fileNodePtr, timeout);
}

static inline int kcan_ioctl_lin_mode(VCanChanData *chd, int *arg)
{
  int val;

  ArgPtrIn(sizeof(int));
  copy_from_user_ret(&val, arg, sizeof(int), -EFAULT);
  chd->linMode = val;

  return 0;
}

static inline int vcan_ioctl_get_channel_info(VCanChanData *chd, int *arg)
{
  int flags = 0;

  ArgPtrOut(sizeof(int));

  if (chd->isOnBus) {
      flags |= VCAN_CHANNEL_STATUS_ON_BUS;
  }
  if (chd->openMode == OPEN_AS_CANFD_NONISO) {
    flags |= VCAN_CHANNEL_STATUS_CANFD;
  }
  if (chd->openMode == OPEN_AS_CANFD_ISO) {
    flags |= VCAN_CHANNEL_STATUS_CANFD;
  }
  if (chd->openMode == OPEN_AS_LIN) {
    flags |= VCAN_CHANNEL_STATUS_LIN;
    // linMode contains one of the flags:
    // VCAN_CHANNEL_STATUS_LIN_MASTER, VCAN_CHANNEL_STATUS_LIN_SLAVE
    flags |= chd->linMode;
  }

  // There's currently no way of knowing if the channel was opened in
  // exclusive mode
  // flags |= canCHANNEL_IS_EXCLUSIVE;

  put_user_ret(flags, arg, -EFAULT);

  return 0;
}

static inline int vcan_ioctl_flush_rcvbuffer(VCanOpenFileNode *fileNodePtr)
{
  unsigned long    rcvLock_irqFlags;

  spin_lock_irqsave(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);
  vCanFlushReceiveBuffer(fileNodePtr);
  spin_unlock_irqrestore(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);

  return 0;
}

static inline int vcan_ioctl_read_timer(VCanChanData *chd,
                                        VCanHWInterface *hwIf,
                                        VCanOpenFileNode *fileNodePtr,
                                        int *vStat,
                                        uint64_t *arg)
{
  uint64_t time;

  ArgPtrOut(sizeof(uint64_t));
  *vStat = hwIf->getTime(chd->vCard, &time);
  if (*vStat == VCAN_STAT_OK) {
    time = time - fileNodePtr->time_start_10usec;
    copy_to_user_ret(arg, &time, sizeof(uint64_t), -EFAULT);
  }

  return 0;
}

static inline int vcan_ioctl_get_rx_queue_level(VCanOpenFileNode *fileNodePtr,
                                                int *arg)
{
  int ql;
  unsigned long rcvLock_irqFlags;

  ArgPtrOut(sizeof(int));
  spin_lock_irqsave(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);
  ql = getQLen(fileNodePtr->rcv->bufHead, fileNodePtr->rcv->bufTail,
               fileNodePtr->rcv->size);
  spin_unlock_irqrestore(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);
  put_user_ret(ql, arg, -EFAULT);

  return 0;
}

static inline int vcan_ioctl_set_txack(VCanOpenFileNode *fileNodePtr, int *arg)
{
  int val;

  ArgPtrIn(sizeof(int));
  copy_from_user_ret(&val, arg, sizeof(int), -EFAULT);
  DEBUGPRINT(3, (TXT("KVASER Try to set VCAN_IOC_SET_TXACK to %d, was %d\n"),
                 val, fileNodePtr->modeTx));
  if (val >= TXACK_OFF && val <= TXACK_DISABLED) {
    fileNodePtr->modeTx = val;
    DEBUGPRINT(3, (TXT("KVASER Managed to set VCAN_IOC_SET_TXACK to %d\n"),
                   fileNodePtr->modeTx));
  } else {
    return -EFAULT;
  }

  return 0;
}

static inline int vcan_ioctl_set_local_txack(VCanOpenFileNode *fileNodePtr, uint32_t  *arg)
{
  int val;

  ArgPtrIn(sizeof(uint32_t ));
  copy_from_user_ret(&val, arg, sizeof(uint32_t ), -EFAULT);
  DEBUGPRINT(3, (TXT("KVASER Try to set VCAN_IOC_SET_LOCAL_TXACK to %d, was %d\n"),
                 val, fileNodePtr->modeTxLocal));
  if (val >= TXACK_OFF && val <= TXACK_ON) {
    fileNodePtr->modeTxLocal = val;
    DEBUGPRINT(3, (TXT("KVASER setting VCAN_IOC_SET_LOCAL_TXACK to %d\n"),
                   fileNodePtr->modeTxLocal));
  } else {
    return -EFAULT;
  }

  return 0;
}

static inline int vcan_ioctl_set_txrq(VCanOpenFileNode *fileNodePtr, int *arg)
{
  int val;

  ArgPtrIn(sizeof(int));
  copy_from_user_ret(&val, arg, sizeof(int), -EFAULT);
  DEBUGPRINT(3, (TXT("KVASER Try to set VCAN_IOC_SET_TXRQ to %d, was %d\n"),
                 val, fileNodePtr->modeTxRq));
  if (val >= 0 && val <= 2) {
    fileNodePtr->modeTxRq = val;
    DEBUGPRINT(3, (TXT("KVASER Managed to set VCAN_IOC_SET_TXRQ to %d\n"),
                   fileNodePtr->modeTxRq));
  } else {
    return -EFAULT;
  }

  return 0;
}

static inline int vcan_ioctl_set_txecho(VCanOpenFileNode *fileNodePtr,
                                        unsigned char *arg)
{
  unsigned char val;
  int flag;

  ArgPtrIn(sizeof(char));
  copy_from_user_ret(&val, arg, sizeof(unsigned char), -EFAULT);
  flag = val;
  DEBUGPRINT(3, (TXT("KVASER Try to set VCAN_IOC_SET_TXECHO to %d, was %d\n"),
                 flag, !fileNodePtr->modeNoTxEcho));
  if (flag >= 0 && flag <= 2) {
    fileNodePtr->modeNoTxEcho = !flag;
    DEBUGPRINT(3, (TXT("KVASER Managed to set VCAN_IOC_SET_TXECHO to %d\n"),
                   !fileNodePtr->modeNoTxEcho));
  } else {
    return -EFAULT;
  }

  return 0;
}

static inline int kcan_ioctl_objbuf_free_all(VCanChanData *chd,
                                             VCanHWInterface *hwIf,
                                             VCanOpenFileNode *fileNodePtr,
                                             int *vStat)
{
  if (fileNodePtr->objbuf) {
    int i;

    // Driver-implemented auto response buffers.
    for (i = 0; i < MAX_OBJECT_BUFFERS; i++) {
      fileNodePtr->objbuf[i].in_use = 0;
    }
  }
  if (hwIf->objbufFree) {
    if (chd->vCard->card_flags & DEVHND_CARD_AUTO_RESP_OBJBUFS) {
      // Firmware-implemented auto response buffers
      *vStat = hwIf->objbufFree(chd, OBJBUF_TYPE_AUTO_RESPONSE, -1);
    }
    if (chd->vCard->card_flags & DEVHND_CARD_AUTO_TX_OBJBUFS) {
      // Firmware-implemented periodic transmit buffers
      *vStat = hwIf->objbufFree(chd, OBJBUF_TYPE_PERIODIC_TX, -1);
    }
  }

  return 0;
}

static inline int vcan_ioctl_get_transceiver_info(VCanChanData *chd,
                                                  VCanHWInterface *hwIf,
                                                  int *vStat,
                                                  uint32_t *arg)
{
  uint32_t tmp;

  if (!hwIf->get_transceiver_type) {
    return -ENOSYS;
  }
  copy_from_user_ret(&tmp, arg, sizeof(uint32_t), -EFAULT);
  *vStat = hwIf->get_transceiver_type(chd, &tmp);
  copy_to_user_ret(arg, &tmp, sizeof(uint32_t), -EFAULT);

  return 0;
}

static inline int kcan_ioctl_tx_interval(VCanChanData *chd,
                                         VCanHWInterface *hwIf,
                                         int *vStat,
                                         uint32_t *arg)
{
  uint32_t tmp;

  if (!hwIf->tx_interval) {
    return -ENOSYS;
  }

  copy_from_user_ret(&tmp, arg, sizeof(uint32_t), -EFAULT);
  *vStat = hwIf->tx_interval(chd, &tmp);
  copy_to_user_ret(arg, &tmp, sizeof(uint32_t), -EFAULT);

  return 0;
}

static inline int kcan_ioctl_set_brlimit(VCanChanData *chd,
                                         uint32_t *arg)
{
  uint32_t val;

  copy_from_user_ret(&val, arg, sizeof(uint32_t), -EFAULT);
  if (val == 0) {
    chd->vCard->current_max_bitrate = chd->vCard->default_max_bitrate;
  } else {
    chd->vCard->current_max_bitrate = val;
  }

  return 0;
}

static inline int kcan_ioctl_objbuf_allocate(VCanChanData *chd,
                                             VCanHWInterface *hwIf,
                                             VCanOpenFileNode *fileNodePtr,
                                             int *vStat,
                                             KCanObjbufAdminData *arg)
{
  KCanObjbufAdminData io;

  ArgPtrOut(sizeof(KCanObjbufAdminData)); // Check first, to make sure
  ArgPtrIn(sizeof(KCanObjbufAdminData));
  copy_from_user_ret(&io, arg, sizeof(KCanObjbufAdminData), -EFAULT);

  DEBUGPRINT(2, (TXT("ObjBuf Allocate type=%d card_flags=0x%x\n"),
                 io.type, chd->vCard->card_flags));

  *vStat = VCAN_STAT_NO_RESOURCES;
  if (io.type == OBJBUF_TYPE_AUTO_RESPONSE) {
    if (chd->vCard->card_flags & DEVHND_CARD_AUTO_RESP_OBJBUFS) {
      // Firmware-implemented auto response buffers
      if (hwIf->objbufAlloc) {
        *vStat = hwIf->objbufAlloc(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                   &io.buffer_number);
      }
    } else {
      // Driver-implemented auto response buffers
      int i;
      if (!fileNodePtr->objbuf) {
        fileNodePtr->objbuf = kmalloc(sizeof(OBJECT_BUFFER) * MAX_OBJECT_BUFFERS,
                                      GFP_KERNEL);
        if (!fileNodePtr->objbuf) {
          *vStat = VCAN_STAT_NO_MEMORY;
        } else {
          memset(fileNodePtr->objbuf, 0,
                 sizeof(OBJECT_BUFFER) * MAX_OBJECT_BUFFERS);
          objbuf_init(fileNodePtr);
        }
      }
      if (fileNodePtr->objbuf) {
        *vStat = VCAN_STAT_NO_MEMORY;
        for (i = 0; i < MAX_OBJECT_BUFFERS; i++) {
          if (!fileNodePtr->objbuf[i].in_use) {
            io.buffer_number = i | OBJBUF_DRIVER_MARKER;
            fileNodePtr->objbuf[i].in_use = 1;
            *vStat = VCAN_STAT_OK;
            break;
          }
        }
      }
    }
  } else if (io.type == OBJBUF_TYPE_PERIODIC_TX) {
    if (chd->vCard->card_flags & DEVHND_CARD_AUTO_TX_OBJBUFS) {
      // Firmware-implemented periodic transmit buffers
      if (hwIf->objbufAlloc) {
        *vStat = hwIf->objbufAlloc(chd, OBJBUF_TYPE_PERIODIC_TX,
                                   &io.buffer_number);
      }
    }
  }

  DEBUGPRINT(2, (TXT("ObjBuf Allocate got nr=%x stat=0x%x\n"),
                 io.buffer_number, *vStat));

  ArgPtrOut(sizeof(KCanObjbufAdminData));
  copy_to_user_ret(arg, &io, sizeof(KCanObjbufAdminData), -EFAULT);

  return 0;
}

static inline int kcan_ioctl_objbuf_free(VCanChanData *chd,
                                         VCanHWInterface *hwIf,
                                         VCanOpenFileNode *fileNodePtr,
                                         int *vStat,
                                         KCanObjbufAdminData *arg)
{
  KCanObjbufAdminData io;

  ArgPtrIn(sizeof(KCanObjbufAdminData));
  copy_from_user_ret(&io, arg, sizeof(KCanObjbufAdminData), -EFAULT);
  DEBUGPRINT(2, (TXT("ObjBuf Free nr=%x card_flags=0x%x\n"),
                 io.buffer_number, chd->vCard->card_flags));

  if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
    int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;

    if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS)) {
      fileNodePtr->objbuf[buffer_number].in_use = 0;
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else if (hwIf->objbufExists && hwIf->objbufFree) {
    if (hwIf->objbufExists(chd, OBJBUF_TYPE_AUTO_RESPONSE, io.buffer_number)) {
      *vStat = hwIf->objbufFree(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                io.buffer_number);
    } else if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX,
                                  io.buffer_number)) {
      *vStat = hwIf->objbufFree(chd, OBJBUF_TYPE_PERIODIC_TX,
                                io.buffer_number);
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else {
    *vStat = VCAN_STAT_NO_RESOURCES;
  }

  return 0;
}

static inline int kcan_ioctl_objbuf_write(VCanChanData *chd,
                                          VCanHWInterface *hwIf,
                                          VCanOpenFileNode *fileNodePtr,
                                          int *vStat,
                                          KCanObjbufBufferData *arg)
{
  KCanObjbufBufferData io;

  ArgPtrIn(sizeof(KCanObjbufBufferData));
  copy_from_user_ret(&io, arg, sizeof(KCanObjbufBufferData), -EFAULT);

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
      fileNodePtr->objbuf[buffer_number].msg.flags  = (unsigned char)io.flags;
      fileNodePtr->objbuf[buffer_number].msg.length = (unsigned char)io.dlc;
      memcpy(fileNodePtr->objbuf[buffer_number].msg.data, io.data,
             sizeof(io.data));
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else if (hwIf->objbufExists && hwIf->objbufWrite) {
    if (hwIf->objbufExists(chd, OBJBUF_TYPE_AUTO_RESPONSE, io.buffer_number)) {
      *vStat = hwIf->objbufWrite(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                 io.buffer_number, io.id, io.flags, io.dlc,
                                 io.data);
    } else if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX,
                                  io.buffer_number)) {
      *vStat = hwIf->objbufWrite(chd, OBJBUF_TYPE_PERIODIC_TX,
                                 io.buffer_number, io.id, io.flags, io.dlc,
                                 io.data);
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else {
    *vStat = VCAN_STAT_NO_RESOURCES;
  }

  return 0;
}

static inline int kcan_ioctl_objbuf_set_filter(VCanChanData *chd,
                                               VCanHWInterface *hwIf,
                                               VCanOpenFileNode *fileNodePtr,
                                               int *vStat,
                                               KCanObjbufAdminData *arg)
{
  KCanObjbufAdminData io;

  ArgPtrIn(sizeof(KCanObjbufAdminData));
  copy_from_user_ret(&io, arg, sizeof(KCanObjbufAdminData), -EFAULT);

  DEBUGPRINT(2, (TXT("ObjBuf SetFilter nr=%x card_flags=0x%x\n"),
                 io.buffer_number, chd->vCard->card_flags));

  if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
    int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;

    if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS) &&
        (fileNodePtr->objbuf[buffer_number].in_use)) {
      fileNodePtr->objbuf[buffer_number].acc_code = io.acc_code;
      fileNodePtr->objbuf[buffer_number].acc_mask = io.acc_mask;
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else if (hwIf->objbufExists && hwIf->objbufSetFilter) {
    if (hwIf->objbufExists(chd, OBJBUF_TYPE_AUTO_RESPONSE, io.buffer_number)) {
      *vStat = hwIf->objbufSetFilter(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                   io.buffer_number,
                                   io.acc_code, io.acc_mask);
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else {
    *vStat = VCAN_STAT_NO_RESOURCES;
  }

  return 0;
}

static inline int kcan_ioctl_objbuf_set_flags(VCanChanData *chd,
                                              VCanHWInterface *hwIf,
                                              VCanOpenFileNode *fileNodePtr,
                                              int *vStat,
                                              KCanObjbufAdminData *arg)
{
  KCanObjbufAdminData io;
  ArgPtrIn(sizeof(KCanObjbufAdminData));
  copy_from_user_ret(&io, arg, sizeof(KCanObjbufAdminData), -EFAULT);

  DEBUGPRINT(2, (TXT("ObjBuf SetFlags nr=%x card_flags=0x%x\n"),
                 io.buffer_number, chd->vCard->card_flags));

  if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
    int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;

    if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS) &&
        (fileNodePtr->objbuf[buffer_number].in_use)) {
      fileNodePtr->objbuf[buffer_number].flags = io.flags;
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else if (hwIf->objbufExists && hwIf->objbufSetFlags) {
    if (hwIf->objbufExists(chd, OBJBUF_TYPE_AUTO_RESPONSE, io.buffer_number)) {
      *vStat = hwIf->objbufSetFlags(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                    io.buffer_number, io.flags);
    } else if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX,
                                  io.buffer_number)) {
      *vStat = hwIf->objbufSetFlags(chd, OBJBUF_TYPE_PERIODIC_TX,
                                    io.buffer_number, io.flags);
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else {
    *vStat = VCAN_STAT_NO_RESOURCES;
  }

  return 0;
}

static inline int kcan_ioctl_objbuf_enable(VCanChanData *chd,
                                           VCanHWInterface *hwIf,
                                           VCanOpenFileNode *fileNodePtr,
                                           int *vStat,
                                           KCanObjbufAdminData *arg)
{
  KCanObjbufAdminData io;

  ArgPtrIn(sizeof(KCanObjbufAdminData));
  copy_from_user_ret(&io, arg, sizeof(KCanObjbufAdminData), -EFAULT);

  DEBUGPRINT(2, (TXT("ObjBuf Enable nr=%x card_flags=0x%x\n"),
                 io.buffer_number, chd->vCard->card_flags));

  if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
    int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;

    if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS) &&
        (fileNodePtr->objbuf[buffer_number].in_use)) {
      fileNodePtr->objbuf[buffer_number].active = 1;
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else if (hwIf->objbufExists && hwIf->objbufEnable) {
    if (hwIf->objbufExists(chd, OBJBUF_TYPE_AUTO_RESPONSE, io.buffer_number)) {
      *vStat = hwIf->objbufEnable(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                  io.buffer_number, 1);
    } else if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX,
                                  io.buffer_number)) {
      *vStat = hwIf->objbufEnable(chd, OBJBUF_TYPE_PERIODIC_TX,
                                  io.buffer_number, 1);
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else {
    *vStat = VCAN_STAT_NO_RESOURCES;
  }

  return 0;
}

static inline int kcan_ioctl_objbuf_disable(VCanChanData *chd,
                                            VCanHWInterface *hwIf,
                                            VCanOpenFileNode *fileNodePtr,
                                            int *vStat,
                                            KCanObjbufAdminData *arg)
{
  KCanObjbufAdminData io;

  ArgPtrIn(sizeof(KCanObjbufAdminData));
  copy_from_user_ret(&io, arg, sizeof(KCanObjbufAdminData), -EFAULT);

  DEBUGPRINT(2, (TXT("ObjBuf Disable nr=%x card_flags=0x%x\n"),
                 io.buffer_number, chd->vCard->card_flags));

  if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
    int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;

    if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS) &&
        (fileNodePtr->objbuf[buffer_number].in_use)) {
      fileNodePtr->objbuf[buffer_number].active = 0;
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else if (hwIf->objbufExists && hwIf->objbufEnable) {
    if (hwIf->objbufExists(chd, OBJBUF_TYPE_AUTO_RESPONSE, io.buffer_number)) {
      *vStat = hwIf->objbufEnable(chd, OBJBUF_TYPE_AUTO_RESPONSE,
                                io.buffer_number, 0);
    } else if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX,
                                 io.buffer_number)) {
      *vStat = hwIf->objbufEnable(chd, OBJBUF_TYPE_PERIODIC_TX,
                                io.buffer_number, 0);
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else {
    *vStat = VCAN_STAT_NO_RESOURCES;
  }

  return 0;
}

static inline int kcan_ioctl_objbuf_set_period(VCanChanData *chd,
                                               VCanHWInterface *hwIf,
                                               VCanOpenFileNode *fileNodePtr,
                                               int *vStat,
                                               KCanObjbufAdminData *arg)
{
  KCanObjbufAdminData io;

  ArgPtrIn(sizeof(KCanObjbufAdminData));
  copy_from_user_ret(&io, arg, sizeof(KCanObjbufAdminData), -EFAULT);

  DEBUGPRINT(2, (TXT("ObjBuf SetPeriod nr=%x card_flags=0x%x\n"),
                 io.buffer_number, chd->vCard->card_flags));

  if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
    int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;

    if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS) &&
        (fileNodePtr->objbuf[buffer_number].in_use)) {
      // Driver-implemented auto tx buffers are not implemented.
      // fileNodePtr->objbuf[buffer_number].period = io.period;
      *vStat = VCAN_STAT_NO_RESOURCES; // Not implemented
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else if (hwIf->objbufExists && hwIf->objbufSetPeriod) {
    if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX, io.buffer_number)) {
      *vStat = hwIf->objbufSetPeriod(chd, OBJBUF_TYPE_PERIODIC_TX,
                                     io.buffer_number,
                                     io.period);
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else {
    *vStat = VCAN_STAT_NO_RESOURCES; // Not implemented
  }

  return 0;
}
static inline int kcan_ioctl_objbuf_set_msg_count(VCanChanData *chd,
                                                  VCanHWInterface *hwIf,
                                                  VCanOpenFileNode *fileNodePtr,
                                                  int *vStat,
                                                  KCanObjbufAdminData *arg)
{
  KCanObjbufAdminData io;

  ArgPtrIn(sizeof(KCanObjbufAdminData));
  copy_from_user_ret(&io, arg, sizeof(KCanObjbufAdminData), -EFAULT);

  DEBUGPRINT(2, (TXT("ObjBuf SetMsgCount nr=%x card_flags=0x%x\n"),
                 io.buffer_number, chd->vCard->card_flags));

  if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
    int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;

    if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS) &&
        (fileNodePtr->objbuf[buffer_number].in_use)) {
      // Driver-implemented auto tx buffers are not implemented.
      // fileNodePtr->objbuf[buffer_number].period = io.period;
      *vStat = VCAN_STAT_NO_RESOURCES; // Not implemented
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else if (hwIf->objbufExists && hwIf->objbufSetMsgCount) {
    if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX, io.buffer_number)) {
      *vStat = hwIf->objbufSetMsgCount(chd, OBJBUF_TYPE_PERIODIC_TX,
                                       io.buffer_number,
                                       io.period);
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else {
    *vStat = VCAN_STAT_NO_RESOURCES; // Not implemented
  }

  return 0;
}

static inline int kcan_ioctl_objbuf_send_burst(VCanChanData *chd,
                                               VCanHWInterface *hwIf,
                                               VCanOpenFileNode *fileNodePtr,
                                               int *vStat,
                                               KCanObjbufAdminData *arg)
{
  KCanObjbufAdminData io;

  ArgPtrIn(sizeof(KCanObjbufAdminData));
  copy_from_user_ret(&io, arg, sizeof(KCanObjbufAdminData), -EFAULT);

  DEBUGPRINT(2, (TXT("ObjBuf SendBurst nr=%x card_flags=0x%x\n"),
                 io.buffer_number, chd->vCard->card_flags));

  if (io.buffer_number & OBJBUF_DRIVER_MARKER) {
    int buffer_number = io.buffer_number & OBJBUF_DRIVER_MASK;

    if (fileNodePtr->objbuf && (buffer_number < MAX_OBJECT_BUFFERS) &&
        (fileNodePtr->objbuf[buffer_number].in_use)) {
      // Driver-implemented auto tx buffers are not implemented.
      *vStat = VCAN_STAT_NO_RESOURCES; // Not implemented
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else if (hwIf->objbufExists && hwIf->objbufSendBurst) {
    if (hwIf->objbufExists(chd, OBJBUF_TYPE_PERIODIC_TX, io.buffer_number)) {
      *vStat = hwIf->objbufSendBurst(chd, OBJBUF_TYPE_PERIODIC_TX,
                                     io.buffer_number,
                                     io.period);
    } else {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  } else {
    *vStat = VCAN_STAT_NO_RESOURCES; // Not implemented
  }

  return 0;
}
static inline int kcan_ioctl_read_tref_list(VCanChanData *chd,
                                            int *vStat,
                                            KCAN_IOCTL_READ_TREF_LIST_VALUE *arg)
{
  int stat;
  KCAN_IOCTL_READ_TREF_LIST_VALUE buffer;

  if (chd->vCard) {
    stat = softSyncGetTRefList(chd->vCard, &buffer,
                               sizeof(KCAN_IOCTL_READ_TREF_LIST_VALUE));
    if (stat != 0) {
      *vStat = VCAN_STAT_BAD_PARAMETER;
    }
  }
  ArgPtrOut(sizeof(KCAN_IOCTL_READ_TREF_LIST_VALUE));
  copy_to_user_ret(arg, &buffer, sizeof(KCAN_IOCTL_READ_TREF_LIST_VALUE),
                   -EFAULT);

  return 0;
}
static inline int kcan_ioctl_open_mode(VCanChanData *chd,
                                       KCAN_IOCTL_OPEN_MODE_T *arg)
{
  KCAN_IOCTL_OPEN_MODE_T mode_data;
  unsigned int openHandles = 0;
  unsigned long irqFlags;
  VCanOpenFileNode *tmpFnp;

  // unsigned char open_mode = OPEN_AS_CAN;
  // unsigned int req_caps = 0;
  ArgPtrIn(sizeof(KCAN_IOCTL_OPEN_MODE_T));
  copy_from_user_ret(&mode_data, arg, sizeof(KCAN_IOCTL_OPEN_MODE_T), -EFAULT);

  // Count the number of open handles to this channel
  spin_lock_irqsave(&chd->openLock, irqFlags);
  for (tmpFnp = chd->openFileList; tmpFnp; tmpFnp = tmpFnp->next) {
    openHandles++;
  }
  spin_unlock_irqrestore(&chd->openLock, irqFlags);
  /*
  if (open_data.action == CAN_MODE_SET)
    open_mode = open_data.mode;
  else
    open_mode = chd->openMode;
  open_data.status = CAN_CANFD_NOT_IMPLEMENTED;
  switch (open_mode) {
    case OPEN_AS_CANFD_NONISO:
      req_caps |= VCAN_CHANNEL_CAP_CANFD_NONISO;
      //intentional fall-through
    case OPEN_AS_CANFD_ISO:
      req_caps |= VCAN_CHANNEL_CAP_CANFD;
      //intentional fall-through
    case OPEN_AS_LIN:
      req_caps |= VCAN_CHANNEL_CAP_LIN_HYBRID;
      //intentional fall-through
    case OPEN_AS_CAN: {
      if (open_data.action == CAN_MODE_SET) {
        open_data.status = CAN_CANFD_FAILURE;
        if ((chd->capabilities & req_caps) == req_caps) {
          // 1 since we have to be open to be here.
          if (1 == openHandles) {
            chd->openMode = open_data.mode;
            open_data.status = CAN_CANFD_SUCCESS;
          }
          else {
            if (open_data.mode == chd->openMode) {
              open_data.status = CAN_CANFD_SUCCESS;
            }
            else {
              open_data.status = CAN_CANFD_MISMATCH;
            }
          }
        }
      }
      else if (open_data.action == CAN_CANFD_READ) {
        open_data.reply = chd->openMode;
        open_data.status = CAN_CANFD_SUCCESS;
      }
    }
  }
  */

  switch (mode_data.action) {
    case CAN_MODE_SET:
    {
      mode_data.status = CAN_OPEN_MODE_FAILURE;
      if (chd) {
        if (mode_data.mode == OPEN_AS_CAN) {
          // OPEN_AS_CAN is ok!
        } else if (mode_data.mode == OPEN_AS_CANFD_NONISO) {
          if (!(chd->capabilities & VCAN_CHANNEL_CAP_CANFD_NONISO)) {
            mode_data.status = CAN_OPEN_MODE_MISMATCH;
            break;
          }
        } else if (mode_data.mode == OPEN_AS_CANFD_ISO) {
          if (!(VCAN_CHANNEL_CAP_CANFD & chd->capabilities)) {
            mode_data.status = CAN_OPEN_MODE_MISMATCH;
            break;
          }
        } else if (mode_data.mode == OPEN_AS_LIN) {
          if (!(VCAN_CHANNEL_CAP_LIN_HYBRID & chd->capabilities)) {
            mode_data.status = CAN_OPEN_MODE_MISMATCH;
            break;
          }
        } else {
          // Say what??! Don't think so!
          mode_data.status = CAN_OPEN_MODE_NOT_IMPLEMENTED;
          break;
        }

        //if (pi->open_count == 1 /* we are the first to open it */) {
        if (1 == openHandles) { /* we are the first to open it */
          chd->openMode   = mode_data.mode;
          mode_data.status = CAN_OPEN_MODE_SUCCESS;
          break;
        } else {
          // same mode as we are already opened in...
          if (mode_data.mode == chd->openMode) {
            mode_data.status = CAN_OPEN_MODE_SUCCESS;
            break;
          } else {
            mode_data.status = CAN_OPEN_MODE_MISMATCH;
            break;
          }
        }
      }
    }
    break;
    case CAN_MODE_READ:
      mode_data.status = CAN_OPEN_MODE_SUCCESS;
      mode_data.mode = chd->openMode;
    break;
    default:
      mode_data.status = CAN_OPEN_MODE_NOT_IMPLEMENTED;
    break;
  }

  ArgPtrOut(sizeof(KCAN_IOCTL_OPEN_MODE_T));
  copy_to_user_ret((KCAN_IOCTL_OPEN_MODE_T *)arg, &mode_data,
  sizeof(KCAN_IOCTL_OPEN_MODE_T), -EFAULT);

  return 0;
}

static inline int vcan_ioctl_req_bus_stats(VCanChanData *chd,
                                           VCanHWInterface *hwIf,
                                           int *vStat)
{
  if (hwIf->reqBusStats) {
    *vStat = hwIf->reqBusStats(chd);
  } else {
    __u32 curt, difft, btime;
    uint64_t ttime;

    vCanTime(chd->vCard, &ttime);
    curt = (__u32) ttime;
    difft = curt - chd->busStats.timestamp;
    btime = chd->busStats.bitTime100ns;
    if (!btime) {
      VCanBusParams busParams;

      *vStat = VCAN_STAT_FAIL;
      if (hwIf->getBusParams) {
        *vStat = hwIf->getBusParams(chd, &busParams);
      }
      if (*vStat != VCAN_STAT_OK) {
        return 0;
      }
      btime = 10000000 / busParams.freq;
      chd->busStats.bitTime100ns = btime;
    } else {
      *vStat = VCAN_STAT_OK;
    }
    if (difft != 0) {
      chd->busStats.busLoad = (chd->busStats.bitCount * btime) / difft * 100;
    } else {
      chd->busStats.busLoad = 0;
    }
    if (chd->busStats.busLoad > 10000) {
      chd->busStats.busLoad = 10000;
    }
    chd->busStats.bitCount = 0;
    chd->busStats.timestamp = curt;
  }

  return 0;
}

static inline int vcan_ioctl_get_card_info(VCanChanData *chd,
                                           VCanHWInterface *hwIf,
                                           int *vStat,
                                           VCAN_IOCTL_CARD_INFO *arg)
{
  VCAN_IOCTL_CARD_INFO ci;

  if (hwIf->getCardInfo == NULL) {
    return -EINVAL;
  }
  memset(&ci, 0, sizeof(ci));
  *vStat = hwIf->getCardInfo(chd->vCard, &ci);
  if (*vStat == VCAN_STAT_OK) {
    ArgPtrOut(sizeof ci);
    copy_to_user_ret((void *)arg, &ci, sizeof ci, -EFAULT);
  }

  DEBUGPRINT(2, (TXT("VCAN_IOCTL_GET_CARD_INFO (VCanOsIf.c):\n")));
  DEBUGPRINT(2, (TXT("  card_name: %s\n"), ci.card_name));
  DEBUGPRINT(2, (TXT("  driver_name: %s\n"), ci.driver_name));
  DEBUGPRINT(2, (TXT("  hardware_type: %d\n"), ci.hardware_type));
  DEBUGPRINT(2, (TXT("*  channel_count: %d\n"), ci.channel_count));
  DEBUGPRINT(2, (TXT("  driver_version: v%d.%d.%d\n"),
                 ci.driver_version_major,
                 ci.driver_version_minor,
                 ci.driver_version_build));
  DEBUGPRINT(2, (TXT("  firmware_version: v%d.%d.%d\n"),
                 ci.firmware_version_major,
                 ci.firmware_version_minor,
                 ci.firmware_version_build));
  DEBUGPRINT(2, (TXT("*  hardware_rev: v%d.%d\n"),
                 ci.hardware_rev_major,
                 ci.hardware_rev_minor));
  DEBUGPRINT(2, (TXT("  license_mask: 0x%x %x\n"), ci.license_mask1, ci.license_mask2));
  DEBUGPRINT(2, (TXT("  card_number: %d\n"), ci.card_number));
  DEBUGPRINT(2, (TXT("*  serial_number: %d\n"), ci.serial_number));
  DEBUGPRINT(2, (TXT("  timer_rate: %d\n"), ci.timer_rate));
  DEBUGPRINT(2, (TXT("  vendor_name: %s\n"), ci.vendor_name));
  DEBUGPRINT(2, (TXT("  channel_prefix: %s\n"), ci.channel_prefix));
  DEBUGPRINT(2, (TXT("  product_version: v%d.%d.%d\n"),
                 ci.product_version_major,
                 ci.product_version_minor,
                 ci.product_version_minor_letter));
  DEBUGPRINT(2, (TXT("*  max_bitrate: %lu\n"), ci.max_bitrate));

  return 0;
}

static inline int kcan_ioctl_get_card_info_2(VCanChanData *chd,
                                             VCanHWInterface *hwIf,
                                             int *vStat,
                                             KCAN_IOCTL_CARD_INFO_2 *arg)
{
  KCAN_IOCTL_CARD_INFO_2 ci;

  if (hwIf->getCardInfo2 == NULL) {
    return -EINVAL;
  }
  memset(&ci, 0, sizeof ci);
  *vStat = hwIf->getCardInfo2(chd->vCard, &ci);
  if (*vStat == VCAN_STAT_OK) {
    ci.softsync_running = chd->vCard->softsync_running;
    if (ci.softsync_running) {
      ci.softsync_instab = softSyncInstab(chd->vCard);
      softSyncInstabMaxMin(chd->vCard, &ci.softsync_instab_max,
                           &ci.softsync_instab_min);
    }
    ArgPtrOut(sizeof ci);
    copy_to_user_ret(arg, &ci, sizeof ci, -EFAULT);
  }

  return 0;
}

static inline int kcan_ioctl_get_cust_channel_name(VCanChanData *chd,
                                                   VCanHWInterface *hwIf,
                                                   int *vStat,
                                                   KCAN_IOCTL_GET_CUST_CHANNEL_NAME_T *arg)
{
  KCAN_IOCTL_GET_CUST_CHANNEL_NAME_T custChannelName;

  if (hwIf->getCustChannelName == NULL) {
    return -EINVAL;
  }
  memset(&custChannelName, 0, sizeof custChannelName);
  *vStat = hwIf->getCustChannelName(chd, custChannelName.data,
                                   sizeof(custChannelName.data),
                                   &custChannelName.status);

  copy_to_user_ret(arg, &custChannelName,
                   sizeof(KCAN_IOCTL_GET_CUST_CHANNEL_NAME_T), -EFAULT);

  return 0;
}

static inline int kcan_ioctl_get_card_info_misc(VCanChanData *chd,
                                                VCanHWInterface *hwIf,
                                                int *vStat,
                                                KCAN_IOCTL_MISC_INFO *arg)
{
  KCAN_IOCTL_MISC_INFO cardInfoMisc;

  if (hwIf->getCardInfoMisc == NULL) {
    return -ENOSYS;
  }
  copy_from_user_ret(&cardInfoMisc, arg, sizeof(cardInfoMisc), -EFAULT);
  *vStat = hwIf->getCardInfoMisc(chd, &cardInfoMisc);
  copy_to_user_ret(arg, &cardInfoMisc, sizeof(cardInfoMisc), -EFAULT);

  return 0;
}

static int ioctl_blocking (VCanOpenFileNode *fileNodePtr,
                           unsigned int      ioctl_cmd,
                           unsigned long     arg)
{
  VCanChanData    *chd;
  int             vStat = VCAN_STAT_OK;
  int             tmp;
  VCanHWInterface *hwIf;
  int             ret = 0;

  chd  = fileNodePtr->chanData;
  hwIf = chd->vCard->driverData->hwIf;

  switch (ioctl_cmd) {
  case VCAN_IOC_OPEN_CHAN:
    ret = vcan_ioctl_open_chan(chd, fileNodePtr, (VCanOpen *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_OPEN_EXCL:
    ret = vcan_ioctl_open_excl(chd, fileNodePtr, (VCanOpen *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_OPEN_INIT_ACCESS:
    ret = vcan_ioctl_open_init_access(chd, fileNodePtr, (VCanInitAccess *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_BUS_ON:
    ret = vcan_ioctl_bus_on(chd, hwIf, fileNodePtr, &vStat,
                            (VCanSetBusOn *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_BUS_OFF:
    ret = vcan_ioctl_bus_off(chd, hwIf, fileNodePtr, &vStat);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_SET_BITRATE:
    ret = vcan_ioctl_set_bitrate(chd, hwIf, fileNodePtr, &vStat,
                                 (VCanSetBusParams *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_SET_BITRATE_TQ:
    ret = vcan_ioctl_set_bitrate_tq(chd, hwIf, fileNodePtr, &vStat,
                                    (VCanBusParamsTq *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_BITRATE_TQ:
    ret = vcan_ioctl_get_bitrate_tq(chd, hwIf, &vStat, (VCanBusParamsTq *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_BITRATE:
    ret = vcan_ioctl_get_bitrate(chd, hwIf, &vStat, (VCanBusParams *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_SCRIPT_ENVVAR_CONTROL:
    ret = kcan_ioctl_script_envvar_control(chd, hwIf, &vStat,
                                           (KCAN_IOCTL_ENVVAR_GET_INFO_T *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_SCRIPT_SET_ENVVAR:
    ret = kcan_ioctl_script_set_envvar(chd, hwIf, &vStat,
                                       (KCAN_IOCTL_SCRIPT_SET_ENVVAR_T *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_SCRIPT_GET_ENVVAR:
    ret = kcan_ioctl_script_get_envvar(chd, hwIf, &vStat,
                                       (KCAN_IOCTL_SCRIPT_GET_ENVVAR_T *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_DEVICE_MESSAGES_SUBSCRIPTION:
    ret = kcan_ioctl_device_messages_subscription(chd, hwIf, fileNodePtr,
                                                  &vStat,
                                                  (KCAN_IOCTL_DEVICE_MESSAGES_SUBSCRIPTION_T *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_FLASH_LEDS:
    ret = vcan_ioctl_flash_leds(chd, hwIf, &vStat,
                                (KCAN_IOCTL_LED_ACTION_I *)arg);
    break;
  //------------------------------------------------------------------
  case KCANY_IOCTL_MEMO_DISK_IO_FAST:
    ret = vcan_ioctl_memo_disk_io_fast(chd, hwIf, &vStat,
                                       (KCANY_MEMO_DISK_IO_FAST *)arg);
    break;
  //------------------------------------------------------------------
  case KCANY_IOCTL_MEMO_CONFIG_MODE:
    ret = vcan_ioctl_memo_config_mode(chd, hwIf, &vStat,
                                      (KCANY_CONFIG_MODE *)arg);
    break;
  //------------------------------------------------------------------
  case KCANY_IOCTL_MEMO_DISK_IO:
    ret = vcan_ioctl_memo_disk_io(chd, hwIf, &vStat, (KCANY_MEMO_DISK_IO *)arg);
    break;
  //------------------------------------------------------------------
  case KCANY_IOCTL_MEMO_GET_DATA:
    ret = vcan_ioctl_memo_get_data(chd, hwIf, &vStat, (KCANY_MEMO_INFO *)arg);
    break;
  //------------------------------------------------------------------
  case KCANY_IOCTL_MEMO_PUT_DATA:
    ret = vcan_ioctl_memo_put_data(chd, hwIf, &vStat, (KCANY_MEMO_INFO *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_SET_OUTPUT_MODE:
    ret = vcan_ioctl_set_output_mode(chd, hwIf, fileNodePtr, &vStat,
                                     (VCanSetBusOutputControl *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_OUTPUT_MODE:
    ret = vcan_ioctl_get_output_mode(chd, hwIf, &vStat, (int *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_SET_DEVICE_MODE:
    ret = vcan_ioctl_set_device_mode(chd, hwIf, &vStat, (uint32_t *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_DEVICE_MODE:
    ret = vcan_ioctl_get_device_mode(chd, hwIf, &vStat, (uint32_t *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_FILE_GET_COUNT:
    ret = vcan_ioctl_file_get_count(chd, hwIf, &vStat, (int *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_FILE_GET_NAME:
    ret = vcan_ioctl_file_get_name(chd, hwIf, &vStat,
                                   (VCAN_FILE_GET_NAME_T *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_SCRIPT_CONTROL:
    ret = kcan_ioctl_script_control(chd, hwIf, &vStat,
                                    (KCAN_IOCTL_SCRIPT_CONTROL_T *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_SET_TRANSID:
    ArgPtrIn(sizeof(unsigned char));
    get_user(fileNodePtr->transId, (unsigned char *)arg);
    fileNodePtr->notify = 1;
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_TRANSID:
    ArgPtrOut(sizeof(unsigned char));
    put_user_ret(fileNodePtr->transId, (unsigned char *)arg, -EFAULT);
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
  case VCAN_IOC_GET_CARD_NUMBER:
    ArgPtrOut(sizeof(uint32_t));
    put_user_ret(chd->vCard->cardNumber, (uint32_t *)arg, -EFAULT);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_BUS_PARAM_LIMITS:
    ret = vcan_ioctl_get_bus_param_limits(chd, (VCanBusParamLimits *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_CLOCK_INFO:
    ret = vcan_ioctl_get_clock_info(chd, hwIf, &vStat, (VCanClockInfo *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_DRIVER_NAME:
    ArgPtrOut(MAX_IOCTL_DRIVER_NAME + 1);
    copy_to_user_ret((char *)arg, chd->vCard->driverData->deviceName,
                     MAX_IOCTL_DRIVER_NAME + 1, -EFAULT);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_WAIT_EMPTY:
    ret = vcan_ioctl_wait_empty(fileNodePtr, (unsigned long *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_NRCHANNELS:
    ArgPtrOut(sizeof(int));
    put_user_ret(chd->vCard->nrChannels, (int *)arg, -EFAULT);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_SERIAL:
    ArgPtrOut(8);
    put_user_ret(chd->vCard->serialNumber, (int *)arg, -EFAULT);
    put_user_ret(0, ((int *)arg) + 1, -EFAULT);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_FIRMWARE_REV:
    ArgPtrOut(8);
    tmp = (chd->vCard->firmwareVersionMajor << 16) |
           chd->vCard->firmwareVersionMinor;
    put_user_ret(tmp, ((int *)arg) + 1, -EFAULT);
    put_user_ret(chd->vCard->firmwareVersionBuild, (int *)arg, -EFAULT);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_EAN:
    ArgPtrOut(8);
    put_user_ret(((int *)chd->vCard->ean)[0], (int *)arg, -EFAULT);
    put_user_ret(((int *)chd->vCard->ean)[1], ((int *)arg) + 1, -EFAULT);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_HARDWARE_REV:
    ArgPtrOut(8);
    tmp = (chd->vCard->hwRevisionMajor << 16) | chd->vCard->hwRevisionMinor;
    put_user_ret(tmp, (int *)arg, -EFAULT);
    put_user_ret(0, ((int *)arg) + 1, -EFAULT);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_CARD_TYPE:
    ArgPtrOut(sizeof(int));
    put_user_ret(chd->vCard->hw_type, (int *)arg, -EFAULT);
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
  //------------------------------------------------------------------
  case VCAN_IOC_GET_CHAN_CAP_EX:
    ArgPtrOut(sizeof(int));
    put_user_ret(chd->capabilities_ex, (uint64_t *)arg, -EFAULT);
    put_user_ret(chd->capabilities_ex_mask, ((uint64_t *)arg) +1, -EFAULT);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_LIN_MODE:
    ret = kcan_ioctl_lin_mode(chd, (int *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_CHANNEL_INFO:
    ret = vcan_ioctl_get_channel_info(chd, (int *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_MAX_BITRATE:
    ArgPtrOut(sizeof(uint32_t));
    put_user_ret(chd->vCard->current_max_bitrate, (uint32_t *)arg, -EFAULT);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_FLUSH_RCVBUFFER:
    ret = vcan_ioctl_flush_rcvbuffer(fileNodePtr);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_FLUSH_SENDBUFFER:
    vStat = hwIf->flushSendBuffer(chd);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_SET_WRITE_TIMEOUT:
    ArgIntIn;
    fileNodePtr->writeTimeout = arg;
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_READ_TIMER:
    ret = vcan_ioctl_read_timer(chd, hwIf, fileNodePtr, &vStat,
                                (uint64_t *)arg);
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
    ArgPtrOut(sizeof(VCanOverrun));
    copy_to_user_ret((VCanOverrun *)arg, &fileNodePtr->overrun,
                     sizeof(VCanOverrun), -EFAULT);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_RESET_OVERRUN_COUNT:
    fileNodePtr->overrun.sw = 0;
    fileNodePtr->overrun.hw = 0;
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_RX_QUEUE_LEVEL:
    ret = vcan_ioctl_get_rx_queue_level(fileNodePtr, (int *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_TX_QUEUE_LEVEL:
    ArgPtrOut(sizeof(int));
    put_user_ret(queue_length(&chd->txChanQueue) + hwIf->txQLen(chd),
                 (int *)arg, -EFAULT);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_CHIP_STATE:
    ArgPtrOut(sizeof(VCanRequestChipStatus));
    hwIf->requestChipState(chd);
    copy_to_user_ret((VCanRequestChipStatus *)arg, &fileNodePtr->chip_status,
                     sizeof(VCanRequestChipStatus), -EFAULT);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_TXACK:
    ArgPtrOut(sizeof(int));
    DEBUGPRINT(3, (TXT("KVASER VCAN_IOC_GET_TXACK, was %d\n"),
                   fileNodePtr->modeTx));
    put_user_ret(fileNodePtr->modeTx, (int *)arg, -EFAULT);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_SET_TXACK:
    ret = vcan_ioctl_set_txack(fileNodePtr, (int *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_SET_LOCAL_TXACK:
    ret = vcan_ioctl_set_local_txack(fileNodePtr, (uint32_t *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_SET_TXRQ:
    ret = vcan_ioctl_set_txrq(fileNodePtr, (int *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_SET_TXECHO:
    ret = vcan_ioctl_set_txecho(fileNodePtr, (unsigned char *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_OBJBUF_FREE_ALL:
    ret = kcan_ioctl_objbuf_free_all(chd, hwIf, fileNodePtr, &vStat);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_TRANSCEIVER_INFO:
    ret = vcan_ioctl_get_transceiver_info(chd, hwIf, &vStat, (uint32_t *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_TX_INTERVAL:
    ret = kcan_ioctl_tx_interval(chd, hwIf, &vStat, (uint32_t *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_SET_BRLIMIT:
    ret = kcan_ioctl_set_brlimit(chd, (uint32_t *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_OBJBUF_ALLOCATE:
    ret = kcan_ioctl_objbuf_allocate(chd, hwIf, fileNodePtr, &vStat,
                                     (KCanObjbufAdminData *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_OBJBUF_FREE:
    ret = kcan_ioctl_objbuf_free(chd, hwIf, fileNodePtr, &vStat,
                                 (KCanObjbufAdminData *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_OBJBUF_WRITE:
    ret = kcan_ioctl_objbuf_write(chd, hwIf, fileNodePtr, &vStat,
                                  (KCanObjbufBufferData *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_OBJBUF_SET_FILTER:
    ret = kcan_ioctl_objbuf_set_filter(chd, hwIf, fileNodePtr, &vStat,
                                       (KCanObjbufAdminData *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_OBJBUF_SET_FLAGS:
    ret = kcan_ioctl_objbuf_set_flags(chd, hwIf, fileNodePtr, &vStat,
                                      (KCanObjbufAdminData *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_OBJBUF_ENABLE:
    ret = kcan_ioctl_objbuf_enable(chd, hwIf, fileNodePtr, &vStat,
                                   (KCanObjbufAdminData *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_OBJBUF_DISABLE:
    ret = kcan_ioctl_objbuf_disable(chd, hwIf, fileNodePtr, &vStat,
                                    (KCanObjbufAdminData *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_OBJBUF_SET_PERIOD:
    ret = kcan_ioctl_objbuf_set_period(chd, hwIf, fileNodePtr, &vStat,
                                       (KCanObjbufAdminData *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_OBJBUF_SET_MSG_COUNT:
    ret = kcan_ioctl_objbuf_set_msg_count(chd, hwIf, fileNodePtr, &vStat,
                                          (KCanObjbufAdminData *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_OBJBUF_SEND_BURST:
    ret = kcan_ioctl_objbuf_send_burst(chd, hwIf, fileNodePtr, &vStat,
                                       (KCanObjbufAdminData *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_READ_TREF_LIST:
    ret = kcan_ioctl_read_tref_list(chd, &vStat,
                                    (KCAN_IOCTL_READ_TREF_LIST_VALUE *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_OPEN_MODE:
    ret = kcan_ioctl_open_mode(chd, (KCAN_IOCTL_OPEN_MODE_T *)arg);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_REQ_BUS_STATS:
    ret = vcan_ioctl_req_bus_stats(chd, hwIf, &vStat);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_BUS_STATS:
    ArgPtrOut(sizeof(VCanBusStatistics));
    vStat = VCAN_STAT_OK;
    copy_to_user_ret((void *)arg, &chd->busStats, sizeof(VCanBusStatistics),
                     -EFAULT);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_RESET_CLOCK:
    if (hwIf->getTime == NULL) {
      return -EINVAL;
    }

    vStat = hwIf->getTime(chd->vCard, &fileNodePtr->time_start_10usec);
    if (vStat != VCAN_STAT_OK) {
      return -EINVAL;
    }
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_GET_CLOCK_OFFSET:
    copy_to_user_ret((uint64_t *)arg, &fileNodePtr->time_start_10usec,
                     sizeof(fileNodePtr->time_start_10usec), -EFAULT);
    break;
  //------------------------------------------------------------------
  case VCAN_IOC_SET_CLOCK_OFFSET:
    copy_from_user_ret(&fileNodePtr->time_start_10usec, (uint64_t *)arg,
                       sizeof(fileNodePtr->time_start_10usec), -EFAULT);
    break;
  //------------------------------------------------------------------
  case VCAN_IOCTL_GET_CARD_INFO:
    ret = vcan_ioctl_get_card_info(chd, hwIf, &vStat,
                                   (VCAN_IOCTL_CARD_INFO *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_GET_CARD_INFO_2:
    ret = kcan_ioctl_get_card_info_2(chd, hwIf, &vStat,
                                     (KCAN_IOCTL_CARD_INFO_2 *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_GET_CUST_CHANNEL_NAME:
    ret = kcan_ioctl_get_cust_channel_name(chd, hwIf, &vStat,
                                           (KCAN_IOCTL_GET_CUST_CHANNEL_NAME_T *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_GET_CARD_INFO_MISC:
    ret = kcan_ioctl_get_card_info_misc(chd, hwIf, &vStat,
                                        (KCAN_IOCTL_MISC_INFO *)arg);
    break;
  //------------------------------------------------------------------
  case KCAN_IOCTL_FLASH_PROG:
    {
      KCAN_FLASH_PROG flashProg;

      if (hwIf->deviceFlashProg == NULL) {
        return -ENOSYS;
      }
      copy_from_user_ret(&flashProg, (KCAN_FLASH_PROG *)arg,
                       sizeof(KCAN_FLASH_PROG), -EFAULT);

      vStat = hwIf->deviceFlashProg(chd, &flashProg);

      copy_to_user_ret((void *)arg, &flashProg, sizeof(flashProg), -EFAULT);
    }
    break;
  //------------------------------------------------------------------
  default:
    if (hwIf->special_ioctl_handler == NULL) {
      DEBUGPRINT(1, (TXT("vCanIOCtrl UNKNOWN VCAN_IOC!!!: %d\n"), ioctl_cmd));
      return -ENOSYS;
    } else {
      DEBUGPRINT(2, (TXT("vCanIOCtrl calling special_ioctl_handler with %u\n"),
                     ioctl_cmd));
      vStat = hwIf->special_ioctl_handler(fileNodePtr, ioctl_cmd, arg);
    }
  }

  if (ret)
    return ret;

  return ioctl_return_value (vStat);
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
  VCanOpenFileNode *fileNodePtr = filp->private_data;
  VCanChanData     *vChd        = fileNodePtr->chanData;
  VCanCardData     *vCard       = vChd->vCard;
  int               ret;

  //the card is removed
  if (!vCard->cardPresent) {
    return -ESHUTDOWN;
  }

  // Use semaphore to enforce mutual exclusion
  // for a specific file descriptor.

  switch (ioctl_cmd)
  {
    case VCAN_IOC_RECVMSG:
    case VCAN_IOC_RECVMSG_SYNC:
    case VCAN_IOC_RECVMSG_SPECIFIC:
    case VCAN_IOC_SENDMSG:
    case KCAN_IOCTL_SCRIPT_GET_TEXT:
      ret = ioctl_non_blocking (fileNodePtr, ioctl_cmd, arg);
      break;
    default:
      wait_for_completion(&vChd->ioctl_completion);
      ret = ioctl_blocking (fileNodePtr, ioctl_cmd, arg);
      complete(&vChd->ioctl_completion);
      break;
  }

  return ret;
}
#  endif



//======================================================================
//  Poll - File operation
//======================================================================

unsigned int vCanPoll (struct file *filp, poll_table *wait)
{
  VCanOpenFileNode *fileNodePtr = filp->private_data;
  VCanChanData     *chd;
  int               full = 0;
  unsigned int      mask = 0;
  unsigned long     rcvLock_irqFlags;

  // Use semaphore to enforce mutual exclusion
  // for a specific file descriptor.
  wait_for_completion(&fileNodePtr->ioctl_completion);

  chd = fileNodePtr->chanData;

  // Add the channel wait queues to the poll
  poll_wait(filp, queue_space_event(&chd->txChanQueue), wait);
  poll_wait(filp, &fileNodePtr->rcv->rxWaitQ, wait);

  spin_lock_irqsave(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);
  if (fileNodePtr->rcv->bufHead != fileNodePtr->rcv->bufTail) {
    // Readable
    mask |= POLLIN | POLLRDNORM;
    DEBUGPRINT(4, (TXT("vCanPoll: Channel %d readable\n"), fileNodePtr->chanNr));
  }
  spin_unlock_irqrestore(&fileNodePtr->rcv->rcvLock, rcvLock_irqFlags);

  full = txQFull(chd);

  if (!full) {
    // Writable
    mask |= POLLOUT | POLLWRNORM;
    DEBUGPRINT(4, (TXT("vCanPoll: Channel %d writable\n"), fileNodePtr->chanNr));
  }

  complete(&fileNodePtr->ioctl_completion);

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
  unsigned int  i;
  uint8_t       found = 0;
  int           inactive_index = -1;
  int           free_index = -1;
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

  // reserve cardNumber
  for (i = 0; i < vCard->driverData->maxCardnumber; i++) {
    if (is_same_card(&vCard->driverData->cardNumbers[i], vCard)) {
      found = 1;
      vCard->cardNumber = i;
      vCard->driverData->cardNumbers[i].active = 1;
      DEBUGPRINT(4, (TXT("re-use existing entry for serial number %u: cardno %u\n"), vCard->serialNumber, i));
      break;
    } else if (free_index < 0 && vCard->driverData->cardNumbers[i].serialNumber == 0) {
      free_index = i;
    } else if (inactive_index < 0 && vCard->driverData->cardNumbers[i].active == 0) {
      inactive_index = i;
    }
  }

  if (!found) {
    if (free_index > -1) {
      DEBUGPRINT(4, (TXT("allocated new entry for serial number %u: cardno %u\n"), vCard->serialNumber, free_index));
      set_cardnumberdata(free_index, vCard);
    } else if (inactive_index > -1) {
      DEBUGPRINT(4, (TXT("use inactive entry for serial number %u: cardno %u\n"), vCard->serialNumber, inactive_index));
      set_cardnumberdata(inactive_index, vCard);
    } else {
      vCard->cardNumber = vCard->driverData->maxCardnumber - 1;
      printk(KERN_INFO "%s: There is no available cardNumber entry for device with serial number %u. Highest cardNumber %u will be reused.\n",
             vCard->driverData->deviceName, vCard->serialNumber, vCard->cardNumber);
    }
  }

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
    init_waitqueue_head(&(vChd->flushQ));
    queue_init(&vChd->txChanQueue, TX_CHAN_BUF_SIZE);

    init_completion(&vChd->busOnCountCompletion);
    complete(&vChd->busOnCountCompletion);

    init_completion(&vChd->ioctl_completion);
    complete(&vChd->ioctl_completion);

    spin_lock_init(&(vChd->openLock));

    atomic_set(&vChd->chanId, 1);
    vChd->busOnCount = 0;

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
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0)
  VCanHWInterface *hwIf = PDE_DATA(inode);
#else
  VCanHWInterface *hwIf = pde_data(inode);
#endif
  return single_open(file, hwIf->procRead, NULL);
}


//======================================================================
// Module init
//======================================================================

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct file_operations kvaser_proc_fops = {
    .open = kvaser_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = seq_release,
};
#else
static const struct proc_ops kvaser_proc_fops = {
    .proc_open = kvaser_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = seq_release,
};
#endif

int vCanInit (VCanDriverData *driverData, unsigned max_channels)
{
  VCanHWInterface *hwIf;
  int result;
  dev_t devno;

  // Initialise card and data structures
  hwIf = driverData->hwIf;
  memset(driverData, 0, sizeof(VCanDriverData));

  driverData->cardNumbers = kmalloc(sizeof(VCanCardNumberData) * max_channels, GFP_KERNEL);
  if(driverData->cardNumbers==NULL) {
    DEBUGPRINT(1, ("kmalloc() of cardNumbers failed"));
    return -1;
  }
  memset(driverData->cardNumbers, 0, (sizeof(VCanCardNumberData)* max_channels));
  driverData->maxCardnumber = max_channels;

  spin_lock_init(&driverData->canCardsLock);

  result = hwIf->initAllDevices();
  if (result == -ENODEV) {
    DEBUGPRINT(1, (TXT("No Kvaser %s cards found!\n"), driverData->deviceName));
    kfree(driverData->cardNumbers);
    return -1;
  } else if (result != 0) {
    DEBUGPRINT(1, (TXT("Error (%d) initializing Kvaser %s driver!\n"), result,
                   driverData->deviceName));
    kfree(driverData->cardNumbers);
    return -1;
  }

  if (!proc_create_data(driverData->deviceName,
                              0,             // default mode
                              NULL,          // parent dir
                              &kvaser_proc_fops,
                              hwIf           // client data
                              )) {
    DEBUGPRINT(1, (TXT("Error creating proc read entry!\n")));
    kfree(driverData->cardNumbers);
    hwIf->closeAllDevices();
    return -1;
  }

  // Register driver for device
  result = alloc_chrdev_region(&devno, 0, max_channels,
                               driverData->deviceName);
  if (result < 0) {
    DEBUGPRINT(1, ("alloc_chrdev_region(%u, %s) error = %d\n",
                   max_channels, driverData->deviceName, result));
    kfree(driverData->cardNumbers);
    hwIf->closeAllDevices();
    return -1;
  }

  cdev_init(&driverData->cdev, &fops);
  driverData->hwIf = hwIf;
  driverData->cdev.owner = THIS_MODULE;

  result = cdev_add(&driverData->cdev, devno, max_channels);
  if (result < 0) {
    DEBUGPRINT(1, ("cdev_add() error = %d\n", result));
    kfree(driverData->cardNumbers);
    hwIf->closeAllDevices();
    return -1;
  }

  kv_do_gettimeofday(&driverData->startTime);

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
  kfree(driverData->cardNumbers);
}
EXPORT_SYMBOL(vCanCleanup);
//======================================================================

int init_module (void)
{
  softSyncInitialize();
  return 0;
}

void cleanup_module (void)
{
  softSyncDeinitialize();
}

#ifdef _LINUX_TIME64_H
static long calc_timeout (struct timespec64 *start, unsigned long wanted_timeout) {
#else
static long calc_timeout (struct timeval *start, unsigned long wanted_timeout) {
#endif
  long retval;

  if ((long)wanted_timeout == -1) {//wait until match
    retval = -1;
  } else {
#ifdef _LINUX_TIME64_H
    struct timespec64 dt;
#else
    struct timeval dt;
#endif
    unsigned long  dt_ms;

    dt = vCanCalc_dt (start);
#ifdef _LINUX_TIME64_H
    dt_ms = (unsigned long)dt.tv_sec * 1000 +  ((unsigned long)dt.tv_nsec + 500000) / 1000000;
#else
    dt_ms = (unsigned long)dt.tv_sec * 1000 +  ((unsigned long)dt.tv_usec + 500) / 1000;
#endif
    if (dt_ms < wanted_timeout) {
      retval = wanted_timeout - dt_ms;
    } else {
      retval = 0;
    }
  }
  return retval;
}

//looks for message with a specific id
static uint32_t read_specific (VCanOpenFileNode *fileNodePtr, VCanRead *readOpt,
                               VCAN_EVENT *msg) {
  int                index = fileNodePtr->rcv->bufTail;
  uint32_t           found = 0;
  unsigned short int flags = 0;

  do {
    if (fileNodePtr->rcv->valid[index]) {
      if ((fileNodePtr->rcv->fileRcvBuffer[index].tagData.msg.id & ~VCAN_EXT_MSG_ID) == readOpt->specific.id) {
        if (readOpt->specific.skip == READ_SPECIFIC_SKIP_MATCHING) {
          fileNodePtr->rcv->valid[index] = 0;
          if (index == fileNodePtr->rcv->bufTail) {
            vCanPopReceiveBuffer (fileNodePtr->rcv);
          }
        }
        found = 1;
        break;
      } else {//save all flags so we can set OVERRUN properly
        flags |= fileNodePtr->rcv->fileRcvBuffer[index].tagData.msg.flags;
      }
    }
    index++;
    if (index >= fileNodePtr->rcv->size) {
      index = 0;
    }
  } while (index != fileNodePtr->rcv->bufHead);

  if (found) {
    if (readOpt->specific.skip == READ_SPECIFIC_SKIP_PRECEEDING) {
      fileNodePtr->rcv->bufTail = index;
      vCanPopReceiveBuffer (fileNodePtr->rcv);
    }

    flags = flags & VCAN_MSG_FLAG_OVERRUN;
    *msg = fileNodePtr->rcv->fileRcvBuffer[index];
    msg->tagData.msg.flags |= flags;
  }
  return found;
}

static int wait_tx_queue_empty (VCanOpenFileNode *fileNodePtr, unsigned long timeout)
{
  VCanChanData    *chd      = fileNodePtr->chanData;
  VCanHWInterface *hwIf     = chd->vCard->driverData->hwIf;
  VCanCardData    *vCard    = chd->vCard;
  unsigned long    timeLeft = -1;

  set_bit(0, &chd->waitEmpty);

  timeLeft = wait_event_interruptible_timeout (chd->flushQ,
                                               (txQEmpty(chd) && hwIf->txAvailable(chd)) || !vCard->cardPresent,
                                               msecs_to_jiffies (timeout));
  clear_bit(0, &chd->waitEmpty);

  if (signal_pending(current)) {
    return -ERESTARTSYS;
  } else if (!vCard->cardPresent) {
    return -ESHUTDOWN;
  }

  if (timeLeft > 0) {
    return 0;
  } else {
    return -EAGAIN;
  }
}
