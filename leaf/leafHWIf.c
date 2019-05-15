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

// Linux Leaf driver

#include <linux/version.h>
#include <linux/usb.h>
#include <linux/types.h>
#include <linux/seq_file.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
#include <linux/sched.h>
#else
#include <linux/sched/signal.h>
#endif /* KERNEL_VERSION < 4.11.0 */

// Non system headers
#include "canlib_version.h"
#include "VCanOsIf.h"
#include "leafHWIf.h"
#include "filo_cmds.h"
#include "queue.h"
#include "debug.h"
#include "hwnames.h"
#include "vcan_ioctl.h"
#include "kcan_ioctl.h"
#include "util.h"
#include "capabilities.h"
#include "ticks.h"

// Get a minor range for your devices from the usb maintainer
// Use a unique set for each driver
#define USB_LEAF_MINOR_BASE   80

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("KVASER");
MODULE_DESCRIPTION("Leaf CAN module.");


//----------------------------------------------------------------------------
// If you do not define LEAF_DEBUG at all, all the debug code will be
// left out.  If you compile with LEAF_DEBUG=0, the debug code will
// be present but disabled -- but it can then be enabled for specific
// modules at load time with a 'pc_debug=#' option to insmod.
//
#ifdef LEAF_DEBUG
    static int pc_debug = LEAF_DEBUG;
    MODULE_PARM_DESC(pc_debug, "Leaf debug level");
    module_param(pc_debug, int, 0644);
#   define DEBUGPRINT(n, arg)     if (pc_debug >= (n)) { DEBUGOUT(n, arg); }
#else
#   define DEBUGPRINT(n, arg)     if ((n) == 1) { DEBUGOUT(n, arg); }
#endif
//----------------------------------------------------------------------------

#ifndef THIS_MODULE
#define THIS_MODULE 0
#endif

//======================================================================
// HW function pointers
//======================================================================

static int leaf_init_driver(void);
static int leaf_set_busparams(VCanChanData *vChd, VCanBusParams *par);
static int leaf_get_busparams(VCanChanData *vChd, VCanBusParams *par);
static int leaf_set_silent(VCanChanData *vChd, int silent);
static int leaf_set_trans_type(VCanChanData *vChd, int linemode, int resnet);
static int leaf_bus_on(VCanChanData *vChd);
static int leaf_bus_off(VCanChanData *vChd);
static int leaf_req_bus_stats(VCanChanData *vChd);
static int leaf_get_tx_err(VCanChanData *vChd);
static int leaf_get_rx_err(VCanChanData *vChd);
static int leaf_outstanding_sync(VCanChanData *vChan);
static int leaf_close_all(void);
static int leaf_proc_read(struct seq_file* m, void* v);
static int leaf_get_chipstate(VCanChanData *vChd);
static int leaf_get_time(VCanCardData *vCard, uint64_t *time);
static int leaf_flush_tx_buffer(VCanChanData *vChan);
static void leaf_schedule_send(VCanCardData *vCard, VCanChanData *vChan);
static unsigned long leaf_get_hw_tx_q_len(VCanChanData *vChan);
static int leaf_objbuf_exists(VCanChanData *chd, int bufType, int bufNo);
static int leaf_objbuf_free(VCanChanData *chd, int bufType, int bufNo);
static int leaf_objbuf_alloc(VCanChanData *chd, int bufType, int *bufNo);
static int leaf_objbuf_write(VCanChanData *chd, int bufType, int bufNo,
                             int id, int flags, int dlc, unsigned char *data);
static int leaf_objbuf_enable(VCanChanData *chd, int bufType, int bufNo,
                              int enable);
static int leaf_objbuf_set_filter(VCanChanData *chd, int bufType, int bufNo,
                                  int code, int mask);
static int leaf_objbuf_set_flags(VCanChanData *chd, int bufType, int bufNo,
                                 int flags);
static int leaf_objbuf_set_period(VCanChanData *chd, int bufType, int bufNo,
                                  int period);
static int leaf_objbuf_set_msg_count(VCanChanData *chd, int bufType, int bufNo,
                                     int count);
static int leaf_objbuf_send_burst(VCanChanData *chd, int bufType, int bufNo,
                                  int burstLen);
static int leaf_get_card_info(VCanCardData *vCard, VCAN_IOCTL_CARD_INFO *ci);
static int leaf_get_card_info_2(VCanCardData *vCard, KCAN_IOCTL_CARD_INFO_2 *ci);
static void leaf_softsync_onoff(VCanCardData *, int enable);

static int leaf_tx_interval  (VCanChanData *chd, unsigned int *interval);
static int leaf_capabilities (VCanCardData *vCard, uint32_t vcan_cmd);
static int leaf_get_cust_channel_name(const VCanChanData * const vChan,
                                      unsigned char * const data,
                                      const unsigned int data_size,
                                      unsigned int * const status);
static int leaf_get_card_info_misc(const VCanChanData *chd, KCAN_IOCTL_MISC_INFO *cardInfoMisc);
static int leaf_flash_leds(const VCanChanData *chd, int action, int timeout);

static VCanDriverData driverData;

static VCanHWInterface hwIf = {
  .initAllDevices     = leaf_init_driver,
  .setBusParams       = leaf_set_busparams,
  .getBusParams       = leaf_get_busparams,
  .setOutputMode      = leaf_set_silent,
  .setTranceiverMode  = leaf_set_trans_type,
  .busOn              = leaf_bus_on,
  .busOff             = leaf_bus_off,
  .reqBusStats        = leaf_req_bus_stats,
  .txAvailable        = leaf_outstanding_sync,            // This isn't really a function thats checks if tx is available!
  .procRead           = leaf_proc_read,
  .closeAllDevices    = leaf_close_all,
  .getTime            = leaf_get_time,
  .flushSendBuffer    = leaf_flush_tx_buffer,
  .getRxErr           = leaf_get_rx_err,
  .getTxErr           = leaf_get_tx_err,
  .txQLen             = leaf_get_hw_tx_q_len,
  .requestChipState   = leaf_get_chipstate,
  .requestSend        = leaf_schedule_send,
  .objbufExists       = leaf_objbuf_exists,
  .objbufFree         = leaf_objbuf_free,
  .objbufAlloc        = leaf_objbuf_alloc,
  .objbufWrite        = leaf_objbuf_write,
  .objbufEnable       = leaf_objbuf_enable,
  .objbufSetFilter    = leaf_objbuf_set_filter,
  .objbufSetFlags     = leaf_objbuf_set_flags,
  .objbufSetPeriod    = leaf_objbuf_set_period,
  .objbufSetMsgCount  = leaf_objbuf_set_msg_count,
  .objbufSendBurst    = leaf_objbuf_send_burst,
  .getCardInfo        = leaf_get_card_info,
  .getCardInfo2       = leaf_get_card_info_2,
  .tx_interval        = leaf_tx_interval,
  .getCustChannelName = leaf_get_cust_channel_name,
  .getCardInfoMisc    = leaf_get_card_info_misc,
  .flashLeds          = leaf_flash_leds,
};



//======================================================================
// Static declarations

// USB packet size
#define MAX_PACKET_OUT      3072        // To device
#define MAX_PACKET_IN       3072        // From device

static unsigned long ticks_to_10us (VCanCardData *vCard,
                                    uint64_t      ticks)
{
  LeafCardData *dev = vCard->hwCardData;
  uint64_t      timestamp  = ticks_to_64bit_ns (&vCard->ticks, ticks, (uint32_t)dev->hires_timer_fq);
  unsigned long retval;

  if (vCard->softsync_running) {
    timestamp = softSyncLoc2Glob(vCard, timestamp);
  }

  retval = div_u64 (timestamp + 4999, 10000);
  return retval;
}

#define NUMBER_OF_BITS_FROM_ACK_TO_VALID_MSG    8

//======================================================================
// Prototypes
static int    leaf_plugin(struct usb_interface *interface,
                          const struct usb_device_id *id);
static void   leaf_remove(struct usb_interface *interface);

// Interrupt handler prototype changed in 2.6.19.
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19))
static void   leaf_write_bulk_callback(struct urb *urb, struct pt_regs *regs);
 #else
static void   leaf_write_bulk_callback(struct urb *urb);
 #endif



static int    leaf_allocate(VCanCardData **vCard);
static void   leaf_deallocate(VCanCardData *vCard);

static int    leaf_start(VCanCardData *vCard);

static int    leaf_tx_available(VCanChanData *vChan);
static int    leaf_transmit(VCanCardData *vCard);
static int    leaf_send_and_wait_reply(VCanCardData *vCard, filoCmd *cmd,
                                       filoCmd *replyPtr,
                                       unsigned char cmdNr,
                                       unsigned char transId,
                                       unsigned char check_trans_id);

static int    leaf_queue_cmd(VCanCardData *vCard, filoCmd *cmd,
                             unsigned int timeout);

static void   leaf_handle_command(filoCmd *cmd, VCanCardData *vCard);
static int    leaf_get_trans_id(filoCmd *cmd);

static int    leaf_fill_usb_buffer(VCanCardData *vCard,
                                   unsigned char *buffer, int maxlen);
static void   leaf_translate_can_msg(VCanChanData *vChan,
                                     filoCmd *filo_msg, CAN_MSG *can_msg);

static void   leaf_get_card_info_dummy(VCanCardData *vCard);
//----------------------------------------------------------------------



//----------------------------------------------------------------------------
// Supported KVASER hardware
#define KVASER_VENDOR_ID                    0x0bfd
#define USB_LEAF_DEVEL_PRODUCT_ID           10 // Kvaser Leaf prototype (P010v2 and v3)
#define USB_LEAF_LITE_PRODUCT_ID            11 // Kvaser Leaf Light (P010v3)
#define USB_LEAF_PRO_PRODUCT_ID             12 // Kvaser Leaf Professional HS
#define USB_LEAF_SPRO_PRODUCT_ID            14 // Kvaser Leaf SemiPro HS
#define USB_LEAF_PRO_LS_PRODUCT_ID          15 // Kvaser Leaf Professional LS
#define USB_LEAF_PRO_SWC_PRODUCT_ID         16 // Kvaser Leaf Professional SWC
#define USB_LEAF_PRO_LIN_PRODUCT_ID         17 // Kvaser Leaf Professional LIN
#define USB_LEAF_SPRO_LS_PRODUCT_ID         18 // Kvaser Leaf SemiPro LS
#define USB_LEAF_SPRO_SWC_PRODUCT_ID        19 // Kvaser Leaf SemiPro SWC
#define USB_MEMO2_DEVEL_PRODUCT_ID          22 // Kvaser Memorator II, Prototype
#define USB_MEMO2_HSHS_PRODUCT_ID           23 // Kvaser Memorator II HS/HS
#define USB_UPRO_HSHS_PRODUCT_ID            24 // Kvaser USBcan Professional HS/HS
#define USB_LEAF_LITE_GI_PRODUCT_ID         25 // Kvaser Leaf Light GI
#define USB_LEAF_PRO_OBDII_PRODUCT_ID       26 // Kvaser Leaf Professional HS (OBD-II connector)
#define USB_MEMO2_HSLS_PRODUCT_ID           27 // Kvaser Memorator Professional HS/LS
#define USB_LEAF_LITE_CH_PRODUCT_ID         28 // Kvaser Leaf Light "China"
#define USB_BLACKBIRD_SPRO_PRODUCT_ID       29 // Kvaser BlackBird SemiPro
#define USB_MEMO_R_SPRO_PRODUCT_ID          32 // Kvaser Memorator R SemiPro
#define USB_OEM_MERCURY_PRODUCT_ID          34 // Kvaser OEM Mercury
#define USB_OEM_LEAF_PRODUCT_ID             35 // Kvaser OEM Leaf
#define USB_OEM_KEY_DRIVING_PRODUCT_ID      38 // Key Driving Interface HS
#define USB_CAN_R_PRODUCT_ID                39 // Kvaser USBcan R
#define USB_LEAF_LITE_V2_PRODUCT_ID         288 // Kvaser Leaf Light v2
#define USB_MINI_PCI_EXPRESS_HS_PRODUCT_ID  289 // Kvaser Mini PCI Express HS
#define USB_LEAF_LIGHT_HS_V2_OEM_PRODUCT_ID 290 // Kvaser Leaf Light HS v2 OEM
#define USB_USBCAN_LIGHT_2HS_PRODUCT_ID     291 // Kvaser USBcan Light 2xHS
#define USB_MINI_PCI_EXPRESS_2HS_PRODUCT_ID 292 // Kvaser Mini PCI Express 2xHS
#define USB_USBCAN_R_V2_PRODUCT_ID          294 // Kvaser USBcan R v2
#define USB_LEAF_LITE_R_V2_PRODUCT_ID       295 // Kvaser Leaf Light R v2
#define USB_OEM_ATI_LEAF_LITE_V2_PRODUCT_ID 296 // Kvaser OEM ATI Leaf Light HS v2


// Table of devices that work with this driver
static struct usb_device_id leaf_table [] = {
  { USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_DEVEL_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LITE_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_SPRO_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_LS_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_SWC_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_LIN_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_SPRO_LS_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_SPRO_SWC_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_MEMO2_DEVEL_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_MEMO2_HSHS_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_UPRO_HSHS_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LITE_GI_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_PRO_OBDII_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_MEMO2_HSLS_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LITE_CH_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_BLACKBIRD_SPRO_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_MEMO_R_SPRO_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_OEM_MERCURY_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_OEM_LEAF_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_CAN_R_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_OEM_KEY_DRIVING_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LITE_V2_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_MINI_PCI_EXPRESS_HS_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LIGHT_HS_V2_OEM_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_USBCAN_LIGHT_2HS_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_MINI_PCI_EXPRESS_2HS_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_USBCAN_R_V2_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_LEAF_LITE_R_V2_PRODUCT_ID) },
  { USB_DEVICE(KVASER_VENDOR_ID, USB_OEM_ATI_LEAF_LITE_V2_PRODUCT_ID) },
  { 0 }  // Terminating entry
};

MODULE_DEVICE_TABLE(usb, leaf_table);

//
// USB class driver info in order to get a minor number from the usb core,
// and to have the device registered with devfs and the driver core
//



// USB specific object needed to register this driver with the usb subsystem
static struct usb_driver leaf_driver = {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 15))
  .owner      =    THIS_MODULE,
#endif
  .name       =    "leaf",
  .probe      =    leaf_plugin,
  .disconnect =    leaf_remove,
  .id_table   =    leaf_table,
};

//============================================================================





//------------------------------------------------------
//
//    ---- CALLBACKS ----
//
//------------------------------------------------------

//============================================================================
//  leaf_write_bulk_callback
//
// Interrupt handler prototype changed in 2.6.19.
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19))
static void leaf_write_bulk_callback (struct urb *urb, struct pt_regs *regs)
#else
static void leaf_write_bulk_callback (struct urb *urb)
#endif
{
  VCanCardData *vCard = (VCanCardData *)urb->context;
  LeafCardData *dev   = (LeafCardData *)vCard->hwCardData;

  // sync/async unlink faults aren't errors
  if (urb->status && !(urb->status == -ENOENT ||
                       urb->status == -ECONNRESET ||
                       urb->status == -ESHUTDOWN)) {
    DEBUGPRINT(2, (TXT("%s - nonzero write bulk status received: %d\n"),
                   __FUNCTION__, urb->status));
  }

  // Notify anyone waiting that the write has finished
  complete(&dev->write_finished);
}




//----------------------------------------------------------------------------
//
//    ---- THREADS ----
//
//----------------------------------------------------------------------------


//============================================================================
//
// leaf_rx_thread
//
static int leaf_rx_thread (void *context)
{
  VCanCardData *vCard   = (VCanCardData *)context;
  LeafCardData *dev     = (LeafCardData *)vCard->hwCardData;
  int          result  = 0;
  int          usbErrorCounter;
  int          ret;
  int          len;

  if (!try_module_get(THIS_MODULE)) {
    return -ENODEV;
  }

  DEBUGPRINT(3, (TXT("rx thread started\n")));

  dev->read_urb = 0;

  usbErrorCounter = 0;

  while (vCard->cardPresent) {

    // Verify that the device wasn't unplugged

    len = 0;
    // Do a blocking bulk read to get data from the device
    // Timeout after 30 seconds
    ret = usb_bulk_msg(dev->udev,
                       usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpointAddr),
                       dev->bulk_in_buffer, dev->bulk_in_size, &len,
// Timeout changed from jiffies to milliseconds in 2.6.12.
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 12))
                       HZ * 30);
#else
                       30000);
#endif

    if (ret) {
      if (ret != -ETIMEDOUT) {
        // Save if runaway
        if (ret == -EILSEQ || ret == -ESHUTDOWN || ret == -EINVAL) {
          DEBUGPRINT(2, (TXT ("usb_bulk_msg error (%d) - Device probably ")
                         TXT2("removed, closing down\n"), ret));
          vCard->cardPresent = 0;
          result = -ENODEV;
          break;
        }

        if (usbErrorCounter > 100) {
          DEBUGPRINT(2, (TXT("rx thread Ended - error (%d)\n"), ret));

          // Since this has failed so many times, stop transfers to device
          vCard->cardPresent = 0;
          result = -ENODEV;
          break;
        }
      }
    }
    else {
      //
      // We got a bunch of bytes. Now interpret them.
      //
      unsigned char  *buffer     = (unsigned char *)dev->bulk_in_buffer;
      filoCmd        *cmd;
      int            loopCounter = 1000;
      unsigned int   count       = 0;


      while (count < len) {
        // A loop counter as a safety measure.
        if (--loopCounter == 0) {
          DEBUGPRINT(2, (TXT("ERROR leaf_rx_thread() LOOPMAX. \n")));
          break;
        }

        // A command will never straddle a bulk_in_MaxPacketSize byte boundary.
        // The firmware will place a zero in the buffer to indicate that
        // the next command will follow after the next
        // bulk_in_MaxPacketSize bytes boundary.

        cmd = (filoCmd *)&buffer[count];
        if (cmd->head.cmdLen == 0) {
          count += dev->bulk_in_MaxPacketSize;
          count &= -(dev->bulk_in_MaxPacketSize);
          continue;
        }
        else {
          count += cmd->head.cmdLen;
        }

        leaf_handle_command(cmd, vCard);
        usbErrorCounter = 0;
      }
    }
  } // while (vCard->cardPresent)



  DEBUGPRINT(3, (TXT("rx thread Ended - finalised\n")));

  module_put(THIS_MODULE);
  do_exit(result);

  return result;
} // _rx_thread



//======================================================================
// Returns whether the transmit queue on a specific channel is empty
// (This is really the same as in VCanOSIf.c, but it may not be
//  intended that this file should call there.)
//======================================================================

static int txQEmpty (VCanChanData *chd)
{
  return queue_empty(&chd->txChanQueue);
}


// This should compile to nothing on an x86,
// which is a little endian CPU.
static void le_to_cpu (filoCmd *cmd)
{
  switch (cmd->head.cmdNo) {
  case CMD_LOG_MESSAGE:
    le16_to_cpus(&cmd->logMessage.time[0]);
    le16_to_cpus(&cmd->logMessage.time[1]);
    le16_to_cpus(&cmd->logMessage.time[2]);
    le32_to_cpus(&cmd->logMessage.id);
    break;
  case CMD_LOG_TRIG:
    le16_to_cpus(&cmd->logTrig.time[0]);
    le16_to_cpus(&cmd->logTrig.time[1]);
    le16_to_cpus(&cmd->logTrig.time[2]);
    le32_to_cpus(&cmd->logTrig.preTrigger);
    le32_to_cpus(&cmd->logTrig.postTrigger);
    break;
  case CMD_LOG_RTC_TIME:
    le16_to_cpus(&cmd->logRtcTime.date);
    le16_to_cpus(&cmd->logRtcTime.time);
    le16_to_cpus(&cmd->logRtcTime.bitrate0L);
    le32_to_cpus(&cmd->logRtcTime.bitrate1L);
    le32_to_cpus(&cmd->logTrig.postTrigger);
    break;
  case CMD_RX_STD_MESSAGE:
  case CMD_RX_EXT_MESSAGE:
    le16_to_cpus(&cmd->rxCanMessage.time[0]);
    le16_to_cpus(&cmd->rxCanMessage.time[1]);
    le16_to_cpus(&cmd->rxCanMessage.time[2]);
    break;
  case CMD_TX_STD_MESSAGE:
  case CMD_TX_EXT_MESSAGE:
    // Nothing to translate.
    break;
  case CMD_TX_ACKNOWLEDGE:
    le16_to_cpus(&cmd->txAck.time[0]);
    le16_to_cpus(&cmd->txAck.time[1]);
    le16_to_cpus(&cmd->txAck.time[2]);
    break;
  case CMD_TX_REQUEST:
    le16_to_cpus(&cmd->txRequest.time[0]);
    le16_to_cpus(&cmd->txRequest.time[1]);
    le16_to_cpus(&cmd->txRequest.time[2]);
    break;
  case CMD_SET_BUSPARAMS_REQ:
    le32_to_cpus(&cmd->setBusparamsReq.bitRate);
    break;
  case CMD_GET_BUSPARAMS_REQ:
    // Nothing to translate.
    break;
  case CMD_GET_BUSPARAMS_RESP:
    le32_to_cpus(&cmd->getBusparamsResp.bitRate);
    break;
  case CMD_GET_CHIP_STATE_REQ:
    // Nothing to translate.
    break;
  case CMD_CHIP_STATE_EVENT:
    le16_to_cpus(&cmd->chipStateEvent.time[0]);
    le16_to_cpus(&cmd->chipStateEvent.time[1]);
    le16_to_cpus(&cmd->chipStateEvent.time[2]);
    break;
  case CMD_SET_DRIVERMODE_REQ:
    // Nothing to translate.
    break;
  case CMD_GET_DRIVERMODE_REQ:
    // Nothing to translate.
    break;
  case CMD_GET_DRIVERMODE_RESP:
    // Nothing to translate.
    break;
  case CMD_RESET_CHIP_REQ:
    // Nothing to translate.
    break;
  case CMD_RESET_CARD_REQ:
    // Nothing to translate.
    break;
  case CMD_START_CHIP_REQ:
    // Nothing to translate.
    break;
  case CMD_START_CHIP_RESP:
    // Nothing to translate.
    break;
  case CMD_STOP_CHIP_REQ:
    // Nothing to translate.
    break;
  case CMD_STOP_CHIP_RESP:
    // Nothing to translate.
    break;
  case CMD_READ_CLOCK_REQ:
    // Nothing to translate.
    break;
  case CMD_READ_CLOCK_RESP:
    le16_to_cpus(&cmd->readClockResp.time[0]);
    le16_to_cpus(&cmd->readClockResp.time[1]);
    le16_to_cpus(&cmd->readClockResp.time[2]);
    break;
  case CMD_SELF_TEST_REQ:
    // Nothing to translate.
    break;
  case CMD_SELF_TEST_RESP:
    le32_to_cpus(&cmd->selfTestResp.results);
    break;
  case CMD_GET_CARD_INFO_2:
    le32_to_cpus(&cmd->getCardInfo2Resp.oem_unlock_code);
    break;
  case CMD_GET_CARD_INFO_REQ:
    // Nothing to translate.
    break;
  case CMD_GET_CARD_INFO_RESP:
    le32_to_cpus(&cmd->getCardInfoResp.serialNumber);
    le32_to_cpus(&cmd->getCardInfoResp.clockResolution);
    le32_to_cpus(&cmd->getCardInfoResp.mfgDate);
    break;
  case CMD_GET_INTERFACE_INFO_REQ:
    // Nothing to translate.
    break;
  case CMD_GET_INTERFACE_INFO_RESP:
    le32_to_cpus(&cmd->getInterfaceInfoResp.channelCapabilities);
    break;
  case CMD_GET_SOFTWARE_INFO_REQ:
    // Nothing to translate.
    break;
  case CMD_GET_SOFTWARE_INFO_RESP:
    le32_to_cpus(&cmd->getSoftwareInfoResp.swOptions);
    le32_to_cpus(&cmd->getSoftwareInfoResp.firmwareVersion);
    le16_to_cpus(&cmd->getSoftwareInfoResp.maxOutstandingTx);
    break;
  case CMD_GET_BUSLOAD_REQ:
    // Nothing to translate.
    break;
  case CMD_GET_BUSLOAD_RESP:
    le16_to_cpus(&cmd->getBusLoadResp.time[0]);
    le16_to_cpus(&cmd->getBusLoadResp.time[1]);
    le16_to_cpus(&cmd->getBusLoadResp.time[2]);
    le16_to_cpus(&cmd->getBusLoadResp.sample_interval);
    le16_to_cpus(&cmd->getBusLoadResp.active_samples);
    le16_to_cpus(&cmd->getBusLoadResp.delta_t);
    break;
  case CMD_RESET_STATISTICS:
    // Nothing to translate.
    break;
  case CMD_CHECK_LICENSE_REQ:
    // Nothing to translate.
    break;
  case CMD_CHECK_LICENSE_RESP:
    le32_to_cpus(&cmd->checkLicenseResp.licenseMask);
    le32_to_cpus(&cmd->checkLicenseResp.kvaserLicenseMask);
    break;
  case CMD_ERROR_EVENT:
    le16_to_cpus(&cmd->errorEvent.time[0]);
    le16_to_cpus(&cmd->errorEvent.time[1]);
    le16_to_cpus(&cmd->errorEvent.time[2]);
    le16_to_cpus(&cmd->errorEvent.addInfo1);
    le16_to_cpus(&cmd->errorEvent.addInfo2);
    break;
  case CMD_FLUSH_QUEUE:
    // Nothing to translate.
    break;
  case CMD_RESET_ERROR_COUNTER:
    // Nothing to translate.
    break;
  case CMD_CAN_ERROR_EVENT:
    le16_to_cpus(&cmd->canErrorEvent.time[0]);
    le16_to_cpus(&cmd->canErrorEvent.time[1]);
    le16_to_cpus(&cmd->canErrorEvent.time[2]);
    break;
  case CMD_SET_TRANSCEIVER_MODE_REQ:
    // Nothing to translate.
    break;
  case CMD_INTERNAL_DUMMY:
    le16_to_cpus(&cmd->internalDummy.time);
    break;
  case CMD_SOUND:
    le16_to_cpus(&cmd->sound.freq);
    le16_to_cpus(&cmd->sound.duration);
    break;
  case CMD_SET_IO_PORTS_REQ:
    le32_to_cpus(&cmd->setIoPortsReq.portVal);
    break;
  case CMD_GET_IO_PORTS_REQ:
    // Nothing to translate.
    break;
  case CMD_GET_IO_PORTS_RESP:
    le32_to_cpus(&cmd->getIoPortsResp.portVal);
    le16_to_cpus(&cmd->getIoPortsResp.status);
    break;
  case CMD_GET_TRANSCEIVER_INFO_REQ:
    // Nothing to translate.
    break;
  case CMD_GET_TRANSCEIVER_INFO_RESP:
    le32_to_cpus(&cmd->getTransceiverInfoResp.transceiverCapabilities);
    break;
  case CMD_SET_HEARTBEAT_RATE_REQ:
    le16_to_cpus(&cmd->setHeartbeatRateReq.rate);
    break;
  case CMD_HEARTBEAT_RESP:
    le16_to_cpus(&cmd->heartbeatResp.time[0]);
    le16_to_cpus(&cmd->heartbeatResp.time[1]);
    le16_to_cpus(&cmd->heartbeatResp.time[2]);
    break;
  case CMD_SET_AUTO_TX_BUFFER:
    // Nothing to translate.
    break;
  case CMD_AUTO_TX_BUFFER_REQ:
    le32_to_cpus(&cmd->autoTxBufferReq.interval);
    break;
  case CMD_AUTO_TX_BUFFER_RESP:
    le32_to_cpus(&cmd->autoTxBufferResp.timerResolution);
    le16_to_cpus(&cmd->autoTxBufferResp.capabilities);
    break;
  case CMD_TREF_SOFNR:
    le16_to_cpus(&cmd->trefSofSeq.sofNr);
    le16_to_cpus(&cmd->trefSofSeq.time[0]);
    le16_to_cpus(&cmd->trefSofSeq.time[1]);
    le16_to_cpus(&cmd->trefSofSeq.time[2]);
    break;
  case CMD_SOFTSYNC_ONOFF:
    le16_to_cpus(&cmd->softSyncOnOff.onOff);
    break;
  case CMD_USB_THROTTLE:
    le16_to_cpus(&cmd->usbThrottle.throttle);
    break;
  default:
    DEBUGPRINT(4, (TXT("translate **** %d ****\n"), cmd->head.cmdNo));
    break;
  }
}


static void cpu_to_le (filoCmd *cmd)
{
  le_to_cpu(cmd);
}


//============================================================================
//
// leaf_handle_command
// Handle a received filoCmd.
//
static void leaf_handle_command (filoCmd *cmd, VCanCardData *vCard)
{
  LeafCardData     *dev = (LeafCardData *)vCard->hwCardData;
  struct list_head *currHead;
  struct list_head *tmpHead;
  WaitNode         *currNode;
  VCAN_EVENT       e;
  unsigned long    irqFlags;

  le_to_cpu(cmd);

  DEBUGPRINT(2, (TXT("*** leaf_handle_command %d\n"), cmd->head.cmdNo));
  switch (cmd->head.cmdNo) {

    case CMD_RX_STD_MESSAGE:
    case CMD_RX_EXT_MESSAGE:
    {
      char          dlc;
      unsigned char flags;
      unsigned int  chan = cmd->rxCanMessage.channel;

      DEBUGPRINT(4, (TXT("CMD_RX_XXX_MESSAGE\n")));

      if (chan < (unsigned)vCard->nrChannels) {
        VCanChanData *vChan = vCard->chanData[cmd->rxCanMessage.channel];

        e.tag               = V_RECEIVE_MSG;
        e.transId           = 0;
        e.timeStamp = ticks_to_10us (vCard, *(uint64_t*)cmd->rxCanMessage.time) +
                                   ((LeafChanData *)vChan->hwChanData)->
                                     timestamp_correction_value;

        e.tagData.msg.id     = cmd->rxCanMessage.rawMessage[0] & 0x1F;
        e.tagData.msg.id   <<= 6;
        e.tagData.msg.id    += cmd->rxCanMessage.rawMessage[1] & 0x3F;
        if (cmd->head.cmdNo == CMD_RX_EXT_MESSAGE) {
          e.tagData.msg.id <<= 4;
          e.tagData.msg.id  += cmd->rxCanMessage.rawMessage[2] & 0x0F;
          e.tagData.msg.id <<= 8;
          e.tagData.msg.id  += cmd->rxCanMessage.rawMessage[3] & 0xFF;
          e.tagData.msg.id <<= 6;
          e.tagData.msg.id  += cmd->rxCanMessage.rawMessage[4] & 0x3F;
          e.tagData.msg.id  += EXT_MSG;
        }

        DEBUGPRINT(5, (TXT("RX:%d 0x%x\n"), chan, e.tagData.msg.id));
//        DEBUGPRINT(5, (TXT("RXMSG(%d,%x)\n"), e.timeStamp, e.tagData.msg.id));
//        DEBUGPRINT(5, (TXT("RXMSG\n")));

        flags = cmd->rxCanMessage.flags;
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

        dlc = cmd->rxCanMessage.rawMessage[5] & 0x0F;
        e.tagData.msg.dlc = dlc;

        memcpy(e.tagData.msg.data, &cmd->rxCanMessage.rawMessage[6], 8);

        DEBUGPRINT(6, (TXT (" - vCanDispatchEvent id: %d (ch:%d), ")
                       TXT2("time %lu, corr %lu\n"),
                       e.tagData.msg.id, vChan->channel, e.timeStamp,
                       ((LeafChanData *)vChan->hwChanData)->timestamp_correction_value));
        vCanDispatchEvent(vChan, &e);
      }
      break;
    }

    case CMD_GET_BUSPARAMS_RESP:
    {
      unsigned int chan = cmd->getBusparamsResp.channel;

      if (chan < (unsigned)vCard->nrChannels) {
        LeafChanData *vChan = vCard->chanData[chan]->hwChanData;

        DEBUGPRINT(4, (TXT("CMD_GET_BUSPARAMS_RESP\n")));
        dev->freq    = cmd->getBusparamsResp.bitRate;
        dev->sjw     = cmd->getBusparamsResp.sjw;
        dev->tseg1   = cmd->getBusparamsResp.tseg1;
        dev->tseg2   = cmd->getBusparamsResp.tseg2;
        dev->samples = cmd->getBusparamsResp.noSamp;

        vChan->timestamp_correction_value =
          (NUMBER_OF_BITS_FROM_ACK_TO_VALID_MSG * 16000ul) / dev->freq /
          dev->hires_timer_fq;
        DEBUGPRINT(6, (TXT ("CMD_GET_BUSPARAMS_RESP: ")
                       TXT2("timestamp_correction_value = %lu\n"),
                       vChan->timestamp_correction_value));
      }
      break;
    }

    case CMD_CHIP_STATE_EVENT:
    {
      unsigned int chan = cmd->chipStateEvent.channel;
      VCanChanData *vChd = vCard->chanData[chan];

      DEBUGPRINT(4, (TXT("CMD_CHIP_STATE_EVENT\n")));

      if (chan < (unsigned)vCard->nrChannels) {
        vChd->chipState.txerr = cmd->chipStateEvent.txErrorCounter;
        vChd->chipState.rxerr = cmd->chipStateEvent.rxErrorCounter;
        if (cmd->chipStateEvent.txErrorCounter ||
            cmd->chipStateEvent.rxErrorCounter) {
          DEBUGPRINT(6, (TXT("CMD_CHIP_STATE_EVENT, chan %d - "), chan));
          DEBUGPRINT(6, (TXT("txErr = %d/rxErr = %d\n"),
                         cmd->chipStateEvent.txErrorCounter,
                         cmd->chipStateEvent.rxErrorCounter));
        }

        // ".busStatus" is the contents of the CnSTRH register.
        switch (cmd->chipStateEvent.busStatus &
                (M16C_BUS_PASSIVE | M16C_BUS_OFF)) {
          case 0:
            vChd->chipState.state = CHIPSTAT_ERROR_ACTIVE;
            break;

          case M16C_BUS_PASSIVE:
            vChd->chipState.state = CHIPSTAT_ERROR_PASSIVE |
                                    CHIPSTAT_ERROR_WARNING;
            break;

          case M16C_BUS_OFF:
            vChd->chipState.state = CHIPSTAT_BUSOFF;
            break;

          case (M16C_BUS_PASSIVE | M16C_BUS_OFF):
            vChd->chipState.state = CHIPSTAT_BUSOFF | CHIPSTAT_ERROR_PASSIVE |
                                    CHIPSTAT_ERROR_WARNING;
            break;
        }

        // Reset is treated like bus-off
        if (cmd->chipStateEvent.busStatus & M16C_BUS_RESET) {
          vChd->chipState.state = CHIPSTAT_BUSOFF;
          vChd->chipState.txerr = 0;
          vChd->chipState.rxerr = 0;
        }

        //if (hCd->waitForChipState)
        //  wake_up(&hCd->waitResponse);

        e.tag       = V_CHIP_STATE;
        e.timeStamp = ticks_to_10us(vCard, *(uint64_t*)cmd->chipStateEvent.time);
        e.transId   = 0;
        e.tagData.chipState.busStatus      = (unsigned char)vChd->chipState.state;
        e.tagData.chipState.txErrorCounter = (unsigned char)vChd->chipState.txerr;
        e.tagData.chipState.rxErrorCounter = (unsigned char)vChd->chipState.rxerr;

        vCanDispatchEvent(vChd, &e);
      }
      break;
    }

    case CMD_GET_DRIVERMODE_RESP:
      DEBUGPRINT(4, (TXT("CMD_GET_DRIVERMODE_RESP - Ignored\n")));
      break;

    case CMD_START_CHIP_RESP:
      DEBUGPRINT(4, (TXT("CMD_START_CHIP_RESP - Ignored\n")));
      break;

    case CMD_STOP_CHIP_RESP:
      DEBUGPRINT(4, (TXT("CMD_STOP_CHIP_RESP - Ignored\n")));
      break;

    case CMD_READ_CLOCK_RESP:
      DEBUGPRINT(4, (TXT("CMD_READ_CLOCK_RESP - Ignored\n")));
      break;

    case CMD_GET_CARD_INFO_RESP:
      DEBUGPRINT(4, (TXT("CMD_GET_CARD_INFO_RESP\n")));
      vCard->nrChannels   = cmd->getCardInfoResp.channelCount;
      vCard->serialNumber = cmd->getCardInfoResp.serialNumber;
      // EAN is 8 bytes in the cmdGetCardInfoResp, but only 6 in the vCard->ean.
      // The EAN is encoded into two 32-bit integers in hex, so that
      // 0x00073301 and 0x30002425 gives the ean 0007330130002425
      memcpy(vCard->ean, &cmd->getCardInfoResp.EAN[0], 8);
      vCard->hwRevisionMajor   = cmd->getCardInfoResp.hwRevision;
      vCard->hwRevisionMinor   = 0;
      vCard->hw_type           = cmd->getCardInfoResp.hwType;
      break;

    case CMD_GET_CARD_INFO_2:
      DEBUGPRINT(4, (TXT("CMD_GET_CARD_INFO_2 - Ignored\n")));
      break;

    case CMD_GET_INTERFACE_INFO_RESP:
      DEBUGPRINT(4, (TXT("CMD_GET_INTERFACE_INFO_RESP - Ignored\n")));
      break;

    case CMD_GET_SOFTWARE_INFO_RESP:
    {
      DEBUGPRINT(4, (TXT("CMD_GET_SOFTWARE_INFO_RESP\n")));
      vCard->firmwareVersionMajor = (cmd->getSoftwareInfoResp.firmwareVersion >> 24) & 0xff;
      vCard->firmwareVersionMinor = (cmd->getSoftwareInfoResp.firmwareVersion >> 16) & 0xff;
      vCard->firmwareVersionBuild = (cmd->getSoftwareInfoResp.firmwareVersion) & 0xffff;


      dev->max_outstanding_tx = cmd->getSoftwareInfoResp.maxOutstandingTx;
      if (dev->max_outstanding_tx > DEMETER_MAX_OUTSTANDING_TX) {
        dev->max_outstanding_tx = DEMETER_MAX_OUTSTANDING_TX;
      }
      dev->max_outstanding_tx--;   // Can't use all elements!

      // Use to require firmware >= 1.1, to weed out beta versions.
      // Now changed to firmware >= 1.2, because of the USB endpoint problem
      // described in ReleaseNotes for m32firm.
      if ((vCard->firmwareVersionMajor  <  1) ||
          ((vCard->firmwareVersionMajor == 1) &&
           (vCard->firmwareVersionMinor <  2))) {

        DEBUGPRINT(2, (TXT("%s: Pls upgrade the f/w to at least 1.2\n"),
                       driverData.deviceName));
        vCard->card_flags |= DEVHND_CARD_REFUSE_TO_USE_CAN;
      }

      if (cmd->getSoftwareInfoResp.swOptions & SWOPTION_BAD_MOOD) {
        DEBUGPRINT(2, (TXT("%s: Firmware configuration error!\n"),
                       driverData.deviceName));
        vCard->card_flags |= DEVHND_CARD_REFUSE_TO_USE_CAN;
      }

      if ((cmd->getSoftwareInfoResp.swOptions & SWOPTION_CPU_FQ_MASK) ==
          SWOPTION_16_MHZ_CLK) {
        dev->hires_timer_fq = 16;
      }
      if ((cmd->getSoftwareInfoResp.swOptions & SWOPTION_CPU_FQ_MASK) ==
          SWOPTION_24_MHZ_CLK) {
        dev->hires_timer_fq = 24;
      }
      if ((cmd->getSoftwareInfoResp.swOptions & SWOPTION_CPU_FQ_MASK) ==
          SWOPTION_32_MHZ_CLK) {
        dev->hires_timer_fq = 32;
      }
      DEBUGPRINT(6, (TXT("[hires timer running at %lu MHz]\n"),
                     dev->hires_timer_fq));

      if (cmd->getSoftwareInfoResp.swOptions & SWOPTION_BETA) {
        DEBUGPRINT(6, (TXT("Beta\n")));
        vCard->card_flags |= DEVHND_CARD_FIRMWARE_BETA;
      }
      if (cmd->getSoftwareInfoResp.swOptions & SWOPTION_RC) {
        DEBUGPRINT(6, (TXT("Release Candidate\n")));
        vCard->card_flags |= DEVHND_CARD_FIRMWARE_RC;
      }
      if (cmd->getSoftwareInfoResp.swOptions & SWOPTION_AUTO_TX_BUFFER) {
        DEBUGPRINT(6, (TXT("Auto tx buffer\n")));
        vCard->card_flags |= DEVHND_CARD_AUTO_TX_OBJBUFS;
      }

      if (cmd->getSoftwareInfoResp.swOptions & SWOPTION_CAP_REQ) {
        vCard->card_flags |= DEVHND_CARD_EXTENDED_CAPABILITIES;
      }

      if (cmd->getSoftwareInfoResp.swOptions & SWOPTION_TIMEOFFSET_VALID) {
        DEBUGPRINT(6, (TXT("Time offset\n")));
        dev->time_offset_valid = 1;
      }
      break;
    }

    case CMD_AUTO_TX_BUFFER_RESP:
    {
      if (cmd->autoTxBufferResp.responseType == AUTOTXBUFFER_CMD_GET_INFO) {
        dev->autoTxBufferCount      = cmd->autoTxBufferResp.bufferCount;
        dev->autoTxBufferResolution = cmd->autoTxBufferResp.timerResolution;
        DEBUGPRINT(2, (TXT("AUTOTXBUFFER_CMD_GET_INFO: count=%d resolution=%d\n"),
                       dev->autoTxBufferCount, dev->autoTxBufferResolution));
      }
      break;
    }

    case CMD_GET_BUSLOAD_RESP:
      if (cmd->getBusLoadResp.delta_t != 0) {
        int chan = cmd->getBusLoadResp.channel;
        VCanChanData *vChd = vCard->chanData[chan];
        __u64 load = (cmd->getBusLoadResp.active_samples * cmd->getBusLoadResp.sample_interval * 10) /
                     (cmd->getBusLoadResp.delta_t);
        vChd->busStats.busLoad = (__u32) (load & 0xFFFF);
        if (vChd->busStats.busLoad > 10000) {
          vChd->busStats.busLoad = 10000;
        }
      }
      break;

    case CMD_RESET_STATISTICS:
      DEBUGPRINT(4, (TXT("CMD_RESET_STATISTICS - Ignored\n")));
      break;

    case CMD_ERROR_EVENT:
      DEBUGPRINT(4, (TXT("CMD_ERROR_EVENT - Ignored\n")));
      break;

    case CMD_RESET_ERROR_COUNTER:
      DEBUGPRINT(4, (TXT("CMD_RESET_ERROR_COUNTER - Ignored\n")));
      break;

      // Send a TxRequest back to the application. This is a
      // little-used message that means that the firmware has _started_
      // sending the message (it is submitted to the CAN controller)
    case CMD_TX_REQUEST:
    {
      unsigned int  transId;
      unsigned int  chan   = cmd->txRequest.channel;
      VCanChanData  *vChan = vCard->chanData[cmd->txRequest.channel];

      DEBUGPRINT(4, (TXT("CMD_TX_REQUEST\n")));

      if (chan < (unsigned)vCard->nrChannels) {
        // A TxReq. Take the current tx message, modify it to a
        // receive message and send it back.
        transId = cmd->txRequest.transId;
        if ((transId == 0) || (transId > dev->max_outstanding_tx)) {
          DEBUGPRINT(6, (TXT ("CMD_TX_REQUEST chan %d ")
                         TXT2("ERROR transid too high %d\n"), chan, transId));
          break;
        }

        if (((LeafChanData *)vChan->hwChanData)->current_tx_message[transId - 1].flags & VCAN_MSG_FLAG_TXRQ) {
          VCAN_EVENT *e = (VCAN_EVENT *)&((LeafChanData *)vChan->hwChanData)->current_tx_message[transId - 1];
          e->tag = V_RECEIVE_MSG;
          e->timeStamp = ticks_to_10us(vCard, *(uint64_t*)cmd->txRequest.time);
          e->tagData.msg.flags &= ~VCAN_MSG_FLAG_TXACK;
          vCanDispatchEvent(vChan, e);
        }
      }
      break;
    }

    case CMD_TX_ACKNOWLEDGE:
    {
      unsigned int  transId;

      VCanChanData  *vChan    = vCard->chanData[cmd->txAck.channel];
      LeafChanData  *leafChan = vChan->hwChanData;
      LeafCardData  *dev      = (LeafCardData *)vCard->hwCardData;

      DEBUGPRINT(4, (TXT("CMD_TX_ACKNOWLEDGE\n")));

      DEBUGPRINT(5, (TXT("TXACK:%d %d 0x%x %d\n"), cmd->txAck.channel,
                          cmd->txAck.transId,
                          ((LeafChanData *)vChan->hwChanData)->
                           current_tx_message[cmd->txAck.transId - 1].id,
                          leafChan->outstanding_tx));

      if (cmd->txAck.channel < (unsigned)vCard->nrChannels) {
        DEBUGPRINT(4, (TXT ("CMD_TX_ACKNOWLEDGE on ch %d ")
                       TXT2("(outstanding tx = %d)\n"),
                       cmd->txAck.channel, leafChan->outstanding_tx));
        transId = cmd->txAck.transId;
        if ((transId == 0) || (transId > dev->max_outstanding_tx)) {
          DEBUGPRINT(2, (TXT("CMD_TX_ACKNOWLEDGE chan %d ERROR transid %d\n"),
                         cmd->txAck.channel, transId));
          break;
        }

        if (((LeafChanData *)vChan->hwChanData)->current_tx_message[transId - 1].flags & VCAN_MSG_FLAG_TXACK) {
          VCAN_EVENT *e = (VCAN_EVENT *)&((LeafChanData *)vChan->hwChanData)->current_tx_message[transId - 1];
          e->tag = V_RECEIVE_MSG;
          e->timeStamp = ticks_to_10us(vCard, *(uint64_t*)cmd->txAck.time);

          if (dev->time_offset_valid && dev->freq >= 100000) {
            e->timeStamp -= cmd->txAck.timeOffset * (1000ul / dev->freq);
          }

          if (!(e->tagData.msg.flags & VCAN_MSG_FLAG_ERROR_FRAME)) {
            e->timeStamp += leafChan->timestamp_correction_value;
          }

          e->tagData.msg.flags &= ~VCAN_MSG_FLAG_TXRQ;
          if (cmd->txAck.flags & MSGFLAG_NERR) {
            // A lowspeed transceiver may report NERR during TX
            e->tagData.msg.flags |= VCAN_MSG_FLAG_NERR;
            DEBUGPRINT(6, (TXT("txack flag=%x\n"), cmd->txAck.flags));
          }

          vCanDispatchEvent(vChan, e);
        }

        spin_lock(&leafChan->outTxLock);
        // If we have cleared the transmit queue, we might get a TX_ACK
        // that should not be counted.
        if (leafChan->outstanding_tx) {
          leafChan->outstanding_tx--;
        }

        // Outstanding are changing from *full* to at least one open slot?
        if (leafChan->outstanding_tx >= (dev->max_outstanding_tx - 1)) {
          spin_unlock(&leafChan->outTxLock);
          DEBUGPRINT(6, (TXT("Buffer in chan %d not full (%d) anymore\n"),
                         cmd->txAck.channel, leafChan->outstanding_tx));
          queue_work(dev->txTaskQ, &dev->txWork);
        }

        // Check if we should *wake* canwritesync
        else if ((leafChan->outstanding_tx == 0) && txQEmpty(vChan) &&
                 test_and_clear_bit(0, &vChan->waitEmpty)) {
          spin_unlock(&leafChan->outTxLock);
          wake_up_interruptible(&vChan->flushQ);
          DEBUGPRINT(6, (TXT("W%d\n"), cmd->txAck.channel));
        }
        else {
#if DEBUG
          if (leafChan->outstanding_tx < 4)
            DEBUGPRINT(6, (TXT("o%d ql%d we%d s%d\n"),
                           leafChan->outstanding_tx,
                           queue_length(&vChan->txChanQueue),
                           constant_test_bit(0, &vChan->waitEmpty),
                           dev->max_outstanding_tx));
#endif
          spin_unlock(&leafChan->outTxLock);
        }

        DEBUGPRINT(6, (TXT("X%d\n"), cmd->txAck.channel));
      }
      break;
    }

    case CMD_CAN_ERROR_EVENT:
    {
      int             errorCounterChanged;
      unsigned int    chan  = cmd->canErrorEvent.channel;
      VCanChanData    *vChd = NULL;

      DEBUGPRINT(4, (TXT("CMD_CAN_ERROR_EVENT\n")));

      // Leaf firm v1.1 doesn't set canErrorEvent.channels
      if (vCard->nrChannels == 1) {
        vChd = vCard->chanData[0];
      }
      else if (chan < vCard->nrChannels) {
        vChd = vCard->chanData[chan];
      }
      else {
        // data corrupted...
        DEBUGPRINT(2, (TXT ("Illegal channel set on CMD_CAN_ERROR_EVENT. ")
                       TXT2("Msg thrown...\n")));
        break;
      }

      // It's an error frame if any of our error counters has
      // increased..
      errorCounterChanged  = (cmd->canErrorEvent.txErrorCounter >
                              vChd->chipState.txerr);
      errorCounterChanged |= (cmd->canErrorEvent.rxErrorCounter >
                              vChd->chipState.rxerr);
      // It's also an error frame if we have seen a bus error.
      errorCounterChanged |= (cmd->canErrorEvent.busStatus & M16C_BUS_ERROR);

      vChd->chipState.txerr = cmd->canErrorEvent.txErrorCounter;
      vChd->chipState.rxerr = cmd->canErrorEvent.rxErrorCounter;


      switch (cmd->canErrorEvent.busStatus & (M16C_BUS_PASSIVE | M16C_BUS_OFF)) {
        case 0:
          vChd->chipState.state = CHIPSTAT_ERROR_ACTIVE;
          break;

        case M16C_BUS_PASSIVE:
          vChd->chipState.state = CHIPSTAT_ERROR_PASSIVE |
                                  CHIPSTAT_ERROR_WARNING;
          break;

        case M16C_BUS_OFF:
          vChd->chipState.state = CHIPSTAT_BUSOFF;
          errorCounterChanged = 0;
          break;

        case (M16C_BUS_PASSIVE | M16C_BUS_OFF):
          vChd->chipState.state = CHIPSTAT_BUSOFF | CHIPSTAT_ERROR_PASSIVE |
                                  CHIPSTAT_ERROR_WARNING;
          errorCounterChanged = 0;
          break;

        default:
          break;
      }

      // Reset is treated like bus-off
      if (cmd->canErrorEvent.busStatus & M16C_BUS_RESET) {
        vChd->chipState.state = CHIPSTAT_BUSOFF;
        vChd->chipState.txerr = 0;
        vChd->chipState.rxerr = 0;
        errorCounterChanged = 0;
      }

      // Dispatch can event

      e.tag = V_CHIP_STATE;

      e.timeStamp = ticks_to_10us(vCard, *(uint64_t*)cmd->canErrorEvent.time);
      e.transId = 0;
      e.tagData.chipState.busStatus      = vChd->chipState.state;
      e.tagData.chipState.txErrorCounter = vChd->chipState.txerr;
      e.tagData.chipState.rxErrorCounter = vChd->chipState.rxerr;
      vCanDispatchEvent(vChd, &e);

      if (errorCounterChanged) {
        e.tag               = V_RECEIVE_MSG;
        e.transId           = 0;
        e.timeStamp = ticks_to_10us(vCard, *(uint64_t*)cmd->canErrorEvent.time);
        e.tagData.msg.id    = 0;
        e.tagData.msg.flags = VCAN_MSG_FLAG_ERROR_FRAME;

        if (cmd->canErrorEvent.flags & MSGFLAG_NERR) {
          // A lowspeed transceiver may report NERR during error
          // frames
          e.tagData.msg.flags |= VCAN_MSG_FLAG_NERR;
        }

        e.tagData.msg.dlc   = 0;
        vCanDispatchEvent(vChd, &e);
      }
      break;
    }


    case CMD_USB_THROTTLE:
      DEBUGPRINT(4, (TXT("CMD_USB_THROTTLE - Ignored\n")));
      break;

    case CMD_TREF_SOFNR:
      if (vCard->softsync_running) {
        if (cmd->trefSofSeq.time[0] != 0 ||
            cmd->trefSofSeq.time[1] != 0 ||
            cmd->trefSofSeq.time[2] != 0) {
          softSyncHandleTRef(vCard,
                             ticks_to_64bit_ns (&vCard->ticks, *(uint64_t*)cmd->trefSofSeq.time, (uint32_t)dev->hires_timer_fq),
                             cmd->trefSofSeq.sofNr << 3); // must shift 3 steps in order to get same id as in a mhydra-driver
        }
      }
      break;

    case CMD_LOG_MESSAGE:
    {
      unsigned int  chan  = cmd->logMessage.channel;
      VCanChanData  *vChd = NULL;

      DEBUGPRINT(4, (TXT("CMD_LOG_MESSAGE\n")));
      if (chan < vCard->nrChannels) {
        vChd = vCard->chanData[chan];

        e.tag               = V_RECEIVE_MSG;
        e.transId           = 0;
        e.timeStamp         = ticks_to_10us(vCard, *(uint64_t*)cmd->logMessage.time);
        if (dev->time_offset_valid && dev->freq >= 100000) {
          e.timeStamp -= cmd->logMessage.timeOffset * (1000ul / dev->freq);
        }

        e.timeStamp        += ((LeafChanData *)vChd->hwChanData)->timestamp_correction_value;
        e.tagData.msg.id    = cmd->logMessage.id;
        e.tagData.msg.flags = cmd->logMessage.flags;
        e.tagData.msg.dlc   = cmd->logMessage.dlc;

        memcpy(e.tagData.msg.data, cmd->logMessage.data, 8);

        vCanDispatchEvent(vChd, &e);
      }
      break;
    }

    case CMD_CHECK_LICENSE_RESP:
      DEBUGPRINT(4, (TXT("CMD_CHECK_LICENCE_RESP - Ignore\n")));
      break;

    case CMD_GET_TRANSCEIVER_INFO_RESP:
      DEBUGPRINT(4, (TXT("CMD_GET_TRANSCEIVER_INFO_RESP - Ignore\n")));
      break;

    case CMD_SELF_TEST_RESP:
      DEBUGPRINT(4, (TXT("CMD_SELF_TEST_RESP - Ignore\n")));
      break;

    case CMD_LED_ACTION_RESP:
      DEBUGPRINT(4, (TXT("CMD_LED_ACTION_RESP - Ignore\n")));
      break;

    case CMD_GET_IO_PORTS_RESP:
      DEBUGPRINT(4, (TXT("CMD_GET_IO_PORTS_RESP - Ignore\n")));
      break;

    case CMD_HEARTBEAT_RESP:
      DEBUGPRINT(4, (TXT("CMD_HEARTBEAT_RESP - Ignore\n")));
      break;

    case CMD_SOFTSYNC_ONOFF:
      switch (cmd->softSyncOnOff.onOff) {
        case SOFTSYNC_OFF:
          softSyncRemoveMember(vCard);
          break;
        case SOFTSYNC_ON:
          softSyncAddMember (vCard, (int)vCard->usb_root_hub_id);
          break;
        case SOFTSYNC_NOT_STARTED:
          break;
        }
        break;
    case CMD_READ_USER_PARAMETER:
      DEBUGPRINT(4, (TXT("CMD_READ_USER_PARAMETER - Ignore\n")));
      break;

    default:
      DEBUGPRINT(2, (TXT("UNKNOWN COMMAND - %d\n"), cmd->head.cmdNo));
  }

  //
  // Wake up those who are waiting for a resp
  //

  spin_lock_irqsave(&dev->replyWaitListLock, irqFlags);
  list_for_each_safe(currHead, tmpHead, &dev->replyWaitList)
  {
    LeafWaitNode *lwn;
    currNode = list_entry(currHead, WaitNode, list);
    lwn = currNode->driver;

    if (currNode->cmdNr == cmd->head.cmdNo &&
        ((leaf_get_trans_id(cmd) == currNode->transId) ||
         !lwn->check_trans_id)) {
      memcpy(currNode->replyPtr, cmd, cmd->head.cmdLen);
      DEBUGPRINT(4, (TXT ("Match: cN->cmdNr(%d) == cmd->cmdNo(%d) && ")
                     TXT2("_get_trans_id(%d) == cN->transId(%d)\n"),
                     currNode->cmdNr, cmd->head.cmdNo,
                     leaf_get_trans_id(cmd), currNode->transId));
      complete(&currNode->waitCompletion);
    }
  }
  spin_unlock_irqrestore(&dev->replyWaitListLock, irqFlags);
} // _handle_command


//============================================================================
// _get_trans_id
//
static int leaf_get_trans_id (filoCmd *cmd)
{
  if (cmd->head.cmdNo > CMD_TX_EXT_MESSAGE) {
    // Any of the commands
    return cmd->getBusparamsReq.transId;
  }
  else {
    DEBUGPRINT(2, (TXT("WARNING: won't give a correct transid\n")));
    return 0;
  }
} // _get_trans_id


//============================================================================
// _send
//
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20)
# define USE_CONTEXT 1
#else
# define USE_CONTEXT 0
#endif

#if USE_CONTEXT
static void leaf_send (void *context)
#else
static void leaf_send (struct work_struct *work)
#endif
{
  unsigned int     i;
#if USE_CONTEXT
  VCanCardData     *vCard     = (VCanCardData *)context;
  LeafCardData     *dev       = (LeafCardData *)vCard->hwCardData;
#else
  LeafCardData     *dev       = container_of(work, LeafCardData, txWork);
  VCanCardData     *vCard     = dev->vCard;
#endif
  VCanChanData     *vChan     = NULL;
  int              tx_needed  = 0;

  DEBUGPRINT(4, (TXT("dev = 0x%p  vCard = 0x%p\n"), dev, vCard));

  if (!vCard->cardPresent) {
    // The device was unplugged before the file was released
    // We cannot deallocate here, it is too early and handled elsewhere
    return;
  }

  wait_for_completion(&dev->write_finished);

  if (!vCard->cardPresent) {
    // The device was unplugged before the file was released
    // We cannot deallocate here it is to early and handled elsewhere
    complete(&dev->write_finished);
    return;
  }

  // Do we have any cmd to send
  DEBUGPRINT(5, (TXT("cmd queue length: %d)\n"), queue_length(&dev->txCmdQueue)));

  if (!queue_empty(&dev->txCmdQueue)) {
    tx_needed = 1;
  } else {
    // Process the channel queues (send can-messages)
    for (i = 0; i < vCard->nrChannels; i++) {
      int qLen;

      // Alternate between channels
      vChan = vCard->chanData[i];

      if (vChan->minorNr < 0) {  // Channel not initialized?
        continue;
      }

      qLen = queue_length(&vChan->txChanQueue);

      DEBUGPRINT(5, (TXT("Transmit queue%d length: %d\n"), i, qLen));
      if (qLen != 0) {
        tx_needed = 1;
        break;
      }
    }
  }

  if (tx_needed) {
    int result;

    if ((result = leaf_transmit(vCard)) <= 0) {
      // The transmission failed - mark write as finished
      complete(&dev->write_finished);
    }

    // Wake up those who are waiting to send a cmd or msg
    // It seems rather likely that we emptied all our queues, and if not,
    // the awoken threads will go back to sleep again, anyway.
    // A better solution would be to do this inside leaf_fill_usb_buffer,
    // where it is actually known what queues were touched.
    queue_wakeup_on_space(&dev->txCmdQueue);
    for (i = 0; i < vCard->nrChannels; i++) {
      vChan = vCard->chanData[i];
      if (vChan->minorNr < 0) {  // Channel not initialized?
        continue;
      }
      queue_wakeup_on_space(&vChan->txChanQueue);
    }
    if (result) {
      // Give ourselves a little extra work in case all the queues could not
      // be emptied this time.
      queue_work(dev->txTaskQ, &dev->txWork);
    }
  }
  else {
    complete(&dev->write_finished);
  }

  return;
} // _send



//============================================================================
//
// _translate_can_msg
// translate from CAN_MSG to filoCmd
//
static void leaf_translate_can_msg (VCanChanData *vChan,
                                    filoCmd *filo_msg,
                                    CAN_MSG *can_msg)
{
  uint32_t id = can_msg->id;

  // Save a copy of the message.
  ((LeafChanData *)vChan->hwChanData)->current_tx_message[atomic_read(&vChan->transId) - 1] = *can_msg;

  filo_msg->txCanMessage.cmdLen  = sizeof(cmdTxCanMessage);
  filo_msg->txCanMessage.channel = (unsigned char)vChan->channel;
  filo_msg->txCanMessage.transId = (unsigned char)atomic_read(&vChan->transId);

  DEBUGPRINT(5, (TXT("can mesg channel:%d transid %d\n"),
                 filo_msg->txCanMessage.channel,
                 filo_msg->txCanMessage.transId));

  if (can_msg->id & VCAN_EXT_MSG_ID) {
    // Extended CAN
    filo_msg->txCanMessage.cmdNo         = CMD_TX_EXT_MESSAGE;
    filo_msg->txCanMessage.rawMessage[0] = (unsigned char)((id >> 24) & 0x1F);
    filo_msg->txCanMessage.rawMessage[1] = (unsigned char)((id >> 18) & 0x3F);
    filo_msg->txCanMessage.rawMessage[2] = (unsigned char)((id >> 14) & 0x0F);
    filo_msg->txCanMessage.rawMessage[3] = (unsigned char)((id >>  6) & 0xFF);
    filo_msg->txCanMessage.rawMessage[4] = (unsigned char)((id      ) & 0x3F);
  }
  else {
    // Standard CAN
    filo_msg->txCanMessage.cmdNo         = CMD_TX_STD_MESSAGE;
    filo_msg->txCanMessage.rawMessage[0] = (unsigned char)((id >>  6) & 0x1F);
    filo_msg->txCanMessage.rawMessage[1] = (unsigned char)((id      ) & 0x3F);
  }
  filo_msg->txCanMessage.rawMessage[5]   = can_msg->length & 0x0F;
  memcpy(&filo_msg->txCanMessage.rawMessage[6], can_msg->data, 8);

  //leafChan->outstanding_tx++; // Removed because calling fkt sometimes breaks b4 actually queueing
  DEBUGPRINT(5, (TXT("outstanding(%d)++ id: %d\n"),
                 ((LeafChanData *)vChan->hwChanData)->outstanding_tx, id));
  DEBUGPRINT(5, (TXT("Trans %d, jif %ld\n"),
                 filo_msg->txCanMessage.transId, jiffies));

  filo_msg->txCanMessage.flags = can_msg->flags & (VCAN_MSG_FLAG_TX_NOTIFY   |
                                                   VCAN_MSG_FLAG_TX_START    |
                                                   VCAN_MSG_FLAG_ERROR_FRAME |
                                                   VCAN_MSG_FLAG_REMOTE_FRAME);
  /* Windows driver has a VCAN_MSG_FLAG_WAKEUP betwen the last two!! */
} // _translate_can_msg



//============================================================================
// Fill the buffer with commands from the sw-command-q (for transfer to USB)
// The firmware requires that no command straddle a
// bulk_out_MaxPacketSize byte boundary.
// This is because the bulk transfer sends bulk_out_MaxPacketSIze bytes per
// stage.
//
static int leaf_fill_usb_buffer (VCanCardData *vCard, unsigned char *buffer,
                                 int maxlen)
{
  int           cmd_bwp = 0;
  int           msg_bwp = 0;
  unsigned int  j;
  filoCmd       command;
  LeafCardData  *dev   = (LeafCardData *)vCard->hwCardData;
  VCanChanData  *vChan;
  int           len;
  int           queuePos;

  // Fill buffer with commands
  while (!queue_empty(&dev->txCmdQueue)) {
    filoCmd   *commandPtr;
    int       len;

    queuePos = queue_front(&dev->txCmdQueue);
    if (queuePos < 0) {   // Did we actually get anything from queue?
      queue_release(&dev->txCmdQueue);
      break;
    }
    commandPtr = &dev->txCmdBuffer[queuePos];
    len = commandPtr->head.cmdLen;

    DEBUGPRINT(5, (TXT("fill buf with cmd nr %d\n"), commandPtr->head.cmdNo));


    // Any space left in the usb buffer?
    if (len > (maxlen - cmd_bwp)) {
      queue_release(&dev->txCmdQueue);
      break;
    }

    // Will this command straddle a bulk_out_MaxPacketSize bytes boundry?
    if ((cmd_bwp & -(dev->bulk_out_MaxPacketSize)) !=
        ((cmd_bwp + len) & -(dev->bulk_out_MaxPacketSize))) {
      // Yes. write a zero here and move the pointer to the next
      // bulk_out_MaxPacketSize bytes boundry
      buffer[cmd_bwp] = 0;
      cmd_bwp = (cmd_bwp + (dev->bulk_out_MaxPacketSize)) &
                -(dev->bulk_out_MaxPacketSize);
      queue_release(&dev->txCmdQueue);
      continue;
    }

    cpu_to_le(commandPtr);

    memcpy(&buffer[cmd_bwp], commandPtr, len);
    cmd_bwp += len;


    queue_pop(&dev->txCmdQueue);
  } // end while

  msg_bwp = cmd_bwp;

  DEBUGPRINT(5, (TXT("bwp: (%d)\n"), msg_bwp));

  // Add the messages


  for (j = 0; j < vCard->nrChannels; j++) {

    LeafChanData *leafChan;
    vChan    = (VCanChanData *)vCard->chanData[j];
    leafChan = vChan->hwChanData;

    if (vChan->minorNr < 0) {  // Channel not initialized?
      continue;
    }

    while (!queue_empty(&vChan->txChanQueue)) {
      // Make sure we dont write more messages than
      // we are allowed to the leaf
      if (!leaf_tx_available(vChan)) {
        DEBUGPRINT(3, (TXT("channel %u: Too many outstanding packets\n"), j));
        break;
      }

      // Get and translate message
      queuePos = queue_front(&vChan->txChanQueue);
      if (queuePos < 0) {   // Did we actually get anything from queue?
        queue_release(&vChan->txChanQueue);
        break;
      }
      leaf_translate_can_msg(vChan, &command, &vChan->txChanBuffer[queuePos]);

      len = command.head.cmdLen;

      // Any space left in the usb buffer?
      if (len > (maxlen - msg_bwp)) {
        queue_release(&vChan->txChanQueue);
        break;
      }

      // Will this command straddle a bulk_out_MaxPacketSize bytes boundry?
      if ((msg_bwp & -(dev->bulk_out_MaxPacketSize)) !=
          ((msg_bwp + len) & -(dev->bulk_out_MaxPacketSize))) {
        // Yes. write a zero here and move the pointer to the next
        // bulk_out_MaxPacketSize bytes boundry

        buffer[msg_bwp] = 0;
        msg_bwp = (msg_bwp + (dev->bulk_out_MaxPacketSize)) &
                  -(dev->bulk_out_MaxPacketSize);
        queue_release(&vChan->txChanQueue);
        continue;
      }


      memcpy(&buffer[msg_bwp], &command, len);
      msg_bwp += len;
      DEBUGPRINT(5, (TXT("memcpy cmdno %d, len %d (%d)\n"),
                     command.head.cmdNo, len, msg_bwp));
      DEBUGPRINT(5, (TXT("x\n")));

      if ((atomic_read(&vChan->transId) + 1u) > dev->max_outstanding_tx) {
        atomic_set(&vChan->transId, 1);
      }
      else {
        atomic_inc(&vChan->transId);
      }

      // Have to be here (after all the breaks and continues)
      spin_lock(&leafChan->outTxLock);
      leafChan->outstanding_tx++;
      spin_unlock(&leafChan->outTxLock);

      DEBUGPRINT(5, (TXT("t leaf, chan %d, out %d\n"),
                     j, leafChan->outstanding_tx));

      queue_pop(&vChan->txChanQueue);
    } // !queue_empty(&vChan->txChanQueue)
  }

  return msg_bwp;
} // _fill_usb_buffer



//============================================================================
// The actual sending
//
static int leaf_transmit (VCanCardData *vCard /*, void *cmd*/)
{
  LeafCardData   *dev     = (LeafCardData *)vCard->hwCardData;
  int            retval   = 0;
  int            fill     = 0;

  fill = leaf_fill_usb_buffer(vCard, dev->write_urb->transfer_buffer,
                              MAX_PACKET_OUT);

  if (fill == 0) {
    // No data to send...
    DEBUGPRINT(5, (TXT("Couldn't send any messages\n")));
    return 0;
  }

  dev->write_urb->transfer_buffer_length = fill;

  if (!vCard->cardPresent) {
    // The device was unplugged before the file was released.
    // We cannot deallocate here, it shouldn't be done from here
    return VCAN_STAT_NO_DEVICE;
  }

  retval = usb_submit_urb(dev->write_urb, GFP_KERNEL);
  if (retval) {
    DEBUGPRINT(1, (TXT("%s - failed submitting write urb, error %d"),
                   __FUNCTION__, retval));
    retval = VCAN_STAT_FAIL;
  }
  else {
    // The semaphore is released on successful transmission
    retval = sizeof(filoCmd);
  }

  return retval;
} // _transmit



//============================================================================
// _get_card_info
//
static void leaf_get_card_info_dummy (VCanCardData* vCard)
{
  LeafCardData *dev = (LeafCardData *)vCard->hwCardData;
  cmdGetCardInfoReq     card_cmd;
  filoCmd               reply;
  cmdGetSoftwareInfoReq cmd;
  cmdAutoTxBufferReq    auto_cmd;

  cmd.cmdLen  = sizeof(cmdGetSoftwareInfoReq);
  cmd.cmdNo   = CMD_GET_SOFTWARE_INFO_REQ;
  cmd.transId = CMD_GET_SOFTWARE_INFO_REQ;

  leaf_send_and_wait_reply(vCard, (filoCmd *)&cmd, &reply,
                           CMD_GET_SOFTWARE_INFO_RESP, cmd.transId, 1);

  DEBUGPRINT(2, (TXT("Using fw version: %d.%d.%d, Max Tx: %d\n"),
                 vCard->firmwareVersionMajor,
                 vCard->firmwareVersionMinor,
                 vCard->firmwareVersionBuild,
                 reply.getSoftwareInfoResp.maxOutstandingTx));

  card_cmd.cmdLen  = sizeof(cmdGetCardInfoReq);
  card_cmd.cmdNo   = CMD_GET_CARD_INFO_REQ;
  card_cmd.transId = CMD_GET_CARD_INFO_REQ;

  leaf_send_and_wait_reply(vCard, (filoCmd *)&card_cmd, &reply,
                           CMD_GET_CARD_INFO_RESP, card_cmd.transId, 1);
  DEBUGPRINT(2, (TXT("channels: %d, s/n: %d, hwrev: %u\n"),
                 reply.getCardInfoResp.channelCount,
                 (int)reply.getCardInfoResp.serialNumber,
                 (unsigned int)reply.getCardInfoResp.hwRevision));

  if (vCard->card_flags & DEVHND_CARD_AUTO_TX_OBJBUFS) {
    auto_cmd.cmdLen      = sizeof(cmdAutoTxBufferReq);
    auto_cmd.cmdNo       = CMD_AUTO_TX_BUFFER_REQ;
    auto_cmd.requestType = AUTOTXBUFFER_CMD_GET_INFO;
    leaf_send_and_wait_reply(vCard, (filoCmd *)&auto_cmd, &reply,
                             CMD_AUTO_TX_BUFFER_RESP, auto_cmd.requestType, 1);
    DEBUGPRINT(2, (TXT("objbufs supported, count=%d resolution=%d\n"),
                   dev->autoTxBufferCount, dev->autoTxBufferResolution));
  }

} // _get_card_info

//============================================================================
//  leaf_send_and_wait_reply
//  Send a filoCmd and wait for the leaf to answer.
//
static int leaf_send_and_wait_reply (VCanCardData *vCard, filoCmd *cmd,
                                     filoCmd *replyPtr, unsigned char cmdNr,
                                     unsigned char transId, unsigned char check_trans_id)
{
  LeafCardData       *dev = vCard->hwCardData;

  WaitNode           waitNode;
  int                ret;
  int                timeout;
  unsigned long      irqFlags;
  LeafWaitNode       lwn;

  // Maybe return something different...
  if (vCard == NULL) {
    return VCAN_STAT_NO_DEVICE;
  }

  // See if dev is present
  if (!vCard->cardPresent) {
    return VCAN_STAT_NO_DEVICE;
  }

  init_completion(&waitNode.waitCompletion);
  waitNode.replyPtr      = replyPtr;
  waitNode.cmdNr         = cmdNr;
  waitNode.transId       = transId;
  waitNode.timedOut      = 0;

  //set driver specific data
  lwn.check_trans_id = check_trans_id;
  waitNode.driver    = &lwn;

  // Add to card's list of expected responses
  spin_lock_irqsave(&dev->replyWaitListLock, irqFlags);
  list_add(&waitNode.list, &dev->replyWaitList);
  spin_unlock_irqrestore(&dev->replyWaitListLock, irqFlags);

  ret = leaf_queue_cmd(vCard, cmd, LEAF_Q_CMD_WAIT_TIME);
  if (ret != 0) {
    spin_lock_irqsave(&dev->replyWaitListLock, irqFlags);
    list_del(&waitNode.list);
    spin_unlock_irqrestore(&dev->replyWaitListLock, irqFlags);
    return ret;
  }

  timeout = wait_for_completion_timeout(&waitNode.waitCompletion, msecs_to_jiffies(LEAF_CMD_RESP_WAIT_TIME));
  // Now we either got a response or a timeout
  spin_lock_irqsave(&dev->replyWaitListLock, irqFlags);
  list_del(&waitNode.list);
  spin_unlock_irqrestore(&dev->replyWaitListLock, irqFlags);

  if (timeout == 0) {
    DEBUGPRINT(2, (TXT("WARNING: waiting for response(%d) timed out! \n"),
                   waitNode.cmdNr));
    return VCAN_STAT_TIMEOUT;
  }

  return VCAN_STAT_OK;

} // _send_and_wait_reply



//============================================================================
//  leaf_queue_cmd
//  Put the command in the command queue
//
// The unrolled sleep is used to catch a missing position in the queue
static int leaf_queue_cmd (VCanCardData *vCard, filoCmd *cmd,
                           unsigned int timeout)
{
  filoCmd *bufCmdPtr = NULL;
  LeafCardData *dev  = (LeafCardData *)vCard->hwCardData;
  int queuePos;
  // Using an unrolled sleep
  wait_queue_entry_t wait;

  init_waitqueue_entry(&wait, current);
  queue_add_wait_for_space(&dev->txCmdQueue, &wait);

  // Sleep when buffer is full and timeout > 0
  while(1) {
    // We are indicated as interruptible
    DEBUGPRINT(5, (TXT("queue cmd len: %d\n"), queue_length(&dev->txCmdQueue)));

    set_current_state(TASK_INTERRUPTIBLE);

    queuePos = queue_back(&dev->txCmdQueue);
    if (queuePos >= 0) {   // Did we actually find space in the queue?
      break;
    }
    queue_release(&dev->txCmdQueue);

    // Do we want a timeout?
    if (timeout == 0) {
      // We shouldn't wait and thus we must be active
      set_current_state(TASK_RUNNING);
      queue_remove_wait_for_space(&dev->txCmdQueue, &wait);
      DEBUGPRINT(2, (TXT("ERROR 1 NO_RESOURCES\n")));
      return VCAN_STAT_NO_RESOURCES;
    } else {
      if (schedule_timeout(msecs_to_jiffies(timeout) + 1) == 0) {
        // Sleep was interrupted by timer
        queue_remove_wait_for_space(&dev->txCmdQueue, &wait);
        DEBUGPRINT(2, (TXT("ERROR 2 NO_RESOURCES\n")));
        return VCAN_STAT_NO_RESOURCES;
      }
    }

    // Are we interrupted by a signal?
    if (signal_pending(current)) {
      queue_remove_wait_for_space(&dev->txCmdQueue, &wait);
      DEBUGPRINT(2, (TXT("ERROR 3 SIGNALED\n")));
      return VCAN_STAT_SIGNALED;
    }
  }

  set_current_state(TASK_RUNNING);
  queue_remove_wait_for_space(&dev->txCmdQueue, &wait);

  // Get a pointer to the right bufferspace
  bufCmdPtr = (filoCmd *)&dev->txCmdBuffer[queuePos];
  memcpy(bufCmdPtr, cmd, cmd->head.cmdLen);
  queue_push(&dev->txCmdQueue);

  // Wake up the tx-thread
  queue_work(dev->txTaskQ, &dev->txWork);

  return VCAN_STAT_OK;
} // _queue_cmd


//============================================================================
//  leaf_plugin
//
//  Called by the usb core when a new device is connected that it thinks
//  this driver might be interested in.
//  Also allocates card info struct mem space and starts workqueues
//
static int leaf_plugin (struct usb_interface *interface,
                        const struct usb_device_id *id)
{
  struct usb_device               *udev = interface_to_usbdev(interface);
  struct usb_host_interface       *iface_desc;
  struct usb_endpoint_descriptor  *endpoint;
  size_t                          buffer_size;
  unsigned int                    i;
  int                             retval = -ENOMEM;
  VCanCardData                    *vCard;
  LeafCardData                    *dev;

  DEBUGPRINT(3, (TXT("leaf: _plugin\n")));

  // See if the device offered us matches what we can accept
  // Add here for more devices
  if (
      (udev->descriptor.idVendor != KVASER_VENDOR_ID) ||
      (
       (udev->descriptor.idProduct != USB_LEAF_DEVEL_PRODUCT_ID)            &&
       (udev->descriptor.idProduct != USB_LEAF_LITE_PRODUCT_ID)             &&
       (udev->descriptor.idProduct != USB_LEAF_PRO_PRODUCT_ID)              &&
       (udev->descriptor.idProduct != USB_LEAF_SPRO_PRODUCT_ID)             &&
       (udev->descriptor.idProduct != USB_LEAF_PRO_LS_PRODUCT_ID)           &&
       (udev->descriptor.idProduct != USB_LEAF_PRO_SWC_PRODUCT_ID)          &&
       (udev->descriptor.idProduct != USB_LEAF_PRO_LIN_PRODUCT_ID)          &&
       (udev->descriptor.idProduct != USB_LEAF_SPRO_LS_PRODUCT_ID)          &&
       (udev->descriptor.idProduct != USB_LEAF_SPRO_SWC_PRODUCT_ID)         &&
       (udev->descriptor.idProduct != USB_MEMO2_DEVEL_PRODUCT_ID)           &&
       (udev->descriptor.idProduct != USB_MEMO2_HSHS_PRODUCT_ID)            &&
       (udev->descriptor.idProduct != USB_UPRO_HSHS_PRODUCT_ID)             &&
       (udev->descriptor.idProduct != USB_LEAF_LITE_GI_PRODUCT_ID)          &&
       (udev->descriptor.idProduct != USB_LEAF_PRO_OBDII_PRODUCT_ID)        &&
       (udev->descriptor.idProduct != USB_MEMO2_HSLS_PRODUCT_ID)            &&
       (udev->descriptor.idProduct != USB_LEAF_LITE_CH_PRODUCT_ID)          &&
       (udev->descriptor.idProduct != USB_BLACKBIRD_SPRO_PRODUCT_ID)        &&
       (udev->descriptor.idProduct != USB_MEMO_R_SPRO_PRODUCT_ID)           &&
       (udev->descriptor.idProduct != USB_OEM_MERCURY_PRODUCT_ID)           &&
       (udev->descriptor.idProduct != USB_OEM_LEAF_PRODUCT_ID)              &&
       (udev->descriptor.idProduct != USB_OEM_KEY_DRIVING_PRODUCT_ID)       &&
       (udev->descriptor.idProduct != USB_LEAF_LITE_V2_PRODUCT_ID)          &&
       (udev->descriptor.idProduct != USB_MINI_PCI_EXPRESS_HS_PRODUCT_ID)   &&
       (udev->descriptor.idProduct != USB_LEAF_LIGHT_HS_V2_OEM_PRODUCT_ID)  &&
       (udev->descriptor.idProduct != USB_USBCAN_LIGHT_2HS_PRODUCT_ID)      &&
       (udev->descriptor.idProduct != USB_MINI_PCI_EXPRESS_2HS_PRODUCT_ID)  &&
       (udev->descriptor.idProduct != USB_CAN_R_PRODUCT_ID)                 &&
       (udev->descriptor.idProduct != USB_USBCAN_R_V2_PRODUCT_ID)           &&
       (udev->descriptor.idProduct != USB_LEAF_LITE_R_V2_PRODUCT_ID)        &&
       (udev->descriptor.idProduct != USB_OEM_ATI_LEAF_LITE_V2_PRODUCT_ID)
      )
     )
  {
    DEBUGPRINT(2, (TXT("==================\n")));
    DEBUGPRINT(2, (TXT("Vendor:  %d\n"),  udev->descriptor.idVendor));
    DEBUGPRINT(2, (TXT("Product:  %d\n"), udev->descriptor.idProduct));
    DEBUGPRINT(2, (TXT("==================\n")));
    return -ENODEV;
  }

#if DEBUG
  switch (udev->descriptor.idProduct) {
    case USB_LEAF_DEVEL_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Leaf prototype (P010v2 and v3) plugged in\n")));
      break;

    case USB_LEAF_LITE_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Leaf Light (P010v3) plugged in\n")));
      break;

    case USB_LEAF_PRO_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Leaf Professional HS plugged in\n")));
      break;

    case USB_LEAF_SPRO_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Leaf SemiPro HS plugged in\n")));
      break;

    case USB_LEAF_PRO_LS_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Leaf Professional LS plugged in\n")));
      break;

    case USB_LEAF_PRO_SWC_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Leaf Professional SWC plugged in\n")));
      break;

    case USB_LEAF_PRO_LIN_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Leaf Professional LIN plugged in\n")));
      break;

    case USB_LEAF_SPRO_LS_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Leaf SemiPro LS plugged in\n")));
      break;

    case USB_LEAF_SPRO_SWC_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Leaf SemiPro SWC plugged in\n")));
      break;

    case USB_MEMO2_DEVEL_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Memorator II, Prototype plugged in\n")));
      break;

    case USB_MEMO2_HSHS_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Memorator II HS/HS plugged in\n")));
      break;

    case USB_UPRO_HSHS_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("USBcan Professional HS/HS plugged in\n")));
      break;

    case USB_LEAF_LITE_GI_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Leaf Light GI plugged in\n")));
      break;

    case USB_LEAF_PRO_OBDII_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Leaf Professional (OBD-II connector) plugged in\n")));
      break;

    case USB_MEMO2_HSLS_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Memorator Professional HS/LS plugged in\n")));
      break;

    case USB_LEAF_LITE_CH_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Leaf Light HS China plugged in\n")));
      break;

    case USB_BLACKBIRD_SPRO_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("BlackBird SemiPro plugged in\n")));
      break;

    case USB_MEMO_R_SPRO_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Memorator R SemiPro plugged in\n")));
      break;

    case USB_OEM_MERCURY_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("OEM Mercury plugged in\n")));
      break;

    case USB_OEM_LEAF_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("OEM Leaf plugged in\n")));
      break;

    case USB_CAN_R_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("USBcan R plugged in\n")));
      break;

    case USB_OEM_KEY_DRIVING_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Key Driving Interface plugged in\n")));
      break;

    case USB_LEAF_LITE_V2_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Leaf Light v2 plugged in\n")));
      break;

    case USB_MINI_PCI_EXPRESS_HS_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Mini PCI Express HS plugged in\n")));
      break;

    case USB_LEAF_LIGHT_HS_V2_OEM_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Leaf Light HS v2 OEM plugged in\n")));
      break;

    case USB_USBCAN_LIGHT_2HS_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("USBcan Light 2xHS plugged in\n")));
      break;

    case USB_MINI_PCI_EXPRESS_2HS_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Mini PCI Express 2xHS plugged in\n")));
      break;

    case USB_USBCAN_R_V2_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("USBcan R v2 plugged in\n")));
      break;

    case USB_LEAF_LITE_R_V2_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("Leaf Light R v2 plugged in\n")));
      break;

    case USB_OEM_ATI_LEAF_LITE_V2_PRODUCT_ID:
      DEBUGPRINT(2, (TXT("\nKVASER ")));
      DEBUGPRINT(2, (TXT("OEM ATI Leaf Light HS v2 plugged in\n")));
      break;

    default:
      DEBUGPRINT(2, (TXT("UNKNOWN product plugged in\n")));
      break;
  }
#endif

  // Allocate datastructures for the card
  if (leaf_allocate(&vCard) != VCAN_STAT_OK) {
    // Allocation failed
    return -ENOMEM;
  }

  dev = vCard->hwCardData;
  dev->udev = udev;
  dev->interface = interface;
  ticks_init (&vCard->ticks);

  // Set up the endpoint information
  // Check out the endpoints
  // Use only the first bulk-in and bulk-out endpoints
  iface_desc = &interface->altsetting[0];
  for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
    endpoint = &iface_desc->endpoint[i].desc;

    if (!dev->bulk_in_endpointAddr &&
        (endpoint->bEndpointAddress & USB_DIR_IN) &&
        ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
         USB_ENDPOINT_XFER_BULK)) {
      // We found a bulk in endpoint
      buffer_size                = MAX_PACKET_IN;
      dev->bulk_in_size          = buffer_size;
      dev->bulk_in_endpointAddr  = endpoint->bEndpointAddress;
      dev->bulk_in_buffer        = kmalloc(buffer_size, GFP_KERNEL);
      dev->bulk_in_MaxPacketSize = le16_to_cpu(endpoint->wMaxPacketSize);
      DEBUGPRINT(2, (TXT("MaxPacketSize in = %d\n"),
                     dev->bulk_in_MaxPacketSize));

      DEBUGPRINT(2, (TXT("MALLOC bulk_in_buffer\n")));
      if (!dev->bulk_in_buffer) {
        DEBUGPRINT(1, (TXT("Couldn't allocate bulk_in_buffer\n")));
        goto error;
      }
      memset(dev->bulk_in_buffer, 0, buffer_size);
    }

    if (!dev->bulk_out_endpointAddr &&
        !(endpoint->bEndpointAddress & USB_DIR_IN) &&
        ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
         USB_ENDPOINT_XFER_BULK)) {
      // We found a bulk out endpoint
      // A probe() may sleep and has no restrictions on memory allocations
      dev->write_urb = usb_alloc_urb(0, GFP_KERNEL);
      if (!dev->write_urb) {
        DEBUGPRINT(1, (TXT("No free urbs available\n")));
        goto error;
      }
      dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;

      // On some platforms using this kind of buffer alloc
      // call eliminates a dma "bounce buffer".
      //
      // NOTE: you'd normally want i/o buffers that hold
      // more than one packet, so that i/o delays between
      // packets don't hurt throughput.
      //

      buffer_size                    = MAX_PACKET_OUT;
      dev->bulk_out_size             = buffer_size;
      dev->bulk_out_MaxPacketSize    = le16_to_cpu(endpoint->wMaxPacketSize);
      DEBUGPRINT(2, (TXT("MaxPacketSize out = %d\n"),
                     dev->bulk_out_MaxPacketSize));
      dev->write_urb->transfer_flags = (URB_NO_TRANSFER_DMA_MAP);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
      dev->bulk_out_buffer = usb_buffer_alloc(dev->udev,
                                              buffer_size, GFP_KERNEL,
                                              &dev->write_urb->transfer_dma);
#else
      dev->bulk_out_buffer = usb_alloc_coherent(dev->udev,
                                                buffer_size, GFP_KERNEL,
                                                &dev->write_urb->transfer_dma);
#endif
      if (!dev->bulk_out_buffer) {
        DEBUGPRINT(1, (TXT("Couldn't allocate bulk_out_buffer\n")));
        goto error;
      }
      usb_fill_bulk_urb(dev->write_urb, dev->udev,
                        usb_sndbulkpipe(dev->udev,
                                        endpoint->bEndpointAddress),
                        dev->bulk_out_buffer, buffer_size,
                        leaf_write_bulk_callback, vCard);
    }
  }
  if (!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)) {
    DEBUGPRINT(1, (TXT("Couldn't find both bulk-in and bulk-out endpoints\n")));
    goto error;
  }


  // Allow device read, write and ioctl
  vCard->cardPresent = 1;

  // We can register the device now, as it is ready
  usb_set_intfdata(interface, vCard);
  dev->vCard = vCard;

  // Set the number on the channels
  for (i = 0; i < MAX_CARD_CHANNELS; i++) {
    VCanChanData   *vChd = vCard->chanData[i];
    vChd->channel  = i;
  }


  // Start up vital stuff
  {
    int r;
    r = leaf_start(vCard);
  }

  vCard->usb_root_hub_id = get_usb_root_hub_id (udev);

  // Let the user know what node this device is now attached to
  DEBUGPRINT(2, (TXT("------------------------------\n")));
  DEBUGPRINT(2, (TXT("Leaf device %d now attached\n"),
                 driverData.noOfDevices));
  for (i = 0; i < MAX_CARD_CHANNELS; i++) {
    DEBUGPRINT(2, (TXT("With minor number %d \n"), vCard->chanData[i]->minorNr));
  }
  DEBUGPRINT(2, (TXT("using driver built %s\n"), TXT2(__TIME__)));
  DEBUGPRINT(2, (TXT("on %s\n"), TXT2(__DATE__)));
  DEBUGPRINT(2, (TXT("------------------------------\n")));

  return 0;

error:
  DEBUGPRINT(2, (TXT("_deallocate from leaf_plugin\n")));
  leaf_deallocate(vCard);

  return retval;
} // leaf_plugin



//========================================================================
//
// Init stuff, called from end of _plugin
//
static int leaf_start (VCanCardData *vCard)
{
  LeafCardData *dev = (LeafCardData *)vCard->hwCardData;
  struct task_struct* rx_thread;
  unsigned int i;

  DEBUGPRINT(3, (TXT("leaf: _start\n")));

  // Initialize queues/waitlists for commands
  spin_lock_init(&dev->replyWaitListLock);

  INIT_LIST_HEAD(&dev->replyWaitList);
  queue_init(&dev->txCmdQueue, KV_LEAF_TX_CMD_BUF_SIZE);

  // Set spinlocks for the outstanding tx
  for (i = 0; i < MAX_CARD_CHANNELS; i++) {
    VCanChanData  *vChd     = vCard->chanData[i];
    LeafChanData  *leafChan = vChd->hwChanData;
    spin_lock_init(&leafChan->outTxLock);
  }

  init_completion(&dev->write_finished);
  complete(&dev->write_finished);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20))
  INIT_WORK(&dev->txWork, leaf_send, vCard);
#else
  INIT_WORK(&dev->txWork, leaf_send);
#endif
  dev->txTaskQ = create_workqueue("leaf_tx");

  rx_thread = kthread_run(leaf_rx_thread, vCard, "Kvaser kernel thread");

  // Gather some card info
  leaf_get_card_info_dummy(vCard);

  if (vCard) {
    DEBUGPRINT(2, (TXT("vCard chnr: %d\n"), vCard->nrChannels));
  }
  else {
    DEBUGPRINT(2, (TXT("vCard is NULL\n")));
  }

  vCard->driverData = &driverData;
  vCanInitData(vCard);

  if (vCard->card_flags & DEVHND_CARD_EXTENDED_CAPABILITIES) {
    int r;
    r = leaf_capabilities (vCard, VCAN_CHANNEL_CAP_SILENTMODE);
    if (r != VCAN_STAT_OK) DEBUGPRINT(2, (TXT("Failed reading capability: VCAN_CHANNEL_CAP_SILENTMODE\n")));

    r = leaf_capabilities (vCard, VCAN_CHANNEL_CAP_SEND_ERROR_FRAMES);
    if (r != VCAN_STAT_OK) DEBUGPRINT(2, (TXT("Failed reading capability: VCAN_CHANNEL_CAP_SEND_ERROR_FRAMES\n")));

    r = leaf_capabilities (vCard, VCAN_CHANNEL_CAP_BUSLOAD_CALCULATION);
    if (r != VCAN_STAT_OK) DEBUGPRINT(2, (TXT("Failed reading capability: VCAN_CHANNEL_CAP_BUSLOAD_CALCULATION\n")));

    r = leaf_capabilities (vCard, VCAN_CHANNEL_CAP_ERROR_COUNTERS);
    if (r != VCAN_STAT_OK) DEBUGPRINT(2, (TXT("Failed reading capability: VCAN_CHANNEL_CAP_ERROR_COUNTERS\n")));

    r = leaf_capabilities (vCard, VCAN_CHANNEL_CAP_SYNC_TX_FLUSH);
    if (r != VCAN_STAT_OK) DEBUGPRINT(2, (TXT("Failed reading capability: VCAN_CHANNEL_CAP_SYNC_TX_FLUSH\n")));
  }

  set_capability_value (vCard,
                        VCAN_CHANNEL_CAP_EXTENDED_CAN  |
                        VCAN_CHANNEL_CAP_TXREQUEST     |
                        VCAN_CHANNEL_CAP_TXACKNOWLEDGE,
                        0xFFFFFFFF,
                        0xFFFFFFFF,
                        MAX_CARD_CHANNELS);

  // Set all channels in normal mode.
  for (i = 0; i < vCard->nrChannels; i++) {
    int r;
    filoCmd cmd;

    cmd.setDrivermodeReq.cmdNo      = CMD_SET_DRIVERMODE_REQ;
    cmd.setDrivermodeReq.cmdLen     = sizeof(cmdSetDrivermodeReq);
    cmd.setDrivermodeReq.channel    = (unsigned char)i;
    cmd.setDrivermodeReq.driverMode = DRIVERMODE_NORMAL;

    r = leaf_queue_cmd(vCard, &cmd, 50);
    if (r != VCAN_STAT_OK) return r;
  }

  vCard->enable_softsync = 1;
  leaf_softsync_onoff(vCard, vCard->enable_softsync);

  return VCAN_STAT_OK;
} // _start


//========================================================================
//
// Allocates space for card structs
//
static int leaf_allocate (VCanCardData **in_vCard)
{
  // Helper struct for allocation
  typedef struct {
    VCanChanData  *dataPtrArray[MAX_CARD_CHANNELS];
    VCanChanData  vChd[MAX_CARD_CHANNELS];
    LeafChanData  hChd[MAX_CARD_CHANNELS];
  } ChanHelperStruct;

  int              chNr;
  ChanHelperStruct *chs;
  VCanCardData     *vCard;

  DEBUGPRINT(3, (TXT("leaf: _allocate\n")));

  // Allocate data area for this card
  vCard = kmalloc(sizeof(VCanCardData) + sizeof(LeafCardData), GFP_KERNEL);
  DEBUGPRINT(2, (TXT("MALLOC _allocate\n")));
  if (!vCard) {
    DEBUGPRINT(1, (TXT("alloc error\n")));
    goto card_alloc_err;
  }
  memset(vCard, 0, sizeof(VCanCardData) + sizeof(LeafCardData));

  // hwCardData is directly after VCanCardData
  vCard->hwCardData = vCard + 1;

  // Allocate memory for n channels
  chs = kmalloc(sizeof(ChanHelperStruct), GFP_KERNEL);
  DEBUGPRINT(2, (TXT("MALLOC _alloc helperstruct\n")));
  if (!chs) {
    DEBUGPRINT(1, (TXT("chan alloc error\n")));
    goto chan_alloc_err;
  }
  memset(chs, 0, sizeof(ChanHelperStruct));


  // Init array and hwChanData
  for (chNr = 0; chNr < MAX_CARD_CHANNELS; chNr++) {
    chs->dataPtrArray[chNr]    = &chs->vChd[chNr];
    chs->vChd[chNr].hwChanData = &chs->hChd[chNr];
    chs->vChd[chNr].minorNr    = -1;   // No preset minor number
  }
  vCard->chanData = chs->dataPtrArray;

  spin_lock(&driverData.canCardsLock);
  // Insert into list of cards
  vCard->next = driverData.canCards;
  driverData.canCards = vCard;
  spin_unlock(&driverData.canCardsLock);

  *in_vCard = vCard;

  return VCAN_STAT_OK;

chan_alloc_err:
  kfree(vCard);

card_alloc_err:

  return VCAN_STAT_NO_MEMORY;
} // _allocate


//============================================================================
// leaf_deallocate
//
static void leaf_deallocate (VCanCardData *vCard)
{
  LeafCardData *dev = (LeafCardData *)vCard->hwCardData;
  VCanCardData *local_vCard;
  int i;

  DEBUGPRINT(3, (TXT("leaf: _deallocate\n")));

  // Make sure all workqueues are finished
  //flush_workqueue(&dev->txTaskQ);

  if (dev->bulk_in_buffer != NULL) {
    kfree(dev->bulk_in_buffer);
    DEBUGPRINT(2, (TXT("Free bulk_in_buffer\n")));
    dev->bulk_in_buffer = NULL;
  }
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
  usb_buffer_free(dev->udev, dev->bulk_out_size,
                  dev->bulk_out_buffer,
                  dev->write_urb->transfer_dma);
#else
  usb_free_coherent(dev->udev, dev->bulk_out_size,
                    dev->bulk_out_buffer,
                    dev->write_urb->transfer_dma);
#endif
  usb_free_urb(dev->write_urb);

  spin_lock(&driverData.canCardsLock);

  local_vCard = driverData.canCards;

  // Identify the card to remove in the global list

  if (local_vCard == vCard) {
    // The first entry is the one to remove
    driverData.canCards = local_vCard->next;
  }
  else {
    while (local_vCard) {
      if (local_vCard->next == vCard) {
        // We have found it!
        local_vCard->next = vCard->next;
        break;
      }

      local_vCard = local_vCard->next;
    }

    // If vCard isn't found in the list we ignore the removal from the list
    // but we still deallocate vCard - fall through
    if (!local_vCard) {
      DEBUGPRINT(1, (TXT("Error: Bad vCard in leaf_dealloc()\n")));
    }
  }

  spin_unlock(&driverData.canCardsLock);

  for(i = 0; i < MAX_CARD_CHANNELS; i++) {
    VCanChanData *vChd     = vCard->chanData[i];
    LeafChanData *leafChan = vChd->hwChanData;
    if (leafChan->objbufs) {
      DEBUGPRINT(2, (TXT("Free vCard->chanData[i]->hwChanData->objbufs[%d]\n"), i));
      kfree(leafChan->objbufs);
      leafChan->objbufs = NULL;
    }
  }
  if (vCard->chanData != NULL) {
    DEBUGPRINT(2, (TXT("Free vCard->chanData\n")));
    kfree(vCard->chanData);
    vCard->chanData = NULL;
  }
  if (vCard != NULL) {
    DEBUGPRINT(2, (TXT("Free vCard\n")));
    kfree(vCard);    // Also frees hwCardData (allocated together)
    vCard = NULL;
  }
} // _deallocate


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 8))
# define USB_KILL_URB(x) usb_unlink_urb(x)
#else
# define USB_KILL_URB(x) usb_kill_urb(x)
#endif

//============================================================================
//     leaf_remove
//
//     Called by the usb core when the device is removed from the system.
//
//     This routine guarantees that the driver will not submit any more urbs
//     by clearing dev->udev.  It is also supposed to terminate any currently
//     active urbs.  Unfortunately, usb_bulk_msg(), does not provide any way
//     to do this.  But at least we can cancel an active write.
//
static void leaf_remove (struct usb_interface *interface)
{
  VCanCardData *vCard;
  VCanChanData *vChan;
  LeafCardData *dev;
  unsigned int i;

  DEBUGPRINT(3, (TXT("leaf: _remove\n")));

  vCard = usb_get_intfdata(interface);
  usb_set_intfdata(interface, NULL);

  dev = vCard->hwCardData;

  // Prevent device read, write and ioctl
  // Needs to be done here, or some commands will seem to
  // work even though the device is no longer present.
  vCard->cardPresent = 0;

  DEBUGPRINT(3, (TXT("leaf: Stopping all \"waitQueue's\"\n")));

  for (i = 0; i < MAX_CARD_CHANNELS; i++) {
    vCanCardRemoved(vCard->chanData[i]);
  }

  DEBUGPRINT(3, (TXT("leaf: Stopping all \"WaitNode's\"\n")));

  {
    struct list_head *currHead;
    struct list_head *tmpHead;
    WaitNode         *currNode;
    unsigned long    irqFlags;

    spin_lock_irqsave(&dev->replyWaitListLock, irqFlags);
    list_for_each_safe(currHead, tmpHead, &dev->replyWaitList)
    {
      currNode = list_entry(currHead, WaitNode, list);
      currNode->timedOut = 1;
      complete(&currNode->waitCompletion);
    }
    spin_unlock_irqrestore(&dev->replyWaitListLock, irqFlags);
  }

  softSyncRemoveMember(vCard);

  for (i = 0; i < MAX_CARD_CHANNELS; i++) {
    vChan = vCard->chanData[i];
    DEBUGPRINT(3, (TXT("leaf: Waiting for all closed on minor %d\n"), vChan->minorNr));
    while (atomic_read(&vChan->fileOpenCount) > 0) {
      set_current_state(TASK_UNINTERRUPTIBLE);
      schedule_timeout(msecs_to_jiffies(10));
    }
  }

  // Terminate workqueues
  flush_scheduled_work();


  // Terminate an ongoing write
  DEBUGPRINT(6, (TXT("Ongoing write terminated\n")));
  USB_KILL_URB(dev->write_urb);
  DEBUGPRINT(6, (TXT("Unlinking urb\n")));
  wait_for_completion(&dev->write_finished);

  // Flush and destroy tx workqueue
  DEBUGPRINT(2, (TXT("destroy_workqueue\n")));
  destroy_workqueue(dev->txTaskQ);

  driverData.noOfDevices -= vCard->nrChannels;

  // Deallocate datastructures
  leaf_deallocate(vCard);

  DEBUGPRINT(2, (TXT("Leaf device removed. Leaf devices present (%d)\n"),
                 driverData.noOfDevices));
} // _remove



//======================================================================
//
// Set bit timing
//
static int leaf_set_busparams (VCanChanData *vChan, VCanBusParams *par)
{
  filoCmd        cmd;
  uint32_t       tmp, PScl;
  int            ret;

  DEBUGPRINT(4, (TXT("leaf: _set_busparam\n")));

  cmd.setBusparamsReq.cmdNo   = CMD_SET_BUSPARAMS_REQ;
  cmd.setBusparamsReq.cmdLen  = sizeof(cmdSetBusparamsReq);
  cmd.setBusparamsReq.bitRate = par->freq;
  cmd.setBusparamsReq.sjw     = (unsigned char)par->sjw;
  cmd.setBusparamsReq.tseg1   = (unsigned char)par->tseg1;
  cmd.setBusparamsReq.tseg2   = (unsigned char)par->tseg2;
  cmd.setBusparamsReq.channel = (unsigned char)vChan->channel;
  cmd.setBusparamsReq.noSamp  = 1;

  // Check bus parameters
  tmp = par->freq * (par->tseg1 + par->tseg2 + 1);
  if (tmp == 0) {
    DEBUGPRINT(2, (TXT("leaf: _set_busparams() tmp == 0!\n")));
    return VCAN_STAT_BAD_PARAMETER;
  }

  PScl = 16000000UL / tmp;

  if (PScl <= 1 || PScl > 256) {
    DEBUGPRINT(2, (TXT("%s: hwif_set_chip_param() prescaler wrong (%d)\n"),
                   driverData.deviceName, PScl & 1 /* even */));
    return VCAN_STAT_BAD_PARAMETER;
  }

  DEBUGPRINT(5, (TXT ("leaf_set_busparams: Chan(%d): Freq (%d) SJW (%d) TSEG1 (%d) TSEG2 (%d) ")
                 TXT2("Samp (%d)\n"),
                 cmd.setBusparamsReq.channel,
                 cmd.setBusparamsReq.bitRate,
                 cmd.setBusparamsReq.sjw,
                 cmd.setBusparamsReq.tseg1,
                 cmd.setBusparamsReq.tseg2,
                 cmd.setBusparamsReq.noSamp));

  ret = leaf_queue_cmd(vChan->vCard, &cmd, 50 /* There is no response */);

  return ret;
} // _set_busparams



//======================================================================
//
//  Get bit timing
//  GetBusParams doesn't return any values.
//
static int leaf_get_busparams (VCanChanData *vChan, VCanBusParams *par)
{
  int ret;
  filoCmd  cmd, reply;

  DEBUGPRINT(3, (TXT("leaf: _get_busparam\n")));

  cmd.getBusparamsReq.cmdNo   = CMD_GET_BUSPARAMS_REQ;
  cmd.getBusparamsReq.cmdLen  = sizeof(cmdGetBusparamsReq);
  cmd.getBusparamsReq.channel = (unsigned char)vChan->channel;
  cmd.getBusparamsReq.transId = (unsigned char)vChan->channel;

  ret = leaf_send_and_wait_reply(vChan->vCard, (filoCmd *)&cmd, &reply,
                                 CMD_GET_BUSPARAMS_RESP,
                                 cmd.getBusparamsReq.transId, 1);

  if (ret == VCAN_STAT_OK) {
    DEBUGPRINT(5, (TXT ("Chan(%d): Freq (%d) SJW (%d) TSEG1 (%d) TSEG2 (%d) ")
                 TXT2("Samp (%d)\n"),
                 reply.getBusparamsResp.channel,
                 reply.getBusparamsResp.bitRate,
                 reply.getBusparamsResp.sjw,
                 reply.getBusparamsResp.tseg1,
                 reply.getBusparamsResp.tseg2,
                 reply.getBusparamsResp.noSamp));

    par->freq    = reply.getBusparamsResp.bitRate;
    par->sjw     = reply.getBusparamsResp.sjw;
    par->tseg1   = reply.getBusparamsResp.tseg1;
    par->tseg2   = reply.getBusparamsResp.tseg2;
    par->samp3   = reply.getBusparamsResp.noSamp; // always 1
  }
  else {
    DEBUGPRINT(3, (TXT("leaf_get_busparam - failed %d\n"),ret));
  }

  return ret;
} // _get_busparams


//======================================================================
//
//  Set silent or normal mode
//
static int leaf_set_silent (VCanChanData *vChan, int silent)
{
  filoCmd cmd;
  int ret;

  DEBUGPRINT(3, (TXT("leaf: _set_silent %d\n"),silent));

  cmd.setDrivermodeReq.cmdNo      = CMD_SET_DRIVERMODE_REQ;
  cmd.setDrivermodeReq.cmdLen     = sizeof(cmdSetDrivermodeReq);
  cmd.setDrivermodeReq.channel    = (unsigned char)vChan->channel;
  cmd.setDrivermodeReq.driverMode = silent ? DRIVERMODE_SILENT :
                                             DRIVERMODE_NORMAL;

  ret = leaf_queue_cmd(vChan->vCard, &cmd, 50 /* There is no response */);

  return ret;
} // _set_silent


//======================================================================
//
//  Line mode
//
static int leaf_set_trans_type (VCanChanData *vChan, int linemode, int resnet)
{
  DEBUGPRINT(3, (TXT("leaf: _set_trans_type is NOT implemented!\n")));

  return VCAN_STAT_OK;
} // _set_trans_type




//======================================================================
//
//  Query chip status
//
static int leaf_get_chipstate (VCanChanData *vChan)
{
  VCanCardData *vCard = vChan->vCard;
  //VCAN_EVENT msg;
  filoCmd      cmd;
  filoCmd      reply;
  int          ret;
  //LeafCardData *dev = vCard->hwCardData;

  DEBUGPRINT(3, (TXT("leaf: _get_chipstate\n")));

  cmd.head.cmdNo              = CMD_GET_CHIP_STATE_REQ;
  cmd.getChipStateReq.cmdLen  = sizeof(cmdGetChipStateReq);
  cmd.getChipStateReq.channel = (unsigned char)vChan->channel;
  cmd.getChipStateReq.transId = (unsigned char)vChan->channel;

  ret = leaf_send_and_wait_reply(vCard, (filoCmd *)&cmd, &reply,
                                 CMD_CHIP_STATE_EVENT,
                                 cmd.getChipStateReq.transId, 1);

  return ret;
} // _get_chipstate



//======================================================================
//
//  Go bus on
//
static int leaf_bus_on (VCanChanData *vChan)
{
  VCanCardData  *vCard    = vChan->vCard;
  LeafChanData  *leafChan = vChan->hwChanData;
  filoCmd cmd;
  filoCmd reply;
  int     ret;

  DEBUGPRINT(3, (TXT("leaf: _bus_on\n")));

  memset(((LeafChanData *)vChan->hwChanData)->current_tx_message, 0, sizeof(((LeafChanData *)vChan->hwChanData)->current_tx_message));
  atomic_set(&vChan->transId, 1);
  spin_lock(&leafChan->outTxLock);
  leafChan->outstanding_tx = 0;
  spin_unlock(&leafChan->outTxLock);

  cmd.head.cmdNo            = CMD_START_CHIP_REQ;
  cmd.startChipReq.cmdLen   = sizeof(cmdStartChipReq);
  cmd.startChipReq.channel  = (unsigned char)vChan->channel;
  cmd.startChipReq.transId  = (unsigned char)vChan->channel;

  DEBUGPRINT(5, (TXT("bus on called - ch %d\n"), cmd.startChipReq.channel));

  ret = leaf_send_and_wait_reply(vCard, (filoCmd *)&cmd, &reply,
                                 CMD_START_CHIP_RESP,
                                 cmd.startChipReq.transId, 1);
  if (ret == VCAN_STAT_OK) {
    vChan->isOnBus = 1;

    leaf_get_chipstate(vChan);
  }

  return ret;
} // _bus_on


//======================================================================
//
//  Go bus off
//
static int leaf_bus_off (VCanChanData *vChan)
{
  VCanCardData *vCard    = vChan->vCard;
  LeafChanData *leafChan = vChan->hwChanData;

  filoCmd cmd;
  filoCmd reply;
  int     ret;

  DEBUGPRINT(3, (TXT("leaf: _bus_off\n")));

  cmd.head.cmdNo            = CMD_STOP_CHIP_REQ;
  cmd.startChipReq.cmdLen   = sizeof(cmdStartChipReq);
  cmd.startChipReq.channel  = (unsigned char)vChan->channel;
  cmd.startChipReq.transId  = (unsigned char)vChan->channel;

  ret = leaf_send_and_wait_reply(vCard, (filoCmd *)&cmd, &reply,
                                 CMD_STOP_CHIP_RESP, cmd.startChipReq.transId, 1);
  if (ret == VCAN_STAT_OK) {
    leaf_get_chipstate(vChan);

    DEBUGPRINT(5, (TXT("bus off channel %d\n"), cmd.startChipReq.channel));

    vChan->isOnBus = 0;
    vChan->chipState.state = CHIPSTAT_BUSOFF;
    memset(leafChan->current_tx_message, 0, sizeof(leafChan->current_tx_message));

    spin_lock(&leafChan->outTxLock);
    leafChan->outstanding_tx = 0;
    spin_unlock(&leafChan->outTxLock);

    atomic_set(&vChan->transId, 1);
  }

  return vCanFlushSendBuffer (vChan);
} // _bus_off


//======================================================================
//
//  Request bus statistics
//
static int leaf_req_bus_stats (VCanChanData *vChan)
{
  VCanCardData *vCard = vChan->vCard;
  filoCmd cmd;

  cmd.head.cmdNo = CMD_GET_BUSLOAD_REQ;
  cmd.getBusLoadReq.cmdLen = sizeof(cmdGetBusLoadReq);
  cmd.getBusLoadReq.channel = vChan->channel;

  return leaf_queue_cmd(vCard, &cmd, LEAF_Q_CMD_WAIT_TIME);
}


//======================================================================
//
//  Clear send buffer on card
//
static int leaf_flush_tx_buffer (VCanChanData *vChan)
{
  LeafChanData     *leafChan = vChan->hwChanData;
  //LeafCardData *dev      = vChan->vCard->hwCardData;
  //VCanCardData   *vCard    = vChan->vCard;
  filoCmd cmd;
  int     ret;

  DEBUGPRINT(3, (TXT("leaf: _flush_tx_buffer - %d\n"), vChan->channel));

  cmd.head.cmdNo         = CMD_FLUSH_QUEUE;
  cmd.flushQueue.cmdLen  = sizeof(cmd.flushQueue);
  cmd.flushQueue.channel = (unsigned char)vChan->channel;
  cmd.flushQueue.flags   = 0;

  // We _must_ clear the queue before outstanding_tx!
  // Otherwise, the transmit code could be in the process of sending
  // a message from the queue, increasing outstanding_tx after our clear.
  // With a cleared queue, the transmit code will not be doing anything.
  queue_reinit(&vChan->txChanQueue);
  spin_lock(&leafChan->outTxLock);
  leafChan->outstanding_tx = 0;
  spin_unlock(&leafChan->outTxLock);

  if (vChan->capabilities & VCAN_CHANNEL_CAP_SYNC_TX_FLUSH) {
    filoCmd reply;
    ret = leaf_send_and_wait_reply(vChan->vCard, &cmd, &reply, CMD_FILO_FLUSH_QUEUE_RESP, 0, 0);
  } else {
    ret = leaf_queue_cmd(vChan->vCard, &cmd, 50 /* There is no response */);
  }

  if (ret == VCAN_STAT_OK) {
    atomic_set(&vChan->transId, 1);
    // Do this once more, for good measure.
    queue_reinit(&vChan->txChanQueue);
    spin_lock(&leafChan->outTxLock);
    leafChan->outstanding_tx = 0;
    spin_unlock(&leafChan->outTxLock);
  }

  return ret;
} // _flush_tx_buffer


//======================================================================
//
// Request send
//
static void leaf_schedule_send (VCanCardData *vCard, VCanChanData *vChan)
{
  LeafCardData *dev = vCard->hwCardData;

  DEBUGPRINT(3, (TXT("leaf: _schedule_send\n")));

  if (leaf_tx_available(vChan) && vCard->cardPresent) {
    queue_work(dev->txTaskQ, &dev->txWork);
  }
#if DEBUG
  else {
    DEBUGPRINT(3, (TXT("SEND FAILED \n")));
  }
#endif
} // _schedule_send



//======================================================================
//  Read transmit error counter
//
static int leaf_get_tx_err (VCanChanData *vChan)
{
  DEBUGPRINT(3, (TXT("leaf: _get_tx_err\n")));

  leaf_get_chipstate(vChan);

  return vChan->chipState.txerr;
  //return vChan->txErrorCounter;
} //_get_tx_err


//======================================================================
//  Read transmit error counter
//
static int leaf_get_rx_err (VCanChanData *vChan)
{
  DEBUGPRINT(3, (TXT("leaf: _get_rx_err\n")));

  leaf_get_chipstate(vChan);

  return vChan->chipState.rxerr;
  //return vChan->rxErrorCounter;
} // _get_rx_err



//======================================================================
//  Read transmit queue length in hardware/firmware
//
static unsigned long leaf_get_hw_tx_q_len (VCanChanData *vChan)
{
  LeafChanData *hChd  = vChan->hwChanData;
  unsigned int res;

  DEBUGPRINT(3, (TXT("leaf: _get_hw_tx_q_len\n")));

  spin_lock(&hChd->outTxLock);
  res = hChd->outstanding_tx;
  spin_unlock(&hChd->outTxLock);

  return res;
} // _get_hw_tx_q_len





//======================================================================
//
// Run when driver is loaded
//
static int leaf_init_driver (void)
{
  int result;

  DEBUGPRINT(3, (TXT("leaf: _init_driver\n")));

  driverData.deviceName = DEVICE_NAME_STRING;

  // Register this driver with the USB subsystem
  result = usb_register(&leaf_driver);
  if (result) {
    DEBUGPRINT(1, (TXT("leaf: usb_register failed. Error number %d"),
                   result));
    return result;
  }

  return 0;
} // _init_driver



//======================================================================
// Run when driver is unloaded
//
static int leaf_close_all (void)
{
  DEBUGPRINT(2, (TXT("leaf: _close_all (deregister driver..)\n")));
  usb_deregister(&leaf_driver);

  return 0;
} // _close_all



//======================================================================
// proc read function
//
static int leaf_proc_read (struct seq_file* m, void* v)
{
  int            channel  = 0;
  VCanCardData  *cardData = driverData.canCards;

  seq_printf(m,"\ntotal channels %d\n", driverData.noOfDevices);
  seq_puts(m,"minor numbers");
  while (NULL != cardData) {
    for (channel = 0; channel < cardData->nrChannels; channel++) {
        seq_printf(m," %d", cardData->chanData[channel]->minorNr);
    }
    cardData = cardData->next;
  }
  seq_puts(m, "\n");

  return 0;
} // _proc_read


//======================================================================
//  Can we send now?
//
static int leaf_tx_available (VCanChanData *vChan)
{
  LeafChanData     *leafChan = vChan->hwChanData;
  VCanCardData     *vCard    = vChan->vCard;
  LeafCardData     *dev      = vCard->hwCardData;
  unsigned int     res;

  DEBUGPRINT(3, (TXT("leaf: _tx_available %d (%d)!\n"),
                 leafChan->outstanding_tx, dev->max_outstanding_tx));

  spin_lock(&leafChan->outTxLock);
  res = leafChan->outstanding_tx;
  spin_unlock(&leafChan->outTxLock);

  return (res < dev->max_outstanding_tx);
} // _tx_available


//======================================================================
//  Are all sent msg's received?
//
static int leaf_outstanding_sync (VCanChanData *vChan)
{
  LeafChanData     *leafChan  = vChan->hwChanData;
  unsigned int     res;

  DEBUGPRINT(3, (TXT("leaf: _outstanding_sync (%d)\n"),
                 leafChan->outstanding_tx));

  spin_lock(&leafChan->outTxLock);
  res = leafChan->outstanding_tx;
  spin_unlock(&leafChan->outTxLock);

  return (res == 0);
} // _outstanding_sync



//======================================================================
// Get time
//
 static int leaf_get_time (VCanCardData *vCard, uint64_t *time)
{
  filoCmd cmd;
  filoCmd reply;
  int ret;

  DEBUGPRINT(3, (TXT("leaf: _get_time\n")));

  memset(&cmd, 0, sizeof(cmd));
  cmd.head.cmdNo           = CMD_READ_CLOCK_REQ;
  cmd.readClockReq.cmdLen  = sizeof(cmd.readClockReq);
  cmd.readClockReq.flags   = 0;

  // CMD_READ_CLOCK_RESP seem to always return 0 as transid
  ret = leaf_send_and_wait_reply(vCard, (filoCmd *)&cmd, &reply,
                                 CMD_READ_CLOCK_RESP, 0, 1);
  if (ret == VCAN_STAT_OK) {
    uint64_t ticks;
    ticks = reply.readClockResp.time[2];
    ticks <<= 16;
    ticks += reply.readClockResp.time[1];
    ticks <<= 16;
    ticks += reply.readClockResp.time[0];
    *time = ticks_to_10us(vCard, ticks);
  }

  return ret;
}


/***************************************************************************/
/* Free an object buffer (or free all) */
static int leaf_objbuf_free (VCanChanData *chd, int bufType, int bufNo)
{
  int ret;
  filoCmd cmd;
  int start, stop, i;
  LeafChanData  *leafChan = chd->hwChanData;
  VCanCardData  *vCard    = chd->vCard;
  LeafCardData  *dev      = (LeafCardData *)vCard->hwCardData;

  if (bufType != OBJBUF_TYPE_PERIODIC_TX) {
    if (bufNo == -1) {
      // All buffers.. cleanup in progress, so we are happy.
      return VCAN_STAT_OK;
    }
    // Tried to free a nonexistent buffer; this is an error.
    return VCAN_STAT_BAD_PARAMETER;
  }
  if (!leafChan->objbufs) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  DEBUGPRINT(4, (TXT("hwif_objbuf_free\n")));

  if (bufNo == -1) {
    start = 0;
    stop  = dev->autoTxBufferCount;  // ci->cc.auto...
  } else {
    start = bufNo;
    stop  = bufNo + 1;
  }

  for (i = start; i < stop; i++) {
    leafChan->objbufs[i].in_use = 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.autoTxBufferReq.cmdNo       = CMD_AUTO_TX_BUFFER_REQ;
    cmd.autoTxBufferReq.cmdLen      = sizeof(cmd.autoTxBufferReq);
    cmd.autoTxBufferReq.requestType = AUTOTXBUFFER_CMD_DEACTIVATE;
    cmd.autoTxBufferReq.channel     = (unsigned char)chd->channel;
    cmd.autoTxBufferReq.bufNo       = (unsigned char)i;

    ret = leaf_queue_cmd(vCard, &cmd, LEAF_Q_CMD_WAIT_TIME);
    if (ret != VCAN_STAT_OK) {
      return VCAN_STAT_NO_MEMORY;
    }
  }

  return VCAN_STAT_OK;
}


/***************************************************************************/
/* Allocate an object buffer */
static int leaf_objbuf_alloc (VCanChanData *chd, int bufType, int *bufNo)
{
  int i;
  LeafChanData  *leafChan = chd->hwChanData;
  VCanCardData  *vCard    = chd->vCard;
  LeafCardData  *dev      = (LeafCardData *)vCard->hwCardData;

  if (bufType != OBJBUF_TYPE_PERIODIC_TX) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (dev->autoTxBufferCount == 0 || dev->autoTxBufferResolution == 0) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  DEBUGPRINT(4, (TXT("hwif_objbuf_alloc\n")));

  if (!leafChan->objbufs) {
    DEBUGPRINT(4, (TXT("Allocating leafChan->objbufs[]\n")));
    leafChan->objbufs = kmalloc(sizeof(OBJECT_BUFFER) * dev->autoTxBufferCount, GFP_KERNEL);
    if (!leafChan->objbufs) {
      return VCAN_STAT_NO_MEMORY;
    }

    memset(leafChan->objbufs, 0, sizeof(OBJECT_BUFFER) * dev->autoTxBufferCount);
  }

  for (i = 0; i < dev->autoTxBufferCount; i++) {
    if (!leafChan->objbufs[i].in_use) {
      leafChan->objbufs[i].in_use = 1;
      *bufNo = i;
      return VCAN_STAT_OK;
    }
  }

  return VCAN_STAT_NO_MEMORY;
}


/***************************************************************************/
/* Write data to an object buffer */
static int leaf_objbuf_write (VCanChanData *chd, int bufType, int bufNo,
                              int id, int flags, int dlc, unsigned char *data)
{
  int ret;
  filoCmd cmd;
  LeafChanData  *leafChan = chd->hwChanData;
  VCanCardData  *vCard    = chd->vCard;
  LeafCardData  *dev      = (LeafCardData *)vCard->hwCardData;

  if (bufType != OBJBUF_TYPE_PERIODIC_TX) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (bufNo < 0 || bufNo >= dev->autoTxBufferCount) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (!leafChan->objbufs) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (!leafChan->objbufs[bufNo].in_use) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  DEBUGPRINT(4, (TXT("hwif_objbuf_write, id=0x%x flags=0x%x dlc=%d\n"),
                 id, flags, dlc));

  leafChan->objbufs[bufNo].msg.id     = id;
  leafChan->objbufs[bufNo].msg.flags  = (unsigned char)flags;
  leafChan->objbufs[bufNo].msg.length = (unsigned char)dlc;
  memcpy(leafChan->objbufs[bufNo].msg.data, data, (dlc > 8) ? 8 : dlc);

  memset(&cmd, 0, sizeof(cmd));
  cmd.setAutoTxBuffer.cmdNo   = CMD_SET_AUTO_TX_BUFFER;
  cmd.setAutoTxBuffer.cmdLen  = sizeof(cmd.setAutoTxBuffer);
  cmd.setAutoTxBuffer.channel = (unsigned char)chd->channel;
  cmd.setAutoTxBuffer.bufNo   = (unsigned char)bufNo;

  cmd.setAutoTxBuffer.flags = 0;
  if (id & EXT_MSG) {
    cmd.setAutoTxBuffer.flags        |= AUTOTXBUFFER_MSG_EXT;
    cmd.setAutoTxBuffer.rawMessage[0] = (unsigned char)((id >> 24) & 0x1F);
    cmd.setAutoTxBuffer.rawMessage[1] = (unsigned char)((id >> 18) & 0x3F);
    cmd.setAutoTxBuffer.rawMessage[2] = (unsigned char)((id >> 14) & 0x0F);
    cmd.setAutoTxBuffer.rawMessage[3] = (unsigned char)((id >>  6) & 0xFF);
    cmd.setAutoTxBuffer.rawMessage[4] = (unsigned char)((id      ) & 0x3F);
  } else {
    cmd.setAutoTxBuffer.rawMessage[0] = (unsigned char)((id >>  6) & 0x1F);
    cmd.setAutoTxBuffer.rawMessage[1] = (unsigned char)((id      ) & 0x3F);
  }
  cmd.setAutoTxBuffer.rawMessage[5] = dlc & 0x0F;
  memcpy(&cmd.setAutoTxBuffer.rawMessage[6], data, 8);

  if (flags & MSGFLAG_REMOTE_FRAME) {
    cmd.setAutoTxBuffer.flags |= AUTOTXBUFFER_MSG_REMOTE_FRAME;
  }

  ret = leaf_queue_cmd(vCard, &cmd, LEAF_Q_CMD_WAIT_TIME);

  return (ret != VCAN_STAT_OK) ? VCAN_STAT_NO_MEMORY : VCAN_STAT_OK;
}


/***************************************************************************/
/* Set filters on an object buffer */
static int leaf_objbuf_set_filter (VCanChanData *chd, int bufType, int bufNo,
                                   int code, int mask)
{
  LeafChanData  *leafChan = chd->hwChanData;

  if (bufType != OBJBUF_TYPE_PERIODIC_TX) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (!leafChan->objbufs) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  DEBUGPRINT(4, (TXT("hwif_objbuf_set_filter\n")));
  // This operation is irrelevant, so we fail.

  return VCAN_STAT_BAD_PARAMETER;
}


/***************************************************************************/
/* Set flags on an object buffer */
static int leaf_objbuf_set_flags (VCanChanData *chd, int bufType, int bufNo,
                                  int flags)
{
  LeafChanData  *leafChan = chd->hwChanData;

  if (bufType != OBJBUF_TYPE_PERIODIC_TX) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (!leafChan->objbufs) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  DEBUGPRINT(4, (TXT("hwif_objbuf_set_flags\n")));
  // This operation is irrelevant.

  return VCAN_STAT_BAD_PARAMETER;
}


/***************************************************************************/
/* Enable/disable an object buffer (or enable/disable all) */
static int leaf_objbuf_enable (VCanChanData *chd, int bufType, int bufNo,
                               int enable)
{
  int ret;
  filoCmd cmd;
  int start, stop, i;
  LeafChanData  *leafChan = chd->hwChanData;
  VCanCardData  *vCard    = chd->vCard;
  LeafCardData  *dev      = (LeafCardData *)vCard->hwCardData;

  if (bufType != OBJBUF_TYPE_PERIODIC_TX) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (!leafChan->objbufs) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  DEBUGPRINT(4, (TXT("hwif_objbuf_enable\n")));

  if (bufNo == -1) {
    start = 0;
    stop  = dev->autoTxBufferCount;
  } else {
    start = bufNo;
    stop  = bufNo + 1;
    if (!leafChan->objbufs[start].in_use) {
      return VCAN_STAT_BAD_PARAMETER;
    }
  }

  for (i = start; i < stop; i++) {
    leafChan->objbufs[i].active = enable;

    memset(&cmd, 0, sizeof(cmd));
    cmd.autoTxBufferReq.cmdNo       = CMD_AUTO_TX_BUFFER_REQ;
    cmd.autoTxBufferReq.cmdLen      = sizeof(cmd.autoTxBufferReq);
    cmd.autoTxBufferReq.requestType = enable ? AUTOTXBUFFER_CMD_ACTIVATE :
                                               AUTOTXBUFFER_CMD_DEACTIVATE;
    cmd.autoTxBufferReq.channel     = (unsigned char)chd->channel;
    cmd.autoTxBufferReq.bufNo       = (unsigned char)i;

    ret = leaf_queue_cmd(vCard, &cmd, LEAF_Q_CMD_WAIT_TIME);
    if (ret != VCAN_STAT_OK) {
      return VCAN_STAT_NO_MEMORY;
    }
  }

  return VCAN_STAT_OK;
}


/***************************************************************************/
/* Set the transmission interval (in microseconds) for an object buffer */
static int leaf_objbuf_set_period (VCanChanData *chd, int bufType, int bufNo,
                                   int period)
{
  int ret;
  filoCmd cmd;
  unsigned int interval;
  LeafChanData  *leafChan = chd->hwChanData;
  VCanCardData  *vCard    = chd->vCard;
  LeafCardData  *dev      = (LeafCardData *)vCard->hwCardData;

  if (bufType != OBJBUF_TYPE_PERIODIC_TX) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (bufNo < 0 || bufNo >= dev->autoTxBufferCount) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (!leafChan->objbufs) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (!leafChan->objbufs[bufNo].in_use) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (dev->autoTxBufferCount == 0 || dev->autoTxBufferResolution == 0) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  ret = 0;
  interval = (period + dev->autoTxBufferResolution / 2) /
             dev->autoTxBufferResolution;
  if (interval == 0) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  DEBUGPRINT(4, (TXT("hwif_objbuf_set_period period=%d (scaled)interval=%d\n"),
                 period, interval));

  leafChan->objbufs[bufNo].period = period;

  memset(&cmd, 0, sizeof(cmd));
  cmd.autoTxBufferReq.cmdNo       = CMD_AUTO_TX_BUFFER_REQ;
  cmd.autoTxBufferReq.cmdLen      = sizeof(cmd.autoTxBufferReq);
  cmd.autoTxBufferReq.requestType = AUTOTXBUFFER_CMD_SET_INTERVAL;
  cmd.autoTxBufferReq.channel     = (unsigned char)chd->channel;
  cmd.autoTxBufferReq.bufNo       = (unsigned char)bufNo;
  cmd.autoTxBufferReq.interval    = interval;

  ret = leaf_queue_cmd(vCard, &cmd, LEAF_Q_CMD_WAIT_TIME);

  return (ret != VCAN_STAT_OK) ? VCAN_STAT_NO_MEMORY : VCAN_STAT_OK;
}


/***************************************************************************/
/* Set the message count for an object buffer */
static int leaf_objbuf_set_msg_count (VCanChanData *chd, int bufType, int bufNo,
                                      int count)
{
  int ret;
  filoCmd cmd;
  LeafChanData  *leafChan = chd->hwChanData;
  VCanCardData  *vCard    = chd->vCard;
  LeafCardData  *dev      = (LeafCardData *)vCard->hwCardData;

  if (bufType != OBJBUF_TYPE_PERIODIC_TX) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (bufNo < 0 || bufNo >= dev->autoTxBufferCount) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (!leafChan->objbufs) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (!leafChan->objbufs[bufNo].in_use) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if ((unsigned)count > 0xffff) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.autoTxBufferReq.cmdNo       = CMD_AUTO_TX_BUFFER_REQ;
  cmd.autoTxBufferReq.cmdLen      = sizeof(cmd.autoTxBufferReq);
  cmd.autoTxBufferReq.requestType = AUTOTXBUFFER_CMD_SET_MSG_COUNT;
  cmd.autoTxBufferReq.channel     = (unsigned char)chd->channel;
  cmd.autoTxBufferReq.bufNo       = (unsigned char)bufNo;
  cmd.autoTxBufferReq.interval    = (unsigned short)count;

  ret = leaf_queue_cmd(vCard, &cmd, LEAF_Q_CMD_WAIT_TIME);

  return (ret != VCAN_STAT_OK) ? VCAN_STAT_NO_MEMORY : VCAN_STAT_OK;
}


/***************************************************************************/
static int leaf_objbuf_exists (VCanChanData *chd, int bufType, int bufNo)
{
  LeafChanData  *leafChan = chd->hwChanData;
  VCanCardData  *vCard    = chd->vCard;
  LeafCardData  *dev      = (LeafCardData *)vCard->hwCardData;

  if (bufType != OBJBUF_TYPE_PERIODIC_TX) {
    return 0;
  }

  if (bufNo < 0 || bufNo >= dev->autoTxBufferCount) {
    return 0;
  }

  if (!leafChan->objbufs) {
    return 0;
  }

  if (!leafChan->objbufs[bufNo].in_use) {
    return 0;
  }

  return 1;
}


/***************************************************************************/
static int leaf_objbuf_send_burst (VCanChanData *chd, int bufType, int bufNo,
                                   int burstLen)
{
  int ret;
  filoCmd cmd;
  LeafChanData  *leafChan = chd->hwChanData;
  VCanCardData  *vCard    = chd->vCard;
  LeafCardData  *dev      = (LeafCardData *)vCard->hwCardData;

  if (bufType != OBJBUF_TYPE_PERIODIC_TX) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (bufNo < 0 || bufNo >= dev->autoTxBufferCount) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (!leafChan->objbufs) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (!leafChan->objbufs[bufNo].in_use) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  if (dev->autoTxBufferCount == 0 || dev->autoTxBufferResolution == 0) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  DEBUGPRINT(4, (TXT("hwif_objbuf_send_burst len=%d \n"), burstLen));

  memset(&cmd, 0, sizeof(cmd));
  cmd.autoTxBufferReq.cmdNo       = CMD_AUTO_TX_BUFFER_REQ;
  cmd.autoTxBufferReq.cmdLen      = sizeof(cmd.autoTxBufferReq);
  cmd.autoTxBufferReq.requestType = AUTOTXBUFFER_CMD_GENERATE_BURST;
  cmd.autoTxBufferReq.channel     = (unsigned char)chd->channel;
  cmd.autoTxBufferReq.bufNo       = (unsigned char)bufNo;
  cmd.autoTxBufferReq.interval    = burstLen;

  ret = leaf_queue_cmd(vCard, &cmd, LEAF_Q_CMD_WAIT_TIME);

  return (ret != VCAN_STAT_OK) ? VCAN_STAT_NO_MEMORY : VCAN_STAT_OK;
}

static int leaf_tx_interval (VCanChanData *chd, unsigned int *interval) {
  filoCmd cmd;
  filoCmd reply;
  VCanCardData  *vCard = chd->vCard;
  int r;

  memset(&cmd, 0, sizeof cmd);

  cmd.txInterval.cmdLen  = sizeof(cmdTxInterval);
  cmd.txInterval.cmdNo   = CMD_TX_INTERVAL_REQ;
  cmd.txInterval.channel = (unsigned char)chd->channel;

  cmd.txInterval.interval = *interval;
  r = leaf_send_and_wait_reply(vCard, &cmd, &reply, CMD_TX_INTERVAL_RESP, cmd.txInterval.channel, 1);

  if (r != 0)
    return r;

  if (reply.txInterval.status != VCAN_STAT_OK)
    return VCAN_STAT_BAD_PARAMETER;

  *interval = reply.txInterval.interval;
  return r;
}

static int leaf_capabilities (VCanCardData *vCard, uint32_t vcan_cmd) {
  filoCmd  cmd;
  filoCmd  reply;
  int      r;
  uint32_t value, mask;
  uint8_t  sub_cmd = convert_vcan_to_hydra_cmd (vcan_cmd);

  if (!vcan_cmd) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  memset(&cmd, 0, sizeof cmd);

  cmd.capabilitiesReq.cmdNo    = CMD_GET_CAPABILITIES_REQ;
  cmd.capabilitiesReq.subCmdNo = sub_cmd;
  cmd.capabilitiesReq.cmdLen   = sizeof(cmdCapabilitiesReq);

  r = leaf_send_and_wait_reply(vCard, &cmd, &reply, CMD_GET_CAPABILITIES_RESP, 0, 0);

  if (r != VCAN_STAT_OK) return r;

  if (reply.capabilitiesResp.status != CAP_STATUS_OK) return VCAN_STAT_FAIL;

  switch (reply.capabilitiesResp.subCmdNo) {
    case CAP_SUB_CMD_SILENT_MODE:
      value = reply.capabilitiesResp.silentMode.value;
      mask  = reply.capabilitiesResp.silentMode.mask;
      break;
    case CAP_SUB_CMD_ERRFRAME:
      value = reply.capabilitiesResp.errframeCap.value;
      mask  = reply.capabilitiesResp.errframeCap.mask;
      break;
    case CAP_SUB_CMD_BUS_STATS:
      value = reply.capabilitiesResp.busstatCap.value;
      mask  = reply.capabilitiesResp.busstatCap.mask;
      break;
    case CAP_SUB_CMD_ERRCOUNT_READ:
      value = reply.capabilitiesResp.errcountCap.value;
      mask  = reply.capabilitiesResp.errcountCap.mask;
      break;
    case CAP_SUB_CMD_SYNC_TX_FLUSH:
      value = reply.capabilitiesResp.syncTxFlushCap.value;
      mask  = reply.capabilitiesResp.syncTxFlushCap.mask;
      break;
    default: return VCAN_STAT_BAD_PARAMETER; break;
  }

  set_capability_value (vCard, vcan_cmd, value, 0xFFFFFFFF, MAX_CARD_CHANNELS);
  set_capability_mask  (vCard, vcan_cmd, mask,  0xFFFFFFFF, MAX_CARD_CHANNELS);

  return r;
}

static int leaf_get_card_info(VCanCardData *vCard, VCAN_IOCTL_CARD_INFO *ci)
{
  filoCmd cmd;
  filoCmd reply;
  int r;

  memset(&cmd, 0, sizeof(cmd));
  memset(&reply, 0, sizeof(reply));

  cmd.getCardInfoReq.cmdLen  = sizeof(cmdGetCardInfoReq);
  cmd.getCardInfoReq.cmdNo = CMD_GET_CARD_INFO_REQ;
  cmd.getCardInfoReq.transId = CMD_GET_CARD_INFO_REQ;
  r = leaf_send_and_wait_reply(vCard, &cmd, &reply,
                               CMD_GET_CARD_INFO_RESP, cmd.getCardInfoReq.transId, 1);

  vCard->serialNumber = reply.getCardInfoResp.serialNumber;
  memcpy(vCard->ean, reply.getCardInfoResp.EAN, sizeof vCard->ean);

  ci->serial_number = vCard->serialNumber;
  ci->channel_count = vCard->nrChannels;

  ci->driver_version_major = CANLIB_MAJOR_VERSION;
  ci->driver_version_minor = CANLIB_MINOR_VERSION;
  ci->driver_version_build = CANLIB_BUILD_VERSION;
  ci->product_version_major = CANLIB_PRODUCT_MAJOR_VERSION;
  ci->product_version_minor = CANLIB_MINOR_VERSION;
  ci->product_version_minor_letter = 0;

  strncpy(ci->driver_name, vCard->driverData->deviceName, MAX_IOCTL_DRIVER_NAME);

  return 0;
}

static int leaf_get_card_info_2(VCanCardData *vCard, KCAN_IOCTL_CARD_INFO_2 *ci)
{
  VCAN_IOCTL_CARD_INFO x;
  int r;

  r = leaf_get_card_info(vCard, &x);
  if (r != 0)
    return r;

  ci->usb_host_id = vCard->usb_root_hub_id;
  memcpy(ci->ean, vCard->ean, sizeof ci->ean);

  return 0;
}

static void leaf_softsync_onoff (VCanCardData *vCard, int enable)
{
  filoCmd cmd;
  int r;

  memset(&cmd, 0, sizeof cmd);
  cmd.softSyncOnOff.cmdNo = CMD_SOFTSYNC_ONOFF;
  cmd.softSyncOnOff.cmdLen = sizeof (cmd.softSyncOnOff);
  cmd.softSyncOnOff.onOff = enable ? SOFTSYNC_ON : SOFTSYNC_OFF;

  r = leaf_queue_cmd(vCard, &cmd, LEAF_Q_CMD_WAIT_TIME);
}

/***************************************************************************/
int leaf_read_user_parameter(const VCanChanData * const vChan,
                             unsigned int * const status,
                             const unsigned int userNo,
                             const unsigned int paramNo,
                             const unsigned int paramLen,
                             unsigned char * const data)
{
  int r = 0;
  filoCmd cmd;
  filoCmd reply;

  memset(&cmd, 0, sizeof(cmd));

  cmd.readUserParameter.cmdNo      = CMD_READ_USER_PARAMETER;
  cmd.readUserParameter.cmdLen     = sizeof(cmd.readUserParameter);
  cmd.readUserParameter.userNo     = (unsigned char) userNo;
  cmd.readUserParameter.paramNo    = (unsigned char) paramNo;

  if (status) {
    *status = 0;
  }

  r = leaf_send_and_wait_reply(vChan->vCard, &cmd, &reply,
                               CMD_READ_USER_PARAMETER, (unsigned char)vChan->channel, 0);
  if (r != VCAN_STAT_OK) {
    return r;
  }

  if (reply.readUserParameter.status == 0) {
    unsigned int copySize = (paramLen <= sizeof(reply.readUserParameter.data)) ?
      paramLen : sizeof(reply.readUserParameter.data);
    memcpy(data, reply.readUserParameter.data, copySize);
  }

  if (status) {
    *status = reply.readUserParameter.status;
  }

  return r;
}

/***************************************************************************/
static int leaf_get_card_info_misc(const VCanChanData *chd, KCAN_IOCTL_MISC_INFO *cardInfoMisc)
{
  int r = 0;
  filoCmd cmd;
  filoCmd reply;

  if (!(chd->vCard->card_flags & DEVHND_CARD_EXTENDED_CAPABILITIES))
  {
    cardInfoMisc->retcode = KCAN_IOCTL_MISC_INFO_NOT_IMPLEMENTED;
    return r;
  }

  memset(&cmd, 0, sizeof(cmd));

  cmd.head.cmdNo = CMD_GET_CAPABILITIES_REQ;
  cmd.head.cmdLen = sizeof(cmdCapabilitiesReq);
  cmd.capabilitiesReq.subData.channel = (uint16_t)chd->channel;

  switch (cardInfoMisc->subcmd) {
    case KCAN_IOCTL_MISC_INFO_SUBCMD_CHANNEL_LOGGER_INFO:
      cmd.capabilitiesReq.subCmdNo = CAP_SUB_CMD_GET_LOGGER_INFO;
      break;
    case KCAN_IOCTL_MISC_INFO_SUBCMD_CHANNEL_REMOTE_INFO:
      cmd.capabilitiesReq.subCmdNo = CAP_SUB_CMD_REMOTE_INFO;
      break;
    default:
      r = VCAN_STAT_OK;
      cardInfoMisc->retcode = KCAN_IOCTL_MISC_INFO_NOT_IMPLEMENTED;
      return r;
  }

  r = leaf_send_and_wait_reply(chd->vCard, &cmd, &reply,
                               CMD_GET_CAPABILITIES_RESP, (unsigned char)chd->channel, 0);
  if (r == VCAN_STAT_OK) {
    if (reply.capabilitiesResp.status == CAP_STATUS_OK)
    {
      cardInfoMisc->retcode = KCAN_IOCTL_MISC_INFO_RETCODE_SUCCESS;
    }
    else
    {
      cardInfoMisc->retcode = KCAN_IOCTL_MISC_INFO_NOT_IMPLEMENTED;
      return r;
    }

    switch (cardInfoMisc->subcmd) {
      case KCAN_IOCTL_MISC_INFO_SUBCMD_CHANNEL_LOGGER_INFO:
        cardInfoMisc->payload.loggerInfo.loggerType = reply.capabilitiesResp.loggerType.data==LOGGERTYPE_NOT_A_LOGGER?KCAN_IOCTL_MISC_INFO_LOGGER_TYPE_NOT_A_LOGGER:KCAN_IOCTL_MISC_INFO_LOGGER_TYPE_V1;
      break;
      case KCAN_IOCTL_MISC_INFO_SUBCMD_CHANNEL_REMOTE_INFO:
        cardInfoMisc->payload.remoteInfo.webServer = reply.capabilitiesResp.remoteInfo.webServer?KCAN_IOCTL_MISC_INFO_REMOTE_WEBSERVER_V1:KCAN_IOCTL_MISC_INFO_REMOTE_NO_WEBSERVER;
        if (reply.capabilitiesResp.remoteInfo.remoteType) {
          cardInfoMisc->payload.remoteInfo.remoteType = reply.capabilitiesResp.remoteInfo.remoteType==REMOTE_TYPE_WLAN?KCAN_IOCTL_MISC_INFO_REMOTE_TYPE_WLAN:KCAN_IOCTL_MISC_INFO_REMOTE_TYPE_LAN;
        }
        else {
          cardInfoMisc->payload.remoteInfo.remoteType = KCAN_IOCTL_MISC_INFO_REMOTE_TYPE_NOT_REMOTE;
        }
      break;
    }
  }

  return r;
}

/***************************************************************************/
static int leaf_flash_leds(const VCanChanData *chd, int action, int timeout)
{
  filoCmd cmd, reply;
  int r;

  cmd.ledActionReq.cmdNo = CMD_LED_ACTION_REQ;
  cmd.ledActionReq.cmdLen = sizeof(cmdLedActionReq);
  cmd.ledActionReq.subCmd = action;
  cmd.ledActionReq.timeout = timeout;
  r = leaf_send_and_wait_reply(chd->vCard, &cmd, &reply,
                               CMD_LED_ACTION_RESP,
                               0, 1);
  return r;
}

/***************************************************************************/
#define PARAM_USER_FIRST                (1)
#define CHANNEL_NAME_ID                 (199)
#define CHANNEL_NAME_PARAM_INDEX_FIRST  (PARAM_USER_FIRST + 1)
// CHANNEL_NAME_NBR_OF_PARAMS: number of parameter storages used for naming each channel.
#define CHANNEL_NAME_NBR_OF_PARAMS      (3)
#define PARAM_HW_DESCRIPTION_PART_LEN   (8)

static int leaf_get_cust_channel_name(const VCanChanData * const vChan,
                                      unsigned char * const data,
                                      const unsigned int data_size,
                                      unsigned int * const status)
{
  const unsigned int channel = vChan->channel;
  int r = 0;
  unsigned char tempdata[PARAM_HW_DESCRIPTION_PART_LEN];
  unsigned int nbr_of_bytes_to_copy;
  unsigned int bytes_copied = 0;
  unsigned int param_index = CHANNEL_NAME_PARAM_INDEX_FIRST + channel * CHANNEL_NAME_NBR_OF_PARAMS;
  unsigned int i;

  nbr_of_bytes_to_copy = PARAM_HW_DESCRIPTION_PART_LEN * CHANNEL_NAME_NBR_OF_PARAMS;
  if (data_size < nbr_of_bytes_to_copy)
    nbr_of_bytes_to_copy = data_size;


  for (i = 0; i < CHANNEL_NAME_NBR_OF_PARAMS; i++) {
    unsigned chunk_size;

    r = leaf_read_user_parameter(vChan, status, CHANNEL_NAME_ID, param_index + i, sizeof(tempdata), tempdata);
    if (*status)
      r = -1;
    if (r)
      return r;

    chunk_size = nbr_of_bytes_to_copy < sizeof(tempdata) ? nbr_of_bytes_to_copy : sizeof(tempdata);
    memcpy(&data[bytes_copied], tempdata, chunk_size);
    nbr_of_bytes_to_copy -= chunk_size;
    bytes_copied += chunk_size;
    if (!nbr_of_bytes_to_copy)
      break;
  }

  // Assure null termination.
  data[data_size - 1] = 0;
  *status = 0;

  return r;
}


int init_module (void)
{
  driverData.hwIf = &hwIf;
  return vCanInit (&driverData, MAX_DRIVER_CHANNELS);
}

void cleanup_module (void)
{
  vCanCleanup (&driverData);
}
