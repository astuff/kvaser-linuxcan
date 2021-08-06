/*
**             Copyright 2020 by Kvaser AB, Molndal, Sweden
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

#include "can_fwu.h"
#include "can_fwu_hydra.h"
#include "hydra_imgheader.h"
#include "kcan_ioctl_flash.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h> /* calloc, free */
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#define DEF2STR(x) case x: return #x;

static const char *fwStatusName(unsigned int fwuStatus)
{
  switch (fwuStatus) {
  DEF2STR(FIRMWARE_STATUS_OK)
  DEF2STR(FIRMWARE_ERROR_NOPRIV)
  DEF2STR(FIRMWARE_ERROR_ADDR)
  DEF2STR(FIRMWARE_ERROR_FLASH_FAILED)
  DEF2STR(FIRMWARE_ERROR_COMMAND)
  DEF2STR(FIRMWARE_ERROR_PARAM_FULL)
  DEF2STR(FIRMWARE_ERROR_PWD_WRONG)
  DEF2STR(FIRMWARE_ERROR_VERSION_WRONG)
  DEF2STR(FIRMWARE_ERROR_BAD_CRC)
  default:
    return "FWU STATUS UNKNOWN";
  }
}

static const char *fwCmdName(unsigned int fwuCmd)
{
  switch (fwuCmd) {
  DEF2STR(FIRMWARE_DOWNLOAD_COMMIT)
  DEF2STR(FIRMWARE_DOWNLOAD_WRITE)
  DEF2STR(FIRMWARE_DOWNLOAD_FINISH)
  DEF2STR(FIRMWARE_DOWNLOAD_STARTUP)
  DEF2STR(FIRMWARE_DOWNLOAD_ERASE)
  default:
    return "FWU CMD UNKNOWN";
  }
}

int loadImageFromFile(const char *filename, uint8_t **imageBuffer,
                      size_t *imageSize)
{
  int r;
  FILE *f;
  struct stat statbuf;

  assert(imageSize);
  assert(imageBuffer);
  assert(*imageBuffer);
  assert(filename);

  f = fopen(filename, "rb");
  if (!f) {
    printf("Error: Unable to open file '%s'\n", filename);
    return -1;
  }

  if (fstat(fileno(f), &statbuf) < 0) {
    printf("Error: Unable to stat file '%s'\n", filename);
    fclose(f);
    return -1;
  }

  *imageSize = statbuf.st_size;
  *imageBuffer = (uint8_t *)calloc(1, *imageSize);

  r = fread(*imageBuffer, 1, *imageSize, f);
  if (r < 0) {
    printf("Error: Unable to read file '%s'\n", filename);
    fclose(f);
    return -1;
  }
  if (r != statbuf.st_size) {
    printf("Error: File '%s' was truncated\n", filename);
    fclose(f);
    return -1;
  }
  fclose(f);

  return 0;
}

int fwUpdateCommandNoParam(struct kvaser_device *device, int cmd)
{
  KCAN_FLASH_PROG io;
  int ret;

  assert(device);
  memset(&io, 0, sizeof(io));
  io.tag = cmd;

  ret = kvaser_fwu_flash_prog(device, &io);
  if (ret) {
    printf("Error: kvaser_fwu_flash_prog for command %s failed: %u %m\n",
           fwCmdName(io.tag), ret);
    return -1;
  }

  if (io.status != FIRMWARE_STATUS_OK) {
    printf("Error: command %s failed with status: %s\n", fwCmdName(io.tag),
           fwStatusName(io.status));
    return -1;
  }

  return 0;
}

int fwUpdateStart(struct kvaser_device *device, tFwUpdateCfg *fwuCfg,
                  unsigned int *buffer_size)
{
  KCAN_FLASH_PROG io;
  int ret;

  assert(device);
  assert(fwuCfg);
  assert(buffer_size);

  memset(&io, 0, sizeof(io));

  io.tag = FIRMWARE_DOWNLOAD_STARTUP;
  io.x.setup.ean[0] = fwuCfg->eanLo;
  io.x.setup.ean[1] = fwuCfg->eanHi;
  io.x.setup.dryrun = fwuCfg->dryRun;
  /* currently only support for version 0 */
  io.x.setup.flash_procedure_version = 0; /* Legacy transfer mode */
  io.x.setup.buffer_size = 0;

  ret = kvaser_fwu_flash_prog(device, &io);
  if (ret) {
    printf("Error: kvaser_fwu_flash_prog for command %s failed: %m\n",
           fwCmdName(io.tag));
    return -1;
  }

  if (io.status != FIRMWARE_STATUS_OK) {
    printf("Error: command %s failed with status: %s\n", fwCmdName(io.tag),
           fwStatusName(io.status));
    return -1;
  }

  *buffer_size = io.x.setup.buffer_size;

  return 0;
}

int fwUpdateDownload(struct kvaser_device *device, uint32_t address, int len,
                     uint8_t *data)
{
  KCAN_FLASH_PROG io;
  int ret;

  assert(device);
  if (!data) {
    return -1;
  }
  if (len <= 0) {
    return -1;
  }

  if (len > KCAN_FLASH_DOWNLOAD_CHUNK) {
    return -1;
  }

  memset(&io, 0, sizeof(io));
  io.status = 0;
  io.tag = FIRMWARE_DOWNLOAD_WRITE;
  io.x.data.address = address;
  io.x.data.len = len;

  if (len > (int)sizeof(io.x.data.data)) {
    return -1;
  }

  memcpy(io.x.data.data, data, len);

  ret = kvaser_fwu_flash_prog(device, &io);
  if (ret) {
    printf("Error: kvaser_fwu_flash_prog for command %s failed: %m\n",
           fwCmdName(io.tag));
    return -1;
  }

  if (io.status != FIRMWARE_STATUS_OK) {
    printf("Error: Download failed (addr=0x%08x, len=0x%x, status=%s)\n",
           address, len, fwStatusName(io.status));
    return -1;
  }

  return 0;
}

/* load image, verify and parse key information */
int fwUpdateLoadImageFile(char *filename, tFwImageType imageType,
                          tFwImageInfo *image)
{
  int result = -1;

  assert(filename);
  assert(image);
  switch (imageType) {
  case FW_IMG_TYPE_HYDRA:
    result = fwUpdateLoadHydraImage(filename, image);
    break;
  default:
    printf("Error: fw image type %d unsupported\n", imageType);
    break;
  }
  return result;
}

void fwUpdateUnloadImageFile(tFwImageInfo *image)
{
  assert(image);
  if (image->imageBuffer) {
    free(image->imageBuffer);
    image->imageBuffer = NULL;
  }
}
