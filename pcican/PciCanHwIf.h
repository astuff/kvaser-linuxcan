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

/* Kvaser CAN driver PCIcan hardware specific parts
** PCIcan definitions
*/

#ifndef _PCICAN_HW_IF_H_
#define _PCICAN_HW_IF_H_

/*****************************************************************************/
/* defines */
/*****************************************************************************/

// Use this to set alternate implementation.
//#define TRY_RT_QUEUE

#define DEVICE_NAME_STRING "pcican"
#define MAX_CARD_CHANNELS 4
#define MAX_DRIVER_CHANNELS 128
#define PCICAN_VENDOR  0x10e8
#define PCICAN_ID 0x8406
#define KVASER_VENDOR 0x1a07    // KVASER
#define PCIECAN_ID 0x8          // KVASER PCIEcan
// Standard value: Pushpull  (OCTP1|OCTN1|OCTP0|OCTN0|OCM1)
#define OCR_DEFAULT_STD 0xDA
// For Galathea piggyback.
#define OCR_DEFAULT_GAL 0xDB

#define MAX_ERROR_COUNT 128
#define ERROR_RATE      800
#define PCICAN_BYTES_PER_CIRCUIT 0x20


/*****************************************************************************/
/* Xilinx                                                                    */
/*****************************************************************************/

//
// These register values are valid for revision 14 of the Xilinx logic.
//
#define XILINX_OUTA         0   // Bit 7 used to write bits to serial memory DS2430.
#define XILINX_INA          1   // Bit 7 used to read back bits from serial memory DS2430.
#define XILINX_CTRLA        2   // Sets the function of the Xilinx pins normally set to zero.
#define XILINX_UNUSED       3
#define XILINX_OUTB         4   // Bit 7 used to reset the SJA-1000.
#define XILINX_INB          5   // Bit 7 used to read back the reset line value.
#define XILINX_CTRLB        6   // Sets the function of the Xilinx pins normally set to zero.
#define XILINX_VERINT       7   // Lower nibble simulate interrupts, high nibble version number.

#define XILINX_PRESUMED_VERSION     14


/* Channel specific data */
typedef struct PciCanChanData
{
    /* Ports and addresses */
    void __iomem       *sja1000;
    void __iomem       *xilinxAddressOut;
    void __iomem       *xilinxAddressCtrl;
    void __iomem       *xilinxAddressIn;

    spinlock_t         lock;
#if !defined(TRY_RT_QUEUE)
    struct work_struct txTaskQ;
#else
    struct workqueue_struct *txTaskQ;
    struct work_struct txWork;
#endif
    DALLAS_CONTEXT      chanEeprom;
    VCanChanData       *vChan;
    uint32_t            outstanding_tx;
    CAN_MSG            *currentTxMsg;
    uint32_t            flushing;
} PciCanChanData;

/*  Cards specific data */
typedef struct PciCanCardData {
    /* Ports and addresses */
    void __iomem       *sjaBase;
    void __iomem       *xilinx;
    void __iomem       *pciIf;
    int                irq;
    DALLAS_CONTEXT     cardEeprom;
} PciCanCardData;

#endif  /* _PCICAN_HW_IF_H_ */
