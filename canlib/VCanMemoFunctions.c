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

/* Kvaser Linux Canlib VCan layer functions used in Memorators */

#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include "kcanio_memorator.h"
#include "kcanio_script.h"
#include "VCanMemoFunctions.h"
#include "VCanFuncUtil.h"
#include "lio_error.h"
#include "dio_error.h"

#if DEBUG
#   define DEBUGPRINT(args) printf args
#else
#   define DEBUGPRINT(args)
#endif

/***************************************************************************/
static canStatus lioResultToCanStatus(LioResult r)
{
  canStatus stat;

  switch (r) {
    case lio_OK:                  stat = canOK; break;

    case lio_queueFull:           stat = canERR_TXBUFOFL; break;
    case lio_CRCError:            stat = canERR_CRC; break;
    case lio_SectorErased:        stat = canERR_DISK; break;
    case lio_FileError:           stat = canERR_DEVICE_FILE; break;
    case lio_DiskError:           stat = canERR_DISK; break;
    case lio_DiskFull_Dir:        stat = canERR_DISK; break;
    case lio_DiskFull_Data:       stat = canERR_DISK; break;
    case lio_EOF:                 stat = canERR_DEVICE_FILE; break;
    case lio_SeqError:            stat = canERR_INTERNAL; break;
    case lio_Error:               stat = canERR_INTERNAL; break;
    case lio_FileSystemCorrupt:   stat = canERR_DISK; break;
    case lio_UnsupportedVersion:  stat = canERR_NOT_IMPLEMENTED; break;
    case lio_NotImplemented:      stat = canERR_NOT_IMPLEMENTED; break;
    case lio_FatalError:          stat = canERR_INTERNAL; break;
    default:
      stat = canERR_MEMO_FAIL;
      break;
  }
  if (stat != canOK) {
    DEBUGPRINT((TXT("lioResultToCanStatus: %d -> %d\n"), r, stat));
  }
  return stat;
}

/***************************************************************************/
static canStatus dioResultToCanStatus(DioResult r)
{
  canStatus stat;

  switch (r) {
    case dio_OK:              stat = canOK; break;

    case dio_QueueFull:       stat = canERR_TXBUFOFL; break;
    case dio_CRCError:        stat = canERR_CRC; break;
    case dio_SectorErased:    stat = canERR_DISK; break;
//  dio_Pending;
    case dio_IllegalRequest:  stat = canERR_PARAM; break;
    case dio_FileNotFound:    stat = canERR_DEVICE_FILE; break;
    case dio_FileError:       stat = canERR_DEVICE_FILE; break;
    case dio_NotImplemented:  stat = canERR_NOT_IMPLEMENTED; break;
    case dio_NotFormatted:    stat = canERR_DISK; break;
    case dio_Timeout:         stat = canERR_HARDWARE; break;
    case dio_WrongDiskType:   stat = canERR_DISK; break;
    case dio_DiskError:       stat = canERR_DISK; break;
    case dio_DiskCommError:   stat = canERR_HARDWARE; break;
    case dio_NoDisk:          stat = canERR_DISK; break;
    case dio_NoMemory:        stat = canERR_NOMEM; break;
    case dio_UserCancel:      stat = canERR_MEMO_FAIL; break;
    case dio_CpldError:       stat = canERR_HARDWARE; break;
    case dio_ConfigError:     stat = canERR_CONFIG; break;
    default:
      stat = canERR_MEMO_FAIL;
      break;
  }
  if (stat != canOK) {
    DEBUGPRINT((TXT("dioResultToCanStatus: %d -> %d\n"), r, stat));
  }
  return stat;
}

/***************************************************************************/
static canStatus memoResultToCanStatus(KCANY_MEMO_INFO *info)
{
  canStatus ret = canOK;

  if (info->status == KCANIO_MEMO_STATUS_FAILED) {
    DEBUGPRINT((TXT("errcodes = stat: %d, dio: %d, lio: %d\n"), info->status,
                info->dio_status, info->lio_status));
    if (info->dio_status) {
      ret = dioResultToCanStatus((DioResult)info->dio_status);
    }
    else if (info->lio_status) {
      ret = lioResultToCanStatus((LioResult)info->lio_status);
    }
    else {
      ret = canERR_MEMO_FAIL;
    }
  }
  return ret;
}

/***************************************************************************/
static int is_filename_invalid(char *filename)
{
  if (strnlen(filename, CANIO_MAX_FILE_NAME + 1) > CANIO_MAX_FILE_NAME) {
    DEBUGPRINT((TXT("Device filenames are limited to 8+1+3 bytes. ")));
    DEBUGPRINT((TXT("'%s'\n"), filename));
    return 1;
  }
  return 0;
}

//======================================================================
// vCanMemo_file_delete
//======================================================================
canStatus vCanMemo_file_delete(HandleData *hData, char *deviceFileName)
{
  KCANY_MEMO_INFO info;
  int             status = 0;
  int             ret = 0;

  if (is_filename_invalid(deviceFileName)) {
    return canERR_PARAM;
  }

  memset(&info, 0, sizeof(info));
  (void) strncpy((char*)&info.buffer[0], deviceFileName, CANIO_MAX_FILE_NAME);

  info.subcommand = KCANIO_MEMO_SUBCMD_DELETE_FILE;
  info.buflen     = CANIO_MAX_FILE_NAME + 2; // 'mode' and '\0';
  info.timeout    = 30*1000;                 // 30s
  ret = ioctl(hData->fd, KCANY_IOCTL_MEMO_PUT_DATA, &info, sizeof(info));
  if (ret != 0) {
    DEBUGPRINT((TXT("vCanFileDelete: Communication error (%d)\n"), ret));
    return errnoToCanStatus(errno);
  }
  status = memoResultToCanStatus(&info);
  if (status != canOK) {
    DEBUGPRINT((TXT("vCanFileDelete: MEMO_SUBCMD_DELETE_FILE error (%d)\n"), (int)info.buffer[0]));
  }
  return status;
}

//======================================================================
// vCanMemo_file_copy_to_device
//======================================================================
canStatus vCanMemo_file_copy_to_device(HandleData *hData, char *hostFileName,
                                       char *deviceFileName)
{
  FILE            *hFile;
  KCANY_MEMO_INFO  info;
  int              status = 0;
  int              ret = 0;

  if (is_filename_invalid(deviceFileName)) {
    return canERR_PARAM;
  }

  hFile = fopen(hostFileName, "rb");

  if (!hFile) {
    DEBUGPRINT((TXT("vCanFileCopyToDevice: Could not open the file '%s'\n"), hostFileName));
    return canERR_HOST_FILE;
  }

  // Open the file and copy the blocks
  memset(&info, 0, sizeof(info));
  (void) strncpy((char*)&info.buffer[1], deviceFileName, CANIO_MAX_FILE_NAME);

  info.buffer[0]  = CANIO_DFS_WRITE;  // 'mode'
  info.subcommand = KCANIO_MEMO_SUBCMD_OPEN_FILE;
  info.buflen     = CANIO_MAX_FILE_NAME + 2;  // 'mode' and '\0';
  info.timeout    = 30*1000;  // 30 seconds
  ret = ioctl(hData->fd, KCANY_IOCTL_MEMO_PUT_DATA, &info, sizeof(info));
  if (ret != 0) {
    DEBUGPRINT((TXT("vCanFileCopyToDevice: (1) Communication error (%d)\n"), ret));
    fclose(hFile);
    return errnoToCanStatus(errno);
  }

  // Check the status
  status = memoResultToCanStatus(&info);
  if (status != canOK) {
    DEBUGPRINT((TXT("vCanFileCopyToDevice: MEMO_SUBCMD_OPEN_FILE error (%d)\n"), (int)info.buffer[0]));
    fclose(hFile);
    return status;
  }

  while (!status) {
    size_t bytes;

    memset(&info, 0, sizeof(info));
    bytes = fread(&info.buffer[4], 1, 512, hFile);
    if (bytes == 0) {
      // No more data to read
      break;
    }
    memcpy(&info.buffer[0],&bytes,sizeof(bytes));
    info.subcommand = KCANIO_MEMO_SUBCMD_WRITE_FILE;
    info.buflen     = (unsigned int) (bytes+sizeof(bytes));
    info.timeout    = 30*1000; // 30s
    ret = ioctl(hData->fd, KCANY_IOCTL_MEMO_PUT_DATA, &info, sizeof(info));
    if (ret != 0) {
      DEBUGPRINT((TXT("vCanFileCopyToDevice: (2) Communication error (%d)\n"), ret));
      status = errnoToCanStatus(errno);
    } else {
      status = memoResultToCanStatus(&info);
    }
  }

  fclose(hFile);

  if (status != canOK) {
    return status;
  }

  memset(&info, 0, sizeof(info));
  info.subcommand = KCANIO_MEMO_SUBCMD_CLOSE_FILE;
  info.timeout    = 30*1000; // 30s
  info.buflen     = CANIO_MAX_FILE_NAME;
  ret = ioctl(hData->fd, KCANY_IOCTL_MEMO_PUT_DATA, &info, sizeof(info));
  if (ret != 0) {
    DEBUGPRINT((TXT("vCanFileCopyToDevice: (3) Communication error (%d)\n"), ret));
    status = errnoToCanStatus(errno);
  } else {
    status = memoResultToCanStatus(&info);
  }
  return status;
}

//======================================================================
// vCanMemo_file_copy_from_device
//======================================================================
canStatus vCanMemo_file_copy_from_device(HandleData *hData,
                                         char *deviceFileName,
                                         char *hostFileName)
{
  FILE            *hFile;
  KCANY_MEMO_INFO  info;
  int              status = 0;
  int              ret = 0;
  uint32_t         bytes = 0;  // Size is specified by FW (src/common/he/hscle/logger_glue.c)

  if (is_filename_invalid(deviceFileName)) {
    return canERR_PARAM;
  }

  memset(&info, 0, sizeof(info));
  (void) strncpy((char*)&info.buffer[1], deviceFileName, CANIO_MAX_FILE_NAME);

  info.buffer[0]  = CANIO_DFS_READ;
  info.subcommand = KCANIO_MEMO_SUBCMD_OPEN_FILE;
  info.buflen     = CANIO_MAX_FILE_NAME + 2; // 'mode' and '\0';
  info.timeout    = 30*1000; // 30s
  ret = ioctl(hData->fd, KCANY_IOCTL_MEMO_PUT_DATA, &info, sizeof(info));

  if (ret != 0) {
    DEBUGPRINT((TXT("vCanFileCopyFromDevice: Communication error (%d)\n"), ret));
    return errnoToCanStatus(errno);
  }

  status = memoResultToCanStatus(&info);

  if (status != canOK) {
    DEBUGPRINT((TXT("vCanFileCopyFromDevice: MEMO_SUBCMD_OPEN_FILE error (%d)\n"), (int)info.buffer[0]));
    return status;
  } else {
    memcpy(&bytes, &(info.buffer[2]), sizeof(bytes));
  }

  hFile = fopen(hostFileName, "wb");
  if (!hFile) {
    DEBUGPRINT((TXT("vCanFileCopyFromDevice: Could not create the file '%s'\n"), hostFileName));
    return canERR_HOST_FILE;
  }

  // Copy data from memorator to file
  while (bytes > 0) {
    memset(&info, 0, sizeof(info));
    info.subcommand = KCANIO_MEMO_SUBCMD_READ_FILE;
    info.buflen     = 1000;    // Only using 512 + bytesRead
    info.timeout    = 30*1000; // 30s
    ret = ioctl(hData->fd, KCANY_IOCTL_MEMO_GET_DATA, &info, sizeof(info));
    if (ret != 0) {
      DEBUGPRINT((TXT("vCanFileCopyFromDevice: (2) Communication error (%d)\n"), ret));
      status = errnoToCanStatus(errno);
      break;
    } else {
      memcpy(&bytes, &(info.buffer[0]), sizeof(bytes));
      status = memoResultToCanStatus(&info);
      if (status != canOK) {
        break;
      }
      ret = (int)fwrite(&info.buffer[4], bytes, 1, hFile);
      if (ret < 0) {
        DEBUGPRINT((TXT("vCanFileCopyFromDevice: Write file error\n")));
        status = canERR_HOST_FILE;
        break;
      }
    }
  }

  if (hFile) {
    fclose(hFile);
  }

  if (status != canOK) {
    return status;
  }

  memset(&info, 0, sizeof(info));
  info.subcommand = KCANIO_MEMO_SUBCMD_CLOSE_FILE;
  info.timeout    = 30*1000; // 30s
  info.buflen     = CANIO_MAX_FILE_NAME + 2; // 'mode' and '\0';
  ret = ioctl(hData->fd, KCANY_IOCTL_MEMO_PUT_DATA, &info, sizeof(info));

  if (ret != 0) {
    DEBUGPRINT((TXT("vCanFileCopyFromDevice: (3) Communication error (%d)\n"), ret));
    status = errnoToCanStatus(errno);
  } else {
    memcpy(&bytes, &(info.buffer[0]), sizeof(bytes));
    status = memoResultToCanStatus(&info);
  }

  return status;
}
