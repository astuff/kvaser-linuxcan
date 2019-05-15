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

#ifndef _CANIF_DATA_H_
#define _CANIF_DATA_H_

#include <linux/types.h>
#include <vcanevt.h>

/* Used as interface datatype */
typedef struct FilterData
{
  __u32 cmdNr;
  __s32 cmdNrMask;
  __u8 chanNr;
  __s8 chanNrMask;
  __u8 flags;
  __s8 flagsMask;
  __u8 isPass;
} FilterData;

typedef struct CanIfStat
{
    __u32   overruns;
    __u16 statSize;
    __u16 sendQL;
    __u16 rcvQL;
} CanIfStat;


typedef struct {
    __s32 freq;
    __u32 sjw;
    __u32 tseg1;
    __u32 tseg2;
    __u32 samp3;
  // After bit rate switch (CAN FD)
    __s32 freq_brs;
    __u32 sjw_brs;
    __u32 tseg1_brs;
    __u32 tseg2_brs;
} VCanBusParams;

typedef struct {
    __u8 eventMask;
  //__u8 msgFlags;
  //__u8 flagsMask;
    __u64 stdId;
    __u64 stdMask;
    __u64 extId;
    __u64 extMask;
    __u8 typeBlock;
} VCanMsgFilter;

typedef struct {
    __u32  stdData;
    __u32  stdRemote;
    __u32  extData;
    __u32  extRemote;
    __u32  errFrame;
    __u32  busLoad;
    __u32  overruns;
    __u32  bitCount;
    __u32  timestamp;
    __u32  bitTime100ns;
} VCanBusStatistics;

typedef struct {
  __u32 buffer_number;
  __u32 acc_code;
  __u32 acc_mask;
  __u32 flags;
  __u32 type;
  __u32 period;
//  __u32 reserved[16];      // For future usage
} KCanObjbufAdminData;


typedef struct {
  __u32 buffer_number;
  __u32 id;
  __u32 dlc;
  __u8 data[128]; // CAN FD/EF
  __u32 flags;
} KCanObjbufBufferData;

//data structure for read functions
#define READ_SPECIFIC_NO_SKIP          0
#define READ_SPECIFIC_SKIP_MATCHING    1
#define READ_SPECIFIC_SKIP_PRECEEDING  2
typedef struct {
  unsigned long timeout;
  struct {
    long          id;
    unsigned char skip;
  } specific;
}VCanRead;

typedef struct {
  unsigned char busStatus;
  unsigned char txErrorCounter;
  unsigned char rxErrorCounter;
}VCanRequestChipStatus;

typedef struct {
  uint32_t sw;
  uint32_t hw;
}VCanOverrun;

typedef struct {
  int action;
  int timeout;
}VCanFlashLeds;

typedef struct {
  uint8_t block;
  uint8_t rx_bus;
  uint8_t tx_bus;
}VCanLoopbackSettings;

typedef struct {
  int timeout;
}VCanMemoConfigMode;

#define VCANOPEN_LOCKED -1
#define VCANOPEN_OPENED -2
typedef struct {
  unsigned int  chanNr;
  unsigned char override_exclusive;
  int           retval;
}VCanOpen;

#define VCANINITACCESS_FAIL -1
typedef struct {
  unsigned char require_init_access;
  unsigned char wants_init_access;
  int           retval;
}VCanInitAccess;

#define VCANSETBUSPARAMS_NO_INIT_ACCESS -1
typedef struct {
  VCanBusParams bp;
  int           retval;
}VCanSetBusParams;

#define VCANSETBUSOUTPUTCONTROL_NO_INIT_ACCESS -1
typedef struct {
  int  silent;
  int  retval;
}VCanSetBusOutputControl;

#define VCANSETBUSON_FAIL -1
typedef struct {
  int  reset_time;
  int  retval;
}VCanSetBusOn;

// Other missing driver stuff
//===========================================================================

typedef struct {
  unsigned char   second;
  unsigned char   minute;
  unsigned char   hour;
  unsigned char   day;
  unsigned char   month;
  unsigned char   year;
} KCANY_MEMO_INFO_RTC;

typedef struct {
  VCanRead *read;
  VCAN_EVENT *msg;
} VCAN_IOCTL_READ_T;


#endif /* _CANIF_DATA_H_ */
