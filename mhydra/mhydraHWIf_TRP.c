/*
**             Copyright 2019 by Kvaser AB, Molndal, Sweden
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

// Linux Mhydra driver routines for printf and trp messages


#include <linux/version.h>
#include <linux/usb.h>
#include <linux/types.h>
#include <linux/seq_file.h>
#include <linux/math64.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
#include <linux/sched.h>
#else
#include <linux/sched/signal.h>
#endif /* KERNEL_VERSION < 4.11.0 */

// Non system headers
#include "debug.h"
#include "vcanevt.h"
#include "VCanOsIf.h"
#include "mhydraHWIf.h"
#include "mhydraHWIf_TRP.h"
#include "hydra_host_cmds.h"

extern VCanDriverData driverData;  // declared in mhydraHWIf.c


//----------------------------------------------------------------------------
// If you do not define MHYDRA_DEBUG at all, all the debug code will be
// left out.  If you compile with MHYDRA_DEBUG=0, the debug code will
// be present but disabled -- but it can then be enabled for specific
// modules at load time with a 'pc_debug=#' option to insmod.
//
#ifdef MHYDRA_DEBUG
#   define DEBUGPRINT(n, arg)     if (pc_debug >= (n)) { DEBUGOUT(n, arg); }
#else
#   define DEBUGPRINT(n, arg)     if ((n) == 1) { DEBUGOUT(n, arg); }
#endif /* MHYDRA_DEBUG */
//----------------------------------------------------------------------------


#define DM_SLOT_NO_MASK 0x00ff


// local type just for transfer of data between local function calls via void*
typedef struct {
  VCanCardData      *vCard;
  VCanChanData      *vChan;
  // DEVICE_EXTENSION  *myext;
  VCAN_EVENT        *header;
  char*             data;
} CtxForPrintf;


static void free_block (VCanCardData *vCard, int index)
{
  MhydraCardData  *dev = vCard->hwCardData;
  dev->block_use &= ~(1 << index);
}

static int find_free_block (VCanCardData *vCard)
{
  MhydraCardData *dev = vCard->hwCardData;
  int x = ~dev->block_use;
  int i;
  int mask = 1;
  for(i = 0; i < TRP_BLOCK_COUNT; i++) {
    if (x & mask)
      return i;
    mask <<= 1;
  }
  return -1;
}

static int allocate_block (VCanCardData *vCard)
{
  MhydraCardData *dev = vCard->hwCardData;
  int i;

  i = find_free_block(vCard);
  if (i == -1)
    return -1;

  if (!dev->trpBuffer[i]) {
    dev->trpBuffer[i] = kmalloc(TRP_BLOCK_SIZE, GFP_KERNEL);
    if (!dev->trpBuffer[i]) {
      return -1;
    }
    memset(dev->trpBuffer[i], 0, TRP_BLOCK_SIZE);
  }
  dev->block_use |= (1 << i);
  return i;
}

static void allocAndFillContainer (VCanCardData *vCard, hydraHostCmd *tmpCmd)
{
  MhydraCardData  *dev = vCard->hwCardData;
  trpContainer *truck = dev->truck;
  int src = (tmpCmd->cmdIOP.srcChannel << 4) | tmpCmd->cmdIOPSeq.srcHE;
  int i;
  trpContainer *ctn = NULL;
  char *trpData;

  if (!truck) {
    DEBUGPRINT(4, (TXT("allocAndFillContainer - bailing out because truck is NULL\n")));
    for (i = 0; i < sizeof(hydraHostCmd); i++) {
      DEBUGPRINT(4, (TXT("%02x "), ((unsigned char *)tmpCmd)[i]));
    }
    return;
  }
  // Allocate a new container
  // Save hydra message for later,
  // index by srcaddr srcChan and sequence number

  for (i = 0; i < TRP_CONTAINERS_PER_HE; i++) {
    ctn = &truck[src * TRP_CONTAINERS_PER_HE + i];
    if (!ctn->occupied) {
      break;
    }
    ctn = NULL;
  }

  if (!ctn) {
    for (i = 0; i < TRP_CONTAINERS_EXTRA; i++) {
      ctn = &truck[MAX_HE_COUNT * TRP_CONTAINERS_PER_HE + i];
      if (!ctn->occupied) {
        break;
      }
      ctn = NULL;
    }
  }

  if (!ctn) {
    DEBUGPRINT(4, (TXT("ERROR: No room for new container, command dropped\n")));
    return;
  }

  if ((ctn->index = (int8_t)allocate_block(vCard)) < 0) {
    DEBUGPRINT(4, (TXT("ERROR: No room for container data, command dropped\n")));
    return;
  }

  trpData        = dev->trpBuffer[ctn->index];
  ctn->occupied  = 1;
  ctn->cmdIOP    = tmpCmd->heAddress;
  ctn->cmdIOPSeq = tmpCmd->transId;
  ctn->flags     = tmpCmd->printfMsg.flags;
  ctn->len       = tmpCmd->printfMsg.len;
  ctn->timeL     = tmpCmd->printfMsg.timeL;
  ctn->timeH     = tmpCmd->printfMsg.timeH;

  // Copy data for easier access when emptying
  // TRP_BLOCK_SIZE is 1024, a whole hydra command is 32.
  memcpy(trpData, tmpCmd->printfMsg.string, sizeof(tmpCmd->printfMsg.string));
  ctn->curLen = sizeof(tmpCmd->printfMsg.string);
}

static int isContainerFull (trpContainer *ctn)
{
  // Return 0 if container has room for one more TRP_data
  // otherwise return 1

  if (TRP_BLOCK_SIZE - ctn->curLen > sizeof(((htrpData *)0)->data)) {
    return 0;
  } else {
    return 1;
  }
}

static int isTransactionDone (trpContainer *ctn)
{
  // Return 1 if container contains the amount of data described in
  // original command, return 0 otherwise.

  if (ctn->len && (ctn->curLen >= ctn->len)) {
    return 1;
  } else {
    return 0;
  }
}

static void releaseContainer (VCanCardData *vCard, trpContainer *ctn)
{
  free_block(vCard, ctn->index);
  ctn->occupied = 0;
}

// Empty container for more incomming data
static void emptyContainer (trpContainer *ctn)
{
  ctn->len   -= ctn->curLen;
  ctn->curLen = 0;
}

static trpContainer *fillAndGetContainer (VCanCardData *vCard,
                                          hydraHostCmd *tmpCmd)
{
  MhydraCardData  *dev = vCard->hwCardData;
  trpContainer *truck = dev->truck;
  int src = (tmpCmd->cmdIOP.srcChannel << 4) | tmpCmd->cmdIOPSeq.srcHE;
  int i;
  int len;
  trpContainer *ctn = NULL;

  if(!truck) {
    DEBUGPRINT(4, (TXT("fillAndGetContainer, - bailing out because truck is NULL\n")));
    return NULL;
  }

  // Find container matching srcAddr, srcChan, sequenceNum
  // return NULL if not found

  for (i = 0; i < TRP_CONTAINERS_PER_HE; i++) {
    ctn = &truck[src * TRP_CONTAINERS_PER_HE + i];
    // This actually checks destination as well...
    if (ctn->occupied                      &&
        (tmpCmd->heAddress == ctn->cmdIOP) &&
        (tmpCmd->transId   == ctn->cmdIOPSeq)) {
      break;
    }
    ctn = NULL;
  }

  if (!ctn) {
    for (i = 0; i < TRP_CONTAINERS_EXTRA; i++) {
      ctn = &truck[MAX_HE_COUNT * TRP_CONTAINERS_PER_HE + i];
      // This actually checks destination as well...
      if (ctn->occupied                      &&
          (tmpCmd->heAddress == ctn->cmdIOP) &&
          (tmpCmd->transId   == ctn->cmdIOPSeq)) {
        break;
      }
      ctn = NULL;
    }
  }

  if (!ctn) {
    DEBUGPRINT(4, (TXT("ERROR: Did not find original printf container, dumping.\n")));
    return NULL;
  }

  // Check that new data will fit
  if (isContainerFull(ctn)) {
    DEBUGPRINT(4, (TXT("ERROR: Container full, dumping.\n")));
    return NULL;
  }

  // Add data from TRP_ and return pointer to container
  len = tmpCmd->trpDataMsg.len;
  memcpy(dev->trpBuffer[ctn->index] + ctn->curLen,
         tmpCmd->trpDataMsg.data, len);
  ctn->curLen += (uint16_t)len;

  return ctn;
}

static void store_dm (VCanCardData *vCard, uint8_t he, uint8_t type,
               uint16_t seq, uint16_t id,
               uint32_t timeLo, uint32_t timeHi,
               uint16_t length, char *buf)
{
  MhydraCardData *dev = vCard->hwCardData;
  dmHeader dm;
  char *src;
  int dst;
  int size, space;
  int i;

  if (!dev->dmBuffer) {
    DEBUGPRINT(1, (TXT("store_dm: Buffer not initialized, dropping packet\n")));
    return;
  }
  space = dev->dmRead - dev->dmWrite;
  if (space <= 0) {
    space += DM_BUFFER_SIZE;
  }
  if ((size_t)space > sizeof(dm) + length) {
    dm.length = length + sizeof(dm);
    dm.seq    = seq;
    dm.time   = (((uint64_t) timeHi) << 32) | timeLo;
    dm.he     = he;
    dm.type   = type;
    dm.id     = id;

    size = sizeof(dm);
    src  = (char *)&dm;
    dst  = dev->dmWrite;
    for(i = 0; i < 2; i++) {
      space = DM_BUFFER_SIZE - dst;
      if (space < size) {
        memcpy(&dev->dmBuffer[dst], src, space);
        size -= space;
        src  += space;
        dst   = 0;
      }
      memcpy(&dev->dmBuffer[dst], src, size);
      dst += size;

      size = length;    // Set up for second copy
      src  = buf;
    }

    dev->dmWrite = dst;
  }
}

static int device_messages_read (VCanCardData *vCard, deviceMessage *mc)
{
  MhydraCardData *dev = vCard->hwCardData;
  int dmRead, dmWrite;
  char *src, *dst;
  int size;

  dmRead  = dev->dmRead;
  dmWrite = dev->dmWrite;

  mc->payloadLen = 0;

  if (!dev->dmBuffer) {
    return 0;
  }

  src  = &dev->dmBuffer[dmRead];
  dst  = mc->payload;
  size = dmWrite - dmRead;
  if (size < 0) {
    size           = DM_BUFFER_SIZE - dmRead;
    mc->payloadLen = size;
    memcpy(dst, src, size);
    src  = &dev->dmBuffer[0];
    dst += size;
    size = dmWrite;
  }
  memcpy(dst, src, size);

  mc->payloadLen += size;
  dev->dmRead = dmWrite;

  return 0;
} // device_messages_read


// convert printf-msg type into flags, only use upper bits...
static unsigned short printf_message_type_to_flag(unsigned char type)
{
  unsigned short flags = 0;

  if (type == 0)      flags |= DM_FLAG_PRINTF;
  else if (type == 1) flags |= DM_FLAG_DEBUG;
  else if (type == 2) flags |= DM_FLAG_ERROR;

  return  flags;
}


int devhnd_dispatchhw_printf (VCanCardData *vCard,
                              VCanChanData *vChan,
                              VCAN_EVENT *printf_evHeader,
                              char* data)
{
  if (vCard && vCard->cardPresent) {
    vCanDispatchPrintfEvent(vCard, vChan, printf_evHeader, data);
  }
  return 0;
}

static uint64_t apply_softsync(uint64_t timestamp_in_ns, VCanCardData *vCard, MhydraChanData *hChd, bool make_correction)
{
  if (make_correction) {
    timestamp_in_ns += hChd->timestamp_correction_value;
  }

  if (vCard->softsync_running) {
    timestamp_in_ns = softSyncLoc2Glob(vCard, timestamp_in_ns);
  }

  return timestamp_in_ns;
}

static uint64_t convert_to_10us(uint64_t timestamp_in_ns)
{
  return div_u64(timestamp_in_ns + 4999, 10000);
}

void printf_msg(VCanCardData *vCard, hydraHostCmd *cmd)
{
  unsigned char  src;
  VCAN_EVENT     e;
  int            chan;
  deviceMessage  *devmsg;
  dmHeader       *dmh;
  unsigned short flags        = 0;
  uint64_t       tmpTimeStamp = 0;
  MhydraCardData *dev = vCard->hwCardData;

  if (!vCard) return;

  devmsg = kmalloc(sizeof(*devmsg), GFP_KERNEL);
  if (devmsg == NULL) {
    DEBUGPRINT(4, (TXT("devmsg not allocated\n")));
    return;
  }
  memset(devmsg, 0, sizeof(*devmsg));

  dmh = (dmHeader*)devmsg->payload;

  if (cmd->printfMsg.flags & CMDPRINTF_TPFOLLOW_FLAG) {
    // One or more TP will follow this message
    allocAndFillContainer(vCard, cmd);
  } else {
    // Complete single message transfer, no following TP
    src = (cmd->cmdIOP.srcChannel << 4) | cmd->cmdIOPSeq.srcHE;
    store_dm(vCard, src, cmd->cmdIOP.dstAddr >> 4, cmd->cmdIOPSeq.transId,
             dev->he2script[src],  // Only script ID's so far
             cmd->printfMsg.timeL, cmd->printfMsg.timeH,
             cmd->printfMsg.len, cmd->printfMsg.string);

    // we have a complete message, so read it from the dm-buffer and
    // chop it up and dispatch it.
    device_messages_read(vCard, devmsg);

    // convert type to flags
    flags = printf_message_type_to_flag(dmh->type);

    // convert micro-second-time to nano-second-time
    tmpTimeStamp = dmh->time * 1000;

    // distribute dm-message to all channels
    e.tag                                     = V_PRINTF_MSG;
    e.tagData.printfMsg.type                  = VCAN_EVT_PRINTF_HEADER;
    e.tagData.printfMsg.printf_header.flags   = flags;
    e.tagData.printfMsg.printf_header.slot    = dmh->id & DM_SLOT_NO_MASK;
    e.tagData.printfMsg.printf_header.datalen = (unsigned short)devmsg->payloadLen - sizeof(dmHeader);

    for (chan = 0; chan < vCard->nrChannels; chan++) {
      VCanChanData *vChan = vCard->chanData[chan];
      if (!vChan) {
        kfree(devmsg);
        return;
      }
      {
        MhydraChanData *hChd = (MhydraChanData *)vChan->hwChanData;
        uint64_t  filtered_timestamp;

        // update timestamp
        filtered_timestamp = tmpTimeStamp;
        filtered_timestamp = apply_softsync(filtered_timestamp, vCard, hChd, true);
        e.timeStamp = convert_to_10us(filtered_timestamp);

        devhnd_dispatchhw_printf(vCard, vChan, &e, &devmsg->payload[sizeof(dmHeader)]);
      }
    }
  }
  kfree(devmsg);
}

// For now, the only difference between a fatal error message and
// a normal one is that the time is set to 0.
void fatal_msg (VCanCardData *vCard, hydraHostCmd *tmpCmd)
{
  unsigned char src;
  VCanChanData      *vChan       = NULL;
  VCAN_EVENT        e;
  int               chan;
  int               i;
  dmHeader          *dmh;
  deviceMessage     *devmsg;
  unsigned short    flags        = 0;
  uint64_t          tmpTimeStamp = 0;
  MhydraCardData    *dev = vCard->hwCardData;

  if (!vCard) return;

  devmsg = kmalloc(sizeof(*devmsg), GFP_KERNEL);
  if (devmsg == NULL) {
    DEBUGPRINT(4, (TXT("devmsg not allocated\n")));
    return;
  }
  memset(devmsg, 0, sizeof(*devmsg));

  dmh = (dmHeader*)devmsg->payload;

  src = (tmpCmd->cmdIOP.srcChannel << 4) | tmpCmd->cmdIOPSeq.srcHE;
  store_dm(vCard, src, tmpCmd->cmdIOP.dstAddr >> 4, tmpCmd->cmdIOPSeq.transId,
           dev->he2script[src],  // Only script ID's so far
           0, 0,
           sizeof(tmpCmd->fatalError.text), tmpCmd->fatalError.text);

  // we have a complete message, so read it from the dm-buffer and
  // chop it up and dispatch it.
  device_messages_read(vCard, devmsg);

  // convert type to flags
  flags = printf_message_type_to_flag(dmh->type);

  // convert micro-second-time to nano-second-time
  tmpTimeStamp = dmh->time * 1000;

  // distribute dm-message to all channels
  e.tag                                     = V_PRINTF_MSG;
  e.tagData.printfMsg.type                  = VCAN_EVT_PRINTF_HEADER;
  e.tagData.printfMsg.printf_header.flags   = flags;
  e.tagData.printfMsg.printf_header.slot    = dmh->id & DM_SLOT_NO_MASK;
  e.tagData.printfMsg.printf_header.datalen = (unsigned short)devmsg->payloadLen - sizeof(dmHeader);

  for (i=0; i < vCard->nrChannels; i++) {
    chan = i;
    vChan = vCard->chanData[chan];
    if (!vChan) {
      kfree(devmsg);
      return;
    }
    {
      MhydraChanData *hChd = (MhydraChanData *)vChan->hwChanData;
      uint64_t  filtered_timestamp;

      // update timestamp
      filtered_timestamp = tmpTimeStamp;
      filtered_timestamp = apply_softsync(filtered_timestamp, vCard, hChd, true);
      e.timeStamp = convert_to_10us(filtered_timestamp);

      devhnd_dispatchhw_printf(vCard, vChan, &e, &devmsg->payload[sizeof(dmHeader)]);
    }
  }
  kfree(devmsg);
}

void trp_msg (VCanCardData *vCard, hydraHostCmd *tmpCmd)
{
  trpContainer      *ctn;
  char              *trpData;
  unsigned char     src;
  int               chan;
  int               i;
  VCanChanData      *vChan        = NULL;
  VCAN_EVENT        e;
  deviceMessage     *devmsg;
  dmHeader          *dmh;
  unsigned short    flags         = 0;
  uint64_t          tmpTimeStamp  = 0;
  MhydraCardData    *dev = vCard->hwCardData;

  if (!vCard) return;

  devmsg = kmalloc(sizeof(*devmsg), GFP_KERNEL);
  if (devmsg == NULL) {
    DEBUGPRINT(4, (TXT("devmsg not allocated\n")));
    return;
  }
  memset(devmsg, 0, sizeof(*devmsg));

  dmh = (dmHeader*)devmsg->payload;

  // Get corresponding container and add data
  ctn = fillAndGetContainer(vCard, tmpCmd);
  if (ctn == NULL) {
    DEBUGPRINT(4, (TXT("Orphan trp recieved in filter_print_msg... cmdNo = %d\n"),
                   tmpCmd->cmdNo));
    kfree(devmsg);
    return;
  }

  trpData = dev->trpBuffer[ctn->index];

  if ((tmpCmd->trpDataMsg.flags & TRPDATA_EOF_FLAG) ||
      isTransactionDone(ctn)) {
    src = (tmpCmd->cmdIOP.srcChannel << 4) | tmpCmd->cmdIOPSeq.srcHE;
    store_dm(vCard, src, (ctn->cmdIOP >> 4) & 0x03, ctn->cmdIOPSeq & 0xfff /*ctn->cmdIOPSeq.transId*/ ,
             dev->he2script[src],  // Only script ID's so far
             ctn->timeL, ctn->timeH,
             ctn->len, trpData);

    // we have a full dm so, read it and dispatch
    device_messages_read(vCard, devmsg);

    // convert type to flags
    flags = printf_message_type_to_flag(dmh->type);

    // convert micro-second-time to nano-second-time
    tmpTimeStamp = dmh->time * 1000;

    e.tag = V_PRINTF_MSG;
    e.tagData.printfMsg.type = VCAN_EVT_PRINTF_HEADER;
    e.tagData.printfMsg.printf_header.flags = flags;
    e.tagData.printfMsg.printf_header.slot    = dmh->id & DM_SLOT_NO_MASK;
    e.tagData.printfMsg.printf_header.datalen = (unsigned short)(devmsg->payloadLen - sizeof(dmHeader));
    for (i=0; i < vCard->nrChannels; i++) {
      chan = i;
      vChan = vCard->chanData[chan];
      if (!vChan) {
        kfree(devmsg);
        return;
      }

      {
        MhydraChanData *hChd = (MhydraChanData *)vChan->hwChanData;
        uint64_t  filtered_timestamp;

        // update timestamp
        filtered_timestamp = tmpTimeStamp;
        filtered_timestamp = apply_softsync(filtered_timestamp, vCard, hChd, true);
        e.timeStamp = convert_to_10us(filtered_timestamp);

        devhnd_dispatchhw_printf(vCard, vChan, &e, &devmsg->payload[sizeof(dmHeader)]);
      }
    }

    releaseContainer(vCard, ctn);
  } else {
    // Streams bigger than PRINT_BUFFER_SIZE should continue
    if (isContainerFull(ctn)) {
      src = (ctn->cmdIOP >> 6) | (ctn->cmdIOPSeq >> 12);
      store_dm(vCard, src, (ctn->cmdIOP >> 4) & 0x03, ctn->cmdIOPSeq & 0xfff,
               dev->he2script[src],  // Only script ID's so far
               ctn->timeL, ctn->timeH,
               ctn->curLen, trpData);
      emptyContainer(ctn);
    }
  }
  kfree(devmsg);
}

static int mhydra_local_device_messages_subscription(VCanCardData *vCard, int32_t in_entity, int32_t requestFlags)
// Windows hydra.c device_messages_subscription
{
  int r = 0;
  hydraHostCmd cmd, reply;
  //  int size   = 0;
  //  int script = 0;
  //  uint8_t  id;
  uint8_t  he, broadcast_class;
  int n, count;
  int entity = in_entity;
  MhydraCardData  *dev = vCard->hwCardData;

  count = 1;
  if ((entity & DM_ALL) == DM_ALL) {  // ALL
    entity &= ~DM_ALL;
    switch (entity & 0x0f000000) {
      case 0x01000000:                  // HE
        entity |= 0x0f;                 // BROADCAST
        break;
      case 0x02000000:                  // SCRIPT
        count = HYDRA_MAX_SCRIPTS;
        break;
      default:
        return VCAN_STAT_BAD_PARAMETER;
    }
  }

  for(n = 0; n < count; n++) {
    switch (entity & 0x0f000000) {
      case 0x01000000:                   // HE
        he = (entity & 0x7f) + n;
        break;
      case 0x02000000:                   // SCRIPT
        he = dev->script2he[(entity & 0x7f) + n];
        if (he == ILLEGAL_HE) {
          continue;
        }
        break;
      default:
        return VCAN_STAT_BAD_PARAMETER;
    }

    switch (entity & DM_ENTITY_MASK_BROADCAST) {
      case DM_TRAFFIC_BROADCAST:
        broadcast_class = 0;
        // Never listen to standard traffic messages from CAN channels.
        // subscribing to "traffic" on a CAN-he means that you get all can messages from that channel.
        // subscribing to another CAN-he is what we want.
        if (dev->he2channel[he] != 0xff) {
          DEBUGPRINT(4, (TXT("Subscribe to traffic invalid he 0x%x (0x%x)\n"),he,dev->he2channel[he]));
          return VCAN_STAT_BAD_PARAMETER;
        }
        break;
      case DM_DEBUG_BROADCAST:
        broadcast_class = 1;
        break;
      case DM_ERROR_BROADCAST:
        broadcast_class = 2;
        break;
      default:
        return VCAN_STAT_BAD_PARAMETER;
    }

    if (entity & 0x00401000) {
      // Handle debug group and level here!
    }

    if (!dev->dmBuffer) {
      int size = DM_BUFFER_SIZE +
        (MAX_HE_COUNT * TRP_CONTAINERS_PER_HE + TRP_CONTAINERS_EXTRA) *
        sizeof(trpContainer);
      dev->dmBuffer = kmalloc(size, GFP_KERNEL);
      if (!dev->dmBuffer) {
        return VCAN_STAT_NO_MEMORY;
      }
      memset(dev->dmBuffer, 0, size);
      dev->truck = (trpContainer *)(dev->dmBuffer + DM_BUFFER_SIZE);
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.cmdNo                 = CMD_LISTEN_TO_HE_REQ;
    setDST(&cmd, ROUTER_HE);
    cmd.listenToHeReq.enable  = (requestFlags == DM_SUBSCRIBE);
    cmd.listenToHeReq.he      = he;
    cmd.listenToHeReq.channel = broadcast_class;

    memset(&reply, 0, sizeof(reply));

    r = mhydra_send_and_wait_reply(vCard, &cmd, &reply,
                                   CMD_LISTEN_TO_HE_RESP,
                                   0,
                                   SKIP_ERROR_EVENT);

    if (r != VCAN_STAT_OK) {
      DEBUGPRINT(4, (TXT("%s: listen_to_he - sync_obj failed, stat=%d\n"), driverData.deviceName, r ));
      return VCAN_STAT_TIMEOUT;
    }
  }

  return r;
}  // mhydra_local_device_messages_subscription

int mhydra_device_messages_subscription (VCanOpenFileNode *fileNodePtr, KCAN_IOCTL_DEVICE_MESSAGES_SUBSCRIPTION_T *sc)
// Windows ntentry.c   case KCAN_IOCTL_DEVICE_MESSAGES_SUBSCRIPTION in special_ioctl_handler()
{
  int vstat;
  uint32_t requestFlags = 0;
  uint32_t entity       = 0;
  unsigned long openLock_irqFlags;
  VCanChanData *vChan = fileNodePtr->chanData;
  VCanCardData *vCard = vChan->vCard;

  entity = DM_TRAFFIC_BROADCAST | DM_SCRIPT(sc->slot);

  if (sc->requestFlags & KCAN_SCRIPT_REQUEST_TEXT_SUBSCRIBE)   requestFlags = DM_SUBSCRIBE;
  if (sc->requestFlags & KCAN_SCRIPT_REQUEST_TEXT_UNSUBSCRIBE) requestFlags = DM_UNSUBSCRIBE;

  if ((sc->slot & DM_ENTITY_MASK_TYPE) == KCAN_SCRIPT_ERROR_SLOT) {
    entity = DM_ERROR_BROADCAST | DM_HE((sc->slot & DM_ENTITY_MASK_ID));
  }
  if ((sc->slot & DM_ENTITY_MASK_TYPE) == KCAN_SCRIPT_INFO_SLOT) {
    entity = DM_DEBUG_BROADCAST | DM_HE((sc->slot & DM_ENTITY_MASK_ID));
  }

  // for now do not unsubscribe to device, since we don't
  // know who else is subscribing, except when just one open handle.
  vstat = VCAN_STAT_OK;
  if (requestFlags != DM_UNSUBSCRIBE) {
    vstat = mhydra_local_device_messages_subscription(vCard, entity, requestFlags);
  }

  if (vstat == VCAN_STAT_OK) {
    spin_lock_irqsave(&vChan->openLock, openLock_irqFlags);

    if ((entity & DM_ENTITY_MASK_BROADCAST) == DM_DEBUG_BROADCAST) {
      // set he and debug
      if (requestFlags == DM_SUBSCRIBE) {
        fileNodePtr->debug_subscriptions_mask = 1;
      }
      else if (requestFlags == DM_UNSUBSCRIBE) {
        fileNodePtr->debug_subscriptions_mask = 0;
      }
      DEBUGPRINT(1, (TXT("New debug subscription: 0x%x\n"), fileNodePtr->debug_subscriptions_mask));
    }
    else if ((entity & DM_ENTITY_MASK_BROADCAST) == DM_ERROR_BROADCAST) {
      // set he and error
      if (requestFlags == DM_SUBSCRIBE) {
        fileNodePtr->error_subscriptions_mask = 1;
      }
      else if (requestFlags == DM_UNSUBSCRIBE) {
        fileNodePtr->error_subscriptions_mask = 0;
      }
      DEBUGPRINT(1, (TXT("New error subscription: 0x%x\n"), fileNodePtr->error_subscriptions_mask));
    }
    else { // printf from script
      if (requestFlags == DM_SUBSCRIBE) {
        if ((entity & DM_ENTITY_MASK_ID) == DM_ALL) fileNodePtr->message_subscriptions_mask |= DM_ALL;
        else fileNodePtr->message_subscriptions_mask |= 1<<(entity & DM_ENTITY_MASK_ID);
      }
      else if (requestFlags == DM_UNSUBSCRIBE) {
        if ((entity & DM_ENTITY_MASK_ID) == DM_ALL) fileNodePtr->message_subscriptions_mask &= DM_ALL;
        else fileNodePtr->message_subscriptions_mask &= ~(1<<(entity & DM_ENTITY_MASK_ID));
      }
      DEBUGPRINT(2, (TXT("New message subscription: 0x%x (0x%x)\n"), fileNodePtr->message_subscriptions_mask, entity));
    }

    spin_unlock_irqrestore(&vChan->openLock, openLock_irqFlags);
  }

  return vstat;
} // mhydra_device_messages_subscription
