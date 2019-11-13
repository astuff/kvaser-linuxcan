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

/* Kvaser Linux Canlib VCan layer functions used in Scripts */

#define _GNU_SOURCE // This is required for recursive mutex support in pthread


#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <pthread.h>


#include "VCanScriptFunctions.h"
#include "VCanFuncUtil.h"
#include "kcanio_script.h"
#include "canstat.h"
#include "vcan_ioctl.h"
#include "debug.h"

#include "md5.h"

#define MD5_HASH_LEN (16)

#if DEBUG
#   define DEBUGPRINT(args) printf args
#else
#   define DEBUGPRINT(args)
#endif


#if defined(PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP)
static pthread_mutex_t scriptEnvvarMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#elif defined(PTHREAD_RECURSIVE_MUTEX_INITIALIZER)
static pthread_mutex_t scriptEnvvarMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
#else
#error Canlib requires GNUC.
#endif

#define MAX_ENVVAR   200
#define MAX_ENVVAR_HND 32

#define VALIDATE_ENVVAR(X) {\
  if (X < 0 || X > MAX_ENVVAR)           \
    return canERR_INVHANDLE;              \
  if (envvarTable[X].openCount == 0) return canERR_PARAM; \
  }



static uint32_t build_hash_from_name (const char *str)
{
  uint32_t hash = 0;
  unsigned char digest[MD5_HASH_LEN];

  if (!str) return hash;

  md5((const unsigned char *)str, (int32_t)strlen(str), digest);
  memcpy(&hash, &digest, sizeof(hash));

  return hash;
}

typedef struct {
  uint32_t      hash;
  int           canlibHnd[MAX_ENVVAR_HND];
  int           type;
  size_t        length;
  void*         dataPtr;
  int           openCount;
} ENVVAR;

ENVVAR envvarTable[MAX_ENVVAR];


/***************************************************************************/
static canStatus scriptControlStatusToCanStatus(unsigned int script_control_status)
{
  canStatus stat;

  switch (script_control_status) {
    case KCANIO_SCRIPT_CTRL_ERR_SUCCESS:
      stat = canOK;
      break;

    case KCANIO_SCRIPT_CTRL_ERR_NO_MORE_PROCESSES:     /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_OUT_OF_CODE_MEM:       /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_OPEN_FILE_NO_MEM:      /* fall through */
      stat = canERR_NOMEM;
      break;

    case KCANIO_SCRIPT_CTRL_ERR_FILE_NOT_FOUND:        /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_OPEN_FILE_ERR:         /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_FILE_READ_ERR:         /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_LOAD_FILE_ERR:         /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_FILE_REWIND_FAIL:      /* fall through */
      stat = canERR_DEVICE_FILE;
      break;

    case KCANIO_SCRIPT_CTRL_ERR_SPI_BUSY:
      stat = canERR_HARDWARE;
      break;

    case KCANIO_SCRIPT_CTRL_ERR_UNKNOWN_COMMAND:       /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_NOT_IMPLEMENTED:
      stat = canERR_NOT_IMPLEMENTED;
      break;

    case KCANIO_SCRIPT_CTRL_ERR_COMPILER_VERSION:
      stat = canERR_SCRIPT_WRONG_VERSION;
      break;

    case KCANIO_SCRIPT_CTRL_ERR_LOAD_FAIL:             /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_SETUP_FAIL:            /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_SETUP_FUN_TABLE_FAIL:  /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_SETUP_PARAMS_FAIL:     /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_PROCESSES_NOT_FOUND:   /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_START_FAILED:          /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_STOP_FAILED:           /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_PROCESS_NOT_STOPPED:   /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_PROCESS_NOT_RUNNING:   /* fall through */
    case KCANIO_SCRIPT_CTRL_ERR_ENVVAR_NOT_FOUND:      /* fall through */
    default:
      stat = canERR_SCRIPT_FAIL;
      break;
  }
  return stat;
}

//======================================================================
// vCanScript_stop
//======================================================================
canStatus vCanScript_stop(HandleData *hData, int slotNo, int mode)
{
  int ret;
  KCAN_IOCTL_SCRIPT_CONTROL_T script_control;

  memset(&script_control, 0, sizeof(script_control));
  script_control.scriptNo = slotNo;
  script_control.command = CMD_SCRIPT_STOP;
  script_control.stopMode = (signed char) mode;
  script_control.channel = hData->channelNr;
  ret = ioctl(hData->fd, KCAN_IOCTL_SCRIPT_CONTROL, &script_control);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }
  return scriptControlStatusToCanStatus(script_control.script_control_status);
}

//======================================================================
// vCanScript_start
//======================================================================
canStatus vCanScript_status(HandleData *hData, int slotNo, unsigned int *status)
{
  int                         ret;
  KCAN_IOCTL_SCRIPT_CONTROL_T script_control;
  canStatus                   stat;

  memset(&script_control, 0, sizeof(script_control));

  script_control.scriptNo = slotNo;
  script_control.command  = CMD_SCRIPT_QUERY_STATUS;
  script_control.channel  = hData->channelNr;

  ret = ioctl(hData->fd, KCAN_IOCTL_SCRIPT_CONTROL, &script_control);

  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  stat = scriptControlStatusToCanStatus(script_control.script_control_status);

  if (stat == canOK) {
    *status = script_control.scriptStatus;
  }

  return stat;
}

//======================================================================
// vCanScript_start
//======================================================================
canStatus vCanScript_start(HandleData *hData, int slotNo)
{
  int ret;
  KCAN_IOCTL_SCRIPT_CONTROL_T script_control;

  memset(&script_control, 0, sizeof(script_control));
  script_control.scriptNo = slotNo;
  script_control.command = CMD_SCRIPT_START;
  script_control.channel = hData->channelNr;
  ret = ioctl(hData->fd, KCAN_IOCTL_SCRIPT_CONTROL, &script_control);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }
  return scriptControlStatusToCanStatus(script_control.script_control_status);
}

//======================================================================
// vCanScript_load_file
// Load a compiled script file directly into the script engine on the device.
//======================================================================
canStatus vCanScript_load_file(HandleData *hData, int slotNo,
                               char *hostFileName)
{
  int ret;
  canStatus status;
  size_t bytes;
  FILE *hFile;
  KCAN_IOCTL_SCRIPT_CONTROL_T script_control;
  const size_t current_block_size = sizeof(script_control.data) - 1;

  hFile = fopen(hostFileName, "rb");
  if (!hFile) {
    DEBUGPRINT((TXT("Could not open script file '%s'\n"), hostFileName));
    return canERR_HOST_FILE;
  }

  // Start transfer of data; setup slot for script
  memset(&script_control, 0, sizeof(script_control));
  script_control.scriptNo = slotNo;
  script_control.channel = hData->channelNr;
  script_control.command = CMD_SCRIPT_LOAD_REMOTE_START;
  ret = ioctl(hData->fd, KCAN_IOCTL_SCRIPT_CONTROL, &script_control);
  if (ret != 0) {
    fclose(hFile);
    return errnoToCanStatus(errno);
  }
  status = scriptControlStatusToCanStatus(script_control.script_control_status);
  if (status != canOK) {
    fclose(hFile);
    return status;
  }

  script_control.command = CMD_SCRIPT_LOAD_REMOTE_DATA;
  do {
    bytes = fread(script_control.script.data, 1, current_block_size, hFile);
    script_control.script.length = bytes;
    ret = ioctl(hData->fd, KCAN_IOCTL_SCRIPT_CONTROL, &script_control);
    if (ret != 0) {
      fclose(hFile);
      return errnoToCanStatus(errno);
    }
    status = scriptControlStatusToCanStatus(script_control.script_control_status);
    if (status != canOK) {
      fclose(hFile);
      return status;
    }
  } while (bytes == current_block_size);
  fclose(hFile);

  // Finish
  script_control.command = CMD_SCRIPT_LOAD_REMOTE_FINISH;
  script_control.script.length = 0;
  ret = ioctl(hData->fd, KCAN_IOCTL_SCRIPT_CONTROL, &script_control);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  status = scriptControlStatusToCanStatus(script_control.script_control_status);
  if (status != canOK) {
    return status;
  }
  return canOK;
}

//======================================================================
// vCanScript_load_file
// Load a compiled script from sd-card
//======================================================================
canStatus vCanScript_load_file_on_device(HandleData *hData, int slotNo, char *localFile)
{
  int                         ret;
  KCAN_IOCTL_SCRIPT_CONTROL_T script_control;
  size_t                      str_length;

  str_length = strnlen(localFile, sizeof(script_control.data));
  if (str_length == sizeof(script_control.data)) {
    return canERR_PARAM;
  }

  memset(&script_control, 0, sizeof(script_control));

  memcpy(script_control.data, localFile, str_length + 1);

  script_control.scriptNo = slotNo;
  script_control.command  = CMD_SCRIPT_LOAD;
  script_control.channel  = hData->channelNr;

  ret = ioctl(hData->fd, KCAN_IOCTL_SCRIPT_CONTROL, &script_control);

  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  return scriptControlStatusToCanStatus(script_control.script_control_status);
}

//======================================================================
// vCanScript_unload
//======================================================================
canStatus vCanScript_unload(HandleData *hData, int slotNo)
{
  int ret;
  KCAN_IOCTL_SCRIPT_CONTROL_T script_control;

  memset(&script_control, 0, sizeof(script_control));
  script_control.scriptNo = slotNo;
  script_control.command = CMD_SCRIPT_UNLOAD;
  script_control.channel = hData->channelNr;
  ret = ioctl(hData->fd, KCAN_IOCTL_SCRIPT_CONTROL, &script_control);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }
  return scriptControlStatusToCanStatus(script_control.script_control_status);
}

//======================================================================
// vCanScript_send_event
//======================================================================
canStatus vCanScript_send_event(HandleData *hData,
                                int slotNo,
                                int eventType,
                                int eventNo,
                                unsigned int data)
{
  int ret;
  KCAN_IOCTL_SCRIPT_CONTROL_T script_control;

  if (eventType != kvEVENT_TYPE_KEY) {
    return canERR_NOT_SUPPORTED;
  }

  memset(&script_control, 0, sizeof(script_control));

  script_control.scriptNo = slotNo;
  script_control.command = CMD_SCRIPT_EVENT;
  script_control.channel = hData->channelNr;

  script_control.event.type = eventType;
  script_control.event.number = eventNo;
  script_control.event.data = data;

  ret = ioctl(hData->fd, KCAN_IOCTL_SCRIPT_CONTROL, &script_control);
  if (ret != 0) {
    return errnoToCanStatus(errno);
  }
  return scriptControlStatusToCanStatus(script_control.script_control_status);
}

//======================================================================
// vCanScript_envvar_init
//======================================================================
void vCanScript_envvar_init(void)
{
  int i, j;

  pthread_mutex_lock(&scriptEnvvarMutex);

  memset(envvarTable, 0, sizeof(envvarTable));

  for (i = 0; i < MAX_ENVVAR; i++) {
    for (j = 0; j < MAX_ENVVAR_HND; j++) {
      envvarTable[i].canlibHnd[j] = -1;
    }
  }

  pthread_mutex_unlock(&scriptEnvvarMutex);
}

//======================================================================
// vCanScript_envvar_open
//======================================================================
kvEnvHandle vCanScript_envvar_open(HandleData *hData,
                                   char* envvarName,
                                   int *envvarType,
                                   int *envvarSize) // returns scriptHandle)
{
  int i;
  int stat = canOK;
  uint32_t hash;
  int free_entry = 0;
  int found = 0;
  envvar_payload ei;
  
  hash = build_hash_from_name(envvarName);

  pthread_mutex_lock(&scriptEnvvarMutex);
  
  // check if already open
  for (i = 0; i < MAX_ENVVAR; i++) {
    if (envvarTable[i].openCount == 0) {
      free_entry = i;
      found      = 1;
      break;
    }
    else if (envvarTable[i].hash == hash) {
      int j;
      // the envvar already exist, but we dont consider this to be an error.
      // consider different approaches

      // save calib-hnd in a list of handles.
      for (j = 0; j < MAX_ENVVAR_HND; j++) {
        if (envvarTable[i].canlibHnd[j] == -1 ) {
          break;
        }
      }

      if (j >= MAX_ENVVAR_HND) {
        pthread_mutex_unlock(&scriptEnvvarMutex);
        return canERR_NOHANDLES;
      }

      envvarTable[i].canlibHnd[j] = hData->handle;

      *envvarType = envvarTable[i].type;
      *envvarSize = (int)envvarTable[i].length;
      envvarTable[i].openCount++;

      pthread_mutex_unlock(&scriptEnvvarMutex);
      // for now return already open index. qqq think this over once more
      // for example could it be wierd because the wrong canlib-hnd could be associated to the
      // envHandle

      return (((int64_t)i) << 32) | hData->handle;
    }
  }

  if (found == 0) {
    // list is full...
    pthread_mutex_unlock(&scriptEnvvarMutex);
    return canERR_NOHANDLES;
  }

  // ask if envvar exist, do that by reading the envvar
  ei.envvar_info.hash = hash;
  {
    int                          ret;
    KCAN_IOCTL_ENVVAR_GET_INFO_T my_arg;
    my_arg.hash       = hash;
    my_arg.subcommand = CMD_ENVVAR_GET_INFO;
    my_arg.channel    = hData->channelNr;
    my_arg.payloadLen = sizeof(ei);
    memcpy(&my_arg.payload, (char *)&ei.envvar_info, sizeof(ei.envvar_info));

    ret = ioctl(hData->fd, KCAN_IOCTL_SCRIPT_ENVVAR_CONTROL, &my_arg);
    
    if (ret != 0) {
      return errnoToCanStatus(errno);
    }

    memcpy((char *)&ei.envvar_info, &my_arg.payload, sizeof(ei.envvar_info));
  }

  if (ei.envvar_info.type == 0) {
    stat = canERR_PARAM;
  }

  if (stat == canOK) {
    envvarTable[free_entry].hash         = hash;
    envvarTable[free_entry].canlibHnd[0] = hData->handle;
    envvarTable[free_entry].type         = ei.envvar_info.type;
    envvarTable[free_entry].length       = ei.envvar_info.length;
    envvarTable[free_entry].openCount    = 1;

    *envvarType = ei.envvar_info.type;
    *envvarSize = ei.envvar_info.length;
  }

  pthread_mutex_unlock(&scriptEnvvarMutex);

  if (stat == canOK) {
    return (((int64_t)free_entry) << 32) | (int64_t)hData->handle;
  } else {
    return (int64_t)stat;
  }
}

//======================================================================
// vCanScript_envvar_close
//======================================================================
canStatus vCanScript_envvar_close(HandleData *hData, int envvarIdx)
{
  int stat = canOK;
  int j;

  VALIDATE_ENVVAR(envvarIdx);

  pthread_mutex_lock(&scriptEnvvarMutex);

  for (j = 0; j < MAX_ENVVAR_HND; j++) {
    if (envvarTable[envvarIdx].canlibHnd[j] == hData->handle){
      envvarTable[envvarIdx].canlibHnd[j] = -1;
      envvarTable[envvarIdx].openCount--;
      break;
    }
  }
  
  if (j == MAX_ENVVAR_HND){
    stat = canERR_INVHANDLE;
  }
  
  pthread_mutex_unlock(&scriptEnvvarMutex);

  return stat;
}


//======================================================================
// vCanScript_envvar_set_int
//======================================================================
canStatus vCanScript_envvar_set_int(HandleData *hData, int envvarIdx, int val)
{
  int                            ret;
  KCAN_IOCTL_SCRIPT_SET_ENVVAR_T my_arg;
  
  VALIDATE_ENVVAR(envvarIdx);
   
  my_arg.hash = envvarTable[envvarIdx].hash;
  memcpy(&my_arg.data, &val, sizeof(val));
  my_arg.dataLen = sizeof(val);
  
  ret = ioctl(hData->fd, KCAN_IOCTL_SCRIPT_SET_ENVVAR, &my_arg);
  
  if (ret) {
    return ret;
  }

  return ret;
}


//======================================================================
// vCanScript_envvar_get_int
//======================================================================
canStatus vCanScript_envvar_get_int(HandleData *hData, int envvarIdx, int *val)
{
  int                            ret;
  KCAN_IOCTL_SCRIPT_GET_ENVVAR_T my_arg;
  
  VALIDATE_ENVVAR(envvarIdx);

  my_arg.hash    = envvarTable[envvarIdx].hash;
  my_arg.dataLen = sizeof(val);
  my_arg.offset  = 0;

  ret = ioctl(hData->fd, KCAN_IOCTL_SCRIPT_GET_ENVVAR, &my_arg);
  
  if (ret) {
    return ret;
  }
  
  memcpy(val, &my_arg.data, sizeof(int));  
  return ret;
}


//======================================================================
// vCanScript_envvar_set_float
//======================================================================
canStatus vCanScript_envvar_set_float(HandleData *hData, int envvarIdx, float val)
{
  int                            ret;
  KCAN_IOCTL_SCRIPT_SET_ENVVAR_T my_arg;
  
  VALIDATE_ENVVAR(envvarIdx);
   
  my_arg.hash = envvarTable[envvarIdx].hash;
  memcpy(&my_arg.data, &val, sizeof(val));
  my_arg.dataLen = sizeof(val);
  
  ret = ioctl(hData->fd, KCAN_IOCTL_SCRIPT_SET_ENVVAR, &my_arg);
  
  if (ret) {
    return ret;
  }

  return ret;
}


//======================================================================
// vCanScript_envvar_get_float
//======================================================================
canStatus vCanScript_envvar_get_float(HandleData *hData, int envvarIdx, float *val)
{
  int                            ret;
  KCAN_IOCTL_SCRIPT_GET_ENVVAR_T my_arg;
  
  VALIDATE_ENVVAR(envvarIdx);

  my_arg.hash    = envvarTable[envvarIdx].hash;
  my_arg.dataLen = sizeof(val);
  my_arg.offset  = 0;

  ret = ioctl(hData->fd, KCAN_IOCTL_SCRIPT_GET_ENVVAR, &my_arg);
  
  if (ret) {
    return ret;
  }
  
  memcpy(val, &my_arg.data, sizeof(float));  
  return ret;
}


//======================================================================
// vCanScript_envvar_set_data
//======================================================================
canStatus vCanScript_envvar_set_data(kvEnvHandle eHnd,
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

//======================================================================
// vCanScript_envvar_get_data
//======================================================================
canStatus vCanScript_envvar_get_data(kvEnvHandle eHnd,
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

//======================================================================
// vCanScript_request_text
//======================================================================
canStatus vCanScript_request_text(HandleData *hData,
                                  unsigned int slot,
                                  unsigned int request)
{
  KCAN_IOCTL_DEVICE_MESSAGES_SUBSCRIPTION_T tmp;
  int                            ret;
  unsigned int                   aslot = 0;
  unsigned int                   flag  = 0;

  if (request == kvSCRIPT_REQUEST_TEXT_UNSUBSCRIBE) {
    flag |= KCAN_SCRIPT_REQUEST_TEXT_UNSUBSCRIBE;
  } else if (request == kvSCRIPT_REQUEST_TEXT_SUBSCRIBE) {
    flag |= KCAN_SCRIPT_REQUEST_TEXT_SUBSCRIBE;
  } else {
    return canERR_PARAM;
  }

  // secret flags for internal use
  aslot = slot & 0xff; // mask out only one byte for script-slot
  if ((slot & 0xf0000000) == 0x30000000) aslot |= KCAN_SCRIPT_ERROR_SLOT;
  if ((slot & 0xf0000000) == 0x20000000) aslot |= KCAN_SCRIPT_INFO_SLOT;

  tmp.slot = aslot;
  tmp.requestFlags = flag;

  ret = ioctl(hData->fd, KCAN_IOCTL_DEVICE_MESSAGES_SUBSCRIPTION, &tmp);

  if (ret != 0) {
    return errnoToCanStatus(errno);
  }

  return canOK;
}


#define MAX_PRINTF_LIST_SIZE  2000
//----------------------------------------------------------------
// add_received_text_to_end_of_list:
//
// local help function to vCanScript_get_text below
//----------------------------------------------------------------
static void add_received_text_to_end_of_list(HandleData *hData, text_element_t *text)
{
  // remove "text" from print_text.text_receiving
  hData->print_text.text_receiving = NULL;

  text->index = 0;
  text->next = NULL;

  if (hData->print_text.received_text_list == NULL) {
    hData->print_text.received_text_list = text;
    hData->print_text.last_received_text = text;
    hData->print_text.number_of_received_texts = 1;
  } else {
    // append text_element last in list

    hData->print_text.last_received_text->next = text;
    hData->print_text.last_received_text = text;

    if (hData->print_text.number_of_received_texts >= MAX_PRINTF_LIST_SIZE) {
      // remove the first or second text_element
      text_element_t *tmp_text = hData->print_text.received_text_list;
      if (tmp_text->index != 0) {
        // user has start reading the first message, remove the second instead
        tmp_text = tmp_text->next;
        hData->print_text.received_text_list->next = tmp_text->next;
      } else {
        hData->print_text.received_text_list = tmp_text->next;
      }
      tmp_text->next->flags |= VCAN_MSG_FLAG_OVERRUN;
      free(tmp_text->payload);
      free(tmp_text);
    } else {
      hData->print_text.number_of_received_texts++;
    }
  }
}


//----------------------------------------------------------------
// filter_flags:
//
// clear the print_text data
//----------------------------------------------------------------
static unsigned short filter_flags(unsigned short flags)
{
  unsigned short tmp_flags = 0;

  //  tmp_flags |= flags & (DM_FLAG_PRINTF | DM_FLAG_DEBUG | DM_FLAG_ERROR);  // type of text
  if (flags & VCAN_MSG_FLAG_OVERRUN)  tmp_flags |= canSTAT_SW_OVERRUN;
  if (flags & canSTAT_RX_PENDING) tmp_flags |= canSTAT_RX_PENDING;

  return tmp_flags;
}


//----------------------------------------------------------------
// clear_print_text_data:
//
// clear the print_text data
//----------------------------------------------------------------
void clear_print_text_data(HandleData *hData)
{
  text_element_t *text;

  text = hData->print_text.text_receiving;
  hData->print_text.text_receiving = NULL;
  if (text != NULL) {
    free(text->payload);
    free(text);
  }
  text = hData->print_text.received_text_list;
  hData->print_text.received_text_list = NULL;
  hData->print_text.last_received_text = NULL;
  hData->print_text.number_of_received_texts = 0;
  while (text != NULL)
  {
    text_element_t *old_text = text;
    text = text->next;

    free(old_text->payload);
    free(old_text);
  }
}

#ifndef min
#define min(a, b)  (((a) < (b)) ? (a) : (b))
#endif

//======================================================================
// vCanScript_get_text
//======================================================================
canStatus vCanScript_get_text(HandleData *hData,
                              int  *slot,
                              unsigned long *time,
                              unsigned int  *flags,
                              char *buf,
                              size_t bufsize)
{
  text_element_t               *text;
  KCAN_IOCTL_SCRIPT_GET_TEXT_T text_event;
  int                          ret;
  VCAN_EVENT                   msg;

  if (!slot | !time | !flags | !buf | (bufsize <= 1)) {
    return canERR_PARAM;
  }

  while (1) {
    // create the structure to receive the text from kernel space, if it doesn't exist
    if (hData->print_text.text_receiving == NULL) {
      text = malloc(sizeof(*text));
      if (text == NULL) {
        return canERR_NOMEM;
      }
      memset(text, 0, sizeof(*text));
      hData->print_text.text_receiving = text;

      text->state = 0;
      text->index = 0;
    } else {
      text = hData->print_text.text_receiving;
    }

    // initialize ioctl data
    memset(&msg, 0, sizeof(msg));
    memset(&text_event, 0, sizeof(text_event));
    text_event.msg = &msg;

    // fetch next message from kernel space
    ret = ioctl(hData->fd, KCAN_IOCTL_SCRIPT_GET_TEXT, &text_event);

    if (ret != 0) {
      int error_number = errno;
      int error_code;

      if (error_number == EAGAIN) {
        break;  // No more messages on kernel side
      }

      text->state = 0;
      text->index = 0;
      error_code = errnoToCanStatus(error_number);
      return error_code;
    }

    // Receive text message
    if (msg.tag == V_PRINTF_MSG) {
      // first message is a Header, followed by a number of Data
      if (msg.tagData.printfMsg.type == VCAN_EVT_PRINTF_HEADER) {
        if (text->state == 1) {
          // New header before the previous message completely loaded
          // Store the incomplete message first

          text->total_payload = text->index;   // we use what we have got so far
          text->index = 0;
          text->next = NULL;
          text->flags |= VCAN_MSG_FLAG_OVERRUN;
          add_received_text_to_end_of_list(hData, text);

          // allocate new text_element
          text = malloc(sizeof(*text));
          if (text == NULL) {
            return canERR_NOMEM;
          }
          memset(text, 0, sizeof(*text));

          hData->print_text.text_receiving = text;
          text->state = 0;
        }

        if (text->state == 0) {
          // allocate memory
          text->payload = malloc(msg.tagData.printfMsg.printf_header.datalen);
          if (text->payload == NULL) {
            return canERR_NOMEM;
          }
          memset(text->payload, 0, msg.tagData.printfMsg.printf_header.datalen);

          text->timeStamp = msg.timeStamp;
          text->flags = msg.tagData.printfMsg.printf_header.flags;
          text->slot = msg.tagData.printfMsg.printf_header.slot;
          text->total_payload = msg.tagData.printfMsg.printf_header.datalen;
          text->index = 0;
          text->state = 1;
        }
      } else if (msg.tagData.printfMsg.type == VCAN_EVT_PRINTF_DATA) {
        if (text->state == 1) {
          unsigned short  number_of_bytes_to_copy = min((text->total_payload - text->index), (unsigned short) sizeof(msg.tagData.printfMsg.printf_data.payload));

          memcpy(&text->payload[text->index], msg.tagData.printfMsg.printf_data.payload, number_of_bytes_to_copy);
          text->index += number_of_bytes_to_copy;
          if (text->index == text->total_payload) {
            text->index = 0;
            text->state = 2;
            add_received_text_to_end_of_list(hData, text);
          }
        }
      }
    } else {
      break;
    }
  }

  if (hData->print_text.number_of_received_texts > 0) {
    // return received text

    text_element_t *text                    = hData->print_text.received_text_list;
    unsigned short  total_payload           = text->total_payload - 1; //remove trailing '0"
    unsigned short  number_of_bytes_to_copy = min((total_payload - text->index), (unsigned short) (bufsize - 1));

    *slot = text->slot;
    *time = text->timeStamp;
    *flags = filter_flags(text->flags);

    memcpy(buf, &text->payload[text->index], number_of_bytes_to_copy);
    buf[number_of_bytes_to_copy] = '\0';  // always a null-terminated string
    text->index += number_of_bytes_to_copy;

    // All data read, remove text element
    if (text->index == total_payload) {
      text_element_t *old_node = hData->print_text.received_text_list;

      hData->print_text.number_of_received_texts--;
      if (hData->print_text.number_of_received_texts > 0) {
        *flags |= canSTAT_RX_PENDING;
      }
      hData->print_text.received_text_list = hData->print_text.received_text_list->next;
      if (hData->print_text.received_text_list == NULL) {
        hData->print_text.last_received_text = NULL;
      }
      free(old_node->payload);
      free(old_node);
    }
    else {
      *flags |= canSTAT_RX_PENDING;
    }
    return canOK;
  }
  return canERR_NOMSG;
}
