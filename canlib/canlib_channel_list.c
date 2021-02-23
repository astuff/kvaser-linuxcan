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

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "canlib.h"
#include "vcan_ioctl.h"
#include "canlib_channel_list.h"

// This has to be modified if we add/remove drivers.
static const char *driver_name[] = {"lapcan", "pcican", "pcicanII",
                                    "usbcanII", "leaf", "mhydra",
                                    "pciefd",
                                    "kvvirtualcan"}; // Virtual channels should always be last


static const char *off_name[] = {"LAPcan", "PCIcan", "PCIcanII",
                                 "USBcanII", "Leaf", "Minihydra",
                                 "PCIe CAN",
                                 "VIRTUALcan"}; // Virtual channels should always be last


int cmpfunc_ean (const void * a, const void * b) {
  //skip virtual as the always are last in the list with driver names
  if ((((ccl_channel*)a)->ean == 0) || (((ccl_channel*)b)->ean == 0))  {
    return 0;
  }

  if (((ccl_channel*)a)->ean > ((ccl_channel*)b)->ean)  {
    return 1;
  } else if (((ccl_channel*)a)->ean < ((ccl_channel*)b)->ean) {
    return -1;
  } else {
    return 0;
  }
}

int cmpfunc_snr (const void * a, const void * b) {
  if (((ccl_channel*)a)->ean == ((ccl_channel*)b)->ean)  {
    if (((ccl_channel*)a)->snr > ((ccl_channel*)b)->snr)  {
      return 1;
    } else if (((ccl_channel*)a)->snr < ((ccl_channel*)b)->snr) {
      return -1;
    } else {
      return 0;
    }
  } else {
    return 0;
  }
}

canStatus ccl_get_channel_list (ccl_class *self)
{
  uint32_t    driver_index, minor_index, index;
  int         number_on_card = 0;
  ccl_channel prev_channel;
  uint32_t    max_nof_channel = (uint32_t)(sizeof(self->channel)/sizeof(ccl_channel));

  self->n_channel = 0;

  for (driver_index = 0; driver_index < sizeof(driver_name) / sizeof(*driver_name); driver_index++) {
    uint32_t number_on_driver = 0;
    for (minor_index = 0; minor_index < 128; minor_index++) {
      uint32_t skip = 0;
      int      fd;
      char     file_name[64];

      sprintf(file_name, "/dev/%s%u", driver_name[driver_index], minor_index);
      fd = open(file_name, O_RDONLY);
      if (fd == -1) {
        if ((errno != ENODEV) && (errno != ENOENT)) {
          return canERR_NOTFOUND;
        }
        skip = 1;
      } else {
        if (ioctl(fd, VCAN_IOC_GET_SERIAL, &self->channel[self->n_channel].snr)) {
          if (errno != ESHUTDOWN) {
            close(fd);
            return canERR_NOTFOUND;
          }
          skip = 1;
        }

        if (ioctl(fd, VCAN_IOC_GET_EAN, &self->channel[self->n_channel].ean)) {
          if (errno != ESHUTDOWN) {
            close(fd);
            return canERR_NOTFOUND;
          }
          skip = 1;
        }

        close(fd);
      }

      if (skip == 0) {
        int n_bytes_written;

        n_bytes_written = snprintf(self->channel[self->n_channel].mknod_name, sizeof(self->channel[self->n_channel].mknod_name), "%s", file_name);
        if (n_bytes_written >= (int)sizeof(self->channel[self->n_channel].mknod_name)) {
          return canERR_NOTFOUND;
        }

        n_bytes_written = snprintf(self->channel[self->n_channel].official_name, sizeof(self->channel[self->n_channel].official_name), "%s", off_name[driver_index]);
        if (n_bytes_written >= (int)sizeof(self->channel[self->n_channel].official_name)) {
          return canERR_NOTFOUND;
        }

        self->channel[self->n_channel].number_on_driver = number_on_driver;
        number_on_driver++;
        self->n_channel++;

        if (self->n_channel >= max_nof_channel) {
          break;
        }
      }
    }
  }

  //sort on ean and serial-number
  if (self->n_channel > 0) {
    qsort(&self->channel, self->n_channel, sizeof(ccl_channel), cmpfunc_ean);
    qsort(&self->channel, self->n_channel, sizeof(ccl_channel), cmpfunc_snr);
  }

  prev_channel.ean = 0xFFFFFFFFFFFFFFFF;
  prev_channel.snr = 0xFFFFFFFFFFFFFFFF;

  //get channel number on card
  for(index = 0; index < self->n_channel; index++) {
    if ((prev_channel.ean != self->channel[index].ean) || (prev_channel.snr != self->channel[index].snr)) {
      number_on_card   = 0;
      prev_channel.ean = self->channel[index].ean;
      prev_channel.snr = self->channel[index].snr;
    } else {
      number_on_card++;
    }

    self->channel[index].number_on_card = number_on_card;
  }

  return canOK;
}
