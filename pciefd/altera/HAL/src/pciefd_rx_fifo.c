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

#include "pciefd_hwif.h"
#include "inc/pciefd_rx_fifo_regs.h"

#ifdef PCIEFD_DEBUG
#define DEBUGPRINT(n, args...) printk("<" #n ">" args)
#else
#define DEBUGPRINT(n, args...)
#endif

unsigned int fifoIrqStatus(void * base)
{
  return IORD_RXBUF_IRQ(base);
}

void fifoIrqClear(void * base, unsigned int icl)
{
  IOWR_RXBUF_IRQ(base, icl);
}

void fifoIrqInit(void * base)
{
  IOWR_RXBUF_IEN(base, 0);
  IOWR_RXBUF_IRQ(base, -1);
}

void fifoIrqEnableReceivedDataAvailable(void * base)
{
  uint32_t tmp;
  tmp = IORD_RXBUF_IEN(base);

  IOWR_RXBUF_IEN(base, tmp | RXBUF_IRQ_RA_MSK);
}

void fifoIrqEnableIllegalAccess(void * base)
{
  uint32_t tmp;
  tmp = IORD_RXBUF_IEN(base);

  IOWR_RXBUF_IEN(base, tmp | RXBUF_IRQ_ILLEGAL_ACCESS_MSK);
}

void fifoIrqEnableUnalignedRead(void * base)
{
  uint32_t tmp;
  tmp = IORD_RXBUF_IEN(base);

  IOWR_RXBUF_IEN(base, tmp | RXBUF_IRQ_UNALIGNED_READ_MSK);
}

void fifoIrqEnableMissingTag(void * base)
{
  uint32_t tmp;
  tmp = IORD_RXBUF_IEN(base);

  IOWR_RXBUF_IEN(base, tmp | RXBUF_IRQ_MISSING_TAG_MSK);
}

void fifoIrqEnableUnderflow(void * base)
{
  uint32_t tmp;
  tmp = IORD_RXBUF_IEN(base);

  IOWR_RXBUF_IEN(base, tmp | RXBUF_IRQ_UNDERFLOW_MSK);
}


int fifoPacketCountRx(void * base)
{
  return RXBUF_NPACKETS_GET(IORD_RXBUF_NPACKETS(base));
}

int fifoPacketCountRxMax(void * base)
{
  return RXBUF_NPACKETS_MAX_GET(IORD_RXBUF_NPACKETS(base));
}

int fifoDataAvailable(void * base)
{
  uint32_t data;

  data = IORD_RXBUF_STAT(base);
  DEBUGPRINT(1,"fifoDataAvailable:%08x",data);
  return RXBUF_STAT_AVAILABLE_GET(data);
}

int dmaIdle(void * base)
{
  uint32_t data;

  data = IORD_RXBUF_STAT(base);
  return RXBUF_STAT_DMA_IDLE_GET(data);
}

int fifoOffset(void * base)
{
  uint32_t data;

  data = IORD_RXBUF_STAT(base);

  return RXBUF_STAT_FIFO_PTR_GET(data);
}

int fifoChannelId(void * base)
{
  uint32_t data;

  data = IORD_RXBUF_STAT(base);

  return RXBUF_STAT_CHID_GET(data);
}

int fifoDataAvailableWords(void * base)
{
  uint32_t data;
  int pos;

  data = IORD_RXBUF_STAT(base);

  if (! RXBUF_STAT_AVAILABLE_GET(data)) {
    return 0;
  }

  pos = RXBUF_STAT_LAST_POS_GET(data);

  if (pos & 0x10) {
    return pos & 0x0f; // Header, use offset
  }

  return pos + 4; // Data, add offset
}

int isEOP(void * base)
{
  uint32_t data;

  data = IORD_RXBUF_STAT(base);

  return RXBUF_STAT_EOP_GET(data);
}

int hwSupportDMA(void * base)
{
  uint32_t data;

  data = IORD_RXBUF_STAT(base);

  return RXBUF_STAT_DMA_SUPPORT_GET(data);
}

char * mode_str;

static int noEOP(void * base, char *str)
{
  if (isEOP(base)) {
    DEBUGPRINT(1,"Unexpected EOP (%s)(%s)",mode_str,str);
    return -1;
  }

  return 0;
}

static int expectEop(void * base, char *str)
{
  if (! isEOP(base)) {
    DEBUGPRINT(1,"Missing EOP (%s)(%s)",mode_str,str);
    return -1;
  }

  return 0;
}




void dumpPacket(void *base)
{
  int i;

  // Reset FIFO
  IOWR_RXBUF_CMD(base, RXBUF_CMD_RESET_MSK);

  for(i=0;i<16;i++) {
    DEBUGPRINT(1,"d:%3u:%08x %c ch:%2u",i,IORD(base,RXBUF_DATA_BASE+i), isEOP(base) ? 'E' : ' ', fifoChannelId(base));
  }

  for(i=0;i<4;i++) {
    DEBUGPRINT(1,"h:%3u:%08x %c ch:%2u", i, IORD(base,RXBUF_HEADER_BASE+i), isEOP(base) ? 'E' : ' ', fifoChannelId(base));
  }

  DEBUGPRINT(1,"Last Pos:%lu",RXBUF_STAT_LAST_POS_GET(IORD_RXBUF_STAT(base)));
  DEBUGPRINT(1,"Pos:%lu",RXBUF_STAT_FIFO_PTR_GET(IORD_RXBUF_STAT(base)));
}


typedef enum {
  NO_EOP,
  EXPECT_EOP
} eop_expect_t;

static int readTimestamp(void * base, pciefd_packet_t *packet, eop_expect_t eop)
{
  uint32_t tmp;

  packet->timestamp = IORD_RXBUF_FIFO(base);
  if (noEOP(base,"timestamp lsb")) {
    return -1;
  }

  if(eop == EXPECT_EOP) {
    packet->timestamp |= (uint64_t)(IORD_RXBUF_FIFO(base)) << 32;

    if (expectEop(base,"timestamp msb")) {
      dumpPacket(base);
      return -1;
    }
    tmp = IORD_RXBUF_FIFO_LAST(base);
  }
  else {
    packet->timestamp |= (uint64_t)(IORD_RXBUF_FIFO(base)) << 32;

    if (noEOP(base,"timestamp msb")) {
      dumpPacket(base);
      return -1;
    }
  }

  return 0;
}

// Return size
int readFIFO(VCanCardData *vCard, pciefd_packet_t *packet)
{
  PciCanCardData *hCard = vCard->hwCardData;
  int nwords;
  unsigned int pos;

  mode_str = "readFifo";
  pos = RXBUF_STAT_FIFO_PTR_GET(IORD_RXBUF_STAT(hCard->canRxBuffer));

  if (pos != 28) { // This is the position used for the first header word in the fifo

    uint32_t tmp;
    unsigned int last_pos;
    unsigned int avail;

    tmp      = IORD_RXBUF_STAT(hCard->canRxBuffer);
    last_pos = RXBUF_STAT_LAST_POS_GET(tmp);
    avail    = RXBUF_STAT_AVAILABLE_GET(tmp);

    DEBUGPRINT(1,"################## Error, Pos=%u last=%u avail=%u",pos,last_pos,avail);

    IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
    return -1;
  }

  // Read first two words to determine packet type
  packet->id = IORD_RXBUF_FIFO(hCard->canRxBuffer);
  if(noEOP(hCard->canRxBuffer,"id")) {
    IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
    return -1;
  }

  // Read second word to calculate dlc and number of words to be read
  packet->control = IORD_RXBUF_FIFO(hCard->canRxBuffer);

  // Data
  if (isDataPacket(packet)) {
    int i;
    int dlc;
    int nbytes;

    mode_str = "data packet";

    if (noEOP(hCard->canRxBuffer, "ctrl")) {
      IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
      return -1;
    }

    dlc = getDLC(packet);

    if (isFlexibleDataRateFormat(packet)) {
      DEBUGPRINT(1,"Warning: CAN FD Frame");

      nbytes = dlcToBytesFD(dlc);
    }
    else {
      nbytes = dlcToBytes(dlc);
    }

    if (isRemoteRequest(packet)) {
      nbytes = 0;
    }

    nwords = bytesToWordsCeil(nbytes);

    if (readTimestamp(hCard->canRxBuffer, packet, nwords ? NO_EOP : EXPECT_EOP)) {
      DEBUGPRINT(1,"id=%u dlc=%u bytes=%u words=%u", getId(packet), dlc, nbytes, nwords);
      DEBUGPRINT(1,"i:%x", packet->id);
      DEBUGPRINT(1,"c:%x", packet->control);
      DEBUGPRINT(1,"0:%x", packet->data[0]);
      DEBUGPRINT(1,"1:%x", packet->data[1]);

      IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
      return -1;
    }

    if (nwords) {
      for (i = 0; i < nwords - 1; i++) {
        packet->data[i] = IORD_RXBUF_FIFO(hCard->canRxBuffer);
        if (noEOP(hCard->canRxBuffer,"data")) {
          IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
          return -1;
        }
      }
      packet->data[i] = IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);

      expectEop(hCard->canRxBuffer,"data");
    }

    return 2+nwords;
  }

  // Ack or txrq packet
  else if( isAckPacket(packet)       ||
           isTxrqPacket(packet)      ||
           isEFlushAckPacket(packet) ||
           isEFrameAckPacket(packet)) {
    mode_str = "ack packet";
    if (noEOP(hCard->canRxBuffer,"Ack")) {
      IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
      return -1;
    }

    if (readTimestamp(hCard->canRxBuffer, packet, EXPECT_EOP)) {
      IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
      return -1;
    }
  }

  // Error packet
  else if(isErrorPacket(packet)) {
    mode_str = "error packet";
    if (noEOP(hCard->canRxBuffer,"control")) {
      IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
      return -1;
    }

    if (readTimestamp(hCard->canRxBuffer, packet, EXPECT_EOP)) {
      IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
      return -1;
    }
  }

  // Offset packet
  else if (isOffsetPacket(packet)) {
    DEBUGPRINT(1,"OFFSET PACKET");

    mode_str = "offset";

    if (expectEop(hCard->canRxBuffer,"offset"))	{
      IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
      return -1;
    }

    IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
  }

  // Bus load packet
  else if (isBusLoadPacket(packet)) {

      mode_str = "bus load";

      if (noEOP(hCard->canRxBuffer,"control")) {
	IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
	return -1;
      }

      if (readTimestamp(hCard->canRxBuffer, packet, EXPECT_EOP)) {
	IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
	return -1;
      }
    }

  // Status Packet
  else if (isStatusPacket(packet)) {

    mode_str = "status";

    if (noEOP(hCard->canRxBuffer,"control")) {
      IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
      return -1;
    }

    if (readTimestamp(hCard->canRxBuffer, packet, EXPECT_EOP)) {
      IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
      return -1;
    }

  }

  // Delay report packet
  else if (isDelayPacket(packet)) {
    DEBUGPRINT(1,"DELAY PACKET");

    mode_str = "delay";

    if (expectEop(hCard->canRxBuffer,"delay")) {
      IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
      return -1;
    }
    IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
  }

  // Unknown packet type
  else {
    mode_str = "unknown";
    DEBUGPRINT(2,"Undefined packet %lu",RPACKET_PTYPE_GET(packet->control));

    IORD_RXBUF_FIFO_LAST(hCard->canRxBuffer);
    return -1;
  }

  return 1;
}

void enableDMA(void *base)
{
  uint32_t tmp;

  tmp = IORD_RXBUF_CTRL(base);

  IOWR_RXBUF_CTRL(base, tmp | RXBUF_CTRL_DMAEN_MSK);
}

void disableDMA(void *base)
{
  uint32_t tmp;

  tmp = IORD_RXBUF_CTRL(base);

  IOWR_RXBUF_CTRL(base, tmp & ~RXBUF_CTRL_DMAEN_MSK);
}

void armDMA0(void * base)
{
  IOWR_RXBUF_CMD(base, RXBUF_CMD_DMATRIG0_MSK);
}

void armDMA1(void * base)
{
  IOWR_RXBUF_CMD(base, RXBUF_CMD_DMATRIG1_MSK);
}

void armDMA(void * base, int id)
{
  if (id == 0) {
    IOWR_RXBUF_CMD(base, RXBUF_CMD_DMATRIG0_MSK);
  }
  else {
    IOWR_RXBUF_CMD(base, RXBUF_CMD_DMATRIG1_MSK);
  }
}


