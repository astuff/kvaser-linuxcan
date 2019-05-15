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

/* Kvaser Linux Canlib */

#ifndef _CANLIB_DATA_H_
#define _CANLIB_DATA_H_


#include "linkedlist.h"
#include "vcanevt.h"
#include "canIfData.h"
#include "kcan_ioctl.h"

#include <canlib.h>
#include <canlib_version.h>


#define OPEN_AS_CAN           0
#define OPEN_AS_CANFD_ISO     1
#define OPEN_AS_CANFD_NONISO  2
#define OPEN_AS_LIN           3

#define DEVICE_NAME_LEN 32


typedef LinkedList HandleList;

struct CANops;

// This struct is associated with each handle
// returned by canOpenChannel
typedef struct HandleData
{
  int                fd;
  char               deviceName[DEVICE_NAME_LEN];
  char               deviceOfficialName[150];
  int                channelNr; // Absolute ch nr i.e. it can be >2 for lapcan
  CanHandle          handle;
  unsigned char      isExtended;
  unsigned char      openMode;
  unsigned char      acceptLargeDlc;
  unsigned char      wantExclusive;
  unsigned char      overrideExclusive;
  unsigned char      acceptVirtual;
  unsigned char      requireInitAccess;
  unsigned char      initAccess;
  unsigned char      report_access_errors;
  long               writeTimeout;
  unsigned long      currentTime;
  uint32_t           timerResolution;
  double             timerScale;
  void               (*callback)(canNotifyData *);
  void               (*callback2)(CanHandle hnd, void* ctx, unsigned int event);
  canNotifyData      notifyData;
  int                notifyFd;
  uint32_t           notifyThread_running;
  pthread_t          notifyThread;
  unsigned int       notifyFlags;
  struct CANOps      *canOps;
  int                valid;
  uint32_t           capabilities;
  unsigned char      auto_reset;
} HandleData;


/* Hardware dependent functions that do the actual work with the card
 * The functions are given a HandleData struct */
//typedef struct HWOps
typedef struct CANOps
{
  /* Read a channel and flags e.g canWANT_EXCLUSIVE and return a file descriptor
   * to read messages from.
   */

  canStatus (*openChannel)(HandleData *);
  /* Read a callback function and flags that defines which events triggers it */
  canStatus (*setNotify)(HandleData *hData, void (*callback) (canNotifyData *),
                         kvCallback_t callback2, unsigned int notifyFlags);
  canStatus (*busOn)(HandleData *);
  canStatus (*busOff)(HandleData *);
  canStatus (*setBusParams)(HandleData *hData, long freq, unsigned int tseg1,
                            unsigned int tseg2, unsigned int sjw, unsigned int noSamp,
                            long freq_brs, unsigned int tseg1_brs, unsigned int tseg2_brs,
                            unsigned int sjw_brs, unsigned int syncmode);
  canStatus (*getBusParams)(HandleData *hData, long *freq, unsigned int *tseg1,
                            unsigned int *tseg2, unsigned int *sjw, unsigned int *noSamp,
                            long *freq_brs, unsigned int *tseg1_brs, unsigned int *tseg2_brs,
                            unsigned int *sjw_brs, unsigned int *syncmode);
  canStatus (*reqBusStats) (HandleData *hData);
  canStatus (*getBusStats) (HandleData *hData, canBusStatistics *stat);
  canStatus (*read)(HandleData *, long *, void *, unsigned int *,
                    unsigned int *, unsigned long *);

  canStatus (*readSync)(HandleData *, unsigned long);

  canStatus (*readWait)(HandleData *, long *, void *, unsigned int *,
                        unsigned int *, unsigned long *, long);

  canStatus (*readSpecific)(HandleData *, long, void *, unsigned int *,
                        unsigned int *, unsigned long *);
  canStatus (*readSpecificSkip)(HandleData *, long, void *, unsigned int *,
                        unsigned int *, unsigned long *);
  canStatus (*readSyncSpecific)(HandleData *, long, unsigned long);
  canStatus (*setBusOutputControl)(HandleData *, unsigned int);
  canStatus (*getBusOutputControl)(HandleData *, unsigned int *);
  canStatus (*kvDeviceSetMode) (HandleData *, int);
  canStatus (*kvDeviceGetMode) (HandleData *, int *);
  canStatus (*kvFileGetCount) (HandleData *, int *);
  canStatus (*kvFileGetName) (HandleData *, int, char *, int);
  canStatus (*kvFileDelete) (HandleData *, char *);
  canStatus (*kvFileCopyToDevice) (HandleData *, char *, char *);
  canStatus (*kvFileCopyFromDevice) (HandleData *, char *, char *);
  canStatus (*kvScriptStart) (HandleData *, int);
  canStatus (*kvScriptStop) (HandleData *, int, int);
  canStatus (*kvScriptLoadFile) (HandleData *, int, char *);
  canStatus (*kvScriptUnload) (HandleData *, int);
  canStatus (*accept)(HandleData *, const long, const unsigned int);
  canStatus (*write)(HandleData *, long, void *, unsigned int, unsigned int);
  canStatus (*writeWait)(HandleData *, long, void *,
                         unsigned int, unsigned int, long);
  canStatus (*writeSync)(HandleData *, unsigned long);
  canStatus (*getNumberOfChannels)(HandleData *, int *);
  canStatus (*readTimer)(HandleData *, unsigned long *);
  canStatus (*kvReadTimer)(HandleData *, unsigned int *);
  canStatus (*kvReadTimer64)(HandleData *, uint64_t *);
  canStatus (*readErrorCounters)(HandleData *, unsigned int *,
                                 unsigned int *, unsigned int *);
  canStatus (*readStatus)(HandleData *, unsigned long *);
  canStatus (*kvFlashLeds)(HandleData *, int action, int timeout);
  canStatus (*requestChipStatus)(HandleData *);
  canStatus (*getChannelData)(char *, int, void *, size_t);
  canStatus (*ioCtl)(HandleData * , unsigned int, void *, size_t);
  canStatus (*objbufFreeAll)(HandleData *hData);
  canStatus (*objbufAllocate)(HandleData *hData, int type, int *number);
  canStatus (*objbufFree)(HandleData *hData, int idx);
  canStatus (*objbufWrite)(HandleData *hData, int idx, int id, void* msg,
                           unsigned int dlc, unsigned int flags);
  canStatus (*objbufSetFilter)(HandleData *hData, int idx,
                               unsigned int code, unsigned int mask);
  canStatus (*objbufSetFlags)(HandleData *hData, int idx, unsigned int flags);
  canStatus (*objbufSetPeriod)(HandleData *hData, int idx, unsigned int period);
  canStatus (*objbufSetMsgCount)(HandleData *hData, int idx, unsigned int count);
  canStatus (*objbufSendBurst)(HandleData *hData, int idx, unsigned int burstLen);
  canStatus (*objbufEnable)(HandleData *hData, int idx);
  canStatus (*objbufDisable)(HandleData *hData, int idx);
  canStatus (*resetClock)(HandleData *hData);
  canStatus (*setClockOffset)(HandleData *hData, HandleData *hFrom);
  canStatus (*getCardInfo)(HandleData *hData, VCAN_IOCTL_CARD_INFO *ci);
  canStatus (*getCardInfo2)(HandleData *hData, KCAN_IOCTL_CARD_INFO_2 *ci2);
} CANOps;

#endif
