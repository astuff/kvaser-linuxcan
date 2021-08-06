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

/*
 * Kvaser firmware image flash tool
 */
#include "can_fwu.h"
#include "kv_flash.h"

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> /* strtol, exit, malloc, free */
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h> /* access */

static tFwUpdateCfg fwuCfg = {.eanHi = 0, /* image ean */
                              .eanLo = 0, /* image ean */
                              .dryRun = 0};

static void printDevInfo(struct kvaser_device *device)
{
  assert(device);

  printf("\t%s\n"
         "\tIndex   %u\n"
         "\tEAN     " KV_FLASH_EAN_FRMT_STR "\n"
         "\tSN      %u\n"
         "\tFW      v%u.%u.%u\n"
         "\tDriver  %s\n"
         "%s\n",
         device->device_name,
         device->index,
         device->ean[1] >> 12,
         ((device->ean[1] & 0xfff) << 8) | (device->ean[0] >> 24),
         device->ean[0] >> 4 & 0xfffff,
         device->ean[0] & 0xf,
         device->serial,
         device->fw[1] >> 16, device->fw[1] & 0xffff, device->fw[0] & 0xffff,
         device->driver_name,
         device->info_str);
}

static void printDevices(struct kvaser_devices *devices)
{
  uint8_t count;

  assert(devices);
  count = devices->count;
  if (count) {
    int i;

    printf("Found %u supported device(s):\n", count);
    for (i = 0; i < count; i++) {
      printDevInfo(&devices->devices[i]);
    }
  }
}

static void printUsageAndExit(char *prgName)
{

  printf("Usage:\n"
         "  '%s [--dryrun] [-y|--yes] [--device=<EAN>:<S/N>|--device-index=<index>] <filename>'\n"
         "  '%s --list'\n",
         prgName,
         prgName);
  printf(
      "Options:\n"
      "  --list                  List all compatible devices and exit\n"
      "  --dryrun                Transfer image and execute all checks, but do not write the image\n"
      "  -y, --yes               Automatically answer yes\n"
      "  --device=<EAN>:<S/N>    Use device with EAN <EAN> and serial number <S/N>\n"
      "  --device-index=<index>  Use device with index <index>\n"
      "  <filename>              Path to the firmware image file\n"
      "Examples:\n"
      "  %s FirmwareUpdateTool/4_9/pcie/pciecan_00683_fpgasys.img\n"
      "  %s -y --device=73-30130-00683-6:11055 FirmwareUpdateTool/4_9/pcie/pciecan_00683_fpgasys.img\n"
      "  %s --yes --dryrun --device-index=0 FirmwareUpdateTool/4_9/pcie/pciecan_00683_fpgasys.img\n",
      prgName,
      prgName,
      prgName);
  exit(1);
}

static void sighand(int sig, siginfo_t *info, void *ucontext)
{
  (void)sig;
  (void)info;
  (void)ucontext;
}

static uint32_t timeGetTime(void)
{
  struct timeval tv;
  static struct timeval start;

  gettimeofday(&tv, NULL);

  if (start.tv_sec == 0) {
    start.tv_sec = tv.tv_sec;
    start.tv_usec = tv.tv_usec;
    return 0;
  } else {
    return (tv.tv_sec - start.tv_sec) * 1000
           + (tv.tv_usec - start.tv_usec) / 1000;
  }
}

static int downloadFirmware(struct kvaser_device *device, tFwImageInfo *info,
                            uint32_t chunkSize)
{
  uint32_t addr;
  uint32_t n;
  int r;
  uint32_t t0;

  assert(device);
  assert(info);
  t0 = timeGetTime();

  n = 0;
  addr = 0;

  while (addr < info->imageSize) {
    uint32_t len = info->imageSize - addr;

    if (len > chunkSize) {
      len = chunkSize;
    }

    r = fwUpdateDownload(device, addr, len, &info->imageBuffer[addr]);
    if (r != 0) {
      return -1;
    }

    addr += len;
    n++;
  }

  t0 = timeGetTime() - t0;
  printf("\nDownload complete (%d bytes/s, total %d bytes)\n",
         info->imageSize * 1000 / (t0 ? t0 : 1), info->imageSize);

  return 0;
}

int main(int argc, char *argv[])
{
  struct sigaction sigact;

  struct kvaser_device *device;
  char *filename;
  int deviceIndex = -1;
  int isIndexSet = 0;
  int isEanSnSet = 0;
  int dryrun = 0;
  int onlyListDevices = 0;
  int userConfirmation = 1;
  struct kvaser_devices kvaser_devices = {0};

  uint32_t inputEan[2] = {0};
  uint32_t inputSerial = 0;

  tFwImageInfo imageInfo = {0};
  uint32_t chunkSize;

  int ret;

  /* Use sighand and allow SIGINT to interrupt syscalls */
  sigact.sa_flags = SA_SIGINFO;
  sigemptyset(&sigact.sa_mask);
  sigact.sa_sigaction = sighand;
  if (sigaction(SIGINT, &sigact, NULL) != 0) {
    perror("sigaction SIGINT failed");
    return -1;
  }

  while (1) {
    char *endPtr = NULL;
    int opt;
    int opti = 0;
    static struct option options[] = {
      {"help",         no_argument,       NULL, 'h'},
      {"dryrun",       no_argument,       NULL, 'd'},
      {"yes",          no_argument,       NULL, 'y'},
      {"device-index", required_argument, NULL, 'i'},
      {"device",       required_argument, NULL, 'e'},
      {"list",         no_argument,       NULL, 'l'},
      {0,              0,                 NULL,  0}
    };
    uint32_t parseEan[4];

    opterr = 0;
    opt = getopt_long(argc, argv, "y", options, &opti);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 'd':
      dryrun = 1;
      break;
    case 'y':
      userConfirmation = 0;
      break;
    case 'l':
      onlyListDevices = 1;
      break;
    case 'i':
      errno = 0;
      deviceIndex = strtol(optarg, &endPtr, 10);
      if ((errno != 0) || (endPtr != (optarg + strlen(optarg)))) {
        /* Failed to convert the string to integer */
        printf("Invalid option --device-index=%s\n", optarg);
        printUsageAndExit(argv[0]);
      }
      isIndexSet = 1;
      break;
    case 'e':
      if (sscanf(optarg, KV_FLASH_EAN_FRMT_STR ":%u",
                 &parseEan[0], &parseEan[1], &parseEan[2], &parseEan[3],
                 &inputSerial) == 5) {
        inputEan[1] = (parseEan[0] << 12) | (parseEan[1] >> 8);
        inputEan[0] = (parseEan[1] << 24) | (parseEan[2] << 4) |
                      (parseEan[3] & 0xf);
      }

      if (inputEan[0] == 0 ||
          inputEan[1] == 0 ||
          inputSerial == 0) {
        printf("Invalid option --device=%s\n", optarg);
        printUsageAndExit(argv[0]);
      }
      isEanSnSet = 1;
      break;
    case '?':
    default:
      printf("Error: Unknown option %s\n\n", argv[--optind]);
    /* fall-through */
    case 'h':
      printUsageAndExit(argv[0]);
      break;
    }
  }

  if (onlyListDevices && (isIndexSet || isEanSnSet || dryrun || !userConfirmation)) {
    printf("Invalid option combination. Do not use --list together with other options.\n");
    printUsageAndExit(argv[0]);
  }

  if (onlyListDevices) {
    if (optind != argc) {
      printUsageAndExit(argv[0]);
    }
  } else {
    if (optind != argc - 1) {
      printUsageAndExit(argv[0]);
    } else {
      filename = argv[optind];
    }

    ret = access(filename, F_OK);
    if (ret) {
      printf("Cannot find image file \"%s\": %m\n", filename);
      return 1;
    }

    if (isIndexSet && isEanSnSet) {
      printf("Invalid combination --device-index together with --device\n");
      printUsageAndExit(argv[0]);
    }
  }

  ret = kvaser_fwu_init_lib(&kvaser_devices);
  if (ret) {
    printf("kvaser_fwu_init_lib() failed: %d\n", ret);
    return 1;
  }

  if (onlyListDevices) {
    printDevices(&kvaser_devices);
    ret = 0;
    goto ErrorExitDeinit;
  }

  if (kvaser_devices.count == 0) {
    printf("No Kvaser device supporting FW upgrade in Linux found.\n");
    ret = 1;
    goto ErrorExitDeinit;
  }

  if (isEanSnSet) {
    int i;

    for (i = 0; i < kvaser_devices.count; i++) {
      struct kvaser_device *currDev = &kvaser_devices.devices[i];

      if (currDev->ean[0] == inputEan[0] &&
          currDev->ean[1] == inputEan[1] &&
          currDev->serial == inputSerial) {
        deviceIndex = i;
        break;
      }
    }
    if (deviceIndex < 0) {
      printf("No match found for --device=" KV_FLASH_EAN_FRMT_STR ":%u\n",
             inputEan[1] >> 12,
             ((inputEan[1] & 0xfff) << 8) | (inputEan[0] >> 24),
             inputEan[0] >> 4 & 0xfffff, inputEan[0] & 0xf,
             inputSerial);
      ret = 1;
      goto ErrorExitDeinit;
    }
  }

  if (!isIndexSet && !isEanSnSet) { // Do list devices
    char indexStr[8];
    char *endPtr = NULL;

    printDevices(&kvaser_devices);
    printf("Select device index [0 - %u]: ", kvaser_devices.count - 1);
    if (fgets(indexStr, sizeof(indexStr), stdin) == NULL) {
      /* Failed to read */
      printf("\n");
      ret = 1;
      goto ErrorExitDeinit;
    }
    errno = 0;
    deviceIndex = strtol(indexStr, &endPtr, 10);
    if ((errno != 0) ||
        (endPtr != (indexStr + strlen(indexStr) - 1))) { /* Ignore newline */
      /* Failed to convert the string to integer */
      printf("Invalid device selection: %s\n", indexStr);
      ret = 1;
      goto ErrorExitDeinit;
    }
  }

  if (deviceIndex < 0 || deviceIndex >= kvaser_devices.count) {
    printf("device-index %d is outside the valid range: 0 - %u\n", deviceIndex,
           kvaser_devices.count - 1);
    if (isIndexSet || isEanSnSet) {
      printDevices(&kvaser_devices);
    }
    ret = 1;
    goto ErrorExitDeinit;
  }

  device = &kvaser_devices.devices[deviceIndex];
  kvaser_fwu_open(device);

  if (dryrun) {
    printf("Starting dry run\n");
  }
  printf("About to flash %s onto device with index %d\n", filename,
         deviceIndex);
  printDevInfo(device);

  if (fwUpdateLoadImageFile(filename, FW_IMG_TYPE_HYDRA, &imageInfo) != 0) {
    ret = 1;
    goto ErrorExit;
  }

  /* Disallow flashing images that does not match the device EAN */
  if ((device->ean[0] != imageInfo.eanLo) ||
      (device->ean[1] != imageInfo.eanHi)) {
    printf("Error: FW image doesn't match device\n"
           "\tDevice EAN: " KV_FLASH_EAN_FRMT_STR "\n"
           "\tImage EAN:  " KV_FLASH_EAN_FRMT_STR "\n",
           device->ean[1] >> 12,
           ((device->ean[1] & 0xfff) << 8) | (device->ean[0] >> 24),
           device->ean[0] >> 4 & 0xfffff, device->ean[0] & 0xf,
           imageInfo.eanHi >> 12,
           ((imageInfo.eanHi & 0xfff) << 8) | (imageInfo.eanLo >> 24),
           (imageInfo.eanLo >> 4) & 0xfffff, imageInfo.eanLo & 0xf);
    ret = 1;
    goto ErrorExit;
  }

  if (userConfirmation) {
    char c;

    printf("Do you want to continue? [y/N] ");
    c = getchar();
    if (c != 'y' && c != 'Y') {
      printf("Abort!\n");
      ret = 1;
      goto ErrorExit;
    }
  }

  fwuCfg.dryRun = dryrun;
  if (fwUpdateStart(device, &fwuCfg, &chunkSize) != 0) {
    ret = 2;
    goto ErrorExit;
  }

  printf("Firmware upgrade started. Do not interrupt or power down.\n");
  printf("Downloading image...\n");
  if (downloadFirmware(device, &imageInfo, chunkSize) != 0) {
    ret = 2;
    goto ErrorExit;
  }

  printf("Committing...\n");
  if (fwUpdateCommandNoParam(device, FIRMWARE_DOWNLOAD_COMMIT) != 0) {
    ret = 2;
    goto ErrorExit;
  }

  if (dryrun) {
    printf("This has been a dry run\n\n");
  } else {
    printf(
        "The firmware was updated successfully, please power cycle the device\n"
        "Note: When updating PCIEcan, a complete shutdown of the computer is required in order to power cycle the device\n\n");
  }

  printf("Finishing...\n");
  if (fwUpdateCommandNoParam(device, FIRMWARE_DOWNLOAD_FINISH) != 0) {
    ret = 2;
    goto ErrorExit;
  }
  ret = 0;

ErrorExit:
  fwUpdateUnloadImageFile(&imageInfo);
  kvaser_fwu_close(device);

ErrorExitDeinit:
  kvaser_fwu_deinit_lib(&kvaser_devices);

  return ret;
}
