/*
**            Copyright 2017-2018 by Kvaser AB, Molndal, Sweden
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

#include "canlib.h"
#include "canlib_version.h"
#include "canIfData.h"
#include "canlib_data.h"

#include "vcan_ioctl.h"    // Need this for IOCtl to check # channels
#include "vcanevt.h"

#include "VCanFunctions.h"
#include "debug.h"


#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <sys/ioctl.h>

#if DEBUG
#   define DEBUGPRINT(args) printf args
#else
#   define DEBUGPRINT(args)
#endif

#define MFGNAME_ASCII "KVASER AB"



static const char *errorStrings[] = {
  "No error",                        // canOK
  "Error in parameter",              // canERR_PARAM
  "No messages available",           // canERR_NOMSG
  "Specified device not found",      // canERR_NOTFOUND
  "Out of memory",                   // canERR_NOMEM
  "No channels available",           // canERR_NOCHANNELS
  "Interrupted by signal",           // canERR_INTERRUPTED
  "Timeout occurred",                // canERR_TIMEOUT
  "Library not initialized",         // canERR_NOTINITIALIZED
  "No more handles",                 // canERR_NOHANDLES
  "Handle is invalid",               // canERR_INVHANDLE
  "Unknown error (-11)",             // canERR_INIFILE
  "CAN driver type not supported",   // canERR_DRIVER
  "Transmit buffer overflow",        // canERR_TXBUFOFL
  "Unknown error (-14)",             // canERR_RESERVED_1
  "A hardware error was detected",   // canERR_HARDWARE
  "Can not find requested DLL",      // canERR_DYNALOAD
  "DLL seems to be wrong version",   // canERR_DYNALIB
  "Error initializing DLL or driver", // canERR_DYNAINIT
  "Operation not supported by hardware or firmware", // canERR_NOT_SUPPORTED
  "Unknown error (-20)",             // canERR_RESERVED_5
  "Unknown error (-21)",             // canERR_RESERVED_6
  "Unknown error (-22)",             // canERR_RESERVED_2
  "Can not load or open the device driver", // canERR_DRIVERLOAD
  "The I/O request failed, probably due to resource shortage", //canERR_DRIVERFAILED
  "Unknown error (-25)",             // canERR_NOCONFIGMGR
  "Card not found",                  // canERR_NOCARD
  "Unknown error (-27)",             // canERR_RESERVED_7
  "Config not found",                // canERR_REGISTRY
  "The license is not valid",        // canERR_LICENSE
  "Internal error in the driver",    // canERR_INTERNAL
  "Access denied",                   // canERR_NO_ACCESS
  "Not implemented",                 // canERR_NOT_IMPLEMENTED
  "Device File error",               // canERR_DEVICE_FILE
  "Host File error",                 // canERR_HOST_FILE
  "Disk error",                      // canERR_DISK
  "CRC error",                       // canERR_CRC
  "Config error",                    // canERR_CONFIG
  "Memo failure",                    // canERR_MEMO_FAIL
  "Script error",                    // canERR_SCRIPT_FAIL
  "Script version mismatch",         // canERR_SCRIPT_WRONG_VERSION
  "Script container version mismatch",  // canERR_SCRIPT_TXE_CONTAINER_VERSION
  "Script container format error",   // canERR_SCRIPT_TXE_CONTAINER_FORMAT
  "Buffer provided too small to hold data",  // canERR_BUFFER_TOO_SMALL
};

struct dev_descr {
   char * descr_string;
   unsigned int ean[2];
};


#define CRITICAL_SECTION pthread_mutex_t
typedef struct list_entry_s {
  struct list_entry_s *Flink;
  struct list_entry_s *Blink;
} LIST_ENTRY;

#define CONTAINING_RECORD(A, T, F) ((T *)((char *)(A) - (long)&(((T *)0)->F)))

typedef struct kvTimeDomainHead_s {
  CRITICAL_SECTION critSect;
  LIST_ENTRY headOfAllHeads;
} kvTimeDomainHead;

typedef struct kvTimeDomainInternal_s {
  LIST_ENTRY link;
  LIST_ENTRY magiSynced;
  LIST_ENTRY nonMagiSynced;
} kvTimeDomainInternal;

typedef struct kvTimeDomainMagisynced_s {
  LIST_ENTRY link;
  LIST_ENTRY head;
  unsigned softsync_group;
} kvTimeDomainMagisynced;

typedef struct kvTimeDomainNonMagisynced_s {
  LIST_ENTRY link;
  LIST_ENTRY head;
  uint64_t ean;
  unsigned sn;
} kvTimeDomainNonMagisynced;

typedef struct kvTimeDomainHandleData_s {
  LIST_ENTRY link;
  int handle;
} kvTimeDomainHandleData;

#define PRINTF_TIMEDOMAIN(x) DEBUGPRINT(x)
static kvTimeDomainHead  timeDomains;

#define FALSE 0
#define TRUE 1



static int Initialized = FALSE;

// This has to be modified if we add/remove drivers.
static const char *dev_name[] = {"lapcan", "pcican", "pcicanII",
                                 "usbcanII", "leaf", "mhydra",
                                 "pciefd",
                                 "kvvirtualcan"}; // Virtual channels should always be last
static const char *off_name[] = {"LAPcan", "PCIcan", "PCIcanII",
                                 "USBcanII", "Leaf", "Minihydra",
                                 "PCIe CAN",
                                 "VIRTUALcan"}; // Virtual channels should always be last
static struct dev_descr dev_descr_list[] = {
          {"Kvaser Unknown",                                    {0x00000000, 0x00000000}},
          {"Kvaser Virtual CAN",                                {0x00000000, 0x00000000}},
          {"Kvaser PCIcan-S, 1*HS",                             {0x30000827, 0x00073301}},
          {"Kvaser PCIcan-D, 2*HS",                             {0x30000834, 0x00073301}},
          {"Kvaser PCIcan-Q, 4 *HS drivers",                    {0x30000841, 0x00073301}},
          {"Kvaser PCIcan II S",                                {0x30001565, 0x00073301}},
          {"Kvaser PCIcan II D",                                {0x30001572, 0x00073301}},
          {"Kvaser USBcan II HS (S)",                           {0x30001589, 0x00073301}},
          {"Kvaser USBcan II HS/HS",                            {0x30001596, 0x00073301}},
          {"Kvaser Memorator HS/LS",                            {0x30001701, 0x00073301}},
          {"Kvaser USBcan II HS/LS",                            {0x30001749, 0x00073301}},
          {"Kvaser Memorator HS/HS",                            {0x30001756, 0x00073301}},
          {"Kvaser USBcan Rugged HS",                           {0x30001800, 0x00073301}},
          {"Kvaser USBcan Rugged HS/HS",                        {0x30001817, 0x00073301}},
          {"Kvaser PCIcanP-Swc",                                {0x30002210, 0x00073301}},
          {"Kvaser USBcan II HS-SWC",                           {0x30002319, 0x00073301}},
          {"Kvaser Memorator HS-SWC",                           {0x30002340, 0x00073301}},
          {"Kvaser Leaf Light HS",                              {0x30002418, 0x00073301}},
          {"Kvaser Leaf SemiPro HS",                            {0x30002425, 0x00073301}},
          {"Kvaser Leaf Professional HS",                       {0x30002432, 0x00073301}},
          {"Kvaser Leaf SemiPro LS",                            {0x30002609, 0x00073301}},
          {"Kvaser Leaf Professional LSS",                      {0x30002616, 0x00073301}},
          {"Kvaser Leaf SemiPro SWC",                           {0x30002630, 0x00073301}},
          {"Kvaser Leaf Professional SWC",                      {0x30002647, 0x00073301}},
          {"Kvaser Leaf Professional LIN",                      {0x30002692, 0x00073301}},
          {"Kvaser PCIcanx 4xHS",                               {0x30003309, 0x00073301}},
          {"Kvaser PCIcanx HS/HS",                              {0x30003316, 0x00073301}},
          {"Kvaser PCIcanx HS",                                 {0x30003323, 0x00073301}},
          {"Kvaser PCIcanx II 2*HS",                            {0x30003439, 0x00073301}},
          {"Kvaser PCIcanx II 1*HS, combo",                     {0x30003446, 0x00073301}},
          {"Kvaser Memorator Professional (HS/HS)",             {0x30003514, 0x00073301}},
          {"Kvaser USBcan Professional (HS/HS)",                {0x30003576, 0x00073301}},
          {"Kvaser Leaf Light HS with OBDII connector",         {0x30004023, 0x00073301}},
          {"Kvaser Leaf SemiPro HS with OBDII connector",       {0x30004030, 0x00073301}},
          {"Kvaser Leaf Professional HS with OBDII connector",  {0x30004047, 0x00073301}},
          {"Kvaser PCIEcan HS/HS",                              {0x30004054, 0x00073301}},
          {"Kvaser Leaf Light GI (Galvanic Isolation)",         {0x30004115, 0x00073301}},
          {"Kvaser USBcan Professional HS/HS, with (standard) RJ45 connectors",   {0x30004139, 0x00073301}},
          {"Kvaser Memorator Professional HS/LS",               {0x30004177, 0x00073301}},
          {"Kvaser Leaf Light Rugged HS",                       {0x30004276, 0x00073301}},
          {"Kvaser Leaf Light HS China",                        {0x30004351, 0x00073301}},
          {"Kvaser BlackBird SemiPro HS",                       {0x30004412, 0x00073301}},
          {"Kvaser BlackBird SemiPro 3xHS",                     {0x30004467, 0x00073301}},
          {"Kvaser BlackBird SemiPro HS/HS",                    {0x30004535, 0x00073301}},
          {"Kvaser Memorator R SemiPro",                        {0x30004900, 0x00073301}},
          {"Kvaser Leaf SemiPro Rugged HS",                     {0x30005068, 0x00073301}},
          {"Kvaser Leaf Professional Rugged HS",                {0x30005099, 0x00073301}},
          {"Kvaser Memorator Light HS",                         {0x30005136, 0x00073301}},
          {"Kvaser Memorator Professional CB",                  {0x30005815, 0x00073301}},
          {"Kvaser Eagle",                                      {0x30005679, 0x00073301}},
          {"Kvaser Leaf Light GI (Medical)",                    {0x30005686, 0x00073301}},
          {"Kvaser USBcan Pro SHS/HS",                          {0x30005716, 0x00073301}},
          {"Kvaser USBcan Pro SHS/SHS",                         {0x30005723, 0x00073301}},
          {"Kvaser USBcan R",                                   {0x30005792, 0x00073301}},
          {"Kvaser BlackBird SemiPro",                          {0x30006294, 0x00073301}},
          {"Kvaser BlackBird v2",                               {0x30006713, 0x00073301}},
          {"Kvaser USBcan Professional CB",                     {0x30006843, 0x00073301}},
          {"Kvaser Leaf Light v2",                              {0x30006850, 0x00073301}},
          {"Kvaser Mini PCI Express HS",                        {0x30006881, 0x00073301}},
          {"Kvaser PCIEcan 4xHS",                               {0x30006829, 0x00073301}},
          {"Kvaser Leaf Light HS v2 OEM",                       {0x30007352, 0x00073301}},
          {"Kvaser Ethercan Light HS",                          {0x30007130, 0x00073301}},
          {"Kvaser Mini PCI Express 2xHS",                      {0x30007437, 0x00073301}},
          {"Kvaser USBcan Light 2xHS",                          {0x30007147, 0x00073301}},
          {"Kvaser PCIEcan 4xHS",                               {0x30006836, 0x00073301}},
          {"Kvaser Memorator Pro 5xHS",                         {0x30007789, 0x00073301}},
          {"Kvaser USBcan Pro 5xHS",                            {0x30007796, 0x00073301}},
          {"Kvaser USBcan Light 4xHS",                          {0x30008311, 0x00073301}},
          {"Kvaser Leaf Pro HS v2",                             {0x30008434, 0x00073301}},
          {"Kvaser USBcan Pro 2xHS v2",                         {0x30007529, 0x00073301}},
          {"Kvaser Memorator 2xHS v2",                          {0x30008212, 0x00073301}},
          {"Kvaser Memorator Pro 2xHS v2",                      {0x30008199, 0x00073301}},
          {"Kvaser PCIEcan 2xHS v2",                            {0x30008618, 0x00073301}},
          {"Kvaser PCIEcan HS v2",                              {0x30008663, 0x00073301}},
          {"Kvaser USBcan Pro 2xHS v2 CB",                      {0x30008779, 0x00073301}},
          {"Kvaser Leaf Light HS v2 M12",                       {0x30008816, 0x00073301}},
          {"Kvaser USBcan R v2",                                {0x30009202, 0x00073301}},
          {"Kvaser Leaf Light R v2",                            {0x30009219, 0x00073301}},
          {"Kvaser Hybrid 2xCAN/LIN",                           {0x30009653, 0x00073301}},
          {"ATI Leaf Light HS v2",                              {0x30009493, 0x00073301}},
          {"ATI USBcan Pro 2xHS v2",                            {0x30009691, 0x00073301}},
          {"ATI Memorator Pro 2xHS v2",                         {0x30009714, 0x00073301}},
          {"Kvaser Mini PCI Express 2xHS v2",                   {0x30010291, 0x00073301}},
          {"Kvaser Mini PCI Express HS v2",                     {0x30010383, 0x00073301}},
          {"Kvaser Hybrid Pro 2xCAN/LIN",                       {0x30010420, 0x00073301}},
          {"Kvaser BlackBird Pro HS v2",                        {0x30009837, 0x00073301}},
          {"Kvaser Ethercan HS",                                {0x30009769, 0x00073301}}
};

static canStatus check_bitrate (const CanHandle hnd, unsigned int bitrate);

//******************************************************
// Find out channel specific data
//******************************************************
static
canStatus getDevParams (int channel, char devName[], int *devChannel,
                        CANOps **canOps, char officialName[])
{
  // For now, we just count the number of /dev/%s%d files (see dev_name),
  // where %d is numbers between 0 and 255.
  // This is slow!

  int         chanCounter = 0;
  int         devCounter  = 0;
  struct stat stbuf;

  unsigned n = 0;

  int CardNo          = -1;
  int ChannelNoOnCard = 0;
  int ChannelsOnCard  = 0;
  int err;
  int fd;

  for(n = 0; n < sizeof(dev_name) / sizeof(*dev_name); n++) {
    CardNo = -1;
    ChannelNoOnCard = 0;
    ChannelsOnCard = 0;

    // There are 256 minor inode numbers
    for(devCounter = 0; devCounter <= 255; devCounter++) {
      snprintf(devName, DEVICE_NAME_LEN, "/dev/%s%d", dev_name[n], devCounter);
      if (stat(devName, &stbuf) != -1) {  // Check for existance

        if (!ChannelsOnCard) {
          err = 1;
          fd = open(devName, O_RDONLY);
          if (fd != -1) {
            err = ioctl(fd, VCAN_IOC_GET_NRCHANNELS, &ChannelsOnCard);
            close(fd);
          }
          if (err) {
            ChannelsOnCard = 1;
          } else {
            ChannelNoOnCard = 0;
            CardNo++;
          }
        } else {
          ChannelNoOnCard++;
        }
        ChannelsOnCard--;

        if (chanCounter++ == channel) {
          *canOps = &vCanOps;
          sprintf(officialName, "KVASER %s channel %d", off_name[n], devCounter);
          *devChannel = ChannelNoOnCard;

          errno = 0; // Calling stat() may set errno.
          return canOK;
        }
      }
      else {
        // Handle gaps in device numbers
        continue;
      }
    }
  }

  DEBUGPRINT((TXT("return canERR_NOTFOUND\n")));
  devName[0]  = 0;
  *devChannel = -1;
  *canOps     = NULL;

  return canERR_NOTFOUND;
}

//
// API FUNCTIONS
//

//******************************************************
// Open a can channel
//******************************************************
CanHandle CANLIBAPI canOpenChannel (int channel, int flags)
{
  canStatus          status;
  HandleData         *hData;
  CanHandle          hnd;
  const int validFlags = canOPEN_EXCLUSIVE           | canOPEN_REQUIRE_EXTENDED |
                         canOPEN_ACCEPT_VIRTUAL      | canOPEN_ACCEPT_LARGE_DLC |
                         canOPEN_CAN_FD              | canOPEN_CAN_FD_NONISO    |
                         canOPEN_INTERNAL_L/*LIN*/   | canOPEN_NO_INIT_ACCESS   |
                         canOPEN_REQUIRE_INIT_ACCESS | canOPEN_OVERRIDE_EXCLUSIVE;

  if ((flags & ~validFlags) != 0) {
    return canERR_PARAM;
  }

  hData = (HandleData *)malloc(sizeof(HandleData));
  if (hData == NULL) {
    DEBUGPRINT((TXT("ERROR: cannot allocate memory (%d)\n"),
                (int)sizeof(HandleData)));
    return canERR_NOMEM;
  }

  memset(hData, 0, sizeof(HandleData));

  hData->isExtended       = flags & canOPEN_REQUIRE_EXTENDED;

  if (flags & canOPEN_CAN_FD_NONISO)
    hData->openMode = OPEN_AS_CANFD_NONISO;
  else if (flags & canOPEN_CAN_FD)
    hData->openMode = OPEN_AS_CANFD_ISO;
  else if (flags & canOPEN_INTERNAL_L/*LIN*/)
    hData->openMode = OPEN_AS_LIN;
  else
    hData->openMode = OPEN_AS_CAN;

  hData->acceptLargeDlc      = ((flags & canOPEN_ACCEPT_LARGE_DLC)    != 0);
  hData->wantExclusive       = ((flags & canOPEN_EXCLUSIVE)           != 0);
  hData->overrideExclusive   = ((flags & canOPEN_OVERRIDE_EXCLUSIVE)  != 0);
  hData->acceptVirtual       = ((flags & canOPEN_ACCEPT_VIRTUAL)      != 0);
  hData->requireInitAccess   = ((flags & canOPEN_REQUIRE_INIT_ACCESS) != 0);
  hData->initAccess          = ((flags & canOPEN_NO_INIT_ACCESS)      == 0);

  hData->notifyFd   = canINVALID_HANDLE;
  hData->valid      = TRUE;
  hData->auto_reset = 1;

  status = getDevParams(channel,
                        hData->deviceName,
                        &hData->channelNr,
                        &hData->canOps,
                        hData->deviceOfficialName);

  if (status < 0) {
    DEBUGPRINT((TXT("getDevParams ret %d\n"), status));
    free(hData);
    return status;
  }

  status = hData->canOps->openChannel(hData);
  if (status < 0) {
    DEBUGPRINT((TXT("openChannel ret %d\n"), status));
    free(hData);
    return status;
  }

  hnd = insertHandle(hData);

  if (hnd < 0) {
    DEBUGPRINT((TXT("insertHandle ret %d\n"), hnd));
    close(hData->fd);
    free(hData);
    return canERR_NOMEM;
  }

  return hnd;
}


//******************************************************
// Close can channel
//******************************************************
int CANLIBAPI canClose (const CanHandle hnd)
{
  HandleData *hData;
  canStatus stat;

  // Try to go Bus Off before closing
  stat = canBusOff(hnd);
  if (stat != canOK) {
    if (errno == ESHUTDOWN) { // The device was probably removed,
                              // continue with the cleanup.
      DEBUGPRINT((TXT("canClose: canBusOff failed"
                      "with stat=%d and errno=%m (%d),"
                      "device was probably removed.\n"),
                 stat, errno));
    } else {
      return stat;
    }
  }

  stat = canSetNotify(hnd, NULL, 0, NULL);
  if (stat != canOK) {
    return stat;
  }


  hData = removeHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  if (close(hData->fd) != 0) {
    return canERR_INVHANDLE;
  }

  free(hData);

  return canOK;
}


//******************************************************
// Get raw handle/file descriptor to use in system calls
//******************************************************
canStatus CANLIBAPI canGetRawHandle (const CanHandle hnd, void *pvFd)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }
  *(int *)pvFd = hData->fd;

  return canOK;
}


//******************************************************
// Go on bus
//******************************************************
canStatus CANLIBAPI canBusOn (const CanHandle hnd)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->busOn(hData);
}


//******************************************************
// Go bus off
//******************************************************
canStatus CANLIBAPI canBusOff (const CanHandle hnd)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  errno = 0;
  return hData->canOps->busOff(hData);
}


//******************************************************
// Try to "reset" the CAN bus.
//******************************************************
canStatus CANLIBAPI canResetBus (const CanHandle hnd)
{
  canStatus stat;
  unsigned long handle_status;

  stat = canReadStatus(hnd, &handle_status);
  if (stat < 0) {
    return stat;
  }
  stat = canBusOff(hnd);
  if (stat < 0) {
    return stat;
  }
  if ((handle_status & canSTAT_BUS_OFF) == 0) {
    stat = canBusOn(hnd);
  }

  return stat;
}


//******************************************************
// Set bus parameters
//******************************************************
canStatus CANLIBAPI
canSetBusParams (const CanHandle hnd, long freq, unsigned int tseg1,
                 unsigned int tseg2, unsigned int sjw,
                 unsigned int noSamp, unsigned int syncmode)
{
  canStatus ret;
  HandleData *hData;
  long freq_brs;
  unsigned int tseg1_brs, tseg2_brs, sjw_brs;

  if ((noSamp != 3) && (noSamp != 1) && (noSamp != 0)) {
    return canERR_PARAM;
  }

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  if (freq < 0) {
    ret = canTranslateBaud(&freq, &tseg1, &tseg2, &sjw, &noSamp, &syncmode);
    if (ret != canOK) {
      return ret;
    }
  }

  ret = check_bitrate (hnd, (unsigned int)freq);
  if (ret != canOK) {
    return ret;
  }

  ret = hData->canOps->getBusParams(hData, NULL, NULL, NULL, NULL, NULL,
                                    &freq_brs, &tseg1_brs, &tseg2_brs,
                                    &sjw_brs, NULL);
  if (ret != canOK) {
    return ret;
  }

  return hData->canOps->setBusParams(hData, freq, tseg1, tseg2, sjw, noSamp,
                                     freq_brs, tseg1_brs, tseg2_brs, sjw_brs,
                                     syncmode);
}

//******************************************************
// Set CAN FD bus parameters
//******************************************************
canStatus CANLIBAPI
canSetBusParamsFd (const CanHandle hnd, long freq_brs, unsigned int tseg1_brs,
                   unsigned int tseg2_brs, unsigned int sjw_brs)
{
  canStatus ret;
  HandleData *hData;
  long freq;
  unsigned int tseg1, tseg2, sjw, noSamp, syncmode;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  if ((OPEN_AS_CANFD_ISO != hData->openMode) && (OPEN_AS_CANFD_NONISO != hData->openMode)) {
    return canERR_INVHANDLE;
  }

  ret = hData->canOps->getBusParams(hData, &freq, &tseg1, &tseg2, &sjw, &noSamp,
                                    NULL, NULL, NULL, NULL, &syncmode);
  if (ret != canOK) {
    return ret;
  }

  if (freq_brs < 0) {
    ret = canTranslateBaud(&freq_brs, &tseg1_brs, &tseg2_brs, &sjw_brs, &noSamp, &syncmode);
    if (ret != canOK) {
      return ret;
    }
  }

  ret = check_bitrate (hnd, (unsigned int)freq_brs);

  if (ret != canOK) {
    return ret;
  }

  ret = hData->canOps->setBusParams(hData, freq, tseg1, tseg2, sjw, noSamp,
                                    freq_brs, tseg1_brs, tseg2_brs, sjw_brs,
                                    syncmode);
  return ret;
}

canStatus CANLIBAPI
canSetBusParamsC200 (const CanHandle hnd, unsigned char btr0, unsigned char btr1)
{
  canStatus    ret;
  long         bitrate;
  unsigned int tseg1;
  unsigned int tseg2;
  unsigned int sjw;
  unsigned int noSamp;
  unsigned int syncmode = 0;

  sjw     = ((btr0 & 0xc0) >> 6) + 1;
  tseg1   = ((btr1 & 0x0f) + 1);
  tseg2   = ((btr1 & 0x70) >> 4) + 1;
  noSamp  = ((btr1 & 0x80) >> 7) ? 3 : 1;
  bitrate = 8000000L * 64 / (((btr0 & 0x3f) + 1) << 6) /
            (tseg1 + tseg2 + 1);

  ret = check_bitrate (hnd, (unsigned int)bitrate);

  if (ret != canOK) {
    return ret;
  }

  return canSetBusParams(hnd, bitrate, tseg1, tseg2, sjw, noSamp, syncmode);
}


//******************************************************
// Get bus parameters
//******************************************************
canStatus CANLIBAPI
canGetBusParams (const CanHandle hnd, long *freq, unsigned int *tseg1,
                 unsigned int *tseg2, unsigned int *sjw,
                 unsigned int *noSamp, unsigned int *syncmode)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->getBusParams(hData, freq, tseg1, tseg2, sjw, noSamp,
                                     NULL, NULL, NULL, NULL, syncmode);
}


//******************************************************
// Get CAN FD bus parameters
//******************************************************
canStatus CANLIBAPI
canGetBusParamsFd (const CanHandle hnd, long *freq, unsigned int *tseg1,
                   unsigned int *tseg2, unsigned int *sjw)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  if (!hData->openMode) return canERR_INVHANDLE;

  return hData->canOps->getBusParams(hData, NULL, NULL, NULL, NULL, NULL,
                                     freq, tseg1, tseg2, sjw, NULL);
}


//******************************************************
// Set bus output control (silent/normal)
//******************************************************
canStatus CANLIBAPI
canSetBusOutputControl (const CanHandle hnd, const unsigned int drivertype)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }
  if (drivertype != canDRIVER_NORMAL && drivertype != canDRIVER_OFF &&
      drivertype != canDRIVER_SILENT && drivertype != canDRIVER_SELFRECEPTION) {
    return canERR_PARAM;
  }

  return hData->canOps->setBusOutputControl(hData, drivertype);
}


//******************************************************
// Get bus output control (silent/normal)
//******************************************************
canStatus CANLIBAPI
canGetBusOutputControl (const CanHandle hnd, unsigned int * drivertype)
{
  HandleData *hData;

  if (drivertype == NULL) {
    return canERR_PARAM;
  }

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->getBusOutputControl(hData, drivertype);
}

//******************************************************
// kvDeviceSetMode
//******************************************************
canStatus CANLIBAPI
kvDeviceSetMode(const CanHandle hnd, int mode)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->kvDeviceSetMode(hData, mode);
}

//******************************************************
// kvDeviceGetMode
//******************************************************
canStatus CANLIBAPI
kvDeviceGetMode(const CanHandle hnd, int *mode)
{
  HandleData *hData;

  if (mode == NULL) {
    return canERR_PARAM;
  }

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->kvDeviceGetMode(hData, mode);
}


//******************************************************
// Set filters
//******************************************************
canStatus CANLIBAPI canAccept (const CanHandle hnd,
                               const long envelope,
                               const unsigned int flag)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->accept(hData, envelope, flag);
}

/***************************************************************************/
canStatus CANLIBAPI canSetAcceptanceFilter(const CanHandle hnd,
                                           unsigned int code,
                                           unsigned int mask,
                                           int is_extended)
{
  (void) hnd;
  (void) code;
  (void) mask;
  (void) is_extended;
  return canERR_NOT_IMPLEMENTED;
}

//******************************************************
// Read bus status
//******************************************************
canStatus CANLIBAPI canReadStatus (const CanHandle hnd,
                                   unsigned long *const flags)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->readStatus(hData, flags);
}


//******************************************************
// Flash LED
//******************************************************
canStatus CANLIBAPI kvFlashLeds (const CanHandle hnd,
                                 int action, int timeout)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->kvFlashLeds(hData, action, timeout);
}


//******************************************************
// Request chip status
//******************************************************
canStatus CANLIBAPI canRequestChipStatus (const CanHandle hnd)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  // canRequestChipStatus is not needed in Linux, but in order to make code
  // portable between Windows and Linux, a dummy function returning canOK
  // was added.
  return hData->canOps->requestChipStatus(hData);
}


//******************************************************
// Read the error counters
//******************************************************
canStatus CANLIBAPI canReadErrorCounters (const CanHandle hnd,
                                          unsigned int *txErr,
                                          unsigned int *rxErr,
                                          unsigned int *ovErr)
{
  HandleData *hData;
  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->readErrorCounters(hData, txErr, rxErr, ovErr);
}


//******************************************************
// Write can message
//******************************************************
canStatus CANLIBAPI
canWrite (const CanHandle hnd, long id, void *msgPtr,
          unsigned int dlc, unsigned int flag)
{
  HandleData *hData;

  // If msgPtr is NULL then dlc must be 0, unless it is a remote frame.
  if ((msgPtr == NULL) && (dlc != 0) && ((flag & canMSG_RTR) == 0)) {
    return canERR_PARAM;
  }

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->write(hData, id, msgPtr, dlc, flag);
}


//******************************************************
// Write can message and wait
//******************************************************
canStatus CANLIBAPI
canWriteWait (const CanHandle hnd, long id, void *msgPtr,
              unsigned int dlc, unsigned int flag, unsigned long timeout)
{
  HandleData *hData;

  // If msgPtr is NULL then dlc must be 0, unless it is a remote frame.
  if ((msgPtr == NULL) && (dlc != 0) && ((flag & canMSG_RTR) == 0)) {
    return canERR_PARAM;
  }

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->writeWait(hData, id, msgPtr, dlc, flag, timeout);
}


//******************************************************
// Read can message
//******************************************************
canStatus CANLIBAPI
canRead (const CanHandle hnd, long *id, void *msgPtr, unsigned int *dlc,
         unsigned int *flag, unsigned long *time)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->read(hData, id, msgPtr, dlc, flag, time);
}

//*********************************************************
// Reads a message with the specified identifier (if available). Any
// preceeding message not matching the specified identifier will be retained
// in the queue.
//*********************************************************
canStatus CANLIBAPI
canReadSpecific(const CanHandle hnd,
                long            id,
                void            *msgPtr,
                unsigned int    *dlc,
                unsigned int    *flag,
                unsigned long   *time)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->readSpecific(hData, id, msgPtr, dlc, flag, time);
}

//*********************************************************
// Waits until the receive buffer contains a message with the specified
// id, or a timeout occurs.
//*********************************************************
canStatus CANLIBAPI
canReadSyncSpecific(const CanHandle hnd,
                    long            id,
                    unsigned long   timeout)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->readSyncSpecific(hData, id, timeout);
}

//*********************************************************
// Reads a message with a specified id from the receive buffer. Any
// preceeding message not matching the specified identifier will be skipped.
//*********************************************************
canStatus CANLIBAPI
canReadSpecificSkip(const CanHandle hnd,
                    long            id,
                    void            *msgPtr,
                    unsigned int    *dlc,
                    unsigned int    *flag,
                    unsigned long   *time)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->readSpecificSkip(hData, id, msgPtr, dlc, flag, time);
}

//*********************************************************
// Read can message or wait until one appears or timeout
//*********************************************************
canStatus CANLIBAPI
canReadWait (const CanHandle hnd, long *id, void *msgPtr, unsigned int *dlc,
             unsigned int *flag, unsigned long *time, unsigned long timeout)
{
  HandleData *hData;

  hData = findHandle(hnd);

  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->readWait(hData, id, msgPtr, dlc, flag, time, timeout);
}

//*********************************************************
// Waits until the receive buffer contains at least one
// message or a timeout occurs.
//*********************************************************
canStatus CANLIBAPI
canReadSync(const CanHandle hnd, unsigned long timeout)
{
  HandleData *hData;

  hData = findHandle(hnd);

  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->readSync(hData, timeout);
}

//****************************************************************
// Wait until all can messages on a circuit are sent or timeout
//****************************************************************
canStatus CANLIBAPI
canWriteSync (const CanHandle hnd, unsigned long timeout)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->writeSync(hData, timeout);
}


//******************************************************
// IOCTL
//******************************************************
canStatus CANLIBAPI
canIoCtl (const CanHandle hnd, unsigned int func,
          void *buf, unsigned int buflen)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  switch (func) {
  case canIOCTL_SET_ERROR_FRAMES_REPORTING:
  case canIOCTL_GET_CHANNEL_QUALITY:
  case canIOCTL_GET_ROUNDTRIP_TIME:
  case canIOCTL_GET_BUS_TYPE:
  case canIOCTL_GET_DEVNAME_ASCII:
  case canIOCTL_GET_TIME_SINCE_LAST_SEEN:
  case canIOCTL_SET_THROTTLE_SCALED:
  case canIOCTL_GET_THROTTLE_SCALED:
    return canERR_NOT_IMPLEMENTED;
  }

  return hData->canOps->ioCtl(hData, func, buf, buflen);
}

//******************************************************
// Read the time from hw
//******************************************************
canStatus CANLIBAPI canReadTimer (const CanHandle hnd, unsigned long *time)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->readTimer(hData, time);
}


//******************************************************
// Read the time from hw
//******************************************************
canStatus CANLIBAPI kvReadTimer (const CanHandle hnd, unsigned int *time)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->kvReadTimer(hData, time);
}


//******************************************************
// Read the 64-bit time from hw
//******************************************************
canStatus CANLIBAPI kvReadTimer64 (const CanHandle hnd, uint64_t *time)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->kvReadTimer64(hData, time);
}


//******************************************************
// Translate from baud macro to bus params
//******************************************************
canStatus CANLIBAPI canTranslateBaud (long *const freq,
                                      unsigned int *const tseg1,
                                      unsigned int *const tseg2,
                                      unsigned int *const sjw,
                                      unsigned int *const nosamp,
                                      unsigned int *const syncMode)
{
  if (!freq || !tseg1 || !tseg2 || !sjw  || !nosamp) {
    return canERR_PARAM;
  }

  if (syncMode != NULL) {
    *syncMode = 0;
  }

  *nosamp = 1;

  switch (*freq) {

  case canFD_BITRATE_8M_60P:
    *freq     = 8000000L;
    *tseg1    = 2;
    *tseg2    = 2;
    *sjw      = 1;
    break;

  case canFD_BITRATE_4M_80P:
    *freq     = 4000000L;
    *tseg1    = 7;
    *tseg2    = 2;
    *sjw      = 2;
    break;

  case canFD_BITRATE_2M_80P:
    *freq     = 2000000L;
    *tseg1    = 15;
    *tseg2    = 4;
    *sjw      = 4;
    break;

  case canFD_BITRATE_1M_80P:
    *freq     = 1000000L;
    *tseg1    = 31;
    *tseg2    = 8;
    *sjw      = 8;
    break;

  case canFD_BITRATE_500K_80P:
    *freq     = 500000L;
    *tseg1    = 63;
    *tseg2    = 16;
    *sjw      = 16;
    break;

  case BAUD_1M:
    *freq     = 1000000L;
    *tseg1    = 5;
    *tseg2    = 2;
    *sjw      = 1;
    break;

  case BAUD_500K:
    *freq     = 500000L;
    *tseg1    = 5;
    *tseg2    = 2;
    *sjw      = 1;
    break;

  case BAUD_250K:
    *freq     = 250000L;
    *tseg1    = 5;
    *tseg2    = 2;
    *sjw      = 1;
    break;

  case BAUD_125K:
    *freq     = 125000L;
    *tseg1    = 11;
    *tseg2    = 4;
    *sjw      = 1;
    break;

  case BAUD_100K:
    *freq     = 100000L;
    *tseg1    = 11;
    *tseg2    = 4;
    *sjw      = 1;
    break;

  case canBITRATE_83K:
    *freq     = 83333L;
    *tseg1    = 5;
    *tseg2    = 2;
    *sjw      = 2;
    break;

  case BAUD_62K:
    *freq     = 62500L;
    *tseg1    = 11;
    *tseg2    = 4;
    *sjw      = 1;
    break;

  case BAUD_50K:
    *freq     = 50000L;
    *tseg1    = 11;
    *tseg2    = 4;
    *sjw      = 1;
    break;

  case canBITRATE_10K:
    *freq     = 10000L;
    *tseg1    = 11;
    *tseg2    = 4;
    *sjw      = 1;
    break;

  default:
    return canERR_PARAM;
  }

  return canOK;
}


//******************************************************
// Get error text
//******************************************************
canStatus CANLIBAPI
canGetErrorText (canStatus err, char *buf, unsigned int bufsiz)
{
  canStatus stat = canOK;
  signed char code;

  code = (signed char)(err & 0xFF);

  if (!buf || bufsiz == 0) {
    return canERR_PARAM;
  }
  if ((code <= 0) && ((unsigned)(-code) < sizeof(errorStrings) / sizeof(char *))) {
    if (errorStrings [-code] == NULL) {
      snprintf(buf, bufsiz, "Unknown error (%d)", (int)code);
      stat = canERR_PARAM;
    } else {
      strncpy(buf, errorStrings[-code], bufsiz);
    }
  } else {
    strncpy(buf, "This is not an error code", bufsiz);
    stat = canERR_PARAM;
  }
  buf[bufsiz - 1] = '\0';

  return stat;
}


//******************************************************
// Get library version
//******************************************************
unsigned short CANLIBAPI canGetVersion (void)
{
  return (CANLIB_MAJOR_VERSION << 8) + CANLIB_MINOR_VERSION;
}

unsigned int CANLIBAPI canGetVersionEx(unsigned int itemCode)
{
  unsigned int prod_letter = 0;
  int beta = 0;

#ifdef PRODUCT_MINOR_LETTER
  prod_letter = PRODUCT_MINOR_LETTER;
#endif

  switch (itemCode) {
      case canVERSION_CANLIB32_VERSION:
          return (CANLIB_MAJOR_VERSION << 8) + CANLIB_MINOR_VERSION;

      case canVERSION_CANLIB32_PRODVER:
          return (CANLIB_PRODUCT_MAJOR_VERSION << 8) + CANLIB_MINOR_VERSION;

      case canVERSION_CANLIB32_PRODVER32:
          return (CANLIB_MAJOR_VERSION << 16) +
                 (CANLIB_MINOR_VERSION << 8) +
                 prod_letter;

      case canVERSION_CANLIB32_BETA:
#ifdef CANLIB_BETA
          beta = CANLIB_BETA;
#endif
          return beta;

      default:
          return 0;
    }
}
//******************************************************
// Get the total number of channels
//******************************************************
canStatus CANLIBAPI canGetNumberOfChannels (int *channelCount)
{
  // For now, we just count the number of /dev/%s%d files (see dev_name),
  // where %d is numbers between 0 and 255.
  // This is slow!

  int tmpCount = 0;
  int cardNr;
  char filename[DEVICE_NAME_LEN];
  unsigned n = 0;

  if (channelCount == NULL) {
    return canERR_PARAM;
  }

  for(n = 0; n < sizeof(dev_name) / sizeof(*dev_name); n++) {
    // There are 256 minor inode numbers
    for(cardNr = 0; cardNr <= 255; cardNr++) {
      snprintf(filename,  DEVICE_NAME_LEN, "/dev/%s%d", dev_name[n], cardNr);
      if (access(filename, F_OK) == 0) {  // Check for existance
        tmpCount++;
      }
      else {
        // Handle gaps in device numbers
        continue;
      }
    }
  }

  *channelCount = tmpCount;

  return canOK;
}



//******************************************************
// Find device description data from EAN
//******************************************************

canStatus CANLIBAPI
canGetDescrData (void *buffer, const size_t bufsize, unsigned int ean[], int cap)
{
  unsigned int len, i;

  len = sizeof(dev_descr_list)/sizeof(struct dev_descr);

  /* set Unknown device description */
  strncpy(buffer, dev_descr_list[0].descr_string, bufsize - 1);

  /* check in device is virtual device */
  if ((ean[0] == dev_descr_list[1].ean[0]) &&
      (ean[1] == dev_descr_list[1].ean[1]) &&
      (cap & canCHANNEL_CAP_VIRTUAL))
  {
    strncpy(buffer, dev_descr_list[1].descr_string, bufsize - 1);
  }
  /* search for description by matching ean number */
  else
  {
    for (i = 2; i<len; i++)
    {
      if ((ean[0] == dev_descr_list[i].ean[0]) && (ean[1] == dev_descr_list[i].ean[1]))
      {
        strncpy(buffer, dev_descr_list[i].descr_string, bufsize - 1);
        break;
      }
    }
  }
  return canOK;

}

static canStatus
getHandleData (HandleData *hData, int item, void *buffer, const size_t bufsize)
{
  canStatus status;
  unsigned int ean[2];
  int cap = 0;

  if ((buffer == NULL) || (bufsize == 0)) {
    return canERR_PARAM;
  }

  switch(item) {
  case canCHANNELDATA_CHANNEL_NAME:
    strncpy(buffer, hData->deviceOfficialName, bufsize - 1);
    return canOK;

  case canCHANNELDATA_CHAN_NO_ON_CARD:
    if (bufsize < sizeof(hData->channelNr)) {
      return canERR_PARAM;
    }
    memcpy(buffer, &hData->channelNr, sizeof(hData->channelNr));
    return canOK;

  case canCHANNELDATA_DEVDESCR_ASCII:
    status = hData->canOps->getChannelData(hData->deviceName,
                                          canCHANNELDATA_CARD_UPC_NO,
                                          &ean,
                                          sizeof(ean));
    if (status != canOK)
    {
      return status;
    }

    status = hData->canOps->getChannelData(hData->deviceName,
                                          canCHANNELDATA_CHANNEL_CAP,
                                          &cap,
                                          sizeof(cap));
    if (status != canOK)
    {
      return status;
    }

    return canGetDescrData(buffer, bufsize, ean, cap);

  case canCHANNELDATA_MFGNAME_ASCII:
    strncpy(buffer, MFGNAME_ASCII, bufsize - 1);
    return canOK;

  case canCHANNELDATA_DLL_FILE_VERSION:
    {
      unsigned short *p = (unsigned short *)buffer;
      if (bufsize < 8) return canERR_PARAM;
      *p++ = 0;
      *p++ = CANLIB_BUILD_VERSION;
      *p++ = CANLIB_MINOR_VERSION;
      *p++ = CANLIB_MAJOR_VERSION;
      return canOK;
    }

  case canCHANNELDATA_DLL_PRODUCT_VERSION:
    {
      unsigned short *p = (unsigned short *)buffer;
      if (bufsize < 8) return canERR_PARAM;
      *p++ = 0;
#ifdef PRODUCT_MINOR_LETTER
      *p++ = (PRODUCT_MINOR_LETTER != '0' ? PRODUCT_MINOR_LETTER : 0);
#else
      *p++ = 0;
#endif
      *p++ = CANLIB_MINOR_VERSION;
      *p++ = CANLIB_PRODUCT_MAJOR_VERSION;
      return canOK;
    }


  case canCHANNELDATA_TRANS_CAP:
  case canCHANNELDATA_TRANS_SERIAL_NO:
  case canCHANNELDATA_TRANS_UPC_NO:
  case canCHANNELDATA_DLL_FILETYPE:
  case canCHANNELDATA_DEVICE_PHYSICAL_POSITION:
  case canCHANNELDATA_UI_NUMBER:
  case canCHANNELDATA_TIMESYNC_ENABLED:
  case canCHANNELDATA_MFGNAME_UNICODE:
  case canCHANNELDATA_DEVDESCR_UNICODE:
  case canCHANNELDATA_CHANNEL_QUALITY:
  case canCHANNELDATA_ROUNDTRIP_TIME:
  case canCHANNELDATA_BUS_TYPE:
  case canCHANNELDATA_TIME_SINCE_LAST_SEEN:
  case canCHANNELDATA_DEVNAME_ASCII:
  case canCHANNELDATA_REMOTE_OPERATIONAL_MODE:
  case canCHANNELDATA_REMOTE_PROFILE_NAME:
  case canCHANNELDATA_REMOTE_HOST_NAME:
  case canCHANNELDATA_REMOTE_MAC:
    return canERR_NOT_IMPLEMENTED;

  default:
    return hData->canOps->getChannelData(hData->deviceName,
                                        item,
                                        buffer,
                                        bufsize);
  }
}

//******************************************************
// Find out channel specific data
//******************************************************
canStatus CANLIBAPI
canGetChannelData (int channel, int item, void *buffer, const size_t bufsize)
{
  canStatus status;
  HandleData hData;

  status = getDevParams(channel, hData.deviceName, &hData.channelNr,
                        &hData.canOps, hData.deviceOfficialName);

  if (status < 0) {
    return status;
  }
  return getHandleData (&hData, item, buffer, bufsize);
}

//******************************************************
// Get handle data
//******************************************************
canStatus CANLIBAPI
canGetHandleData (const CanHandle hnd, int item, void *buffer,
                  const size_t bufsize)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  if (hData->valid == FALSE) {
    return canERR_PARAM;
  }

  return getHandleData (hData, item, buffer, bufsize);
}

//===========================================================================
canStatus CANLIBAPI canObjBufFreeAll (const CanHandle hnd)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->objbufFreeAll(hData);
}


//===========================================================================
canStatus CANLIBAPI canObjBufAllocate (const CanHandle hnd, int type)
{
  HandleData *hData;
  int number;
  canStatus status;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  status = hData->canOps->objbufAllocate(hData, type, &number);

  return (status == canOK) ? number : status;
}


//===========================================================================
canStatus CANLIBAPI canObjBufFree (const CanHandle hnd, int idx)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->objbufFree(hData, idx);
}


//===========================================================================
canStatus CANLIBAPI
canObjBufWrite (const CanHandle hnd, int idx, int id, void *msg,
                unsigned int dlc, unsigned int flags)
{
  HandleData *hData;

  // If msgPtr is NULL then dlc must be 0, unless it is a remote frame.
  if ((msg == NULL) && (dlc != 0) && ((flags & canMSG_RTR) == 0)) {
    return canERR_PARAM;
  }

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->objbufWrite(hData, idx, id, msg, dlc, flags);
}


//===========================================================================
canStatus CANLIBAPI
canObjBufSetFilter (const CanHandle hnd, int idx,
                    unsigned int code, unsigned int mask)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->objbufSetFilter(hData, idx, code, mask);
}


//===========================================================================
canStatus CANLIBAPI
canObjBufSetFlags (const CanHandle hnd, int idx, unsigned int flags)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->objbufSetFlags(hData, idx, flags);
}


//===========================================================================
canStatus CANLIBAPI
canObjBufSetPeriod (const CanHandle hnd, int idx, unsigned int period)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->objbufSetPeriod(hData, idx, period);
}


//===========================================================================
canStatus CANLIBAPI
canObjBufSetMsgCount (const CanHandle hnd, int idx, unsigned int count)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->objbufSetMsgCount(hData, idx, count);
}


//===========================================================================
canStatus CANLIBAPI
canObjBufSendBurst (const CanHandle hnd, int idx, unsigned int burstLen)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->objbufSendBurst(hData, idx, burstLen);
}


//===========================================================================
canStatus CANLIBAPI canObjBufEnable (const CanHandle hnd, int idx)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->objbufEnable(hData, idx);
}


//===========================================================================
canStatus CANLIBAPI canObjBufDisable (const CanHandle hnd, int idx)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->objbufDisable(hData, idx);
}


//******************************************************
// Flush receive queue
//******************************************************
canStatus CANLIBAPI
canFlushReceiveQueue (const CanHandle hnd)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->ioCtl(hData, canIOCTL_FLUSH_RX_BUFFER, NULL, 0);
}


//******************************************************
// Flush transmit queue
//******************************************************
canStatus CANLIBAPI
canFlushTransmitQueue (const CanHandle hnd)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->ioCtl(hData, canIOCTL_FLUSH_TX_BUFFER, NULL, 0);
}

//******************************************************
// Request bus statistics
//******************************************************
canStatus CANLIBAPI
canRequestBusStatistics(const CanHandle hnd)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->reqBusStats(hData);
}

//******************************************************
// Read bus statistics
//******************************************************
canStatus CANLIBAPI canGetBusStatistics (const CanHandle hnd,
                                         canBusStatistics *stat,
                                         size_t bufsiz)
{
  HandleData *hData;

  if ((stat == NULL) || (bufsiz != sizeof(canBusStatistics))) {
    return canERR_PARAM;
  }

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  return hData->canOps->getBusStats(hData, stat);
}


/***************************************************************************/
kvStatus CANLIBAPI kvFileCopyToDevice(const CanHandle hnd, char *hostFileName, char *deviceFileName)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }
  if (!hostFileName || !deviceFileName) {
    return canERR_PARAM;
  }

  return hData->canOps->kvFileCopyToDevice(hData, hostFileName, deviceFileName);
}

/***************************************************************************/
kvStatus CANLIBAPI kvFileCopyFromDevice(const CanHandle hnd, char *deviceFileName, char *hostFileName)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }
  if (!hostFileName || !deviceFileName) {
    return canERR_PARAM;
  }

  return hData->canOps->kvFileCopyFromDevice(hData, deviceFileName, hostFileName);
}

/***************************************************************************/
kvStatus CANLIBAPI kvFileDelete(const CanHandle hnd, char *deviceFileName)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }
  if (!deviceFileName) {
    return canERR_PARAM;
  }

  return hData->canOps->kvFileDelete(hData, deviceFileName);
}

/***************************************************************************/
kvStatus CANLIBAPI kvFileGetName(const CanHandle hnd, int fileNo, char *name, int namelen)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  // We need room for at least one character + '\n'
  if (!name || namelen < 3) {
    return canERR_PARAM;
  }

  return hData->canOps->kvFileGetName(hData, fileNo, name, namelen);
}

/***************************************************************************/
// return number of files
kvStatus CANLIBAPI kvFileGetCount(const CanHandle hnd, int *count)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  if (!count) {
    return canERR_PARAM;
  }

  return hData->canOps->kvFileGetCount(hData, count);
}

/***************************************************************************/
kvStatus CANLIBAPI kvFileGetSystemData(const CanHandle hnd, int itemCode, int *result)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }

  (void) itemCode;
  (void) result;
  return canERR_NOT_IMPLEMENTED;
}

/***************************************************************************/
kvStatus CANLIBAPI kvReadDeviceCustomerData(const CanHandle hnd,
                                            int userNumber,
                                            int itemNumber,
                                            void *data,
                                            size_t bufsiz)
{
  (void) hnd;
  (void) userNumber;
  (void) itemNumber;
  (void) data;
  (void) bufsiz;
  return canERR_NOT_IMPLEMENTED;
}

/***************************************************************************/
kvStatus CANLIBAPI kvScriptStatus(const CanHandle hnd,
                                  int  slot,
                                  unsigned int *status)
{
  (void) hnd;
  (void) slot;
  (void) status;
  return canERR_NOT_IMPLEMENTED;
}

/***************************************************************************/
kvStatus CANLIBAPI kvScriptStart(const CanHandle hnd, int slotNo)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }
  return hData->canOps->kvScriptStart(hData, slotNo);
}

/***************************************************************************/
kvStatus CANLIBAPI kvScriptStop(const CanHandle hnd, int slotNo, int mode)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }
  return hData->canOps->kvScriptStop(hData, slotNo, mode);
}

/***************************************************************************/
kvStatus CANLIBAPI kvScriptUnload(const CanHandle hnd, int slotNo)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }
  return hData->canOps->kvScriptUnload(hData, slotNo);
}

/***************************************************************************/
kvStatus CANLIBAPI kvScriptSendEvent(const CanHandle hnd,
                                      int slotNo,
                                      int eventType,
                                      int eventNo,
                                      unsigned int data)
{
  (void) hnd;
  (void) slotNo;
  (void) eventType;
  (void) eventNo;
  (void) data;
  return canERR_NOT_IMPLEMENTED;
}

/***************************************************************************/
kvEnvHandle CANLIBAPI kvScriptEnvvarOpen(const CanHandle hnd,
                                         char* envvarName,
                                         int *envvarType,
                                         int *envvarSize)
{
  (void) hnd;
  (void) envvarName;
  (void) envvarType;
  (void) envvarSize;
  return canERR_NOT_IMPLEMENTED;
}

/***************************************************************************/
kvStatus CANLIBAPI kvScriptEnvvarClose (kvEnvHandle eHnd)
{
  (void) eHnd;
  return canERR_NOT_IMPLEMENTED;
}

/***************************************************************************/
kvStatus CANLIBAPI kvScriptEnvvarSetInt(kvEnvHandle eHnd, int val)
{
  (void) eHnd;
  (void) val;
  return canERR_NOT_IMPLEMENTED;
}

/***************************************************************************/
kvStatus CANLIBAPI kvScriptEnvvarGetInt(kvEnvHandle eHnd, int *val)
{
  (void) eHnd;
  (void) val;
  return canERR_NOT_IMPLEMENTED;
}

/***************************************************************************/
kvStatus CANLIBAPI kvScriptEnvvarSetFloat(kvEnvHandle eHnd, float val)
{
  (void) eHnd;
  (void) val;
  return canERR_NOT_IMPLEMENTED;
}

/***************************************************************************/
kvStatus CANLIBAPI kvScriptEnvvarGetFloat(kvEnvHandle eHnd, float *val)
{
  (void) eHnd;
  (void) val;
  return canERR_NOT_IMPLEMENTED;
}

/***************************************************************************/
kvStatus CANLIBAPI kvScriptEnvvarSetData(kvEnvHandle eHnd,
                                         void *buf,
                                         int start_index,
                                         int data_len)
{
  (void) eHnd;
  (void) buf;
  (void) start_index;
  (void) data_len;
  return canERR_NOT_IMPLEMENTED;
}

/***************************************************************************/
kvStatus CANLIBAPI kvScriptEnvvarGetData(kvEnvHandle eHnd,
                                         void *buf,
                                         int start_index,
                                         int data_len)
{
  (void) eHnd;
  (void) buf;
  (void) start_index;
  (void) data_len;
  return canERR_NOT_IMPLEMENTED;
}

/***************************************************************************/
kvStatus CANLIBAPI kvScriptLoadFile(const CanHandle hnd,
                                    int slotNo,
                                    char *hostFileName)
{
  HandleData *hData;

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }
  return hData->canOps->kvScriptLoadFile(hData, slotNo, hostFileName);
}

/***************************************************************************/
kvStatus CANLIBAPI kvScriptLoadFileOnDevice(const CanHandle hnd,
                                            int slotNo,
                                            char *localFile)
{
  (void) hnd;
  (void) slotNo;
  (void) localFile;
  return canERR_NOT_IMPLEMENTED;
}

/***************************************************************************/




static void InitializeListHead (LIST_ENTRY *head)
{
  head->Flink = head->Blink = head;
}

static void InsertHeadList (LIST_ENTRY *head, LIST_ENTRY *element)
{
  element->Flink = head->Flink;
  element->Blink = head;

  head->Flink->Blink = element;
  head->Flink = element;
}

static int IsListEmpty (LIST_ENTRY *list)
{
  return list->Flink == list;
}

static LIST_ENTRY *RemoveHeadList (LIST_ENTRY *head)
{
  LIST_ENTRY *element = head->Flink;
  element->Flink->Blink = head;
  head->Flink = element->Flink;
  return element;
}

static void RemoveEntryList (LIST_ENTRY *entry)
{
  LIST_ENTRY *f = entry->Flink, *b = entry->Blink;
  b->Flink = f;
  f->Blink = b;
}

#define EnterTimeDomainCritical() \
    do { \
      if (pthread_mutex_lock(&timeDomains.critSect)){ \
        PRINTF_TIMEDOMAIN(("EnterTimeDomainCritical: failed\n")); \
      } \
    } while(0)

#define LeaveTimeDomainCritical() \
    do { \
      if (pthread_mutex_unlock(&timeDomains.critSect)) { \
        PRINTF_TIMEDOMAIN(("LeaveTimeDomainCritical: failed\n")); \
      } \
    } while(0)

/***************************************************************************/
kvStatus  kvTimeDomainCreate (kvTimeDomain *domain)
{
  kvTimeDomainInternal *td;

  // Check input parameters
  if (!Initialized) return canERR_NOTINITIALIZED;
  if (!domain)      return canERR_PARAM;

  // Allocate and initialize memory for the domain structure
  td = (kvTimeDomainInternal *)malloc(sizeof(*td));
  if (!td) return canERR_NOMEM;
  memset(td, 0, sizeof(kvTimeDomainInternal));

  // Initialize the member lists
  InitializeListHead(&td->magiSynced);
  InitializeListHead(&td->nonMagiSynced);

  // Link the new domain to list of all domains.
  EnterTimeDomainCritical();
  InsertHeadList(&timeDomains.headOfAllHeads, &td->link);
  LeaveTimeDomainCritical();

  // Return the newly created domain
  *domain = (kvTimeDomain *)td;

  PRINTF_TIMEDOMAIN(("kvTimeDomainCreate: Created\n"));

  return canOK;
}


/***************************************************************************/
kvStatus  kvTimeDomainDelete (kvTimeDomain domain)
{
  kvTimeDomainInternal *td;
  kvTimeDomainHandleData *member;
  kvTimeDomainMagisynced *group;
  kvTimeDomainNonMagisynced *card;
  LIST_ENTRY *lGroup, *pdLink, *lCard;

  // Check input parameters
  if (!Initialized) return canERR_NOTINITIALIZED;
  if (!domain)      return canERR_PARAM;

  td = (kvTimeDomainInternal *)domain;

  // Delete all members before deleting the domain itself.
  while (!IsListEmpty(&td->magiSynced)) {
    lGroup = RemoveHeadList(&td->magiSynced);
    group  = CONTAINING_RECORD(lGroup, kvTimeDomainMagisynced, link);

    while (!IsListEmpty(&group->head)) {
      pdLink = RemoveHeadList(&group->head);
      member = CONTAINING_RECORD(pdLink, kvTimeDomainHandleData, link);
      PRINTF_TIMEDOMAIN(("kvTimeDomainDelete: Deleted synced member %u\n",
                         member->handle));
      free(member);
    }

    PRINTF_TIMEDOMAIN(("kvTimeDomainDelete: Deleted group %u\n",
                       group->softsync_group));
    free(group);
  }

  while (!IsListEmpty(&td->nonMagiSynced)) {
    lCard = RemoveHeadList(&td->nonMagiSynced);
    card  = CONTAINING_RECORD(lCard, kvTimeDomainNonMagisynced, link);

    while (!IsListEmpty(&card->head)) {
      pdLink = RemoveHeadList(&card->head);
      member = CONTAINING_RECORD(pdLink, kvTimeDomainHandleData, link);
      PRINTF_TIMEDOMAIN(("kvTimeDomainDelete: Deleted nonsynced member %u\n",
                         member->handle));
      free(member);
    }

    PRINTF_TIMEDOMAIN(("kvTimeDomainDelete: Deleted card %llx with sn %u\n", (long long) card->ean, card->sn));
    free(card);
  }

  // Delete the domain from the list of all domains.
  EnterTimeDomainCritical();
  RemoveEntryList(&td->link);
  LeaveTimeDomainCritical();

  // Free the data structures of the domain.
  free(td);
  PRINTF_TIMEDOMAIN(("kvTimeDomainDelete: Deleted domain\n"));

  return canOK;
}

/***************************************************************************/
kvStatus  kvTimeDomainResetTime (kvTimeDomain domain)
{
  kvTimeDomainInternal *td;
  kvTimeDomainHandleData *member;
  kvTimeDomainMagisynced *group;
  kvTimeDomainNonMagisynced *card;
  LIST_ENTRY *lGroup, *pdLink, *lCard;
  HandleData *h1, *h2;

  // Check input parameters
  if (!Initialized) return canERR_NOTINITIALIZED;
  if (!domain)      return canERR_PARAM;

  td = (kvTimeDomainInternal *)domain;

  PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: Entered\n"));

  EnterTimeDomainCritical();

  lGroup = td->magiSynced.Flink;
  while (lGroup != &td->magiSynced) {
    group  = CONTAINING_RECORD(lGroup, kvTimeDomainMagisynced, link);
    lGroup = lGroup->Flink;
    pdLink = group->head.Flink;

    PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: Found group %x\n",
                       group->softsync_group));

    // Find the first valid handle and delete all nonvalid handles while
    // searching for a valid one.
    member = CONTAINING_RECORD(pdLink, kvTimeDomainHandleData, link);
    h1     = findHandle(member->handle);

    PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: Found synced member %u\n",
                       member->handle));

    while (h1->valid == FALSE) {
      LIST_ENTRY *pdLink;
      pdLink = RemoveHeadList(&group->head);
      member = CONTAINING_RECORD(pdLink, kvTimeDomainHandleData, link);
      PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: Deleted synced member %u\n",
                         member->handle));
      free(member);
      if (IsListEmpty(&group->head)) break;
      member = CONTAINING_RECORD(group->head.Flink,
                                 kvTimeDomainHandleData, link);
      h1     = findHandle(member->handle);
    }

    if (IsListEmpty(&group->head)) {
      PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: Deleted group %u\n",
                         group->softsync_group));
      free(group);
      continue;
    }

    // Reset the time for the first valid handle.
    PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: Reset synced member %u\n",
                       member->handle));
    h1->canOps->resetClock(h1);

    // Copy the clock offset of the first handle to all other valid handles
    // in the group and delete all nonvalid handles mean while.
    pdLink = member->link.Flink;
    while (pdLink != &group->head) {
      member = CONTAINING_RECORD(pdLink, kvTimeDomainHandleData, link);
      PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: Found synced member %u\n",
                         member->handle));

      pdLink = pdLink->Flink;
      h2     = findHandle(member->handle);
      if (h2->valid) {
        PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: Copied clock "
                           "to synced member %u\n", member->handle));
        h1->canOps->setClockOffset(h2, h1);
      }
      else {
        RemoveEntryList(&member->link);
        PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: Deleted synced "
                           "member %u\n", member->handle));
        free(member);
      }
    }
  }

  // Reset the time for all non magisynced handles
  lCard = td->nonMagiSynced.Flink;
  while (lCard != &td->nonMagiSynced) {
    card   = CONTAINING_RECORD(lCard, kvTimeDomainNonMagisynced, link);
    lCard  = lCard->Flink;
    pdLink = card->head.Flink;

    PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: Found card %llx with sn %u\n", (long long) card->ean, card->sn));

    // Find the first valid handle and delete all nonvalid handles while
    // searching for a valid one.
    member = CONTAINING_RECORD(pdLink, kvTimeDomainHandleData, link);
    h1     = findHandle(member->handle);

    PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: "
                       "Found nonsynced member %u\n",
                       member->handle));

    while (h1->valid == FALSE) {
      LIST_ENTRY *pdLink;
      pdLink = RemoveHeadList(&card->head);
      member = CONTAINING_RECORD(pdLink, kvTimeDomainHandleData, link);
      PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: "
                         "Deleted nonsynced member %u\n",
                         member->handle));
      free(member);
      if (IsListEmpty(&card->head)) break;
      member = CONTAINING_RECORD(card->head.Flink,
                                 kvTimeDomainHandleData, link);
      h1 = findHandle(member->handle);
    }

    if (IsListEmpty(&card->head)) {
      PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: Deleted card %llx with sn %u\n", (long long) card->ean,
                        card->sn));
      free(card);
      continue;
    }

    // Reset the time for the first valid handle.
    PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: "
                       "Reset nonsynced member %u\n",
                       member->handle));
    h1->canOps->resetClock(h1);

    // Copy the clock offset of the first handle to all other valid handles
    // on the card and delete all nonvalid handles mean while.
    pdLink = member->link.Flink;
    while (pdLink != &card->head) {
      member = CONTAINING_RECORD(pdLink, kvTimeDomainHandleData, link);
      PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: "
                         "Found nonsynced member %u\n",
                         member->handle));

      pdLink = pdLink->Flink;
      h2 = findHandle(member->handle);
      if (h2->valid) {
        PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: "
                           "Copied clock to nonsynced member %u\n",
                           member->handle));
        h1->canOps->setClockOffset(h2, h1);
      }
      else {
        RemoveEntryList(&member->link);
        PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: "
                           "Deleted nonsynced member %u\n",
                           member->handle));
        free(member);
      }
    }
  }

  LeaveTimeDomainCritical();

  return canOK;
}


/***************************************************************************/
kvStatus  kvTimeDomainGetData (kvTimeDomain domain,
                                          kvTimeDomainData *data,
                                          size_t bufsiz)
{
  kvTimeDomainInternal *td;
  kvTimeDomainMagisynced *group;
  kvTimeDomainNonMagisynced *card;
  LIST_ENTRY *lGroup, *pdLink, *lCard;

  PRINTF_TIMEDOMAIN(("kvTimeDomainGetData: Entered\n"));

  if (!Initialized) return canERR_NOTINITIALIZED;
  if (!domain || !data || bufsiz != sizeof(kvTimeDomainData)) {
    return canERR_PARAM;
  }
  memset(data, 0, sizeof(kvTimeDomainData));
  td = (kvTimeDomainInternal *)domain;

  EnterTimeDomainCritical();

  lGroup = td->magiSynced.Flink;
  while (lGroup != &td->magiSynced) {
    data->nMagiSyncGroups++;
    group = CONTAINING_RECORD(lGroup, kvTimeDomainMagisynced, link);
    PRINTF_TIMEDOMAIN(("kvTimeDomainGetData: Found group %x\n",
                       group->softsync_group));
    pdLink = group->head.Flink;
    while (pdLink != &group->head) {
      PRINTF_TIMEDOMAIN(("kvTimeDomainGetData: Found synced member\n"));
      data->nMagiSyncedMembers++;
      pdLink = pdLink->Flink;
    }
    lGroup = lGroup->Flink;
  }

  lCard = td->nonMagiSynced.Flink;
  while (lCard != &td->nonMagiSynced) {
    data->nNonMagiSyncCards++;
    card = CONTAINING_RECORD(lCard, kvTimeDomainNonMagisynced, link);
    PRINTF_TIMEDOMAIN(("kvTimeDomainGetData: Found card %llx with sn %u\n", (long long) card->ean, card->sn));
    pdLink = card->head.Flink;
    while (pdLink != &card->head) {
      PRINTF_TIMEDOMAIN(("kvTimeDomainGetData: Found nonsynced member\n"));
      data->nNonMagiSyncedMembers++;
      pdLink = pdLink->Flink;
    }
    lCard = lCard->Flink;
  }

  LeaveTimeDomainCritical();

  return canOK;
}


/***************************************************************************/
kvStatus  kvTimeDomainAddHandle (kvTimeDomain domain, int handle)
{
  kvTimeDomainInternal *td;
  kvTimeDomainHandleData *member;
  VCAN_IOCTL_CARD_INFO ci;
  KCAN_IOCTL_CARD_INFO_2 ci2;
  HandleData *hData;
  uint64_t ean;
  int err;

  if (!Initialized) return canERR_NOTINITIALIZED;
  if (!domain || (findHandle(handle) == NULL)) {
    return canERR_PARAM;
  }

  td = (kvTimeDomainInternal *)domain;

  // Allocate and initialize memory for the member structure
  member = (kvTimeDomainHandleData *)malloc(sizeof(*member));
  if (!member) return canERR_NOMEM;
  member->handle = handle;

  memset (&ci2, 0, sizeof(ci2));

  // Determine whether softsync is running and if so which
  // softsync group it belongs to.
  hData = findHandle(handle);
  err = hData->canOps->getCardInfo (hData, &ci);
  err |= hData->canOps->getCardInfo2 (hData, &ci2);
  if (err) {
    free(member);
    return canERR_INTERNAL;
  }
  memcpy(&ean, ci2.ean, 8);

  EnterTimeDomainCritical();

  if (ci2.softsync_running) {
    // Check if any previously added handles are connected to the same hub.
    // If so add this handle to that group otherwise create a new.
    kvTimeDomainMagisynced *group;
    LIST_ENTRY *lGroup = td->magiSynced.Flink;

    while (lGroup != &td->magiSynced) {
      group = CONTAINING_RECORD(lGroup, kvTimeDomainMagisynced, link);
      PRINTF_TIMEDOMAIN(("kvTimeDomainAddHandle: Found group %x\n",
                         group->softsync_group));

      if (group->softsync_group == ci2.usb_host_id) {
        LIST_ENTRY *pdLink;
        HandleData *h1, *h2;

        InsertHeadList(&group->head, &member->link);
        PRINTF_TIMEDOMAIN(("kvTimeDomainAddHandle: Added synced member %u "
                           "to group %x\n", member->handle, group->softsync_group));

        // h1 is the handle of the newly inserted member
        h1 = findHandle(member->handle);

        // Copy the clock offset from the second member in the list to
        // the new member (that we just inserted at the head)
        pdLink = member->link.Flink;
        while (pdLink != &group->head) {
          member = CONTAINING_RECORD(pdLink, kvTimeDomainHandleData, link);
          PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: Found already synced member %u\n",
                             member->handle));

          pdLink = pdLink->Flink;
          h2     = findHandle(member->handle);
          if (h2->valid) {
            PRINTF_TIMEDOMAIN(("kvTimeDomainResetTime: Copied clock "
                               "from already synced member %u\n", member->handle));
            h1->canOps->setClockOffset(h1, h2);
            break;
          }
        }

        LeaveTimeDomainCritical();
        return canOK;
      }
      lGroup = lGroup->Flink;
    }

    group = (kvTimeDomainMagisynced *)malloc(sizeof(*group));
    if (!group) {
      LeaveTimeDomainCritical();
      free(member);
      return canERR_NOMEM;
    }
    InitializeListHead(&group->head);
    group->softsync_group = ci2.usb_host_id;
    InsertHeadList(&group->head, &member->link);
    InsertHeadList(&td->magiSynced, &group->link);
    PRINTF_TIMEDOMAIN(("kvTimeDomainAddHandle: Added synced member %u to"
                " new group %x\n", member->handle, group->softsync_group));
  }
  else {
    kvTimeDomainNonMagisynced *card;
    LIST_ENTRY *lCard = td->nonMagiSynced.Flink;

    while (lCard != &td->nonMagiSynced) {
      card = CONTAINING_RECORD(lCard, kvTimeDomainNonMagisynced, link);
      PRINTF_TIMEDOMAIN(("kvTimeDomainAddHandle: Found card %llx with sn %u\n", (long long) card->ean,
                        card->sn));

      if (card->ean == ean && card->sn == ci.serial_number) {
        InsertHeadList(&card->head, &member->link);
        PRINTF_TIMEDOMAIN(("kvTimeDomainAddHandle: Added nonsynced member"
                           " %u to card %llx with sn %u\n",
                           member->handle, (long long) card->ean, card->sn));
        LeaveTimeDomainCritical();
        return canOK;
      }
      lCard = lCard->Flink;
    }

    card = (kvTimeDomainNonMagisynced *)malloc(sizeof(*card));
    if (!card) {
      LeaveTimeDomainCritical();
      free(member);
      return canERR_NOMEM;
    }
    InitializeListHead(&card->head);
    card->ean = ean;
    card->sn  = ci.serial_number;
    InsertHeadList(&card->head, &member->link);
    InsertHeadList(&td->nonMagiSynced, &card->link);
    PRINTF_TIMEDOMAIN(("kvTimeDomainAddHandle: Added nonsynced member"
                       " %u to new card %llx with sn %u\n",
                       member->handle, (long long) card->ean, card->sn));
  }

  LeaveTimeDomainCritical();

  return canOK;
}


/***************************************************************************/
kvStatus  kvTimeDomainRemoveHandle (kvTimeDomain domain, int handle)
{
  kvTimeDomainInternal *td;
  kvTimeDomainHandleData *member;
  kvTimeDomainMagisynced *group;
  kvTimeDomainNonMagisynced *card;
  LIST_ENTRY *lGroup, *lCard, *pdLink;

#if NEW_HANDLE
  VALIDATE_AND_DETHREAD(handle, (kvStatus));
#endif

  if (!Initialized) return canERR_NOTINITIALIZED;
  if (!domain || (findHandle(handle) == NULL)) {
    return canERR_PARAM;
  }

  td = (kvTimeDomainInternal *)domain;

  EnterTimeDomainCritical();

  lGroup = td->magiSynced.Flink;
  while (lGroup != &td->magiSynced) {
    group  = CONTAINING_RECORD(lGroup, kvTimeDomainMagisynced, link);
    pdLink = group->head.Flink;
    while (pdLink != &group->head) {
      member = CONTAINING_RECORD(pdLink, kvTimeDomainHandleData, link);
      if (member->handle == handle) {
        RemoveEntryList(pdLink);
        PRINTF_TIMEDOMAIN(("kvTimeDomainRemoveHandle: Deleted synced "
                           "member %u\n", handle));
        free(member);
        if (IsListEmpty(&group->head)) {
          RemoveEntryList(lGroup);
          PRINTF_TIMEDOMAIN(("kvTimeDomainRemoveHandle: Deleted group %u\n",
                             group->softsync_group));
          free(group);
        }
        LeaveTimeDomainCritical();
        return canOK;
      }
      pdLink = pdLink->Flink;
    }
    lGroup = lGroup->Flink;
  }

  lCard = td->nonMagiSynced.Flink;
  while (lCard != &td->nonMagiSynced) {
    card   = CONTAINING_RECORD(lCard, kvTimeDomainNonMagisynced, link);
    pdLink = card->head.Flink;
    while (pdLink != &card->head) {
      member = CONTAINING_RECORD(pdLink, kvTimeDomainHandleData, link);
      if (member->handle == handle) {
        RemoveEntryList(pdLink);
        PRINTF_TIMEDOMAIN(("kvTimeDomainRemoveHandle: "
                           "Deleted nonsynced member %u\n", handle));
        free(member);
        if (IsListEmpty(&card->head)) {
          RemoveEntryList(lCard);
          PRINTF_TIMEDOMAIN(("kvTimeDomainRemoveHandle: "
                             "Deleted card %llx with sn %u\n",
                             (long long) card->ean, card->sn));
          free(card);
        }
        LeaveTimeDomainCritical();
        return canOK;
      }
      pdLink = pdLink->Flink;
    }
    lCard = lCard->Flink;
  }

  LeaveTimeDomainCritical();

  PRINTF_TIMEDOMAIN(("kvTimeDomainRemoveHandle: Handle not found %u\n",
                     handle));
  return canOK;  // Not found
}


//******************************************************
// Set notification callback
//******************************************************
canStatus CANLIBAPI
canSetNotify (const CanHandle hnd, void (*callback)(canNotifyData *),
              unsigned int notifyFlags, void *tag)
//
// Notification is done by filtering out interesting messages and
// doing a blocked read from a thread.
//
{
  HandleData *hData;
  const int validFlags = canNOTIFY_RX | canNOTIFY_TX | canNOTIFY_ERROR |
                         canNOTIFY_STATUS | canNOTIFY_ENVVAR  | canNOTIFY_BUSONOFF;

  if (notifyFlags & ~validFlags) {
    return canERR_PARAM;
  }

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }
  if (notifyFlags == 0 || callback == NULL) {
    if (hData->notifyFd != canINVALID_HANDLE) {
      // We want to shut off notification, close file and clear callback

      pthread_cancel(hData->notifyThread);

      // Wait for thread to finish
      pthread_join(hData->notifyThread, NULL);

      if (hData->notifyFd != canINVALID_HANDLE) {
        close(hData->notifyFd);
      }
      hData->notifyFd = canINVALID_HANDLE;
    }

    return canOK;
  }

  hData->notifyData.tag = tag;

  return hData->canOps->setNotify(hData, callback, NULL, notifyFlags);
}

kvStatus CANLIBAPI kvSetNotifyCallback(const CanHandle hnd,
                                       kvCallback_t callback, void* context,
                                       unsigned int notifyFlags)
{
  HandleData *hData;
  const unsigned int validFlags = canNOTIFY_RX | canNOTIFY_TX | canNOTIFY_ERROR |
                                  canNOTIFY_STATUS | canNOTIFY_ENVVAR | canNOTIFY_BUSONOFF;

  if (notifyFlags & ~validFlags) {
    return canERR_PARAM;
  }

  hData = findHandle(hnd);
  if (hData == NULL) {
    return canERR_INVHANDLE;
  }
  if (notifyFlags == 0 || callback == NULL) {
    if (hData->notifyFd != canINVALID_HANDLE) {
      // We want to shut off notification, close file and clear callback

      pthread_cancel(hData->notifyThread);

      // Wait for thread to finish
      pthread_join(hData->notifyThread, NULL);

      if (hData->notifyFd != canINVALID_HANDLE) {
        close(hData->notifyFd);
      }
      hData->notifyFd = canINVALID_HANDLE;
    }

    return canOK;
  }

  hData->notifyData.tag = context;

  return hData->canOps->setNotify(hData, NULL, callback, notifyFlags);
}


//******************************************************
// Initialize library
//******************************************************
void CANLIBAPI canInitializeLibrary (void)
{
  if (Initialized != TRUE) {
    memset(&timeDomains, 0, sizeof(kvTimeDomainHead));
    if (pthread_mutex_init(&timeDomains.critSect, PTHREAD_PROCESS_PRIVATE)) {
      PRINTF_TIMEDOMAIN(("canInitializeLibrary: pthread_mutex_init failed\n"));
    } else {
      InitializeListHead(&timeDomains.headOfAllHeads);
      Initialized = TRUE;
    }
  }
  return;
}

//******************************************************
// Unload library
//******************************************************
canStatus CANLIBAPI canUnloadLibrary (void)
{
  while (!IsListEmpty(&timeDomains.headOfAllHeads)) {
    LIST_ENTRY *td = RemoveHeadList(&timeDomains.headOfAllHeads);
    kvTimeDomainDelete(td);
  }
  foreachHandle(&canClose);
  if (Initialized) {
    if (pthread_mutex_destroy(&timeDomains.critSect)) {
      PRINTF_TIMEDOMAIN(("canUnloadLibrary: pthread_mutex_destroy failed\n"));
    }
  }
  Initialized = FALSE;

  return canOK;
}

static canStatus check_bitrate (const CanHandle hnd, unsigned int bitrate)
{
  canStatus    ret;
  unsigned int max_bitrate;

  ret = canGetHandleData(hnd, canCHANNELDATA_MAX_BITRATE, &max_bitrate, sizeof(max_bitrate));

  if (ret != canOK) {
    return ret;
  }

  if (max_bitrate) {
    if (bitrate > max_bitrate) {
      return canERR_PARAM;
    }
  }
  return canOK;
}
