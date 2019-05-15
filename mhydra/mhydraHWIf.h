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

//
// Linux Mhydra driver
//

#ifndef _MHYDRA_HW_IF_H_
#define _MHYDRA_HW_IF_H_


#include "hydra_host_cmds.h"
#include "objbuf.h"

/*****************************************************************************/
/* defines */
/*****************************************************************************/

#define DEVICE_NAME_STRING            "mhydra"
// Maximum number of channels for a device of this type.
#define HYDRA_MAX_CARD_CHANNELS       5U
#define HYDRA_MAX_DRIVER_CHANNELS     128U

#define KV_MHYDRA_MAIN_RCV_BUF_SIZE   16U
#define KV_MHYDRA_TX_CMD_BUF_SIZE     16U
#define MHYDRA_CMD_RESP_WAIT_TIME     5000U
#define HYDRA_MAX_OUTSTANDING_TX      200U


// Bits in the CxSTRH register in the M32C.
#define M32C_BUS_RESET                0x01U    // Chip is in Reset state
#define M32C_BUS_ERROR                0x10U    // Chip has seen a bus error
#define M32C_BUS_PASSIVE              0x20U    // Chip is error passive
#define M32C_BUS_OFF                  0x40U    // Chip is bus off

#define MAX_HE_COUNT                  64U

#if DEBUG
#   define MHYDRA_Q_CMD_WAIT_TIME     800
#else
#   define MHYDRA_Q_CMD_WAIT_TIME     200
#endif


/* Channel specific data */
typedef struct MhydraChanData {
  /* These are the number of outgoing packets residing in the device */
  uint32_t      outstanding_tx;
  spinlock_t    outTxLock;
  uint64_t      timestamp_correction_value;
  OBJECT_BUFFER *objbufs;
  CAN_MSG       current_tx_message[HYDRA_MAX_OUTSTANDING_TX];
} MhydraChanData;

#define NUM_IN_PIPES 2

typedef struct MhydraUsbPipeInfo {
  uint8_t   *buffer;            // the buffer to receive data
  size_t    size;               // the size of the receive buffer
  uint8_t   endpointAddr;       // the address of the bulk in endpoint
  uint32_t  maxPacketSize;
} MhydraUsbPipeInfo;

/*  Cards specific data */
typedef struct MhydraCardData {
  // Map channel (0,1,2,...) to HE (6-bit number meaningful only to fw)
  uint8_t   channel2he[HYDRA_MAX_CARD_CHANNELS];
  uint8_t   he2channel[MAX_HE_COUNT];
  uint32_t  max_outstanding_tx;
  int32_t   autoTxBufferCount;
  int32_t   autoTxBufferResolution;

  spinlock_t  replyWaitListLock;
  struct list_head replyWaitList;

  /* Structure to hold all of our device specific stuff */

  struct workqueue_struct  *txTaskQ;
  struct work_struct       txWork;

  hydraHostCmdExt txCmdBuffer[KV_MHYDRA_TX_CMD_BUF_SIZE]; /* Control messages */
  Queue           txCmdQueue;

  // busparams
  uint64_t  freq;
  uint8_t   sjw;
  uint8_t   tseg1;
  uint8_t   tseg2;
  uint8_t   samples;

  struct usb_device       *udev;        // save off the usb device pointer
  struct usb_interface    *interface;   // the interface for this device

  MhydraUsbPipeInfo  bulk_in[NUM_IN_PIPES];
  struct workqueue_struct  *memoBulkQ;
  struct work_struct memoBulkWork;
  struct completion  bulk_in_1_data_valid; // bulk_in[1].buffer has valid data

  uint8_t   *bulk_out_buffer;           // the buffer to send data
  size_t    bulk_out_size;              // the size of the send buffer

  uint32_t  bulk_out_MaxPacketSize;

  struct urb *write_urb;                // the urb used to send data
  uint8_t    bulk_out_endpointAddr;    // the address of the bulk out endpoint
  struct completion   write_finished;     // wait for the write to finish

  int32_t  present;                     // if the device is not disconnected

  VCanCardData  *vCard;

  // General data (from Windows version)
  // Time stamping timer frequency in MHz
  uint64_t  hires_timer_fq;
  uint64_t  time_offset_valid;

  uint32_t max_bitrate;
  uint8_t  rxCmdBuffer[sizeof(hydraHostCmdExt)];
  uint32_t rxCmdBufferLevel;

} MhydraCardData;



#endif  /* _MHYDRA_HW_IF_H_ */
