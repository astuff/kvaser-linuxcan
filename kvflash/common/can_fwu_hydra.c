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

#include "can_fwu_hydra.h"
#include "crc32.h"
#include "hydra_imgheader.h"

#include <assert.h>
#include <stdio.h>

static int verifyHydraImage(uint8_t *imageBuffer, size_t imageSize)
{
  uint32_t checkSum = 0;
  HydraImgHeader *header;

  assert(imageBuffer);
  header = (HydraImgHeader *)imageBuffer;

  if (imageSize != (sizeof(HydraImgHeader) + header->imgLength + 4)) {
    printf("Error: Image file was truncated, got %zu expected %zu\n", imageSize,
           sizeof(HydraImgHeader) + header->imgLength + 4);
    return -1;
  }

  /* verify entire image header */
  checkSum = crc32Calc((uint8_t *)imageBuffer, sizeof(HydraImgHeader) - 4);

  if (checkSum != header->hdCrc) {
    printf("Error: Image is corrupt, wrong CRC for image header\n");
    return -1;
  }

  if ((header->imgType != IMG_TYPE_SYSTEM_CONTAINER)) {
    printf("Error: Image file is of wrong type, expected %u, got %u\n",
           IMG_TYPE_SYSTEM_CONTAINER, header->imgType);
    return -1;
  }

  /* verify entire image content */
  checkSum = crc32Calc((uint8_t *)(imageBuffer + sizeof(HydraImgHeader)),
                       header->imgLength);

  if (checkSum != header->imgCrc) {
    printf("Error: Image is corrupt, wrong CRC for image\n");
    return -1;
  }

  return 0;
}

int fwUpdateLoadHydraImage(char *filename, tFwImageInfo *image)
{
  HydraImgHeader *header;
  uint8_t *imageBuffer;
  size_t imageSize;

  assert(filename);
  assert(image);
  if (loadImageFromFile(filename, &imageBuffer, &imageSize) != 0) {
    return -1;
  }

  if (verifyHydraImage(imageBuffer, imageSize) != 0) {
    return -1;
  }

  header = (HydraImgHeader *)imageBuffer;

  image->imageBuffer = imageBuffer;
  image->imageSize = imageSize;
  image->eanLo = header->eanLo;
  image->eanHi = header->eanHi;

  printf("Successfully loaded hydra image\n\t%s\n", filename);
  printf("\tEAN " KV_FLASH_EAN_FRMT_STR "\n", header->eanHi >> 12,
         ((header->eanHi & 0xfff) << 8) | (header->eanLo >> 24),
         (header->eanLo >> 4) & 0xfffff, header->eanLo & 0xf);
  printf("\t    v%x.%x.%x\n\n", header->version >> 24,
         header->version >> 16 & 0xff, header->version & 0xffff);

  return 0;
}
