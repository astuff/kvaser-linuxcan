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

#ifndef __PCIEFD_H__
#define __PCIEFD_H__

#include "inc/pciefd_regs.h"
#include "inc/pciefd_packet_defs.h"
#include "HAL/inc/pciefd_packet.h"
#include "VCanOsIf.h"

extern char *pciefd_ctrl_reg_names[];

int statResetRequest(void * base);
int statInResetMode(void * base);
int statTransmitterIdle(void * base);
int statAbortRequest(void * base);
int statPendingRequest(void *base);
int statIdle(void *base);
int statFlushRequest(void * base);

int istatTransmitterFlushDone(void * base);
void irqClearTransmitterFlushDone(void * base);

int fifoPacketCountTx(void * base);
int fifoPacketCountTxMax(void * base);

int hwSupportCanFD(void * base);
int hwSupportCAP(void * base);

int readIrqReceiverOverflow(void * base);
unsigned int irqReceiverOverflow(unsigned int irq);

int readIrqReceiverUnaligned(void * base);
unsigned int irqReceiverUnaligned(unsigned int irq);

int readIrqTransmitterOverflow(void * base);
unsigned int irqTransmitterOverflow(unsigned int irq);
unsigned int irqTransmitterEmpty(unsigned int irq);
int irqDisableTransmitterEmpty(void * base);

int readIrqTransmitterUnderflow(void * base);
unsigned int irqTransmitterUnderflow(unsigned int irq);

int readIrqTransmitterUnaligned(void * base);
unsigned int irqTransmitterUnaligned(unsigned int irq);

void irqClearReceivedDataAvailable(void * base);
unsigned int irqReceivedDataAvailable(unsigned int irq);
void irqEnableReceivedDataAvailable(void * base);
void irqEnableTransmitterError(void * base);
void irqEnableTransmitFifoEmpty(void * base);
void irqClearTransmitFifoEmpty(void * base);

void irqClearReceivedUnaligned(void * base);

int irqEnabled(void * base);
unsigned int irqStatus(void * base);
void irqClear(void * base, unsigned int icl);
int istatCheck(void * base, uint32_t icl);
void irqInit(void * base);

void enableErrorPackets(void * base);
void disableErrorPackets(void * base);

void enableNonIsoMode(void * base);
void disableNonIsoMode(void * base);

void enableSlaveMode(void * base);
void disableSlaveMode(void * base);
unsigned int getSlaveMode(void * base);

void enableErrorPassiveMode(void * base);
void disableErrorPassiveMode(void * base);
unsigned int getErrorPassiveMode(void * base);

void enableClassicCanMode(void * base);
void disableClassicCanMode(void * base);
unsigned int getClassicCanMode(void * base);

void speedUp(int canid, int value);
void speedNominal(int canid);

void fpgaStatusRequest(void * base, int seq_no);
void fpgaFlushTransmit(void * base);
void fpgaFlushAll(void * base, int seq_no);
void resetErrorCount(void * base);

int writeFIFO(VCanChanData *vChd, pciefd_packet_t *packet);

void setLedStatus(void * base, int on);
void ledOn(void * base);
void ledOff(void * base);

#endif
