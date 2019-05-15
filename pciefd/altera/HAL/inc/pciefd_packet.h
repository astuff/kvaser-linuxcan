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

#ifndef __PCIEFD_PACKET_H__
#define __PCIEFD_PACKET_H__

#include "VCanOsIf.h"
#include "inc/pciefd_packet_defs.h"

// Packet handling
#define USE_CAN_FD (1)

#if USE_CAN_FD
#define NR_OF_DATA_WORDS 16
#else
#define NR_OF_DATA_WORDS 2
#endif


typedef struct {
  uint32_t id;
  uint32_t control;
  uint32_t data[NR_OF_DATA_WORDS];
  uint64_t timestamp;
} pciefd_packet_t;


int isDataPacket(pciefd_packet_t *packet);
int isAckPacket(pciefd_packet_t *packet);
int isTxrqPacket(pciefd_packet_t *packet);
int isErrorPacket(pciefd_packet_t *packet);
int isEFlushAckPacket(pciefd_packet_t *packet);
int isEFrameAckPacket(pciefd_packet_t *packet);
int isOffsetPacket(pciefd_packet_t *packet);
int isBusLoadPacket(pciefd_packet_t *packet);
int isStatusPacket(pciefd_packet_t *packet);
int isDelayPacket(pciefd_packet_t *packet);

int packetChannelId(pciefd_packet_t *packet);

int statusInResetMode(pciefd_packet_t *packet);
int statusResetModeChangeDetected(pciefd_packet_t *packet);
int statusReset(pciefd_packet_t *packet);
int statusOverflow(pciefd_packet_t *packet);
int statusErrorWarning(pciefd_packet_t *packet);
int statusErrorPassive(pciefd_packet_t *packet);
int statusBusOff(pciefd_packet_t *packet);
int statusReceiveErrorCount(pciefd_packet_t *packet);
int statusTransmitErrorCount(pciefd_packet_t *packet);
int statusCmdSeqNo(pciefd_packet_t *packet);

int getChannelId(pciefd_packet_t *packet);

int dlcToBytes(int dlc);
int dlcToBytesFD(int dlc);
int bytesToDlc(int bytes);

#define PACKET_WORDS(length)  (2 + (3+length)/4)
#define FD_PACKET_WORDS  (2 + (3+packet_size[packet_size_index])/4)

int bytesToWordsCeil(int bytes);

// +----------------------------------------------------------------------------
// Received data packets
// +----------------------------------------------------------------------------
int getId(pciefd_packet_t *packet);
void setId(pciefd_packet_t *packet, int id);

int isErrorPassive(pciefd_packet_t *packet);
int receiverOverflow(pciefd_packet_t *packet);
int errorWarning(pciefd_packet_t *packet);

int isFlexibleDataRateFormat(pciefd_packet_t *packet);
int isAlternateBitRate(pciefd_packet_t *packet);
int errorStateIndicated(pciefd_packet_t *packet);
int getSRR(pciefd_packet_t *packet);
int isExtendedId(pciefd_packet_t *packet);
int isRemoteRequest(pciefd_packet_t *packet);
int getDLC(pciefd_packet_t *packet);
void setDLC(pciefd_packet_t *packet, int dlc);

int getSeqNo(pciefd_packet_t *packet);

int getOffsetQuantas(pciefd_packet_t *packet);
int getOffsetNbits(pciefd_packet_t *packet);

int getBusLoad(pciefd_packet_t *packet);

// +----------------------------------------------------------------------------
// | Received ack packets
// +----------------------------------------------------------------------------
int getAckSeqNo(pciefd_packet_t *packet);
int isFlushed(pciefd_packet_t *packet);
int isNack(pciefd_packet_t *packet);
int isABL(pciefd_packet_t *packet);
int isControlAck(pciefd_packet_t *packet);

// +----------------------------------------------------------------------------
// | Received txrq packets
// +----------------------------------------------------------------------------
int getTxrqSeqNo(pciefd_packet_t *packet);

// +----------------------------------------------------------------------------
// | Received error packets
// +----------------------------------------------------------------------------
int getTransmitErrorCount(pciefd_packet_t *packet);
int getReceiveErrorCount(pciefd_packet_t *packet);

etype_t getErrorType(pciefd_packet_t *packet);
int isTransmitError(pciefd_packet_t *packet);
int getErrorField(pciefd_packet_t *packet);
int getErrorFieldPos(pciefd_packet_t *packet);
void printErrorCode(pciefd_packet_t *packet);

// +----------------------------------------------------------------------------
// | Transmit packets
// +----------------------------------------------------------------------------
void setRemoteRequest(pciefd_packet_t *packet);
void setExtendedId(pciefd_packet_t *packet);
void setBitRateSwitch(pciefd_packet_t *packet);
void setAckRequest(pciefd_packet_t *packet);
void setTxRequest(pciefd_packet_t *packet);
void setSingleShotMode(pciefd_packet_t *packet);

int setupBaseFormat(pciefd_packet_t *packet, int id, int dlc, int seqno);
int setupExtendedFormat(pciefd_packet_t *packet, int id, int dlc, int seqno);
int setupFDBaseFormat(pciefd_packet_t *packet, int id, int dlc, int seqno);
int setupFDExtendedFormat(pciefd_packet_t *packet, int id, int dlc, int seqno);

#endif
