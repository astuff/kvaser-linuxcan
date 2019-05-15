/**
 *             Copyright 2017 by Kvaser AB, Molndal, Sweden
 *                         http://www.kvaser.com
 *
 * \section LICENSE
 * This software is dual licensed under the following two licenses:
 * BSD-new and GPLv2. You may use either one. See the included
 * COPYING file for details.
 *
 * License: BSD-new
 * ==============================================================================
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * License: GPLv2
 * ==============================================================================
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 *
 *
 * IMPORTANT NOTICE:
 * ==============================================================================
 * This source code is made available for free, as an open license, by Kvaser AB,
 * for use with its applications. Kvaser AB does not accept any liability
 * whatsoever for any third party patent or other immaterial property rights
 * violations that may result from any usage of this source code, regardless of
 * the combination of source code and various applications that it can be used
 * in, or with.
 *
 * -----------------------------------------------------------------------------
*/

#define _GNU_SOURCE // This is required for recursive mutex support in pthread

#  include <sys/time.h>
#  include <unistd.h>
#include <canlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "compilerassert.h"
#include "canlib_version.h"

#  include <pthread.h>

#  define FALSE                       0
#  define TRUE                        (!FALSE)
#  define OutputDebugString(buf)
#  define CRITICAL_SECTION            pthread_mutex_t

#if DEBUG
#  define PRINTF(x)                   dbg_printf x
#else
#  define PRINTF(x)
#endif /*DEBUG*/

#if defined(PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP)
static pthread_mutex_t handleMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#elif defined(PTHREAD_RECURSIVE_MUTEX_INITIALIZER)
static pthread_mutex_t handleMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
#else
#error Canlib requires GNUC.
#endif /* PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP */

#define canOPEN_LIN canOPEN_INTERNAL_L

#include <linlib.h>

// #define ICD2 0 // Doesn't matter
#include "linboot.h"

typedef signed char         int8;
typedef unsigned char       uint8;
typedef short               int16;
typedef unsigned short      uint16;
typedef int                 int32;
typedef unsigned int        uint32;

#if DEBUG
static void dbg_printf(const char *fmt, ...)
{
  char buf[1024];
  va_list argptr;

  va_start(argptr, fmt);
  strcpy(buf, "LINLIB: ");
  vsprintf(buf+strlen(buf), fmt, argptr);
  va_end(argptr);
  fputs(buf, stderr);
  OutputDebugString(buf);
}
#endif /*DEBUG*/


CRITICAL_SECTION    crit;

int isInitialized = FALSE;

//placeholder for the Timing0-struct
typedef struct {
  uint16 res1;
  uint16 res2;
  uint16 res3;
  uint16 res4;
} Timing0_ph;

//Timing0-struct for leaf
typedef struct {
  uint16 frameStart;
  uint8 z[6];
} Timing0_leaf;

//Timing0-struct for hybrid
typedef struct {
  uint32 sof; //start of frame
  uint32 eof; //end of frame
} Timing0_hybrid;

//Timing0-struct
typedef union {
  Timing0_ph     ph;
  Timing0_leaf   leaf;
  Timing0_hybrid hybrid;
} Timing0;

//Timing1-struct that is common for leaf and hybrid
typedef struct {
  uint16 lin_break_length;
  uint16 lin_synch_length;
  uint16 res1;
  struct {
    // For MSC, the type can't be int, or the struct will be too large
    unsigned char timeShift : 3;
    unsigned char parity    : 2;
    unsigned char bitError  : 1;
    unsigned char z         : 1;
    unsigned char msgToCome : 1;
  } flags;
  uint8 msgCsum;
} Timing1_common;

//Timing1-struct that is uniq for leaf
typedef struct {
  uint16 res1;
  uint16 res2;
  uint16 frameLength;
  uint16 res3;
} Timing1_leaf;

//Timing1-struct
typedef union {
  Timing1_common common;
  Timing1_leaf   leaf;
  Timing1_common hybrid;
} Timing1;

typedef struct {
  uint16 t[4];
} TimingSynch;

typedef struct {
  uint16 t[8];
} TimingBytes;


CompilerAssert(sizeof(Timing0_ph) == 8);
CompilerAssert(sizeof(Timing0_leaf) == 8);
CompilerAssert(sizeof(Timing0_hybrid) == 8);
CompilerAssert(sizeof(Timing0) == 8);
CompilerAssert(sizeof(Timing1_common) == 8);
CompilerAssert(sizeof(Timing1_leaf) == 8);
CompilerAssert(sizeof(Timing1) == 8);
CompilerAssert(sizeof(TimingSynch) == 8);


typedef struct {
  int           inUse;
  int           running;    // Non-zero if the application is running
  int           canChannel;
  canHandle     ch;
  LinHandle     h;    // Refers to ourselves
  int           bitrate;
  BOOL          master;

  BOOL          timing0Valid;
  BOOL          timing1Valid;
  BOOL          timingSValid;
  BOOL          timingByteTime0Valid;
  BOOL          timingByteTime1Valid;


  Timing0       timing0;
  Timing1       timing1;
  TimingSynch   timingS;
  TimingBytes   timingB;
  unsigned long timestamp0;

  uint16        bootVerMajor;
  uint16        bootVerMinor;
  uint16        bootVerBuild;
  uint16        appVerMajor;
  uint16        appVerMinor;
  uint16        appVerBuild;

  unsigned int  openMode;
} LinChannel;

/*
 * Macro to convert the application version to a numeric value.
 * The version isn't read until BusOn; if the cable isn't on bus, 0 is returned.
 */
#define VERSION_INT(major,minor,build) (((major) << 16) + ((minor) << 8) + (build))
#define LINAPP_VERSION(lh) (lh->running ? VERSION_INT(lh->appVerMajor, lh->appVerMinor, lh->appVerBuild) : 0)

// Tests if h is a valid LinHandle, and if so assigns lh. Evaluates to
// non-zero if OK.
#define ASSIGN_LIN_HANDLE(lh, h) ((h) >= 0 && \
                                  (h) < MAXHANDLES && \
                                  linChannels[h].inUse ? (lh = &linChannels[h]) : (lh = NULL))


#define MAXHANDLES 64

typedef LinChannel* LinHandleInt;

static LinChannel     linChannels[MAXHANDLES];
static unsigned int   command_counter = 0;

/* Prototypes
*/
static unsigned long get_time_1ms(void);
static LinStatus lin_command(LinHandleInt lh,
                             unsigned char command,
                             unsigned int cmdCounter,
                             uint8 p1, uint8 p2, uint8 p3,
                             uint8 p4, uint8 p5, uint8 p6,
                             unsigned char *response,
                             unsigned long timeout);

#define LIN_COMMAND_DEF_TIMEOUT 1000
static LinStatus check_for_lin_cable(LinHandleInt lh);
static LinStatus reset_lin_cable(LinHandleInt lh, BOOL wait_for_bootloader);
static LinStatus linSetOperationMode(LinHandleInt lh, BOOL master);
static LinStatus lin_authorize(LinHandleInt lh);
static LinStatus lin_read_version(LinHandleInt lh, BOOL reboot);


void EnterCriticalSection (void *crit)
{
  pthread_mutex_lock(&handleMutex);
}


void LeaveCriticalSection (void *crit)
{
  pthread_mutex_unlock(&handleMutex);
}


static unsigned long timeGetTime (void)
{
  struct timeval tv;
  static struct timeval start;
  gettimeofday(&tv, NULL);
  if (start.tv_sec == 0) {
    start.tv_sec  = tv.tv_sec;
    start.tv_usec = tv.tv_usec;
    return 0;
  } else {
    if (tv.tv_usec >= start.tv_usec) {
      return (tv.tv_sec - start.tv_sec) * 1000 + (tv.tv_usec - start.tv_usec + 500) / 1000;
    } else {
      return (tv.tv_sec - 1 - start.tv_sec) * 1000 + (tv.tv_usec + 1000000 - start.tv_usec + 500) / 1000;
    }
  }
}


static void Sleep (int len)
{
  usleep(len * 1000);
}


//===========================================================================
static BOOL is_supported_lin_hw(unsigned int hwtype)
{
  switch (hwtype) {
    case canHWTYPE_LAPCAN:
    case canHWTYPE_LEAF:
      return TRUE;
    default:
      return FALSE;
  }
}


//===========================================================================
void LINLIBAPI linInitializeLibrary(void)
{
  int i;

  EnterCriticalSection(&crit);
  if (!isInitialized) {
    canInitializeLibrary();

    for (i = 0; i < MAXHANDLES; i++)
      linChannels[i].inUse = 0;
  }
  isInitialized = TRUE;
  LeaveCriticalSection(&crit);
}

//===========================================================================
void LINLIBAPI linUnloadLibrary(void)
{
  int i;

  EnterCriticalSection(&crit);
  canUnloadLibrary();
  for (i = 0; i < MAXHANDLES; i++) {
    linClose(linChannels[i].h);
    linChannels[i].inUse = 0;
  }
  isInitialized = FALSE;
  LeaveCriticalSection(&crit);
}



//===========================================================================
// Return the CAN handle given an open LIN handle
LinStatus LINLIBAPI linGetCanHandle(LinHandle h, unsigned int *canHandle)
{
  LinHandleInt lh;

  if (!canHandle) {
    return linERR_PARAM;
  }

  EnterCriticalSection(&crit);
  if (!ASSIGN_LIN_HANDLE(lh, h)) {
    LeaveCriticalSection(&crit);
    return linERR_INVHANDLE;
  }
  *canHandle = lh->ch;
  LeaveCriticalSection(&crit);

  return linOK;
}

//===========================================================================
static BOOL is_supported_hybrid_hw(int channel)
{
  int stat;
  unsigned int cap = 0;
  unsigned int cap_mask = 0;

  stat = canGetChannelData(channel, canCHANNELDATA_CHANNEL_CAP, &cap, sizeof(cap));
  if (stat != canOK) {
    return 0;
  }
  stat = canGetChannelData(channel, canCHANNELDATA_CHANNEL_CAP_MASK, &cap_mask, sizeof(cap_mask));
  if (stat != canOK) {
    return 0;
  }

  return ((cap & cap_mask & canCHANNEL_CAP_LIN_HYBRID) != 0);
}

//===========================================================================
LinHandle LINLIBAPI linOpenChannel(int channel, int flags)
{
  int i;
  unsigned long hwtype;
  canStatus stat;
  LinHandle h;
  LinHandleInt lh;
  LinStatus lres;
  unsigned int canlibFlags = canOPEN_REQUIRE_INIT_ACCESS;

  EnterCriticalSection(&crit);

  // Locate a free slot in linChannels[].
  for (i = 0; i < MAXHANDLES; i++) {
    if (!linChannels[i].inUse) {
      break;
    }
  }
  if (i == MAXHANDLES) {
    LeaveCriticalSection(&crit);
    return linERR_NOCHANNELS;
  }

  h = i;
  lh = &linChannels[h];
  memset(lh, 0, sizeof(*lh));

  // Assign to linChannels[h]. We doesn't set inUse, so it is left
  // unallocated for a while.
  lh->openMode   = 0;
  lh->canChannel = channel;
  lh->bitrate    = 10000;
  lh->running    = 0;
  lh->canChannel = channel;
  lh->h          = h;

  if (flags & LIN_MASTER) {
    lh->master = 1;
  } else if (flags & LIN_SLAVE)  {
    lh->master = 0;
  } else {
    LeaveCriticalSection(&crit);
    return linERR_PARAM;
  }

  // Check that the CAN channel isn't opened (by us)  before
  for (i = 0; i < MAXHANDLES; i++) {
    if (linChannels[i].inUse && linChannels[i].canChannel == channel) {
      canlibFlags = 0;
    }
  }

  // Check that it is a LIN channel
  stat = canGetChannelData(channel, canCHANNELDATA_CARD_TYPE,
                           &hwtype, sizeof (hwtype));
  if (stat != canOK) {
    LeaveCriticalSection(&crit);
    return linERR_NOTFOUND;
  }

  if (is_supported_hybrid_hw (channel)) {
    canlibFlags |= canOPEN_LIN;
    lh->openMode = canOPEN_LIN;
  } else {
    if (is_supported_lin_hw(hwtype) == FALSE) {
      LeaveCriticalSection(&crit);
      return linERR_NOTFOUND;
    }
  }

  // Now, we could check that there is a LIN transceiver connected to
  // the CAN channel, but this currently doesn't work very well.

  //PRINTF(("canOpenChannel %d initAcces %d\n", channel, canlibFlags));
  lh->ch = canOpenChannel(lh->canChannel, canlibFlags);
  if (lh->ch < 0) {
    LeaveCriticalSection(&crit);
    return linERR_NOTFOUND;
  }

  stat = canSetBusParams(lh->ch, BAUD_500K, 0, 0, 0, 0, 0);
  if (stat < 0) {
    (void)canClose(lh->ch);
    LeaveCriticalSection(&crit);
    return linERR_NOTFOUND;
  }

  stat = canSetBusOutputControl(lh->ch, canDRIVER_NORMAL);
  if (stat < 0) {
    (void)canClose(lh->ch);
    LeaveCriticalSection(&crit);
    return linERR_NOTFOUND;
  }

  stat = canBusOn(lh->ch);
  if (stat < 0) {
    (void)canClose(lh->ch);
    LeaveCriticalSection(&crit);
    return linERR_NOTFOUND;
  }

  stat = check_for_lin_cable(lh);
  if (stat) {
    (void)canClose(lh->ch);
    LeaveCriticalSection(&crit);
    PRINTF(("linOpenChannel err = %d\n", stat));
    return linERR_NOTFOUND;
  }

  // writes to eeprom so rebooting is ok
  lres = linSetOperationMode(lh, lh->master);
  if (lres != linOK) {
    (void)canClose(lh->ch);
    LeaveCriticalSection(&crit);
    return lres;
  }

  // lin_read_version reboots lin-interface
  // but this should be ok here since the only
  // settings made are written to the eeprom
  lres = lin_read_version(lh, 1);
  if (lres != linOK) {
    (void)canClose(lh->ch);
    LeaveCriticalSection(&crit);
    return lres;
  }

  lh->inUse = 1;
  LeaveCriticalSection(&crit);

  return h;
}


//===========================================================================
// Close the LIN channel.
//
LinStatus LINLIBAPI linClose(LinHandle h)
{
  LinHandleInt lh;
  LinStatus r = linOK;

  EnterCriticalSection(&crit);
  if (!ASSIGN_LIN_HANDLE(lh, h)) {
    LeaveCriticalSection(&crit);
    return linERR_INVHANDLE;
  }
  if (lh->running) {
    r = linBusOff(lh->h);
  }
  if (canClose(lh->ch) != canOK) {
    r = linERR_CANERROR;
  }

  lh->inUse = 0;
  LeaveCriticalSection(&crit);
  return r;
}


//===========================================================================
LinStatus LINLIBAPI linGetVersion(int *major, int *minor, int *build)
{
  if (major == NULL || minor == NULL || build == NULL) {
    return linERR_PARAM;
  }

  *major = CANLIB_MAJOR_VERSION;
  *minor = CANLIB_MINOR_VERSION;
  *build = 0;

  return linOK;
}


//===========================================================================
LinStatus LINLIBAPI linGetFirmwareVersion(LinHandle h,
                                          unsigned char *bootVerMajor,
                                          unsigned char *bootVerMinor,
                                          unsigned char *bootVerBuild,
                                          unsigned char *appVerMajor,
                                          unsigned char *appVerMinor,
                                          unsigned char *appVerBuild)
{
  LinHandleInt lh;
  LinStatus    retval = linOK;

  EnterCriticalSection(&crit);
  if (!ASSIGN_LIN_HANDLE(lh, h)) {
    LeaveCriticalSection(&crit);
    return linERR_INVHANDLE;
  }

  if (lh->openMode == canOPEN_LIN) {

    // when it is a hybrid-interface the version will be truncated from short -> char
    // therefore clear the fw revision so we don't return a bad revision
    // use canCHANNELDATA_CARD_FIRMWARE_REV to get the fw revision
    retval = linERR_NOT_IMPLEMENTED;
  } else {
    //return's the pic's fw revison for the leaf
    if (bootVerMajor) {
      *bootVerMajor = (unsigned char)lh->bootVerMajor;
    }
    if (bootVerMinor) {
      *bootVerMinor = (unsigned char)lh->bootVerMinor;
    }
    if (bootVerBuild) {
      *bootVerBuild = (unsigned char)lh->bootVerBuild;
    }
    if (appVerMajor) {
      *appVerMajor = (unsigned char)lh->appVerMajor;
    }
    if (appVerMinor) {
      *appVerMinor = (unsigned char)lh->appVerMinor;
    }
    if (appVerBuild) {
      *appVerBuild = (unsigned char)lh->appVerBuild;
    }
  }

  LeaveCriticalSection(&crit);
  return retval;
}

//===========================================================================
LinStatus LINLIBAPI linGetChannelData(int channel, int item, void *buffer, size_t bufsize)
{
  int channels = 0;

  if (!isInitialized) {
    return linERR_NOTINITIALIZED;
  }

  if ((buffer == NULL) || (bufsize == 0)) {
    return linERR_PARAM;
  }
  canGetNumberOfChannels(&channels);
  if (channel >= channels) {
    return linERR_NOTFOUND;
  }

  switch (item) {
    case linCHANNELDATA_CARD_FIRMWARE_REV:
    {
      int stat;
      uint16 fw[4];

      if (bufsize < sizeof(fw)) {
        return linERR_PARAM;
      }

      stat = canGetChannelData(channel, canCHANNELDATA_CARD_FIRMWARE_REV, &fw, sizeof(fw));
      if (stat != canOK) {
        return linERR_NOCHANNELS;
      }

      memcpy(buffer, fw, sizeof(fw));
      return linOK;
    }
    default:
      return linERR_PARAM;
  }
}


//===========================================================================
LinStatus LINLIBAPI linSetBitrate(LinHandle h, unsigned int bps)
{
  LinHandleInt lh;
  unsigned int bittime = (unsigned int) (4000000.0 / bps + 0.5);
  LinStatus r;

  EnterCriticalSection(&crit);

  if (!ASSIGN_LIN_HANDLE(lh, h)) {
    LeaveCriticalSection(&crit);
    return linERR_INVHANDLE;
  }

  if (lh->running) {
    LeaveCriticalSection(&crit);
    return linERR_RUNNING;
  }


  r = lin_command(lh, COMMAND_EEPROM_WRITE_OLD,
                  command_counter++,
                  EEPROM_LOC_BITTIME_LOW, (uint8)(bittime & 0xff),
                  EEPROM_PROG_PASSWD0,EEPROM_PROG_PASSWD1, 0,0,
                  NULL, LIN_COMMAND_DEF_TIMEOUT);

  if (r == 0) {
    r = lin_command(lh, COMMAND_EEPROM_WRITE_OLD,
                    command_counter++,
                    EEPROM_LOC_BITTIME_HIGH, (uint8)((bittime >> 8) & 0xff),
                    EEPROM_PROG_PASSWD0,EEPROM_PROG_PASSWD1, 0,0,
                    NULL, LIN_COMMAND_DEF_TIMEOUT);
  }

  if (r) {
    PRINTF(("Set LIN bitrate failed\n"));
    LeaveCriticalSection(&crit);
    return r;
  } else {
    LeaveCriticalSection(&crit);
    return linOK;
  }
}


//===========================================================================
/* Reset the cable, authorize and start the application
*/
LinStatus LINLIBAPI linBusOn(LinHandle h)
{
  LinHandleInt lh;
  LinStatus r;

  EnterCriticalSection(&crit);

  if (!ASSIGN_LIN_HANDLE(lh, h)) {
      LeaveCriticalSection(&crit);
    return linERR_INVHANDLE;
  }

  if (lh->openMode == canOPEN_LIN) {
    r = reset_lin_cable(lh, TRUE);
    if (r) {
      LeaveCriticalSection(&crit);
      PRINTF(("Failed resetting LIN cable %d (buson)\n", r));
      return r;
    }
  } else {
    // lin_read_version reboots lin-interface
    // but this should be ok here since the only
    // settings made are written to the eeprom
    r = lin_read_version(lh, 0);
    if (r != linOK) {
      (void)canClose(lh->ch);
      LeaveCriticalSection(&crit);
      return r;
    }
  }

  lh->timing0Valid = FALSE;
  lh->timing1Valid = FALSE;
  lh->timingSValid = FALSE;
  lh->timingByteTime0Valid = FALSE;
  lh->timingByteTime1Valid = FALSE;

  lh->running = 1;

  LeaveCriticalSection(&crit);
  return linOK;
}


//===========================================================================
/* Reset the cable, remain in boot mode
*/
LinStatus LINLIBAPI linBusOff(LinHandle h)
{
  LinHandleInt lh;
  LinStatus r;

  EnterCriticalSection(&crit);

  if (!ASSIGN_LIN_HANDLE(lh, h)) {
    LeaveCriticalSection(&crit);
    return linERR_INVHANDLE;
  }

  if (lh->openMode == canOPEN_LIN) {
    r = reset_lin_cable(lh, FALSE);
  } else {
    // If the cable is busy receiving or transmitting a LIN message, it won't
    // react on the reset command until done.
    // So we transmit a dummy command first.

    r = lin_command(lh,
                  COMMAND_GET_STATUS,
                  command_counter++,
                  0,0,0,0,0,0,
                  NULL, LIN_COMMAND_DEF_TIMEOUT);


    if (r) {
      LeaveCriticalSection(&crit);
      PRINTF(("Reading of status failed\n"));
      return r;
    }
    r = reset_lin_cable(lh, TRUE);
  }

  if (r) {
    PRINTF(("Failed resetting LIN cable in linBusOff()"));
  }

  lh->running = 0;
  LeaveCriticalSection(&crit);
  return r;
}


//===========================================================================
unsigned long LINLIBAPI linReadTimer(LinHandle h)
{
  LinHandleInt lh;

  EnterCriticalSection(&crit);
  if (!ASSIGN_LIN_HANDLE(lh, h)) {
    LeaveCriticalSection(&crit);
    return (unsigned long)linERR_INVHANDLE;
  }
  {
    unsigned long time = 0;
    (void)canReadTimer(lh->ch, &time);  // Can't do anything about error!
    LeaveCriticalSection(&crit);
    return time;
  }
}


//===========================================================================
LinStatus LINLIBAPI linWriteMessage(LinHandle h, unsigned int id,
                                    const void *msg, unsigned int dlc)
{
  LinHandleInt lh;

  EnterCriticalSection(&crit);

  if (!ASSIGN_LIN_HANDLE(lh, h)) {
    LeaveCriticalSection(&crit);
    return linERR_INVHANDLE;
  }

  if (!lh->running) {
    LeaveCriticalSection(&crit);
    return linERR_NOTRUNNING;
  }
  if (!lh->master) {
    PRINTF(("Only master %d\n", lh->ch));
    LeaveCriticalSection(&crit);
    return linERR_MASTERONLY;
  }
  id &= 0x3f; // Remove the parity bits.
  if (canWrite(lh->ch, (long)id, (void*)msg, dlc, canMSG_STD) != canOK) {
    LeaveCriticalSection(&crit);
    return linERR_CANERROR;
  } else {
    LeaveCriticalSection(&crit);
    return linOK;
  }
}


//===========================================================================
LinStatus LINLIBAPI linUpdateMessage(LinHandle h, unsigned int id,
                                     const void *msg, unsigned int dlc)
{
  LinHandleInt lh;

  EnterCriticalSection(&crit);
  if (!ASSIGN_LIN_HANDLE(lh, h)) {
    LeaveCriticalSection(&crit);
    return linERR_INVHANDLE;
  }
  if (!lh->running) {
    LeaveCriticalSection(&crit);
    return linERR_NOTRUNNING;
  }
  if (lh->master) {
    LeaveCriticalSection(&crit);
    return linERR_SLAVEONLY;
  }
  id &= 0x3f; // Remove the parity bits.
  if (canWrite(lh->ch, (long)id, (void*)msg, dlc, canMSG_STD) != canOK) {
    LeaveCriticalSection(&crit);
    return linERR_CANERROR;
  } else {
    LeaveCriticalSection(&crit);
    return linOK;
  }
}


//===========================================================================
/* Setup an illegal slave response.
* Supported from version 2.4.1.
*/
LinStatus LINLIBAPI linSetupIllegalMessage(LinHandle h, unsigned int id,
                                           unsigned int cFlags, unsigned int delay)
{
  LinHandleInt lh;
  LinStatus r;

  EnterCriticalSection(&crit);

  if (!ASSIGN_LIN_HANDLE(lh, h)) {
    LeaveCriticalSection(&crit);
    return linERR_INVHANDLE;
  }

  if (!lh->running) {
    LeaveCriticalSection(&crit);
    return linERR_NOTRUNNING;
  }

  if (LINAPP_VERSION(lh) < VERSION_INT(2, 4, 1)) {
    LeaveCriticalSection(&crit);
    return linERR_VERSION;
  }

  if (id > 64 || delay > 256) {
    LeaveCriticalSection(&crit);
    return linERR_PARAM;
  }

  r = lin_command(lh, COMMAND_SETUP_ILLEGAL_MESSAGE,
                  command_counter++,
                  0, (uint8)id, (uint8)cFlags, (uint8)delay, 0,0,
                  NULL, LIN_COMMAND_DEF_TIMEOUT);

  LeaveCriticalSection(&crit);

  //not all devices has illegal messages
  if (r == linERR_ERRRESP) {
    r = linERR_NOT_IMPLEMENTED;
  }

  return r;
}


//===========================================================================
/* Setup global message flags and settings.
* Supported from version 2.5.1.
*/
LinStatus LINLIBAPI linSetupLIN(LinHandle h, unsigned int lFlags, unsigned int bps)
{
  LinHandleInt lh;
  LinStatus r;
  unsigned int bittime;

  EnterCriticalSection(&crit);
  if (!ASSIGN_LIN_HANDLE(lh, h)) {
    LeaveCriticalSection(&crit);
    return linERR_INVHANDLE;
  }
  if (!lh->running) {
    LeaveCriticalSection(&crit);
    return linERR_NOTRUNNING;
  }
  if (LINAPP_VERSION(lh) < VERSION_INT(2, 5, 1)) {
    LeaveCriticalSection(&crit);
    return linERR_VERSION;
  }
  if (bps) {
    bittime = (unsigned int) (4000000.0 / bps + 0.5);
  } else {
    bittime = 0;
  }
  r = lin_command(lh,
                  COMMAND_SETUP_LIN,
                  command_counter++,
                  0,
                  (uint8)lFlags,
                  (uint8)(bittime & 0xff),
                  (uint8)((bittime >> 8) & 0xff),
                  0,
                  0,
                  NULL,
                  LIN_COMMAND_DEF_TIMEOUT);
  LeaveCriticalSection(&crit);
  return r;
}


//===========================================================================
#define DEFAULT_BREAK_LEN_IN_BITTIME 5
#define HYBRID_WAKEUP_COUNT          0
#define HYBRID_WAKEUP_BREAK_LEN      1
#define HYBRID_WAKEUP_INTERVAL_LOW   2
#define HYBRID_WAKEUP_INTERVAL_HI    3
LinStatus LINLIBAPI linWriteWakeup(LinHandle h,
                                   unsigned int count,
                                   unsigned int interval)
{
  LinHandleInt lh;
  unsigned int dlc;
  unsigned char buf[4];

  EnterCriticalSection(&crit);
  if (!ASSIGN_LIN_HANDLE(lh, h)) {
    LeaveCriticalSection(&crit);
    return linERR_INVHANDLE;
  }
  if (!lh->running) {
    LeaveCriticalSection(&crit);
    return linERR_NOTRUNNING;
  }

  if (lh->openMode == canOPEN_LIN) {
    if (count) {
      if (count > 0xffff || interval > 0xffff) {
        LeaveCriticalSection(&crit);
        return linERR_PARAM;
      }
      // {count, breaklength, interval0, interval1}
      buf[HYBRID_WAKEUP_COUNT]         = count;
      buf[HYBRID_WAKEUP_BREAK_LEN]     = DEFAULT_BREAK_LEN_IN_BITTIME;
      buf[HYBRID_WAKEUP_INTERVAL_LOW]  = (uint8)(interval & 0x00ff);
      buf[HYBRID_WAKEUP_INTERVAL_HI]   = (uint8)((interval & 0xff00)>>8);
      dlc = 4;
    } else {
      dlc = 0;
    }
    if (canWrite(lh->ch, CAN_ID_TX_WAKEUP_HYBRID, buf, dlc, canMSG_STD) != canOK) {
      LeaveCriticalSection(&crit);
      return linERR_CANERROR;
    } else {
      LeaveCriticalSection(&crit);
      return linOK;
    }

    return linOK;
  } // end hybrid

  if (count) {
    if (count > 255 || interval > 255) {
      LeaveCriticalSection(&crit);
      return linERR_PARAM;
    }
    buf[0] = count;
    buf[1] = interval;
    dlc = 2;
  } else {
    dlc = 0;
  }
  if (canWrite(lh->ch, CAN_ID_TX_WAKEUP, buf, dlc, canMSG_STD) != canOK) {
    LeaveCriticalSection(&crit);
    return linERR_CANERROR;
  } else {
    LeaveCriticalSection(&crit);
    return linOK;
  }
}


//===========================================================================
LinStatus LINLIBAPI linClearMessage(LinHandle h, unsigned int id)
{
  LinHandleInt lh;

  EnterCriticalSection(&crit);
  if (!ASSIGN_LIN_HANDLE(lh, h)) {
    LeaveCriticalSection(&crit);
    return linERR_INVHANDLE;
  }
  if (!lh->running) {
    LeaveCriticalSection(&crit);
    return linERR_NOTRUNNING;
  }
  if (lh->master) {
    LeaveCriticalSection(&crit);
    return linERR_SLAVEONLY;
  }
  id &= 0x3f; // Remove the parity bits.
  if (canWrite(lh->ch, (long)id, NULL, 0, canMSG_STD) != canOK) {
    LeaveCriticalSection(&crit);
    return linERR_CANERROR;
  } else {
    LeaveCriticalSection(&crit);
    return linOK;
  }
}


//===========================================================================
LinStatus LINLIBAPI linWriteSync(LinHandle h, unsigned long timeout)
{
  canStatus stat;
  LinHandleInt lh;

  EnterCriticalSection(&crit);
  if (!ASSIGN_LIN_HANDLE(lh, h)) {
    LeaveCriticalSection(&crit);
    return linERR_INVHANDLE;
  }
  if (!lh->running) {
    LeaveCriticalSection(&crit);
    return linERR_NOTRUNNING;
  }

  stat = canWriteSync(lh->ch, timeout);
  if (stat == canERR_TIMEOUT) {
    LeaveCriticalSection(&crit);
    return linERR_TIMEOUT;
  } else if (stat != canOK) {
    LeaveCriticalSection(&crit);
    return linERR_CANERROR;
  }

  /* We now know that there are no queued CAN messages. But we still
  * don't know if everything has been transmitted to the LIN bus or, for
  * that matter, processed by the cable.
  *
  * For updates, processing should be quite fast, but when the master
  * transmits a message, it might take some time.
  *
  * We could add a busy-flag or something in the response to
  * COMMAND_GET_STATUS.  But as long as the message is received after a
  * linWriteMessage(), one should be safe.
  */
  LeaveCriticalSection(&crit);
  return linOK;
}


//===========================================================================
/* Write a LIN header (which a slave is expected to fill in with data).
* Only available in master mode.
*/
LinStatus LINLIBAPI linRequestMessage(LinHandle h, unsigned int id)
{
  LinHandleInt lh;

  EnterCriticalSection(&crit);
  if (!ASSIGN_LIN_HANDLE(lh, h)) {
    LeaveCriticalSection(&crit);
    return linERR_INVHANDLE;
  }
  if (!lh->running) {
    LeaveCriticalSection(&crit);
    return linERR_NOTRUNNING;
  }
  if (!lh->master) {
    LeaveCriticalSection(&crit);
    return linERR_MASTERONLY;
  }
  id &= 0x3f; // Remove the parity bits.
  if (canWrite(lh->ch, (long)id, NULL, 8, canMSG_STD|canMSG_RTR) != canOK) {
    LeaveCriticalSection(&crit);
    return linERR_CANERROR;
  } else {
    LeaveCriticalSection(&crit);
    return linOK;
  }
}


//===========================================================================
/* If a message is received (including echo of what we transmitted ourselves
* with linWriteMessage() if a master or as response to a poll as a slave),
* it is stored in *id / *msg / *dlc / *msgInfo. Returns linOK if a message is
* received, linERR_NOMSG if not.
*/
LinStatus LINLIBAPI linReadMessage(LinHandle h, unsigned int *id,
                                   void *msg, unsigned int *dlc,
                                   unsigned int *flags, LinMessageInfo *msgInfo)
{
  canStatus stat;
  //unsigned long hwtype;
  unsigned long canId;
  unsigned int canFlags;
  unsigned long timestamp;
  unsigned int linFlags;
  LinHandleInt lh;

  EnterCriticalSection(&crit);
  if (!ASSIGN_LIN_HANDLE(lh, h)) {
    LeaveCriticalSection(&crit);
    return linERR_INVHANDLE;
  }
  if (!lh->running) {
    LeaveCriticalSection(&crit);
    return linERR_NOTRUNNING;
  }

  linFlags = 0;
  for (;;) {
    stat = canRead(lh->ch, (long*)&canId, msg, dlc, &canFlags, &timestamp);
    if (stat != canOK) {
      LeaveCriticalSection(&crit);
      return linERR_NOMSG;
    }
    if (canFlags & (canMSG_TXACK | canMSG_TXRQ | canMSG_ERROR_FRAME | canMSG_EXT | canMSG_RTR)) {
      continue; // This is nothing we want.
    }
    if (canId == CAN_ID_TIMING0 && *dlc == 8) {
      lh->timing0Valid = TRUE;
      memcpy(&lh->timing0.ph, msg, 8);
      lh->timestamp0 = timestamp;
    } else if (canId == CAN_ID_TIMING && *dlc == 8) {
      lh->timing1Valid = TRUE;
      memcpy(&lh->timing1.common, msg, 8);
    } else if (canId == CAN_ID_TIMING_SYNCH && *dlc == 8) {
      lh->timingSValid = TRUE;
      memcpy(&lh->timingS, msg, 8);
    } else if (canId == CAN_ID_TIMING_DATA0 && *dlc == 8) {
      lh->timingByteTime0Valid = TRUE;
      memcpy(&lh->timingB.t, msg, 8);
    } else if (canId == CAN_ID_TIMING_DATA1 && *dlc == 8) {
      lh->timingByteTime1Valid = TRUE;
      memcpy(&(lh->timingB.t[4]), msg, 8);
    } else if (canId == CAN_ID_SYNCHERROR) {
      linFlags |= LIN_SYNCH_ERROR;
      break;
    } else if(canId == CAN_ID_RX_WAKEUP) {
      /* We will receive a CAN_ID_RX_WAKEUP if we received a wake up frame
      * as a master or a slave. */
      linFlags |= (LIN_WAKEUP_FRAME | LIN_RX);
      break;
    } else if(canId == CAN_ID_TXE_WAKEUP) {
      /* We will receive a CAN_ID_TXE_WAKEUP both if we transmitted a wake up frame
      * as a master or a slave. */
      linFlags |= (LIN_WAKEUP_FRAME | LIN_TX);
      break;
    } else if (canId == CAN_ID_COMMAND || canId == CAN_ID_RESPONSE || canId == CAN_ID_STATUS) {
      continue;
    } else if (canId == CAN_ID_TERM) {
      continue;
    } else if (canId & 0xc00) {
      // Drop any CAN message that isn't a LIN message.
      // Only the '0x400'-bit can be set for a CAN id, but we check also the high bit.
      continue;
    } else {
      unsigned long canIdM;
      canIdM = canId & 0xfffffc3f;  // set status bits to zero.
      if (canIdM > 63) {
        continue;
      }
      // A LIN message.
      if (!lh->timing0Valid) {
        lh->timestamp0 = timestamp; // At least some kind of timestamp
      }
      if (canId & LINID_DONGLE_RCV) {
        linFlags |= LIN_RX;
      }
      if (canId & LINID_DONGLE_TX) {
        linFlags |= LIN_TX;
      }
      if (canId & LINID_CSUM_ERROR) {
        linFlags |= (LIN_CSUM_ERROR | LIN_PARITY_ERROR);
      }
      if (canId & LINID_NO_DATA) {
        linFlags |= LIN_NODATA;
      }
      if (id) {
        *id = canIdM;
      }
      break;
    }
  }

  // Prepare the message info
  if (msgInfo) {
    memset(msgInfo, 0, sizeof(*msgInfo));
    if (lh->timing0Valid) {
      unsigned long frameStart;

      if (lh->openMode == canOPEN_LIN) {
        if (lh->timing0.hybrid.eof > lh->timing0.hybrid.sof) {
          msgInfo->frameLength = lh->timing0.hybrid.eof - lh->timing0.hybrid.sof;
        } else {
          msgInfo->frameLength = 1048575 + lh->timing0.hybrid.eof - lh->timing0.hybrid.sof;
        }

        msgInfo->frameLength =  msgInfo->frameLength / 5;
        msgInfo->timestamp   = (lh->timestamp0 * 1000 - msgInfo->frameLength + 500) / 1000;
      } else {
        frameStart = (lh->timing0.leaf.frameStart << lh->timing1.common.flags.timeShift)/4; // Unit: us
        msgInfo->timestamp = lh->timestamp0 - (frameStart + 500) / 1000;
      }
    } else {
      msgInfo->timestamp = lh->timestamp0;
    }

    if (lh->timing1Valid) {
      msgInfo->synchBreakLength = (lh->timing1.common.lin_break_length+2)/4;
      msgInfo->checkSum = lh->timing1.common.msgCsum;
      if  (lh->timing1.common.flags.bitError) {
        linFlags |= LIN_BIT_ERROR;
      }

      if (lh->openMode != canOPEN_LIN) {
        msgInfo->frameLength = (lh->timing1.leaf.frameLength << lh->timing1.common.flags.timeShift)/4;
      }

      if (lh->timing1.common.lin_synch_length) {
        msgInfo->bitrate = 4*8*1000000L/lh->timing1.common.lin_synch_length;
      }

      msgInfo->idPar = (*id & 0x3f) + (lh->timing1.common.flags.parity << 6);

      if (linFlags & LIN_WAKEUP_FRAME) { // The frame length is sometimes not correct
        msgInfo->frameLength = msgInfo->synchBreakLength;
      }
    }
    if (lh->timingSValid) {
      int i;
      for (i = 0; i < 4; i++)
        msgInfo->synchEdgeTime[i] = (lh->timingS.t[i]+2)/4;
    }
    if (lh->timingByteTime0Valid && lh->timingByteTime1Valid) {
      int i;
      if (lh->openMode == canOPEN_LIN) {
        uint16 sof;

        switch (lh->timing1.common.flags.timeShift) {
          case  0: sof =  0x0000FFFF & lh->timing0.hybrid.sof; break;
          case  1: sof = (0x0001FFFE & lh->timing0.hybrid.sof) >> 1; break;
          case  2: sof = (0x0003FFFC & lh->timing0.hybrid.sof) >> 2; break;
          case  3: sof = (0x0007FFF8 & lh->timing0.hybrid.sof) >> 3; break;
          case  4: sof = (0x000FFFF0 & lh->timing0.hybrid.sof) >> 4; break;
          default: return linERR_INTERNAL;
        }

        for (i = 0; i < 8; i++) {
          if (sof < lh->timingB.t[i]) {
            msgInfo->byteTime[i] = (unsigned long)lh->timingB.t[i] - (unsigned long)sof;
          } else {
            msgInfo->byteTime[i] = (unsigned long)65535 + (unsigned long)lh->timingB.t[i] - (unsigned long)sof;
          }
          msgInfo->byteTime[i] = (msgInfo->byteTime[i] << (lh->timing1.common.flags.timeShift + 4)) / 80;
        }
      } else {
        for (i = 0; i < 8; i++)
          msgInfo->byteTime[i] = (lh->timingB.t[i] << lh->timing1.common.flags.timeShift)/4;
      }
    }
  }

  if (flags) {
    *flags = linFlags;
  }

  lh->timing0Valid = FALSE;
  lh->timing1Valid = FALSE;
  lh->timingSValid = FALSE;
  lh->timingByteTime0Valid = FALSE;
  lh->timingByteTime1Valid = FALSE;
  LeaveCriticalSection(&crit);
  return linOK;
}


//===========================================================================
/* As linReadMessage(), but wait up to timeout ms.
* Returns linOK if a message is received, linERR_NOMSG if not.
*/
LinStatus LINLIBAPI linReadMessageWait(LinHandle h, unsigned int *id,
                                       void *msg, unsigned int *dlc,
                                       unsigned int *flags,
                                       LinMessageInfo *msgInfo,
                                       unsigned long timeout)
{
  LinStatus lres;
  unsigned long tstart = get_time_1ms();

  for (;;) {
    lres = linReadMessage(h, id, msg, dlc, flags, msgInfo);
    if (lres != linERR_NOMSG) {
      return lres;
    }
    if ((get_time_1ms() - tstart) > timeout) {
      break;
    }
    Sleep(1);
  }
  return linERR_NOMSG;
}


//===========================================================================
/* Wait a while for the boot message from the dongle. Returns non-zero if one is
* received.
* The parameter should be a CAN handle.
*/
static LinStatus waitForBootMsg(LinHandleInt lh)
{
  canStatus stat;
  long id;
  unsigned int dlc, flags;
  unsigned char data[8];
  unsigned long timestamp;
  unsigned long tstart = get_time_1ms();
  unsigned long timeout = 1000;

  for (;;) {
    stat = canRead(lh->ch, &id, data, &dlc, &flags, &timestamp);
    if (stat == canOK) {
      if (((flags & (canMSG_ERROR_FRAME | canMSG_EXT | canMSG_RTR)) == 0) &&
          (dlc == 8) &&
          (id == CAN_ID_STATUS))
      {
        // Check response
        if (strncmp((char*)data, "LIN BOOT", 8) == 0) {
          return linOK;
        }
      }
    } else if ((get_time_1ms() - tstart) > timeout) {
      break;
    }
  }

  return linERR_TIMEOUT;
}


//===========================================================================
/* Check if we have ref-power connected to the lin channel
*/
static LinStatus check_for_lin_refpower(LinHandleInt lh)
{
  LinStatus r;
  unsigned char response[8];

  if (lh->openMode != canOPEN_LIN) {
    return linOK;
  }

  memset(response, 0, sizeof(response));

  r = lin_command(lh, COMMAND_GET_INFO,
                  command_counter++,
                  0,GET_INFO_TYPE_REFPOWER,0,0,0,0, response, LIN_COMMAND_DEF_TIMEOUT);
  if (r) {
    return r;
  }

  // response[3] == 0 -> no power
  if ((response[3] != GET_INFO_TYPE_REFPOWER) || (response[4] == 0)) {
    PRINTF(("No power supplied!\n"));
    return linERR_NO_REF_POWER;
  }

  return linOK;
}


//===========================================================================
/* Verifies that there is a LIN cable connected to the CAN channel.
* The parameter shuold be a CAN handle.
* The cable will be reset and left in boot mode.
*/
static LinStatus check_for_lin_cable_leaf(LinHandleInt lh)
{
  int           retry_count = 3;
  LinStatus     r;

  while (retry_count-- > 0) {
    r = lin_command(lh, COMMAND_START_BOOTLOADER,
                    command_counter++,
                    0,0,0,0,0,0, NULL, 0);

    if (r) {
      return r;
    }

    // Make sure the reset command is transmitted ...
    r = canWriteSync(lh->ch, 1000);
    if (r) {
      return r;
    }

    // ... and wait a bit longer
    Sleep(50);

    r = waitForBootMsg(lh);

    if (r == linOK) {
      return r;
    }
  }
  return linERR_TIMEOUT;
}

//===========================================================================
/* Verifies that there is a LIN cable connected to the CAN channel.
* The parameter shuold be a CAN handle.
* The cable will be reset and left in boot mode.
*/
static LinStatus check_for_lin_cable(LinHandleInt lh)
{
  if (lh->openMode == canOPEN_LIN) {
    return check_for_lin_refpower(lh);
  } else {
    return check_for_lin_cable_leaf(lh);
  }
}

//===========================================================================
static LinStatus reset_lin_cable(LinHandleInt lh, BOOL wait_for_bootloader)
{
  LinStatus r;

  r = lin_command(lh, COMMAND_RESET,
                  command_counter++,
                  0,0,0,0,0,0, NULL, 0);
  if (r == linOK && wait_for_bootloader) {
    // Make sure the reset command is transmitted ...
    (void)canWriteSync(lh->ch, 1000);
    // ... and wait a bit longer
    Sleep(50);
    r = lin_command(lh, COMMAND_START_BOOTLOADER,
                    command_counter++,
                    0,0,0,0,0,0, NULL, 0);
    if (r == linOK) {
      r = waitForBootMsg(lh);
    }
  }

  return r;
} // reset_lin_cable


//===========================================================================
static LinStatus linSetOperationMode(LinHandleInt lh, BOOL master)
{
  LinStatus r;
  uint8 opMode;
  int mode;

  if (lh->running) {
    return linERR_RUNNING;
  }

  mode = (master ? canCHANNEL_IS_LIN_MASTER : canCHANNEL_IS_LIN_SLAVE);
  canIoCtl(lh->ch, canIOCTL_LIN_MODE, &mode, sizeof(mode));
  /* Best effort, don't report error. */

  if (master) {
    opMode = EEPROM_OP_MODE_LIN_MASTER;
  } else {
    opMode = EEPROM_OP_MODE_LIN_SLAVE;
  }

  r = lin_command(lh, COMMAND_EEPROM_WRITE_OLD,
                  command_counter++,
                  EEPROM_LOC_OPERATION_MODE, opMode,
                  EEPROM_PROG_PASSWD0,EEPROM_PROG_PASSWD1, 0,0,
                  NULL, LIN_COMMAND_DEF_TIMEOUT);
  if (r) {
    PRINTF(("Set LIN operation mode failed %d\n", r));
    return r;
  } else
    return linOK;
}


//===========================================================================
static unsigned long get_time_1ms()
{
  return timeGetTime();
}

//===========================================================================
LinStatus LINLIBAPI linGetTransceiverDataHybrid(int            channel,
                                                unsigned char  eanNo[8],
                                                unsigned char  serNo[8],
                                                int           *ttype)
{
  canStatus stat;

  stat = canGetChannelData(channel, canCHANNELDATA_CARD_UPC_NO, eanNo, 8);
  if (stat != canOK) {
    return linERR_CANERROR;
  }

  stat = canGetChannelData(channel, canCHANNELDATA_CARD_SERIAL_NO, serNo, 8);
  if (stat != canOK) {
    return linERR_CANERROR;
  }

  if (ttype) {
    stat = canGetChannelData(channel, canCHANNELDATA_TRANS_TYPE, ttype, sizeof(ttype));
    if (stat != canOK) {
      return linERR_CANERROR;
    }
  }

  return linOK;
}

//===========================================================================
LinStatus LINLIBAPI linGetTransceiverDataLeaf(int            channel,
                                              unsigned char  eanNo[8],
                                              unsigned char  serNo[8],
                                              int           *ttype)
{
  canStatus stat;
  DWORD hwtype;

  stat = canGetChannelData(channel, canCHANNELDATA_CARD_TYPE, &hwtype, sizeof(hwtype));
  if (stat != canOK) {
    return linERR_CANERROR;
  }

  // For the Leaf, card EAN and serial number is returned
  if(hwtype == canHWTYPE_LEAF) {
    stat = canGetChannelData(channel, canCHANNELDATA_CARD_UPC_NO, eanNo, 8);
    if (stat != canOK) {
      return linERR_CANERROR;
    }
    stat = canGetChannelData(channel, canCHANNELDATA_CARD_SERIAL_NO, serNo, 8);
    if (stat != canOK) {
      return linERR_CANERROR;
    }
  }
  // For LAPcan, transceiver EAN and S/N is returned
  else if(hwtype == canHWTYPE_LAPCAN) {
    stat = canGetChannelData(channel, canCHANNELDATA_TRANS_UPC_NO, eanNo, 8);
    if (stat != canOK) {
      return linERR_CANERROR;
    }
    stat = canGetChannelData(channel, canCHANNELDATA_TRANS_SERIAL_NO, serNo, 8);
    if (stat != canOK) {
      return linERR_CANERROR;
    }
  } else {
    return linERR_NOTFOUND;
  }

  // Is this a Leaf Professional LIN?
  if ((*(DWORD *)&eanNo[4] == 0x00073301) &&
      (*(DWORD *)&eanNo[0] == 0x30002692)) {
    *ttype = canTRANSCEIVER_TYPE_LIN;
  } else {
    *ttype = 0;   // We're not interested in anything besides LIN, so...
  }
  return linOK;
}


//===========================================================================
LinStatus LINLIBAPI linGetTransceiverData(int            channel,
                                          unsigned char  eanNo[8],
                                          unsigned char  serNo[8],
                                          int           *ttype)
{
  if (is_supported_hybrid_hw(channel)) {
    return linGetTransceiverDataHybrid(channel, eanNo, serNo, ttype);
  } else {
    return linGetTransceiverDataLeaf(channel, eanNo, serNo, ttype);
  }
}

//===========================================================================
#define CRCPOLY32 0xedb88320L    /* CRC-32 polynomial */
/*
 * CRC32 Using the polynominal 0x04c11db7 (which is reversed to
 * 0x0xedb88320 in the algorithm).
 *
 * Used in pkzip, Ethernet etc.
 * Test values (when using the initial value 0xffffffff and inverting at the end):
 *   Input        Output
 *   ----------   ------
 *   "abc"       0x352441c2
 *   "123456789" 0xcbf43926
 */
static unsigned long crc32one(unsigned char b, unsigned long crc)
{
  unsigned int i, z;
  for (i = 0; i < 8; i++) {
    z = b ^ crc;
    crc >>= 1;
    if (z & 1) {
      crc ^= CRCPOLY32;
    }
    b >>= 1;
  }
  return crc;
}

//===========================================================================
static LinStatus lin_read_version_hybrid(LinHandleInt lh, BOOL reboot)
{
  int    stat;
  uint16 fw[4];

  stat = canGetChannelData(lh->canChannel, canCHANNELDATA_CARD_FIRMWARE_REV, &fw, sizeof(fw));
  if (stat != canOK) {
    return linERR_NOCHANNELS;
  }

  lh->bootVerMajor = 0;
  lh->bootVerMinor = 0;
  lh->bootVerBuild = 0;

  lh->appVerMajor = fw[3];
  lh->appVerMinor = fw[2];
  lh->appVerBuild = fw[0];

  return linOK;
}

//===========================================================================
//
// note that this function reboots the LIN interface so
// states etc is not kept intact
// the version is updated in the lh context
//
static LinStatus lin_read_version_leaf(LinHandleInt lh, BOOL reboot)
{
  LinStatus r;
  unsigned char response[8];

  // make sure we reside in the bootcode
  r = reset_lin_cable(lh, TRUE);
  if (r) {
    LeaveCriticalSection(&crit);
    PRINTF(("Failed resetting LIN cable %d\n", r));
    return r;
  }

  // Get the boot version
  r = lin_command(lh,
                  COMMAND_GET_VERSION,
                  command_counter++,
                  0,0,0,0,0,0,
                  response, LIN_COMMAND_DEF_TIMEOUT);

  if (r) {
    PRINTF(("Error reading the boot version!\n"));
    LeaveCriticalSection(&crit);
    return r;
  }

  lh->bootVerMajor = response[3];
  lh->bootVerMinor = response[4];
  lh->bootVerBuild = response[5];

  r = lin_authorize(lh);
  if (r) {
    LeaveCriticalSection(&crit);
    PRINTF(("Failed connecting to the application\n"));
    return r;
  }

  // The dongle/lief should now be starting the application code if
  // autostart was set to 1, or be ready to answer to the command
  // COMMAND_START_APPLICATION.

  Sleep(10); // Give the application some time to start
  r = lin_command(lh,
                  COMMAND_START_APPLICATION,
                  command_counter++,
                  0,0,0,0,0,0, NULL, LIN_COMMAND_DEF_TIMEOUT);


  if (r) {
    LeaveCriticalSection(&crit);
    PRINTF(("Start of application failed\n"));
    return r;
  }

  // Give the application some time to start
  Sleep(10);

  // Verify that the application really is running:
  r = lin_command(lh,
                  COMMAND_GET_STATUS,
                  command_counter++,
                  0,0,0,0,0,0,
                  response, LIN_COMMAND_DEF_TIMEOUT);
  if (r) {
    LeaveCriticalSection(&crit);
    PRINTF(("Reading of status failed\n"));
    return r;
  }
  if (response[3] != LINSTAT_APP) {
    LeaveCriticalSection(&crit);
    PRINTF(("Error: the application isn't running!\n"));
    return linERR_WRONGRESP;
  }

  r = lin_command(lh,
                  COMMAND_GET_VERSION,
                  command_counter++,
                  0,0,0,0,0,0,
                  response, LIN_COMMAND_DEF_TIMEOUT);
  if (r) {
    PRINTF(("Error reading the application version!\n"));
    LeaveCriticalSection(&crit);
    return r;
  }
  lh->appVerMajor = response[3];
  lh->appVerMinor = response[4];
  lh->appVerBuild = response[5];

  if (reboot) {
    // reset to put it back in normal state
    r = reset_lin_cable(lh, TRUE);
    if (r) {
      LeaveCriticalSection(&crit);
      PRINTF(("Failed resetting LIN cable %d\n", r));
      return r;
    }
  }
  return linOK;
}

//===========================================================================
//
// note that this function reboots the LIN interface so
// states etc is not kept intact
// the version is updated in the lh context
//
static LinStatus lin_read_version(LinHandleInt lh, BOOL reboot)
{
  if (lh->openMode == canOPEN_LIN) {
    return lin_read_version_hybrid(lh, reboot);
  } else {
    return lin_read_version_leaf(lh, reboot);
  }
}


//===========================================================================
static LinStatus lin_authorize_hybrid(LinHandleInt lh)
{
  // no point for HYBRID
  return linOK;
}

//===========================================================================
static LinStatus lin_authorize_leaf(LinHandleInt lh)
{
  LinStatus r;
  unsigned char buf[8];
  unsigned long rand;
  unsigned char *randV = (unsigned char*)&rand;
  unsigned char salt;
  int i;

  r = lin_command(lh, COMMAND_HELLO,
                  command_counter++,
                  0,0,0,0,0,0,
                  buf, LIN_COMMAND_DEF_TIMEOUT);
  if (r) {
    return r;
  }

  rand = 0xe62f65e1; // Our secret key
  // We place a random number as the last byte in the response which is also
  // mixed into the 32 bit response. This way, if someone emulates a slave and
  // transmits different challenges to us, each response from us will differ.
  salt = (unsigned char)(get_time_1ms() & 0xff);
  rand = crc32one(salt, rand);
  for (i = 0; i < 4; i++)
    rand = crc32one(buf[3+i], rand);

  r = lin_command(lh, COMMAND_CERTIFY,
                  command_counter++,
                  0, randV[0], randV[1], randV[2], randV[3], salt,
                  NULL, LIN_COMMAND_DEF_TIMEOUT);
  if (r) {
    PRINTF(("Error connecting to LIN cable\n"));
  }
  return r;
}

//===========================================================================
static LinStatus lin_authorize(LinHandleInt lh)
{
  if (lh->openMode == canOPEN_LIN) {
    return lin_authorize_hybrid(lh);
  } else {
    return lin_authorize_leaf(lh);
  }
}


//===========================================================================
/* Transmit a command to the cable.
* If timeout is non-zero, a response is expected and waited for this many ms.
* If response is non-zero, the response is stored there.
*/
static LinStatus lin_command(LinHandleInt lh,
                             uint8 command, unsigned int cmdCounter,
                             uint8 p1, uint8 p2, uint8 p3,
                             uint8 p4, uint8 p5, uint8 p6,
                             unsigned char *response, unsigned long timeout)
{

  canStatus stat;
  long id;
  unsigned int dlc, flags;
  unsigned char data[8];
  unsigned long tstart;
  unsigned long timestamp;

  data[0] = command;
  data[1] = (unsigned char)cmdCounter;
  data[2] = p1; data[3] = p2; data[4] = p3;
  data[5] = p4; data[6] = p5; data[7] = p6;
  stat = canWrite(lh->ch, CAN_ID_COMMAND, data, 8, canMSG_STD);

  if (stat != canOK) {
    PRINTF(("lin_command: canWrite() failed\n"));
    return linERR_CANERROR;
  }

  if (timeout == 0 && response == NULL) {
    return linOK;
  }

  tstart = get_time_1ms();
  for (;;) {
    stat = canReadWait(lh->ch, &id, data, &dlc, &flags, &timestamp, 1);
    if (stat == canOK) {
      if ((flags & (canMSG_ERROR_FRAME | canMSG_EXT | canMSG_RTR)) == 0 &&
          id == CAN_ID_RESPONSE &&
          dlc == 8 &&
          data[0] == command &&
          data[1] == (unsigned char)cmdCounter) {
        if (response) {
          memcpy(response, data, 8);
        }

        // Check the response
        if (data[2] != 0) {
          PRINTF(("lin_command: command 0x%02x, counter=%d returned status=%d\n",
                  command, cmdCounter, data[2]));
          return linERR_ERRRESP;
        }
        return linOK;
      }
    } else if ((get_time_1ms() - tstart) > timeout) {
      break;
    }
  }

  PRINTF(("lin_command: timeout in command 0x%02x, counter=%d\n", command, cmdCounter));
  return linERR_TIMEOUT;
}


