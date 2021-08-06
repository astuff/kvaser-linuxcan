/*
 *             Copyright 2021 by Kvaser AB, Molndal, Sweden
 *                         http://www.kvaser.com
 *
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
 *     * Neither the name of the \<organization\> nor the
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

#include "kcan_ioctl.h"
#include "kcan_ioctl_flash.h"
#include "kv_flash.h"
#include <canlib.h>

#include <assert.h>
#include <errno.h> /* EIO, ENODEV, ENOMEM */
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

struct canlib_device {
  canHandle hnd;
  int channel;
};

static const char *supportedDrivers[] = {"pciefd", "\0"};

static void checkCanStatus(const char *id, canStatus stat)
{
  assert(id);
  if (stat != canOK) {
    char buf[64];
    buf[0] = '\0';
    canGetErrorText(stat, buf, sizeof(buf));
    printf("Error: %s: failed, stat=%d (%s)\n", id, (int)stat, buf);
  }
}

static int isDriverSupported(const char *driverName)
{
  int i;

  assert(driverName);
  /* Check if driver is supported */
  for (i = 0; strnlen(supportedDrivers[i], 16); i++) {
    if (strcmp(driverName, supportedDrivers[i]) == 0) {
      return 1;
    }
  }

  return 0;
}

static canStatus canlibFlashFindDevices(struct kvaser_devices *devices)
{
  canStatus stat;
  int chanCount = 0;
  int i;

  assert(devices);
  stat = canGetNumberOfChannels(&chanCount);
  if (stat != canOK) {
    checkCanStatus("canGetNumberOfChannels", stat);
    return 1;
  }

  for (i = 0; i < chanCount; i++) {
    uint32_t noOnCard = -1;
    char driverName[64] = {0};
    char deviceName[64] = {0};
    uint32_t ean[2] = {0};
    uint32_t fw[2] = {0};
    uint32_t serial[2] = {0};
    struct canlib_device *canlib_dev;
    struct kvaser_device *device;

    stat = canGetChannelData(i, canCHANNELDATA_DRIVER_NAME, &driverName,
                             sizeof(driverName));
    if (stat != canOK) {
      checkCanStatus("canGetChannelData: DRIVER_NAME", stat);
      return 1;
    }

    if (!isDriverSupported(driverName)) {
      continue;
    }

    /* We are only looking for first channel on card */
    stat = canGetChannelData(i, canCHANNELDATA_CHAN_NO_ON_CARD, &noOnCard,
                             sizeof(noOnCard));
    if (stat != canOK) {
      checkCanStatus("canGetChannelData: CHAN_NO_ON_CARD", stat);
      return 1;
    }
    if (noOnCard != 0) {
      continue;
    }

    stat = canGetChannelData(i, canCHANNELDATA_CARD_UPC_NO, &ean, sizeof(ean));
    if (stat != canOK) {
      checkCanStatus("canGetChannelData: CARD_UPC_NO", stat);
      return 1;
    }

    stat = canGetChannelData(i, canCHANNELDATA_CARD_SERIAL_NO, &serial,
                             sizeof(serial));
    if (stat != canOK) {
      checkCanStatus("canGetChannelData: CARD_SERIAL_NO", stat);
      return 1;
    }

    stat = canGetChannelData(i, canCHANNELDATA_DEVDESCR_ASCII, &deviceName,
                             sizeof(deviceName));
    if (stat != canOK) {
      checkCanStatus("canGetChannelData: DEVDESCR_ASCII", stat);
      return 1;
    }
    stat = canGetChannelData(i, canCHANNELDATA_CARD_FIRMWARE_REV, &fw,
                             sizeof(fw));
    if (stat != canOK) {
      checkCanStatus("canGetChannelData: CARD_FIRMWARE_REV", stat);
      return 1;
    }

    device = &devices->devices[devices->count++];
    canlib_dev = malloc(sizeof(struct canlib_device));
    if (canlib_dev == NULL) {
      printf("Error: malloc canlib_dev failed\n");
      return -ENOMEM;
    }
    device->index = devices->count - 1;

    device->ean[0] = ean[0];
    device->ean[1] = ean[1];
    device->fw[0] = fw[0];
    device->fw[1] = fw[1];
    device->serial = serial[0];
    strncpy(device->driver_name, driverName, sizeof(device->driver_name));
    strncpy(device->device_name, deviceName, sizeof(device->device_name));
    snprintf(device->info_str, sizeof(device->info_str), "\tChannel %d\n", i);
    canlib_dev->channel = i;
    canlib_dev->hnd = -1;
    device->lib_data = canlib_dev;
  }

  return 0;
}

/*  kv_flash.h API
 * ======================================================================
 */
int kvaser_fwu_flash_prog(struct kvaser_device *device, KCAN_FLASH_PROG *fp)
{
  struct canlib_device *dev;
  int fd = -1;
  canHandle hnd;

  assert(device);
  assert(fp);
  dev = device->lib_data;
  assert(dev);
  hnd = dev->hnd;

  if (canGetRawHandle(hnd, &fd) != canOK) {
    printf("Error: canGetRawHandle failed: %m\n");
    return -1;
  }

  return ioctl(fd, KCAN_IOCTL_FLASH_PROG, fp);
}

int kvaser_fwu_deinit_lib(struct kvaser_devices *devices)
{
  canStatus stat;

  if (devices) {
    int i;

    for (i = 0; i < devices->count; i++) {
      struct kvaser_device *device = &devices->devices[i];

      if (device->lib_data) {
        free(device->lib_data);
        device->lib_data = NULL;
      }
    }
    devices->count = 0;
  }
  stat = canUnloadLibrary();
  checkCanStatus("canUnloadLibrary", stat);

  return stat;
}

int kvaser_fwu_init_lib(struct kvaser_devices *devices)
{
  int ret;

  if (devices == NULL) {
    printf("kvaser_fwu_init_lib(): devices is NULL\n");
    return 1;
  }
  canInitializeLibrary();

  ret = canlibFlashFindDevices(devices);
  if (ret) {
    kvaser_fwu_deinit_lib(devices);
    printf("canlibFlashFindDevices() failed: %d\n", ret);
    return ret;
  }

  return ret;
}

int kvaser_fwu_open(struct kvaser_device *device)
{
  int ret = -1;

  if (device) {
    struct canlib_device *dev;
    int channel;
    canHandle hnd;

    dev = device->lib_data;
    assert(dev);
    channel = dev->channel;
    hnd = canOpenChannel(channel, canOPEN_EXCLUSIVE);

    if (hnd >= 0) {
      dev->hnd = hnd;
    } else {
      ret = hnd;
    }
  }

  return ret;
}

int kvaser_fwu_close(struct kvaser_device *device)
{
  int ret = -1;

  if (device) {
    struct canlib_device *dev;
    canHandle hnd;

    dev = device->lib_data;
    assert(dev);
    hnd = dev->hnd;
    ret = canClose(hnd);
  }

  return ret;
}
