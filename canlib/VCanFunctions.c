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

/*  Kvaser Linux Canlib VCan layer functions */
#define _GNU_SOURCE // This is required for recursive mutex support in pthread

#include <stdint.h>
#include "vcan_ioctl.h"
#include "kcan_ioctl.h"
#include "canIfData.h"
#include "canlib_data.h"
#include "vcanevt.h"
#include "dlc.h"

#include <canlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>

#include "VCanMemoFunctions.h"
#include "VCanScriptFunctions.h"
#include "VCanFunctions.h"
#include "VCanFuncUtil.h"
#include "debug.h"


#   if DEBUG
#      define DEBUGPRINT(args) printf args
#   else
#      define DEBUGPRINT(args)
#   endif

// Standard resolution for the time stamps and canReadTimer is
// 1 ms, i.e. 100 VCAND ticks.
#define DEFAULT_TIMER_FACTOR 100


static uint32_t capabilities_table[][2] = {
  {VCAN_CHANNEL_CAP_EXTENDED_CAN,        canCHANNEL_CAP_EXTENDED_CAN},
  {VCAN_CHANNEL_CAP_BUSLOAD_CALCULATION, canCHANNEL_CAP_BUS_STATISTICS},
  {VCAN_CHANNEL_CAP_ERROR_COUNTERS,      canCHANNEL_CAP_ERROR_COUNTERS},
  {VCAN_CHANNEL_CAP_CAN_DIAGNOSTICS,     canCHANNEL_CAP_CAN_DIAGNOSTICS},
  {VCAN_CHANNEL_CAP_SEND_ERROR_FRAMES,   canCHANNEL_CAP_GENERATE_ERROR},
  {VCAN_CHANNEL_CAP_TXREQUEST,           canCHANNEL_CAP_TXREQUEST},
  {VCAN_CHANNEL_CAP_TXACKNOWLEDGE,       canCHANNEL_CAP_TXACKNOWLEDGE},
  {VCAN_CHANNEL_CAP_VIRTUAL,             canCHANNEL_CAP_VIRTUAL},
  {VCAN_CHANNEL_CAP_SIMULATED,           canCHANNEL_CAP_SIMULATED},
  {VCAN_CHANNEL_CAP_REMOTE,              canCHANNEL_CAP_RESERVED_1},
  {VCAN_CHANNEL_CAP_CANFD,               canCHANNEL_CAP_CAN_FD},
  {VCAN_CHANNEL_CAP_CANFD_NONISO,        canCHANNEL_CAP_CAN_FD_NONISO},
  {VCAN_CHANNEL_CAP_SILENTMODE,          canCHANNEL_CAP_SILENT_MODE},
  {VCAN_CHANNEL_CAP_SINGLE_SHOT,         canCHANNEL_CAP_SINGLE_SHOT},
  {VCAN_CHANNEL_CAP_HAS_LOGGER,          canCHANNEL_CAP_LOGGER},
  {VCAN_CHANNEL_CAP_HAS_REMOTE,          canCHANNEL_CAP_REMOTE_ACCESS},
  {VCAN_CHANNEL_CAP_HAS_SCRIPT,          canCHANNEL_CAP_SCRIPT},
  {VCAN_CHANNEL_CAP_LIN_HYBRID,          canCHANNEL_CAP_LIN_HYBRID},
  {VCAN_CHANNEL_CAP_DIAGNOSTICS,         canCHANNEL_CAP_DIAGNOSTICS}
};


// If there are more handles than this, the rest will be
// handled by a linked list.
#define MAX_ARRAY_HANDLES 64

static HandleData  *handleArray[MAX_ARRAY_HANDLES];
static HandleList  *handleList;
static CanHandle   handleMax   = MAX_ARRAY_HANDLES;
#if defined(PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP)
static pthread_mutex_t handleMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#elif defined(PTHREAD_RECURSIVE_MUTEX_INITIALIZER)
static pthread_mutex_t handleMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
#else
#error Canlib requires GNUC.
#endif

static uint32_t get_capabilities (uint32_t cap);
static uint32_t convert_channel_info_flags (uint32_t vflags);

#define ERROR_WHEN_NEQ 0
#define ERROR_WHEN_LT  1
static int check_args (void* buf, uint32_t buf_len, uint32_t limit, uint32_t method);

//******************************************************
// Compare handles
//******************************************************
static int hndCmp (const void *hData1, const void *hData2)
{
  return ((HandleData *)(hData1))->handle ==
         ((HandleData *)(hData2))->handle;
}


//******************************************************
// Find handle in list
//******************************************************
HandleData * findHandle (CanHandle hnd)
{
  HandleData dummyHandleData, *found;
  dummyHandleData.handle = hnd;

  if (hnd < 0) {
    return NULL;
  }

  pthread_mutex_lock(&handleMutex);
  if (hnd < MAX_ARRAY_HANDLES) {
    found = handleArray[hnd];
  } else {
    found = listFind(&handleList, &dummyHandleData, &hndCmp);
  }
  pthread_mutex_unlock(&handleMutex);

  return found;
}


//******************************************************
// Remove handle from list
//******************************************************
HandleData * removeHandle (CanHandle hnd)
{
  HandleData dummyHandleData, *found;
  dummyHandleData.handle = hnd;

  if (hnd < 0) {
    return NULL;
  }

  pthread_mutex_lock(&handleMutex);
  if (hnd < MAX_ARRAY_HANDLES) {
    found = handleArray[hnd];
    handleArray[hnd] = NULL;
  } else {
    found = listRemove(&handleList, &dummyHandleData, &hndCmp);
  }
  pthread_mutex_unlock(&handleMutex);

  return found;
}

//******************************************************
// Insert handle in list
//******************************************************
CanHandle insertHandle (HandleData *hData)
{
  CanHandle hnd = -1;
  int i;

  pthread_mutex_lock(&handleMutex);

  for(i = 0; i < MAX_ARRAY_HANDLES; i++) {
    if (!handleArray[i]) {
      hData->handle = hnd = (CanHandle)i;
      handleArray[i] = hData;
      break;
    }
  }

  if (i == MAX_ARRAY_HANDLES) {
    if (listInsertFirst(&handleList, hData) == 0) {
      hData->handle = hnd = handleMax++;
    }
  }

  pthread_mutex_unlock(&handleMutex);

  return hnd;
}

void foreachHandle (int (*func)(const CanHandle))
{
  int i;
  pthread_mutex_lock(&handleMutex);
  for (i = 0; i < MAX_ARRAY_HANDLES; i++) {
    CanHandle hnd = -1;
    if (!handleArray[i]) {
      continue;
    }
    hnd = handleArray[i]->handle;
    if (func(hnd) != canOK) {
      DEBUGPRINT((TXT("foreachHandle func failed.\n")));
    }
  }
  if (handleMax > MAX_ARRAY_HANDLES) {
    HandleList *hndListNext = handleList;
    while (hndListNext != NULL) {
      HandleData *found;
      HandleList *hndList = hndListNext;
      hndListNext = hndList->next;
      if ((found = hndList->elem)) {
        if (func(found->handle) != canOK) {
          DEBUGPRINT((TXT("foreachHandle func failed.\n")));
        }
      }
    }
    handleMax = MAX_ARRAY_HANDLES;
  }
  pthread_mutex_unlock(&handleMutex);
}


//======================================================================
// Set CAN FD mode
//======================================================================
static canStatus kCanSetOpenMode (HandleData *hData)
{
  int ret;
  KCAN_IOCTL_OPEN_MODE_T open_data;

  memset(&open_data, 0, sizeof(KCAN_IOCTL_OPEN_MODE_T));
  open_data.mode = hData->openMode;
  open_data.action = CAN_MODE_SET;

  ret = ioctl(hData->fd, KCAN_IOCTL_OPEN_MODE, &open_data);
  if (ret != 0) {
    DEBUGPRINT((TXT("kCanSetOpenMode failed!\n")));
    return errnoToCanStatus(errno);
  }

  if (open_data.status != CAN_OPEN_MODE_SUCCESS) {
    DEBUGPRINT((TXT("Open mode (CAN FD / CAN / LIN) Mismatch -> can't open channel. Open mode status %d\n"), open_data.status));

    return canERR_NOTFOUND;
  }

  return canOK;
}

static void notify (HandleData *hData, VCAN_EVENT *msg, uint32_t *prev_busoff)
{
  canNotifyData *notifyData = &hData->notifyData;
  unsigned int   cb2_notify = 0;
  uint32_t       curr_busoff;

  if (msg->tag == V_CHIP_STATE) {
    struct s_vcan_chip_state *chipState = &msg->tagData.chipState;

    if (chipState->busStatus & CHIPSTAT_BUSOFF) {
      curr_busoff = 1;
    } else {
      curr_busoff = 0;
    }

    if (hData->notifyFlags & canNOTIFY_BUSONOFF) {
      if (curr_busoff != *prev_busoff) {
        cb2_notify   |= canNOTIFY_BUSONOFF;
        *prev_busoff  = curr_busoff;
      }
    }

    if (hData->notifyFlags & canNOTIFY_STATUS) {
      cb2_notify |= canNOTIFY_STATUS;
    }

    //just 1 event
    if (cb2_notify & canNOTIFY_BUSONOFF) {
      notifyData->eventType = canEVENT_BUSONOFF;
    } else if (cb2_notify & canNOTIFY_STATUS) {
      notifyData->eventType = canEVENT_STATUS;
    }

    notifyData->info.status.busStatus      = chipState->busStatus;
    notifyData->info.status.txErrorCounter = chipState->txErrorCounter;
    notifyData->info.status.rxErrorCounter = chipState->rxErrorCounter;
    notifyData->info.status.time           = (msg->timeStamp * 10UL) / (hData->timerResolution);

  } else if (msg->tag == V_RECEIVE_MSG) {
    if (msg->tagData.msg.flags & VCAN_MSG_FLAG_ERROR_FRAME) {
      if (hData->notifyFlags & canNOTIFY_ERROR) {
        notifyData->eventType        = canEVENT_ERROR;
        notifyData->info.busErr.time = (msg->timeStamp * 10UL) / (hData->timerResolution);
        cb2_notify                   = canNOTIFY_ERROR;
      }
    } else if (msg->tagData.msg.flags & VCAN_MSG_FLAG_TXACK) {
      if (hData->notifyFlags & canNOTIFY_TX) {
        notifyData->eventType    = canEVENT_TX;
        notifyData->info.tx.id   = msg->tagData.msg.id & ~EXT_MSG;
        notifyData->info.tx.time = (msg->timeStamp * 10UL) / (hData->timerResolution);
        cb2_notify               = canNOTIFY_TX;
      }
    } else {
      if (hData->notifyFlags & canNOTIFY_RX) {
        notifyData->eventType    = canEVENT_RX;
        notifyData->info.rx.id   = msg->tagData.msg.id & ~EXT_MSG;
        notifyData->info.rx.time = (msg->timeStamp * 10UL) / (hData->timerResolution);
        cb2_notify               = canNOTIFY_RX;
      }
    }
  }

  if (!cb2_notify) {
    return;
  }
  
  if (hData->callback) {
    hData->callback(notifyData);
  } else if (hData->callback2) {
    hData->callback2(hData->handle, notifyData->tag, cb2_notify);
  }
}

//======================================================================
// Notification thread
//======================================================================
static void *vCanNotifyThread (void *arg)
{
  HandleData        *hData = (HandleData *)arg;
  int                ret;
  VCAN_IOCTL_READ_T  ioctl_read_arg;
  VCAN_EVENT         msg;
  VCanRead           read;
  uint32_t           busoff;

  memset(&read, 0, sizeof(VCanRead));

  read.timeout        = 1;  //1 ms
  ioctl_read_arg.msg  = &msg;
  ioctl_read_arg.read = &read;

  //are we buson or busoff?
  {
    VCanRequestChipStatus  chip_status;
    
    if (ioctl(hData->notifyFd, VCAN_IOC_GET_CHIP_STATE, &chip_status)) {
      pthread_exit(0); // When this thread is cancelled, ioctl will be interrupted by a signal.
    }
    
    if (chip_status.busStatus & CHIPSTAT_BUSOFF) {
      busoff = 1;
    } else {
      busoff = 0;
    }
  }
  
  //wait 1ms before we say that the thread is running in order to not miss any events
  ret = ioctl(hData->notifyFd, VCAN_IOC_RECVMSG, &ioctl_read_arg);

  hData->notifyThread_running = 1;

  if (ret == 0) {
    notify(hData, &msg, &busoff);
  } else {
    if (errno != EAGAIN) {
      pthread_exit(0); // When this thread is cancelled, ioctl will be interrupted by a signal.
    }
  }

  read.timeout = 50;
  
  while (1) {
    pthread_testcancel();

    hData->notifyThread_running = 1;
    ret = ioctl(hData->notifyFd, VCAN_IOC_RECVMSG, &ioctl_read_arg);

    if (ret == 0) {
      notify(hData, &msg, &busoff);
    } else {
      if (errno != EAGAIN) {
        pthread_exit(0); // When this thread is cancelled, ioctl will be interrupted by a signal.
      }
    }
  }
}


//======================================================================
// vCanSetNotify
//======================================================================
static canStatus vCanSetNotify (HandleData *hData,
                                void (*callback) (canNotifyData *),
                                kvCallback_t callback2,
                                unsigned int notifyFlags)
{
  int           ret;
  VCanMsgFilter filter;
  unsigned char transId;

  if (hData->notifyFd == canINVALID_HANDLE) {
    // Open an fd to read events from
    hData->notifyFd = open(hData->deviceName, O_RDONLY);

    if (hData->notifyFd == canINVALID_HANDLE) {
      goto error_open;
    }

    //notiFd must have same transId as fd, otherwise canNOTIFY_TX won't work
    ret = ioctl(hData->fd, VCAN_IOC_GET_TRANSID, &transId);
    if (ret != 0) {
      goto error_ioc;
    }

    ret = ioctl(hData->notifyFd, VCAN_IOC_SET_TRANSID, &transId);
    if (ret != 0) {
      goto error_ioc;
    }
  } else { //must kill thread in order to set new params.
    pthread_cancel(hData->notifyThread);

    // Wait for thread to finish
    pthread_join(hData->notifyThread, NULL);
  }

  hData->notifyThread_running = 0;
  hData->notifyFlags          = notifyFlags;

  // Set filters
  memset(&filter, 0, sizeof(VCanMsgFilter));
  filter.eventMask = 0;

  if ((notifyFlags & canNOTIFY_RX) ||
      (notifyFlags & canNOTIFY_TX) ||
      (notifyFlags & canNOTIFY_ERROR)) {
    filter.eventMask |= V_RECEIVE_MSG;
  }

  if ((notifyFlags & canNOTIFY_STATUS) ||
      (notifyFlags & canNOTIFY_BUSONOFF)) {
    filter.eventMask |= V_CHIP_STATE;
  }

  ret = ioctl(hData->notifyFd, VCAN_IOC_SET_MSG_FILTER, &filter);
  if (ret != 0) {
    goto error_ioc;
  }

  if (notifyFlags & canNOTIFY_TX) {
    int par = 1;
    ret = ioctl(hData->notifyFd, VCAN_IOC_SET_TXACK, &par);
    if (ret != 0) {
      goto error_ioc;
    }
  }

  ret = ioctl(hData->fd, VCAN_IOC_FLUSH_RCVBUFFER, NULL);
  if (ret != 0) {
    goto error_ioc;
  }

  ret = pthread_create(&hData->notifyThread, NULL, vCanNotifyThread, hData);
  if (ret != 0) {
    goto error_ioc;
  }

  hData->callback  = callback;
  hData->callback2 = callback2;

  //wait for the thread to start
  {
    uint32_t n = 0;
    while (1) {
      if (hData->notifyThread_running) {
        break;
      }

      n++;
      if (n > 10) {
        break;
      }
      usleep(1000);
    }
  }

  return canOK;

error_ioc:
  close(hData->notifyFd);
  hData->notifyFd = canINVALID_HANDLE;
 error_open:
  return canERR_NOTFOUND;
}


//======================================================================
// vCanOpenChannel
//======================================================================
static canStatus vCanOpenChannel (HandleData *hData)
{
  int             ret = 0;
  VCanMsgFilter   filter;
  uint32_t        capability = 0;

  hData->fd = open(hData->deviceName, O_RDONLY);
  if (hData->fd == canINVALID_HANDLE) {
    return canERR_NOTFOUND;
  }

  {
    VCanOpen my_arg;
    my_arg.retval = 0;

    my_arg.chanNr              = hData->channelNr;
    my_arg.override_exclusive  = hData->overrideExclusive;

    if (hData->wantExclusive) {
      ret = ioctl(hData->fd, VCAN_IOC_OPEN_EXCL, &my_arg);
    } else {
      ret = ioctl(hData->fd, VCAN_IOC_OPEN_CHAN, &my_arg);
    }

    if (ret) {
      close(hData->fd);
      return canERR_NOTFOUND;
    }

    if (my_arg.retval == VCANOPEN_LOCKED) {
      close(hData->fd);
      if (my_arg.override_exclusive) {
        return canERR_NO_ACCESS;
      } else {
        return canERR_NOCHANNELS;
      }
    } else if (my_arg.retval == VCANOPEN_OPENED) {
      close(hData->fd);
      return canERR_NOCHANNELS;
    }
  }

  {
    VCanInitAccess my_arg;
    my_arg.retval = 0;

    my_arg.require_init_access = hData->requireInitAccess;
    my_arg.wants_init_access   = hData->initAccess;

    ret = ioctl(hData->fd, VCAN_IOC_OPEN_INIT_ACCESS, &my_arg);

    if (ret) {
      close(hData->fd);
      return canERR_NOTFOUND;
    }

    if (my_arg.retval == VCANINITACCESS_FAIL) {
      close(hData->fd);
      return canERR_NO_ACCESS;
    }

    hData->report_access_errors = 0;
  }

  ret = ioctl(hData->fd, VCAN_IOC_GET_CHAN_CAP, &capability);
  if (ret) {
    close(hData->fd);
    return canERR_NOTFOUND;
  }

  // save capabilities, this is a channel property rather than a handle proprty
  hData->capabilities = capability;

  if (!hData->acceptVirtual) {
    if (capability & VCAN_CHANNEL_CAP_VIRTUAL) {
      close(hData->fd);
      return canERR_NOTFOUND;
    }
  }

  if (capability & VCAN_CHANNEL_CAP_CANFD) {
    if ( canOK != kCanSetOpenMode(hData) ) {
      close(hData->fd);
      return canERR_NOTFOUND;
    }
  }

  memset(&filter, 0, sizeof(VCanMsgFilter));
  // Read only CAN messages
  filter.eventMask = V_RECEIVE_MSG | V_TRANSMIT_MSG;
  ret = ioctl(hData->fd, VCAN_IOC_SET_MSG_FILTER, &filter);

  hData->timerScale = 1.0 / DEFAULT_TIMER_FACTOR;
  hData->timerResolution = (unsigned int)(10.0 / hData->timerScale);

  return canOK;
}

//======================================================================
// vCanBusOn
//======================================================================
static canStatus vCanBusOn (HandleData *hData)
{
  int          ret;
  VCanSetBusOn my_arg;

  my_arg.retval     = 0;
  my_arg.reset_time = (uint32_t)hData->auto_reset;

  ret = ioctl(hData->fd, VCAN_IOC_BUS_ON, &my_arg);

  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  if (my_arg.retval == VCANSETBUSON_FAIL) {
    return canERR_INTERNAL;
  }
  
  return canOK;
}


//======================================================================
// vCanBusOff
//======================================================================
static canStatus vCanBusOff (HandleData *hData)
{
  int ret;

  ret = ioctl(hData->fd, VCAN_IOC_BUS_OFF, NULL);
  if (ret != 0) {
    return canERR_INVHANDLE;
  }

  return canOK;
}

//======================================================================
// vCanSetBusparams
//======================================================================
static canStatus vCanSetBusParams (HandleData *hData,
                                   long freq,
                                   unsigned int tseg1,
                                   unsigned int tseg2,
                                   unsigned int sjw,
                                   unsigned int noSamp,
                                   long freq_brs,
                                   unsigned int tseg1_brs,
                                   unsigned int tseg2_brs,
                                   unsigned int sjw_brs,
                                   unsigned int syncmode)
{
  VCanSetBusParams my_arg;
  int              ret;

  (void)syncmode; // Unused.

  my_arg.retval = 0;

  my_arg.bp.freq    = (signed long)freq;
  my_arg.bp.sjw     = sjw;
  my_arg.bp.tseg1   = tseg1;
  my_arg.bp.tseg2   = tseg2;
  my_arg.bp.samp3   = noSamp;   // This variable is # of samples inspite of name!

  my_arg.bp.freq_brs  = (signed long)freq_brs;
  my_arg.bp.sjw_brs   = sjw_brs;
  my_arg.bp.tseg1_brs = tseg1_brs;
  my_arg.bp.tseg2_brs = tseg2_brs;

  ret = ioctl(hData->fd, VCAN_IOC_SET_BITRATE, &my_arg);

  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  if ((my_arg.retval == VCANSETBUSPARAMS_NO_INIT_ACCESS) && (hData->report_access_errors == 1)) {
    return canERR_NO_ACCESS;
  }

  return canOK;
}


//======================================================================
// vCanGetBusParams
//======================================================================
static canStatus vCanGetBusParams(HandleData *hData,
                                  long *freq,
                                  unsigned int *tseg1,
                                  unsigned int *tseg2,
                                  unsigned int *sjw,
                                  unsigned int *noSamp,
                                  long *freq_brs,
                                  unsigned int *tseg1_brs,
                                  unsigned int *tseg2_brs,
                                  unsigned int *sjw_brs,
                                  unsigned int *syncmode)
{
  VCanBusParams busParams;
  int ret;

  ret = ioctl(hData->fd, VCAN_IOC_GET_BITRATE, &busParams);

  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  if (freq)     *freq     = busParams.freq;
  if (sjw)      *sjw      = busParams.sjw;
  if (tseg1)    *tseg1    = busParams.tseg1;
  if (tseg2)    *tseg2    = busParams.tseg2;
  if (noSamp)   *noSamp   = busParams.samp3;

  if (freq_brs)  *freq_brs  = busParams.freq_brs;
  if (sjw_brs)   *sjw_brs   = busParams.sjw_brs;
  if (tseg1_brs) *tseg1_brs = busParams.tseg1_brs;
  if (tseg2_brs) *tseg2_brs = busParams.tseg2_brs;

  if (syncmode) *syncmode = 0;

  return canOK;
}

//======================================================================
// vCanReqBusStats
//======================================================================
static canStatus vCanReqBusStats(HandleData *hData)
{
  if (ioctl(hData->fd, VCAN_IOC_REQ_BUS_STATS, NULL)) {
    return errnoToCanStatus(errno);
  }
  return canOK;
}

//======================================================================
// vCanGetBusStats
//======================================================================
static canStatus vCanGetBusStats(HandleData *hData, canBusStatistics *stat)
{
  VCanBusStatistics tstat;
  memset(stat, 0, sizeof(canBusStatistics));
  if (ioctl(hData->fd, VCAN_IOC_GET_BUS_STATS, &tstat)) {
    return errnoToCanStatus(errno);
  }

  stat->stdData = tstat.stdData;
  stat->stdRemote = tstat.stdRemote;
  stat->extData = tstat.extData;
  stat->extRemote = tstat.extRemote;
  stat->errFrame = tstat.errFrame;

  stat->busLoad = tstat.busLoad;
  stat->overruns = tstat.overruns;

  return canOK;
}


//======================================================================
// vCanReadInternal
//======================================================================
static canStatus vCanReadInternal (HandleData *hData, unsigned int iotcl_cmd,
                                   VCanRead *readOpt, long *id,
                                   void *msgPtr, unsigned int *dlc,
                                   unsigned int *flag, unsigned long *time)
{
  int i;
  int ret;
  VCAN_IOCTL_READ_T ioctl_read_arg;
  VCAN_EVENT msg;

  ioctl_read_arg.msg = &msg;
  ioctl_read_arg.read = readOpt;

  while (1) {
    ret = ioctl(hData->fd, iotcl_cmd, &ioctl_read_arg);
    if (ret != 0) {
      return errnoToCanStatus(errno);
    }
    // Receive CAN message
    if (msg.tag == V_RECEIVE_MSG) {
      unsigned int flags;
      int count = 0;

      if (msg.tagData.msg.id & EXT_MSG) {
        flags = canMSG_EXT;
      } else {
        flags = canMSG_STD;
      }
      if (msg.tagData.msg.flags & VCAN_MSG_FLAG_ERROR_FRAME)
        flags = canMSG_ERROR_FRAME;
      if (msg.tagData.msg.flags & VCAN_MSG_FLAG_FDF)
        flags |= canFDMSG_FDF;
      if (msg.tagData.msg.flags & VCAN_MSG_FLAG_BRS)
        flags |= canFDMSG_BRS;
      if (msg.tagData.msg.flags & VCAN_MSG_FLAG_ESI)
        flags |= canFDMSG_ESI;
      if (msg.tagData.msg.flags & VCAN_MSG_FLAG_OVERRUN)
        flags |= canMSGERR_HW_OVERRUN | canMSGERR_SW_OVERRUN;
      if (msg.tagData.msg.flags & VCAN_MSG_FLAG_REMOTE_FRAME)
        flags |= canMSG_RTR;
      if (msg.tagData.msg.flags & VCAN_MSG_FLAG_TX_START)
        flags |= canMSG_TXRQ;

      if (flags & canFDMSG_FDF) {
        count = dlc_dlc_to_bytes_fd (msg.tagData.msg.dlc);
      } else {
        count = dlc_dlc_to_bytes_classic (msg.tagData.msg.dlc);
      }

      if (msg.tagData.msg.flags & VCAN_MSG_FLAG_SSM_NACK) {
        flags |= canMSG_TXNACK;
      } else if (msg.tagData.msg.flags & VCAN_MSG_FLAG_SSM_NACK_ABL) {
        flags |= canMSG_TXNACK;
        flags |= canMSG_ABL;
      } else {
        if (msg.tagData.msg.flags & VCAN_MSG_FLAG_TXACK) {
          flags |= canMSG_TXACK;
        }
      }


      // Copy data
      if (msgPtr) {
        for (i = 0; i < count; i++)
          ((unsigned char *)msgPtr)[i] = msg.tagData.msg.data[i];
      }

      // MSb is extended flag
      if (id)   *id   = msg.tagData.msg.id & ~EXT_MSG;
      if (dlc) {
        if (hData->acceptLargeDlc && !(flags & canFDMSG_FDF)) {
          *dlc = msg.tagData.msg.dlc;
        }
        else {
          *dlc  = count;
        }
      }
      if (time) *time = (msg.timeStamp * 10UL) / (hData->timerResolution) ;
      if (flag) *flag = flags;

      break;
    }
  }

  return canOK;
}


//======================================================================
// vCanRead
//======================================================================
static canStatus vCanRead (HandleData    *hData,
                           long          *id,
                           void          *msgPtr,
                           unsigned int  *dlc,
                           unsigned int  *flag,
                           unsigned long *time)
{
  VCanRead read;

  memset(&read, 0, sizeof(VCanRead));
  read.timeout = 0;
  return vCanReadInternal(hData, VCAN_IOC_RECVMSG, &read,
                          id, msgPtr, dlc, flag, time);
}

//======================================================================
// vCanReadSync
//======================================================================
static canStatus vCanReadSync (HandleData    *hData,
                               unsigned long timeout)
{
  int ret;

  ret = ioctl(hData->fd, VCAN_IOC_RECVMSG_SYNC, &timeout);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  return canOK;
}

//======================================================================
// vCanReadSpecific
//======================================================================
static canStatus vCanReadSpecific (HandleData    *hData,
                                   long          id,
                                   void          *msgPtr,
                                   unsigned int  *dlc,
                                   unsigned int  *flag,
                                   unsigned long *time)
{
  VCanRead read;

  read.specific.skip = READ_SPECIFIC_SKIP_MATCHING;
  read.specific.id   = id;
  read.timeout       = 0;

  return vCanReadInternal(hData, VCAN_IOC_RECVMSG_SPECIFIC, &read,
                          NULL, msgPtr, dlc, flag, time);
}

//======================================================================
// vCanReadSpecificSkip
//======================================================================
static canStatus vCanReadSpecificSkip (HandleData    *hData,
                                       long          id,
                                       void          *msgPtr,
                                       unsigned int  *dlc,
                                       unsigned int  *flag,
                                       unsigned long *time)
{
  VCanRead read;

  read.specific.skip = READ_SPECIFIC_SKIP_PRECEEDING;
  read.specific.id   = id;
  read.timeout       = 0;

  return vCanReadInternal(hData, VCAN_IOC_RECVMSG_SPECIFIC, &read,
                          NULL, msgPtr, dlc, flag, time);
}

//======================================================================
// vCanReaSyncdSpecific
//======================================================================
static canStatus vCanReadSyncSpecific (HandleData    *hData,
                                       long          id,
                                       unsigned long timeout)
{
  VCanRead read;

  read.specific.skip = READ_SPECIFIC_NO_SKIP;
  read.specific.id   = id;
  read.timeout       = timeout;

  return vCanReadInternal(hData, VCAN_IOC_RECVMSG_SPECIFIC, &read,
                          NULL, NULL, NULL, NULL, NULL);
}

//======================================================================
// vCanReadWait
//======================================================================
static canStatus vCanReadWait (HandleData    *hData,
                               long          *id,
                               void          *msgPtr,
                               unsigned int  *dlc,
                               unsigned int  *flag,
                               unsigned long *time,
                               long           timeout)
{
  VCanRead read;

  memset(&read, 0, sizeof(VCanRead));
  read.timeout = timeout;
  return vCanReadInternal(hData, VCAN_IOC_RECVMSG, &read,
                          id, msgPtr, dlc, flag, time);
}


//======================================================================
// vCanSetBusOutputControl
//======================================================================
static canStatus vCanSetBusOutputControl (HandleData   *hData,
                                          unsigned int drivertype)
{
  VCanSetBusOutputControl my_arg;
  int                     ret;
  my_arg.retval = 0;

  my_arg.retval = 0;

  switch (drivertype) {
    case canDRIVER_NORMAL:
      my_arg.silent = 0;
      break;
    case canDRIVER_SILENT:
      my_arg.silent = 1;
      break;
    default:
      return canERR_PARAM;
  }

  ret = ioctl(hData->fd, VCAN_IOC_SET_OUTPUT_MODE, &my_arg);

  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  if ((my_arg.retval == VCANSETBUSOUTPUTCONTROL_NO_INIT_ACCESS) && (hData->report_access_errors == 1)) {
    return canERR_NO_ACCESS;
  }

  return canOK;
}


//======================================================================
// vCanGetBusOutputControl
//======================================================================
static canStatus vCanGetBusOutputControl (HandleData *hData,
                                          unsigned int *drivertype)
{
  int silent;
  int ret;

  ret = ioctl(hData->fd, VCAN_IOC_GET_OUTPUT_MODE, &silent);

  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  switch (silent) {
  case 0:
    *drivertype = canDRIVER_NORMAL;
    break;
  case 1:
    *drivertype = canDRIVER_SILENT;
    break;
  default:
    break;
  }

  return canOK;
}

//======================================================================
// vCanSetDeviceMode
//======================================================================
static canStatus vCanSetDeviceMode (HandleData *hData,
                                            int mode)
{
  int ret;

  ret = ioctl(hData->fd, VCAN_IOC_SET_DEVICE_MODE, &mode);
  DEBUGPRINT((TXT("VCAN_IOC_SET_DEVICE_MODE 0x%x, ret %d\n"), mode, ret));
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  return canOK;
}


//======================================================================
// vCanGetDeviceMode
//======================================================================
static canStatus vCanGetDeviceMode (HandleData *hData,
                                           int *mode)
{
  int ret;
  int devicemode;

  ret = ioctl(hData->fd, VCAN_IOC_GET_DEVICE_MODE, &devicemode);
  DEBUGPRINT((TXT("VCAN_IOC_GET_DEVICE_MODE 0x%x, ret %d\n"), devicemode, ret));
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }
  *mode = devicemode;
  return canOK;
}

//======================================================================
// vCanFileGetCount
//======================================================================
static canStatus vCanFileGetCount(HandleData *hData, int *count)
{
  int ret = 0;

  ret = ioctl(hData->fd, VCAN_IOC_FILE_GET_COUNT, count);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }
  return canOK;
}

//======================================================================
// vCanFileGetName
//======================================================================
static canStatus vCanFileGetName(HandleData *hData, int fileNo, char *name, int namelen)
{
  VCAN_FILE_GET_NAME_T fileGetName;
  int ret;

  memset(&fileGetName, 0, sizeof(fileGetName));
  fileGetName.fileNo = fileNo;
  fileGetName.name = name;
  fileGetName.namelen = namelen;
  ret = ioctl(hData->fd, VCAN_IOC_FILE_GET_NAME, &fileGetName);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }
  return canOK;
}

//======================================================================
// vCanFileDelete
//======================================================================
static canStatus vCanFileDelete(HandleData *hData, char *deviceFileName)
{
  return vCanMemo_file_delete(hData, deviceFileName);
}

//======================================================================
// vCanFileCopyToDevice
//======================================================================
static canStatus vCanFileCopyToDevice(HandleData *hData, char *hostFileName,
                                      char *deviceFileName)
{
  return vCanMemo_file_copy_to_device(hData, hostFileName, deviceFileName);
}

//======================================================================
// vCanFileCopyFromDevice
//======================================================================
static canStatus vCanFileCopyFromDevice(HandleData *hData, char *deviceFileName,
                                        char *hostFileName)
{
  return vCanMemo_file_copy_from_device(hData, deviceFileName, hostFileName);
}

//======================================================================
// vCanScriptStart
//======================================================================
static canStatus vCanScriptStart(HandleData *hData, int slotNo)
{
  return vCanScript_start(hData, slotNo);
}

//======================================================================
// vCanScriptStop
//======================================================================
static canStatus vCanScriptStop(HandleData *hData, int slotNo, int mode)
{
  return vCanScript_stop(hData, slotNo, mode);
}

//======================================================================
// vCanScriptLoadFile
//======================================================================
static canStatus vCanScriptLoadFile(HandleData *hData, int slotNo,
                                    char *hostFileName)
{
  return vCanScript_load_file(hData, slotNo, hostFileName);
}

//======================================================================
// vCanScriptUnLoad
//======================================================================
static canStatus vCanScriptUnload(HandleData *hData, int slotNo)
{
  return vCanScript_unload(hData, slotNo);
}

//======================================================================
// vCanAccept
//======================================================================
static canStatus vCanAccept(HandleData *hData,
                            const long envelope,
                            const unsigned int flag)
{
  VCanMsgFilter filter;
  int ret;

  ret = ioctl(hData->fd, VCAN_IOC_GET_MSG_FILTER, &filter);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  switch (flag) {
  case canFILTER_SET_CODE_STD:
    filter.stdId   = envelope & 0xFFFF; //from windows
    break;
  case canFILTER_SET_MASK_STD:
    filter.stdMask = envelope & 0xFFFF;  //from windows
    break;
  case canFILTER_SET_CODE_EXT:
    filter.extId   = envelope & ((1 << 29) - 1);
    break;
  case canFILTER_SET_MASK_EXT:
    filter.extMask = envelope & ((1 << 29) - 1);
    break;
  default:
    return canERR_PARAM;
  }
  ret = ioctl(hData->fd, VCAN_IOC_SET_MSG_FILTER, &filter);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  return canOK;
}


//======================================================================
// vCanWriteInternal
//======================================================================
static canStatus vCanWriteInternal(HandleData *hData, long id, void *msgPtr,
                                   unsigned int dlc, unsigned int flag)
{
  CAN_MSG msg;

  int ret;
  unsigned char sendExtended;
  unsigned int nbytes;
  unsigned int dlcFD;

  msg.flags = 0;

  if      (flag & canMSG_STD) sendExtended = 0;
  else if (flag & canMSG_EXT) sendExtended = 1;
  else                        sendExtended = hData->isExtended;

  if (sendExtended) {
    if (id >= (1 << 29)) {
      DEBUGPRINT((TXT("canERR_PARAM on line %d\n"), __LINE__));  // Was 3,
      return canERR_PARAM;
    }
    msg.id = (id | EXT_MSG);
  } else {
    if (id >= (1 << 11)) {
      DEBUGPRINT((TXT("canERR_PARAM on line %d\n"), __LINE__));  // Was 3,
      return canERR_PARAM;
    }
    msg.id = id;
  }

  if (!dlc_is_dlc_ok (hData->acceptLargeDlc, (flag & canFDMSG_FDF), dlc)) {
    return canERR_PARAM;
  }

  if (flag & canFDMSG_FDF) {
    if (hData->openMode) {
      msg.flags |= VCAN_MSG_FLAG_FDF;
    } else {
      return canERR_PARAM;
    }

    if (flag & canMSG_RTR) {
      return canERR_PARAM;
    }

    if (flag & canFDMSG_BRS)  msg.flags |= VCAN_MSG_FLAG_BRS;

    dlcFD  = dlc_bytes_to_dlc_fd (dlc);
    nbytes = dlc_dlc_to_bytes_fd (dlcFD);

  } else {
    if (flag & canFDMSG_BRS) {
      return canERR_PARAM;
    }

    if (flag & canMSG_RTR) msg.flags |= VCAN_MSG_FLAG_REMOTE_FRAME;

    nbytes = dlc > 8 ? 8   : dlc;
    dlcFD  = dlc > 15 ? 15 : dlc;
  }

  if ((flag & canFDMSG_ESI) || (flag & canMSG_TXNACK) || (flag & canMSG_ABL)) {
    // ESI can only be received, not transmitted
    return canERR_PARAM;
  }

  if (flag & canMSG_SINGLE_SHOT) {
    if (! (hData->capabilities & VCAN_CHANNEL_CAP_SINGLE_SHOT)) {
      return canERR_NOT_SUPPORTED;
    }
    else {
      msg.flags |= VCAN_MSG_FLAG_SINGLE_SHOT;
    }
  }

  msg.length = dlcFD;

  if (flag & canMSG_ERROR_FRAME) msg.flags |= VCAN_MSG_FLAG_ERROR_FRAME;

  if (msgPtr) {
    memcpy(msg.data, msgPtr, nbytes);
  }

  ret = ioctl(hData->fd, VCAN_IOC_SENDMSG, &msg);

#if DEBUG
  if (ret == 0) {
    ;
  } else if (errno == EAGAIN) {
    DEBUGPRINT((TXT("VCAN_IOC_SENDMSG canERR_TXBUFOFL\n")));
  } else if (errno == EBADMSG) {
    DEBUGPRINT((TXT("VCAN_IOC_SENDMSG canERR_PARAM\n")));
  } else if (errno == EINTR) {
    DEBUGPRINT((TXT("VCAN_IOC_SENDMSG canERR_INTERRUPTED\n")));
  } else {
    DEBUGPRINT((TXT("VCAN_IOC_SENDMSG ERROR: %d\n"), errno));
  }
#endif

  if      (ret   == 0)       return canOK;
  else if (errno == EAGAIN)  return canERR_TXBUFOFL;
  else                       return errnoToCanStatus(errno);
}


//======================================================================
// vCanWrite
//======================================================================
static canStatus vCanWrite (HandleData *hData, long id, void *msgPtr,
                            unsigned int dlc, unsigned int flag)
{
  return vCanWriteInternal(hData, id, msgPtr, dlc, flag);
}


//======================================================================
// vCanWriteSync
//======================================================================
static canStatus vCanWriteSync (HandleData *hData, unsigned long timeout)
{
  int ret;
  ret = ioctl(hData->fd, VCAN_IOC_WAIT_EMPTY, &timeout);

  if      (ret   == 0)       return canOK;
  else if (errno == EAGAIN)  return canERR_TIMEOUT;
  else                       return errnoToCanStatus(errno);
}

//======================================================================
// vCanWriteWait
//======================================================================
static canStatus vCanWriteWait (HandleData *hData, long id, void *msgPtr,
                                unsigned int dlc, unsigned int flag,
                                long timeout)
{
  canStatus retval;

  retval = vCanWrite (hData, id, msgPtr, dlc, flag);

  if (retval) {
    return retval;
  }

  return vCanWriteSync (hData, timeout);
}

//======================================================================
// vKvReadTimer64
//======================================================================
static canStatus vKvReadTimer64 (HandleData *hData, uint64_t *time)
{
  uint64_t tmpTime;

  if (!time) {
    return canERR_PARAM;
  }

  if (ioctl(hData->fd, VCAN_IOC_READ_TIMER, &tmpTime)) {
    return errnoToCanStatus(errno);
  }
  *time = (tmpTime * 10UL) / (hData->timerResolution) ;

  return canOK;
}

//======================================================================
// vKvReadTimer
//======================================================================
static canStatus vKvReadTimer (HandleData *hData, unsigned int *time)
{
  uint64_t tmpTime;
  canStatus stat;

  if (!time) {
    return canERR_PARAM;
  }

  stat = vKvReadTimer64(hData, &tmpTime);
  if (stat == canOK) {
    *time = (unsigned int) tmpTime;
  }

  return stat;
}

//======================================================================
// vCanReadTimer
//======================================================================
static canStatus vCanReadTimer (HandleData *hData, unsigned long *time)
{

  uint64_t tmpTime;
  canStatus stat;

  if (!time) {
    return canERR_PARAM;
  }

  stat = vKvReadTimer64(hData, &tmpTime);
  if (stat == canOK) {
    *time = (unsigned long) tmpTime;
  }

  return stat;
}

//======================================================================
// vCanReadErrorCounters
//======================================================================
static canStatus vCanReadErrorCounters (HandleData *hData, unsigned int *txErr,
                                        unsigned int *rxErr, unsigned int *ovErr)
{
  VCanOverrun overrun;

  if (txErr != NULL) {
    if (ioctl(hData->fd, VCAN_IOC_GET_TX_ERR, txErr)) {
      goto ioc_error;
    }
  }
  if (rxErr != NULL) {
    if (ioctl(hData->fd, VCAN_IOC_GET_RX_ERR, rxErr)) {
      goto ioc_error;
    }
  }
  if (ovErr != NULL) {
    if (ioctl(hData->fd, VCAN_IOC_GET_OVER_ERR, &overrun)) {
      goto ioc_error;
    }
    if (overrun.hw || overrun.sw) {
      *ovErr = 1;
    } else {
      *ovErr = 0;
    }
  }

  return canOK;

ioc_error:
  return errnoToCanStatus(errno);
}

//======================================================================
// vCanReadStatus
//======================================================================
static canStatus vCanReadStatus (HandleData *hData, unsigned long *flags)
{
  int                   reply;
  VCanRequestChipStatus chip_status;
  VCanOverrun           overrun;

  if (flags == NULL) {
    return canERR_PARAM;
  }

  *flags = 0;

  if (ioctl(hData->fd, VCAN_IOC_GET_CHIP_STATE, &chip_status)) {
    goto ioctl_error;
  }

  if (chip_status.busStatus & CHIPSTAT_BUSOFF) {
    *flags  |= canSTAT_BUS_OFF;
  } else if (chip_status.busStatus & CHIPSTAT_ERROR_PASSIVE) {
    *flags  |= canSTAT_ERROR_PASSIVE;
  } else if (chip_status.busStatus & CHIPSTAT_ERROR_WARNING) {
    *flags  |= canSTAT_ERROR_WARNING;
  } else if (chip_status.busStatus & CHIPSTAT_ERROR_ACTIVE) {
    *flags  |= canSTAT_ERROR_ACTIVE;
  }

  if (chip_status.txErrorCounter) {
    *flags |= canSTAT_TXERR;
  }

  if (chip_status.rxErrorCounter) {
    *flags |= canSTAT_RXERR;
  }

  if (ioctl(hData->fd, VCAN_IOC_GET_OVER_ERR, &overrun)) {
    goto ioctl_error;
  }

  if (overrun.sw) {
    *flags |= canSTAT_SW_OVERRUN;
  }

  if (overrun.hw) {
    *flags |= canSTAT_HW_OVERRUN;
  }

  if (ioctl(hData->fd, VCAN_IOC_GET_RX_QUEUE_LEVEL, &reply)) {
    goto ioctl_error;
  }
  if (reply) {
    *flags |= canSTAT_RX_PENDING;
  }

  if (ioctl(hData->fd, VCAN_IOC_GET_TX_QUEUE_LEVEL, &reply)) {
    goto ioctl_error;
  }
  if (reply) {
    *flags |= canSTAT_TX_PENDING;
  }

  return canOK;

ioctl_error:
  return errnoToCanStatus(errno);
}

//======================================================================
// vKvFlashLeds
//======================================================================
static canStatus vKvFlashLeds (HandleData *hData, int action, int timeout)
{
  KCAN_IOCTL_LED_ACTION_I buffer;

  buffer.sub_command = action;
  buffer.timeout = timeout;

  if (ioctl(hData->fd, VCAN_IOC_FLASH_LEDS, &buffer)) {
    return errnoToCanStatus(errno);
  }
  return canOK;
}

//======================================================================
// vCanRequestChipStatus
//======================================================================
static canStatus vCanRequestChipStatus (HandleData *hData)
{
  // canRequestChipStatus is not needed in Linux, but in order to make code
  // portable between Windows and Linux, this dummy function was added.
  (void) hData;
  return canOK;
}

//======================================================================
// vCanGetChannelData
//======================================================================
static canStatus vCanGetChannelData (char *deviceName, int item,
                                     void *buffer, size_t bufsize)
{
  int fd;
  int err = 1;

  fd = open(deviceName, O_RDONLY);
  if (fd == canINVALID_HANDLE) {
    DEBUGPRINT((TXT("Unable to open %s\n"), deviceName));
    return canERR_NOTFOUND;
  }

  switch (item) {
  case canCHANNELDATA_CARD_NUMBER:
    err = ioctl(fd, VCAN_IOC_GET_CARD_NUMBER, buffer);
    break;

  case canCHANNELDATA_TRANS_TYPE:
    err = ioctl(fd, VCAN_IOC_GET_TRANSCEIVER_INFO, buffer);
    break;

  case canCHANNELDATA_CARD_SERIAL_NO:
    if (bufsize < 8){
      close(fd);
      return canERR_PARAM;
    }
    err = ioctl(fd, VCAN_IOC_GET_SERIAL, buffer);
    break;

  case canCHANNELDATA_CARD_UPC_NO:
    err = ioctl(fd, VCAN_IOC_GET_EAN, buffer);
    break;

  case canCHANNELDATA_DRIVER_NAME:
    err = ioctl(fd, VCAN_IOC_GET_DRIVER_NAME, buffer);
    break;

  case canCHANNELDATA_CARD_FIRMWARE_REV:
    err = ioctl(fd, VCAN_IOC_GET_FIRMWARE_REV, buffer);
    break;

  case canCHANNELDATA_CARD_HARDWARE_REV:
    err = ioctl(fd, VCAN_IOC_GET_HARDWARE_REV, buffer);
    break;

  case canCHANNELDATA_CHANNEL_CAP:
    err = ioctl(fd, VCAN_IOC_GET_CHAN_CAP, buffer);
    if (!err) {
      *(uint32_t *)buffer = get_capabilities (*(uint32_t *)buffer);
    }
    break;

  case canCHANNELDATA_CHANNEL_CAP_MASK:
    err = ioctl(fd, VCAN_IOC_GET_CHAN_CAP_MASK, buffer);
    if (!err) {
      *(uint32_t *)buffer = get_capabilities (*(uint32_t *)buffer);
    }
    break;

  case canCHANNELDATA_CHANNEL_FLAGS:
    err = ioctl(fd, VCAN_IOC_GET_CHANNEL_INFO, buffer);
    if (!err) {
      *(uint32_t *)buffer =  convert_channel_info_flags(*(uint32_t *)buffer);
    }
    break;

  case canCHANNELDATA_CARD_TYPE:
    err = ioctl(fd, VCAN_IOC_GET_CARD_TYPE, buffer);
    break;

  case canCHANNELDATA_MAX_BITRATE:
    err = ioctl(fd, VCAN_IOC_GET_MAX_BITRATE, buffer);
    break;

  case canCHANNELDATA_CUST_CHANNEL_NAME:
    {
      KCAN_IOCTL_GET_CUST_CHANNEL_NAME_T custChannelName;
      unsigned int maxCopySize;

      if (bufsize == 0) {
        close(fd);
        return canERR_PARAM;
      }

      memset(buffer, 0, bufsize);
      maxCopySize = bufsize - 1; // Assure null termination.
      if (maxCopySize == 0) {
        close(fd);
        return canOK;
      }

      if (maxCopySize > sizeof(custChannelName.data)) {
        maxCopySize = sizeof(custChannelName.data);
      }

      memset(&custChannelName, 0, sizeof(custChannelName));
      err = ioctl(fd, KCAN_IOCTL_GET_CUST_CHANNEL_NAME, &custChannelName);
      if (!err) {
        memcpy(buffer, custChannelName.data, maxCopySize);
      }
      else {
        close(fd);
        return canERR_NOT_IMPLEMENTED;
      }
    }
    break;

  case canCHANNELDATA_DRIVER_FILE_VERSION:
    {
      VCAN_IOCTL_CARD_INFO ci;
      unsigned short *p = (unsigned short *)buffer;

      if (bufsize < 8){
        close(fd);
        return canERR_PARAM;
      }
      err = ioctl(fd, VCAN_IOCTL_GET_CARD_INFO, &ci);
      if (!err) {
        *p++ = 0;
        *p++ = ci.driver_version_build;
        *p++ = ci.driver_version_minor;
        *p++ = ci.driver_version_major;
      }
      break;
    }

  case canCHANNELDATA_DRIVER_PRODUCT_VERSION:
    {
      VCAN_IOCTL_CARD_INFO ci;
      unsigned short *p = (unsigned short *)buffer;

      if (bufsize < 8){
        close(fd);
        return canERR_PARAM;
      }
      err = ioctl(fd, VCAN_IOCTL_GET_CARD_INFO, &ci);
      if (!err) {
        *p++ = 0;
        *p++ = 0; // (unsigned short) ci.driver_version_minor_letter;
        *p++ = (unsigned short) ci.product_version_minor;
        *p++ = (unsigned short) ci.product_version_major;
      }
      break;
    }

  case canCHANNELDATA_IS_REMOTE:
    // No remote devices for Linux so far
    {
      if (bufsize < 4 /*"32-bit unsigned int"*/|| !buffer) {
        close(fd);
        return canERR_PARAM;
      }
      *(uint32_t*)buffer = 0;
    }
    break;

  case canCHANNELDATA_FEATURE_EAN:
  case canCHANNELDATA_HW_STATUS:
  case canCHANNELDATA_LOGGER_TYPE:
  case canCHANNELDATA_REMOTE_TYPE:
    {
      KCAN_IOCTL_MISC_INFO miscInfo;
      if (bufsize < 4 /*"32-bit unsigned int"*/|| !buffer) {
        close(fd);
        return canERR_PARAM;
      }

      memset(&miscInfo, 0, sizeof(miscInfo));

      switch (item) {
        case canCHANNELDATA_FEATURE_EAN:
          miscInfo.subcmd = KCAN_IOCTL_MISC_INFO_SUBCMD_FEATURE_EAN;
          break;
        case canCHANNELDATA_HW_STATUS:
          miscInfo.subcmd = KCAN_IOCTL_MISC_INFO_SUBCMD_HW_STATUS;
          break;
        case canCHANNELDATA_LOGGER_TYPE:
          miscInfo.subcmd = KCAN_IOCTL_MISC_INFO_SUBCMD_CHANNEL_LOGGER_INFO;
          break;

        case canCHANNELDATA_REMOTE_TYPE:
          miscInfo.subcmd = KCAN_IOCTL_MISC_INFO_SUBCMD_CHANNEL_REMOTE_INFO;
          break;
        default:
          close(fd);
          return canERR_PARAM;
      }

      err = ioctl(fd, KCAN_IOCTL_GET_CARD_INFO_MISC, &miscInfo);
      if (!err) {
        if (miscInfo.retcode == KCAN_IOCTL_MISC_INFO_RETCODE_SUCCESS){
          switch (item) {
            case canCHANNELDATA_FEATURE_EAN:
              if (bufsize < sizeof(miscSubCmdFeatureEan)) {
                close(fd);
                return canERR_PARAM;
              }
              memcpy(buffer, &miscInfo.payload.featureEan, sizeof(miscSubCmdFeatureEan));
              break;
            case canCHANNELDATA_HW_STATUS:
              if (bufsize < sizeof(sizeof(miscSubCmdHwStatus))) {
                close(fd);
                return canERR_PARAM;
              }
              memcpy(buffer, &miscInfo.payload.hwStatus, sizeof(miscSubCmdHwStatus));
              break;
            case canCHANNELDATA_LOGGER_TYPE:
              *(uint32_t *)buffer = miscInfo.payload.loggerInfo.loggerType;
              break;
            case canCHANNELDATA_REMOTE_TYPE:
              *(uint32_t *)buffer = miscInfo.payload.remoteInfo.remoteType;
              break;
            default:
              close(fd);
              return canERR_PARAM;
          }
        }
        else {
          close(fd);
          return canERR_NOT_IMPLEMENTED;
        }

      }
    }
    break;

  default:
    close(fd);
    return canERR_PARAM;
  }

  close(fd);

  if (err) {
    if (!(item == canCHANNELDATA_CUST_CHANNEL_NAME && err == ENOSYS)) {
      DEBUGPRINT((TXT("Error on ioctl %d: %d / %d\n"), item, err, errno));
    }
    return errnoToCanStatus(errno);
  }

  return canOK;
}


//======================================================================
// vCanIoCtl
//======================================================================
static canStatus vCanIoCtl(HandleData *hData, unsigned int func,
                           void *buf, size_t buflen)
{
  switch(func) {
  case canIOCTL_GET_RX_BUFFER_LEVEL:
    // buf points at a uint32_t which receives the current RX queue level.
    if (check_args (buf, buflen, sizeof (uint32_t), ERROR_WHEN_NEQ)) {
      return canERR_PARAM;
    }

    if (ioctl(hData->fd, VCAN_IOC_GET_RX_QUEUE_LEVEL, buf)) {
      return errnoToCanStatus(errno);
    }
    break;
  case canIOCTL_GET_TX_BUFFER_LEVEL:
    // buf points at a uint32_t which receives the current TX queue level.
    if (check_args (buf, buflen, sizeof (uint32_t), ERROR_WHEN_NEQ)) {
      return canERR_PARAM;
    }

    if (ioctl(hData->fd, VCAN_IOC_GET_TX_QUEUE_LEVEL, buf)) {
      return errnoToCanStatus(errno);
    }
    break;
  case canIOCTL_FLUSH_RX_BUFFER:
    // Discard the current contents of the RX queue.
    if (ioctl(hData->fd, VCAN_IOC_FLUSH_RCVBUFFER, buf)) {
      return errnoToCanStatus(errno);
    }
    break;
  case canIOCTL_FLUSH_TX_BUFFER:
    //  Discard the current contents of the TX queue.
    if (ioctl(hData->fd, VCAN_IOC_FLUSH_SENDBUFFER, buf)) {
      return errnoToCanStatus(errno);
    }
    break;
  case canIOCTL_SET_TXACK:
    // buf points at a uint32_t which contains 0/1 to turn TXACKs on/ff
    if (check_args (buf, buflen, sizeof (uint32_t), ERROR_WHEN_NEQ)) {
      return canERR_PARAM;
    }

    if (ioctl(hData->fd, VCAN_IOC_SET_TXACK, buf)) {
      return errnoToCanStatus(errno);
    }
    break;
  case canIOCTL_GET_TXACK:
    // buf points at a uint32_t which receives current TXACKs setting
    if (check_args (buf, buflen, sizeof (uint32_t), ERROR_WHEN_NEQ)) {
      return canERR_PARAM;
    }

    if (ioctl(hData->fd, VCAN_IOC_GET_TXACK, buf)) {
      return errnoToCanStatus(errno);
    }
    break;
  case canIOCTL_SET_TXRQ:
    // buf points at a uint32_t which contains 0/1 to turn TXRQs on/ff
    if (check_args (buf, buflen, sizeof (uint32_t), ERROR_WHEN_NEQ)) {
      return canERR_PARAM;
    }

    if (ioctl(hData->fd, VCAN_IOC_SET_TXRQ, buf)) {
      return errnoToCanStatus(errno);
    }
    break;
  case canIOCTL_SET_LOCAL_TXECHO:
    // buf points at an unsigned char which contains 0/1 to turn TXECHO on/ff
    if (check_args (buf, buflen, sizeof (uint8_t), ERROR_WHEN_LT)) {
      return canERR_PARAM;
    }

    if (ioctl(hData->fd, VCAN_IOC_SET_TXECHO, buf)) {
      return errnoToCanStatus(errno);
    }
    break;

  case canIOCTL_RESET_OVERRUN_COUNT:
    if (ioctl(hData->fd, VCAN_IOC_RESET_OVERRUN_COUNT, NULL)) {
      return errnoToCanStatus(errno);
    }
    break;

  case canIOCTL_SET_TIMER_SCALE:
    {
      uint32_t t;
      //
      // t is the desired resolution in microseconds.
      // VCAN uses 10 us ticks, so we scale by 10 here.
      //
      if (check_args (buf, buflen, sizeof (uint32_t), ERROR_WHEN_NEQ)) {
        return canERR_PARAM;
      }

      t = *(uint32_t *)buf;
      if (t == 0) {
        t = DEFAULT_TIMER_FACTOR * 10;
      }
      hData->timerScale = 10.0 / t;
      hData->timerResolution = t;
      break;
    }
  case canIOCTL_GET_TIMER_SCALE:
    //
    // Report the used resolution in microseconds.
    //
    if (check_args (buf, buflen, sizeof (uint32_t), ERROR_WHEN_NEQ)) {
      return canERR_PARAM;
    }

    *(unsigned int *)buf = hData->timerResolution;
    break;

  case canIOCTL_GET_TREF_LIST:
    if (!buf) {
      return canERR_PARAM;
    }
    if (ioctl(hData->fd, KCAN_IOCTL_READ_TREF_LIST, buf)) {
      return errnoToCanStatus(errno);
    }
    break;

  case canIOCTL_TX_INTERVAL:
    if (check_args (buf, buflen, sizeof (uint32_t), ERROR_WHEN_NEQ)) {
      return canERR_PARAM;
    }

    if (ioctl(hData->fd, KCAN_IOCTL_TX_INTERVAL, buf)) {
      return errnoToCanStatus(errno);
    }
    break;

  case canIOCTL_SET_BRLIMIT :
    if (check_args (buf, buflen, sizeof (long), ERROR_WHEN_NEQ)) {
      return canERR_PARAM;
    }

    if (ioctl(hData->fd, KCAN_IOCTL_SET_BRLIMIT, buf)) {
      return errnoToCanStatus(errno);
    }

    break;

  case canIOCTL_GET_REPORT_ACCESS_ERRORS:
    if (check_args (buf, buflen, sizeof (unsigned char), ERROR_WHEN_LT)) {
      return canERR_PARAM;
    }

    *(unsigned char*)buf = hData->report_access_errors;
    break;

  case canIOCTL_SET_REPORT_ACCESS_ERRORS:
    if (check_args (buf, buflen, sizeof (unsigned char), ERROR_WHEN_LT)) {
      return canERR_PARAM;
    }

    hData->report_access_errors = *(unsigned char*)buf;
    break;

  // canIOCTL_LIN_MODE is used by LINlib to set LIN mode to MASTER or LIN
  // this LIN mode can later be retrieved via canCHANNELDATA_CHANNEL_FLAGS
  case canIOCTL_LIN_MODE:
    {
      uint32_t c_flags;
      uint32_t v_flags = 0;

      if (check_args (buf, buflen, sizeof (uint32_t), ERROR_WHEN_LT)) {
        return canERR_PARAM;
      }

      c_flags = *(uint32_t *)buf;
      if (c_flags & canCHANNEL_IS_LIN_MASTER) {
        v_flags |= VCAN_CHANNEL_STATUS_LIN_MASTER;
      }
      if (c_flags & canCHANNEL_IS_LIN_SLAVE) {
        v_flags |= VCAN_CHANNEL_STATUS_LIN_SLAVE;
      }

      if (ioctl(hData->fd, KCAN_IOCTL_LIN_MODE, &v_flags)) {
        return errnoToCanStatus(errno);
      }
      break;
    }

   case canIOCTL_SET_BUSON_TIME_AUTO_RESET:
    {
      if (check_args (buf, buflen, sizeof (uint32_t), ERROR_WHEN_NEQ)) {
        return canERR_PARAM;
      }

      hData->auto_reset = (unsigned char)*(uint32_t *)buf;

      break;
    }

  default:
    return canERR_PARAM;
  }

  return canOK;
}


static canStatus kCanObjbufFreeAll (HandleData *hData)
{
  int ret;

  ret = ioctl(hData->fd, KCAN_IOCTL_OBJBUF_FREE_ALL, NULL);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  return canOK;
}

static canStatus kCanObjbufAllocate (HandleData *hData, int type, int *number)
{
  int ret;
  KCanObjbufAdminData ioc;

  ioc.type = type;

  ret = ioctl(hData->fd, KCAN_IOCTL_OBJBUF_ALLOCATE, &ioc);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  *number = ioc.buffer_number;

  return canOK;
}


static canStatus kCanObjbufFree (HandleData *hData, int idx)
{
  int ret;
  KCanObjbufAdminData ioc;

  ioc.buffer_number = idx;

  ret = ioctl(hData->fd, KCAN_IOCTL_OBJBUF_FREE, &ioc);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  return canOK;
}


static canStatus kCanObjbufWrite (HandleData *hData, int idx, int id, void* msg,
                                  unsigned int dlc, unsigned int flags)
{
  int                  retval;
  KCanObjbufBufferData ioc;

  ioc.flags = 0;

  if (!dlc_is_dlc_ok (hData->acceptLargeDlc, (flags & canFDMSG_FDF), dlc)) {
    return canERR_PARAM;
  }

  if (flags & canFDMSG_FDF) {
    if (!hData->openMode) {
      return canERR_PARAM;
    }

    if (flags & canMSG_RTR) {
      return canERR_PARAM;
    }

    if (flags & canFDMSG_BRS) {
      ioc.flags |= VCAN_AUTOTX_MSG_FLAG_BRS;
    }

    ioc.flags |= VCAN_AUTOTX_MSG_FLAG_FDF;
  } else {
    if (flags & canFDMSG_BRS)
    {
      return canERR_PARAM;
    }

    if (flags & canMSG_RTR) {
      ioc.flags |= VCAN_AUTOTX_MSG_FLAG_REMOTE_FRAME;
    }

    if (dlc > 15) {
      dlc = 15;
    }
  }

  if (flags & canMSG_SINGLE_SHOT) {
    if (! (hData->capabilities & VCAN_CHANNEL_CAP_SINGLE_SHOT)) {
      return canERR_NOT_SUPPORTED;
    } else {
      ioc.flags |= VCAN_MSG_FLAG_SINGLE_SHOT;
    }
  }

  ioc.buffer_number = idx;

  if (flags & canMSG_EXT) {
    ioc.id = (id | EXT_MSG);
  } else {
    ioc.id = id;
  }

  ioc.dlc = dlc;

  if (msg) {
    memcpy(ioc.data, msg, sizeof(ioc.data));
  }

  retval = ioctl(hData->fd, KCAN_IOCTL_OBJBUF_WRITE, &ioc);

  if (retval != 0) {
    return errnoToCanStatus(errno);
  }

  return canOK;
}


static canStatus kCanObjbufSetFilter (HandleData *hData, int idx,
                                      unsigned int code, unsigned int mask)
{
  int ret;
  KCanObjbufAdminData ioc;

  ioc.buffer_number = idx;
  ioc.acc_code      = code;
  ioc.acc_mask      = mask;

  ret = ioctl(hData->fd, KCAN_IOCTL_OBJBUF_SET_FILTER, &ioc);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  return canOK;
}


static canStatus kCanObjbufSetFlags (HandleData *hData, int idx, unsigned int flags)
{
  int ret;
  KCanObjbufAdminData ioc;

  ioc.buffer_number = idx;
  ioc.flags         = flags;

  ret = ioctl(hData->fd, KCAN_IOCTL_OBJBUF_SET_FLAGS, &ioc);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  return canOK;
}


static canStatus kCanObjbufSetPeriod (HandleData *hData, int idx, unsigned int period)
{
  int ret;
  KCanObjbufAdminData ioc;

  ioc.buffer_number = idx;
  ioc.period        = period;

  ret = ioctl(hData->fd, KCAN_IOCTL_OBJBUF_SET_PERIOD, &ioc);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  return canOK;
}


static canStatus kCanObjbufSetMsgCount (HandleData *hData, int idx, unsigned int count)
{
  int ret;
  KCanObjbufAdminData ioc;

  ioc.buffer_number = idx;
  ioc.period        = count;

  ret = ioctl(hData->fd, KCAN_IOCTL_OBJBUF_SET_MSG_COUNT, &ioc);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  return canOK;
}


static canStatus kCanObjbufSendBurst (HandleData *hData, int idx, unsigned int burstLen)
{
  int ret;
  KCanObjbufAdminData ioc;

  ioc.buffer_number = idx;
  ioc.period        = burstLen;

  ret = ioctl(hData->fd, KCAN_IOCTL_OBJBUF_SEND_BURST, &ioc);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  return canOK;
}


static canStatus kCanObjbufEnable (HandleData *hData, int idx)
{
  int ret;
  KCanObjbufAdminData ioc;

  ioc.buffer_number = idx;

  ret = ioctl(hData->fd, KCAN_IOCTL_OBJBUF_ENABLE, &ioc);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  return canOK;
}


static canStatus kCanObjbufDisable (HandleData *hData, int idx)
{
  int ret;
  KCanObjbufAdminData ioc;

  ioc.buffer_number = idx;

  ret = ioctl(hData->fd, KCAN_IOCTL_OBJBUF_DISABLE, &ioc);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  return canOK;
}

static uint32_t convert_channel_info_flags (uint32_t vflags) {
  uint32_t retval = 0;

  if (vflags & VCAN_CHANNEL_STATUS_ON_BUS) {
    retval |= canCHANNEL_IS_OPEN;
  }
  if (vflags & VCAN_CHANNEL_STATUS_EXCLUSIVE) {
    retval |= canCHANNEL_IS_EXCLUSIVE;  // Currently not working but added for completeness
  }
  if (vflags & VCAN_CHANNEL_STATUS_CANFD) {
    retval |= canCHANNEL_IS_CANFD;
  }
  if (vflags & VCAN_CHANNEL_STATUS_LIN) {
    retval |= canCHANNEL_IS_LIN;
  }
  if (vflags & VCAN_CHANNEL_STATUS_LIN_MASTER) {
    retval |= canCHANNEL_IS_LIN_MASTER;
  }
  if (vflags & VCAN_CHANNEL_STATUS_LIN_SLAVE) {
    retval |= canCHANNEL_IS_LIN_SLAVE;
  }
  return retval;
}

static uint32_t get_capabilities (uint32_t cap) {
  uint32_t i;
  uint32_t retval = 0;

  for(i = 0;
      i < sizeof(capabilities_table) / sizeof(capabilities_table[0]);
      i++) {
    if (cap & capabilities_table[i][0]) {
      retval |= capabilities_table[i][1];
    }
  }

  return retval;
}

static int check_args (void*buf, uint32_t buf_len, uint32_t limit, uint32_t method)
{
  if (!buf) {
    return -1;
  }

  switch (method) {
    case ERROR_WHEN_NEQ: if (buf_len != limit) return canERR_PARAM; break;
    case ERROR_WHEN_LT:  if (buf_len < limit)  return canERR_PARAM; break;
    default: return canERR_PARAM; break;
  }
  return 0;
}


static canStatus vCanResetClock (HandleData *hData)
{
  if (ioctl(hData->fd, VCAN_IOC_RESET_CLOCK, NULL))
    goto ioc_error;

  return canOK;

 ioc_error:
  return errnoToCanStatus(errno);
}

static canStatus vCanSetClockOffset (HandleData *hData, HandleData *hFrom)
{
  uint64_t offset;

  if (ioctl(hFrom->fd, VCAN_IOC_GET_CLOCK_OFFSET, &offset))
    goto ioc_error;

  if (ioctl(hData->fd, VCAN_IOC_SET_CLOCK_OFFSET, &offset))
    goto ioc_error;

  return canOK;

 ioc_error:
  return errnoToCanStatus(errno);
}

static canStatus vCanGetCardInfo (HandleData *hData, VCAN_IOCTL_CARD_INFO *ci)
{
  if (ioctl(hData->fd, VCAN_IOCTL_GET_CARD_INFO, ci))
    goto ioc_error;

  return canOK;

 ioc_error:
  return errnoToCanStatus(errno);
}

static canStatus vCanGetCardInfo2 (HandleData *hData, KCAN_IOCTL_CARD_INFO_2 *ci)
{
  if (ioctl(hData->fd, KCAN_IOCTL_GET_CARD_INFO_2, ci))
    goto ioc_error;

  return canOK;

 ioc_error:
  return errnoToCanStatus(errno);
}

CANOps vCanOps = {
  // VCan Functions
  .setNotify           = vCanSetNotify,
  .openChannel         = vCanOpenChannel,
  .busOn               = vCanBusOn,
  .busOff              = vCanBusOff,
  .setBusParams        = vCanSetBusParams,
  .getBusParams        = vCanGetBusParams,
  .reqBusStats         = vCanReqBusStats,
  .getBusStats         = vCanGetBusStats,
  .read                = vCanRead,
  .readSync            = vCanReadSync,
  .readWait            = vCanReadWait,
  .readSpecific        = vCanReadSpecific,
  .readSpecificSkip    = vCanReadSpecificSkip,
  .readSyncSpecific    = vCanReadSyncSpecific,
  .setBusOutputControl = vCanSetBusOutputControl,
  .getBusOutputControl = vCanGetBusOutputControl,
  .kvDeviceGetMode     = vCanGetDeviceMode,
  .kvDeviceSetMode     = vCanSetDeviceMode,
  .kvFileGetCount      = vCanFileGetCount,
  .kvFileGetName       = vCanFileGetName,
  .kvFileDelete        = vCanFileDelete,
  .kvFileCopyToDevice  = vCanFileCopyToDevice,
  .kvFileCopyFromDevice  = vCanFileCopyFromDevice,
  .kvScriptStart       = vCanScriptStart,
  .kvScriptStop        = vCanScriptStop,
  .kvScriptLoadFile    = vCanScriptLoadFile,
  .kvScriptUnload      = vCanScriptUnload,
  .accept              = vCanAccept,
  .write               = vCanWrite,
  .writeWait           = vCanWriteWait,
  .writeSync           = vCanWriteSync,
  .readTimer           = vCanReadTimer,
  .kvReadTimer         = vKvReadTimer,
  .kvReadTimer64       = vKvReadTimer64,
  .readErrorCounters   = vCanReadErrorCounters,
  .readStatus          = vCanReadStatus,
  .kvFlashLeds         = vKvFlashLeds,
  .requestChipStatus   = vCanRequestChipStatus,
  .getChannelData      = vCanGetChannelData,
  .ioCtl               = vCanIoCtl,
  .objbufFreeAll       = kCanObjbufFreeAll,
  .objbufAllocate      = kCanObjbufAllocate,
  .objbufFree          = kCanObjbufFree,
  .objbufWrite         = kCanObjbufWrite,
  .objbufSetFilter     = kCanObjbufSetFilter,
  .objbufSetFlags      = kCanObjbufSetFlags,
  .objbufSetPeriod     = kCanObjbufSetPeriod,
  .objbufSetMsgCount   = kCanObjbufSetMsgCount,
  .objbufSendBurst     = kCanObjbufSendBurst,
  .objbufEnable        = kCanObjbufEnable,
  .objbufDisable       = kCanObjbufDisable,
  .resetClock          = vCanResetClock,
  .setClockOffset      = vCanSetClockOffset,
  .getCardInfo         = vCanGetCardInfo,
  .getCardInfo2        = vCanGetCardInfo2,
};
