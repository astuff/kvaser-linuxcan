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

// Linux Mhydra driver

#ifndef _MHYDRA_HW_IF_H_
#define _MHYDRA_HW_IF_H_


#include "hydra_host_cmds.h"
#include "objbuf.h"

//-----------------------------------------------------------------------------------------------
// assert that memo and script defines match ------------------
#include "compilerassert.h"
#include "kcanio_script.h"
#include "kcanio_memorator.h"
CompilerAssert(KCANIO_MEMO_STATUS_SUCCESS         == MEMO_STATUS_SUCCESS);
CompilerAssert(KCANIO_MEMO_STATUS_MORE_DATA       == MEMO_STATUS_MORE_DATA);
CompilerAssert(KCANIO_MEMO_STATUS_UNKNOWN_COMMAND == MEMO_STATUS_UNKNOWN_COMMAND);
CompilerAssert(KCANIO_MEMO_STATUS_FAILED          == MEMO_STATUS_FAILED);
CompilerAssert(KCANIO_MEMO_STATUS_EOF             == MEMO_STATUS_EOF);

CompilerAssert(KCANIO_MEMO_SUBCMD_GET_FS_INFO                == MEMO_SUBCMD_GET_FS_INFO             );
CompilerAssert(KCANIO_MEMO_SUBCMD_GET_DISK_INFO_A            == MEMO_SUBCMD_GET_DISK_INFO_A         );
CompilerAssert(KCANIO_MEMO_SUBCMD_GET_DISK_INFO_B            == MEMO_SUBCMD_GET_DISK_INFO_B         );
CompilerAssert(KCANIO_MEMO_SUBCMD_READ_PHYSICAL_SECTOR       == MEMO_SUBCMD_READ_PHYSICAL_SECTOR    );
CompilerAssert(KCANIO_MEMO_SUBCMD_WRITE_PHYSICAL_SECTOR      == MEMO_SUBCMD_WRITE_PHYSICAL_SECTOR   );
CompilerAssert(KCANIO_MEMO_SUBCMD_ERASE_PHYSICAL_SECTOR      == MEMO_SUBCMD_ERASE_PHYSICAL_SECTOR   );
CompilerAssert(KCANIO_MEMO_SUBCMD_READ_LOGICAL_SECTOR        == MEMO_SUBCMD_READ_LOGICAL_SECTOR     );
CompilerAssert(KCANIO_MEMO_SUBCMD_WRITE_LOGICAL_SECTOR       == MEMO_SUBCMD_WRITE_LOGICAL_SECTOR    );
CompilerAssert(KCANIO_MEMO_SUBCMD_ERASE_LOGICAL_SECTOR       == MEMO_SUBCMD_ERASE_LOGICAL_SECTOR    );
CompilerAssert(KCANIO_MEMO_SUBCMD_FORMAT_DISK                == MEMO_SUBCMD_FORMAT_DISK             );
CompilerAssert(KCANIO_MEMO_SUBCMD_INIT_DISK                  == MEMO_SUBCMD_INIT_DISK               );
CompilerAssert(KCANIO_MEMO_SUBCMD_CLEAR_DATA                 == MEMO_SUBCMD_CLEAR_DATA              );
CompilerAssert(KCANIO_MEMO_SUBCMD_GET_MISC_INFO              == MEMO_SUBCMD_GET_MISC_INFO           );
CompilerAssert(KCANIO_MEMO_SUBCMD_GET_RTC_INFO               == MEMO_SUBCMD_GET_RTC_INFO            );
CompilerAssert(KCANIO_MEMO_SUBCMD_PUT_RTC_INFO               == MEMO_SUBCMD_PUT_RTC_INFO            );
CompilerAssert(KCANIO_MEMO_SUBCMD_GET_FS_INFO_B              == MEMO_SUBCMD_GET_FS_INFO_B           );
CompilerAssert(KCANIO_MEMO_SUBCMD_FASTREAD_PHYSICAL_SECTOR   == MEMO_SUBCMD_FASTREAD_PHYSICAL_SECTOR);
CompilerAssert(KCANIO_MEMO_SUBCMD_FASTREAD_LOGICAL_SECTOR    == MEMO_SUBCMD_FASTREAD_LOGICAL_SECTOR );
CompilerAssert(KCANIO_MEMO_SUBCMD_OPEN_FILE                  == MEMO_SUBCMD_OPEN_FILE               );
CompilerAssert(KCANIO_MEMO_SUBCMD_READ_FILE                  == MEMO_SUBCMD_READ_FILE               );
CompilerAssert(KCANIO_MEMO_SUBCMD_CLOSE_FILE                 == MEMO_SUBCMD_CLOSE_FILE              );
CompilerAssert(KCANIO_MEMO_SUBCMD_WRITE_FILE                 == MEMO_SUBCMD_WRITE_FILE              );
CompilerAssert(KCANIO_MEMO_SUBCMD_DELETE_FILE                == MEMO_SUBCMD_DELETE_FILE             );

CompilerAssert(KCANIO_SCRIPT_ENVVAR_SUBCMD_SET_START      == SCRIPT_ENVVAR_SUBCMD_SET_START      );
CompilerAssert(KCANIO_SCRIPT_ENVVAR_SUBCMD_GET_START      == SCRIPT_ENVVAR_SUBCMD_GET_START      );
CompilerAssert(KCANIO_SCRIPT_ENVVAR_RESP_OK               == SCRIPT_ENVVAR_RESP_OK               );
CompilerAssert(KCANIO_SCRIPT_ENVVAR_RESP_UNKNOWN_VAR      == SCRIPT_ENVVAR_RESP_UNKNOWN_VAR      );
CompilerAssert(KCANIO_SCRIPT_ENVVAR_RESP_WRONG_VAR_LEN    == SCRIPT_ENVVAR_RESP_WRONG_VAR_LEN    );
CompilerAssert(KCANIO_SCRIPT_ENVVAR_RESP_OUT_OF_MEMORY    == SCRIPT_ENVVAR_RESP_OUT_OF_MEMORY    );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_SUCCESS             == SCRIPT_CTRL_ERR_SUCCESS             );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_NO_MORE_PROCESSES   == SCRIPT_CTRL_ERR_NO_MORE_PROCESSES   );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_FILE_NOT_FOUND      == SCRIPT_CTRL_ERR_FILE_NOT_FOUND      );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_OPEN_FILE_ERR       == SCRIPT_CTRL_ERR_OPEN_FILE_ERR       );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_OPEN_FILE_NO_MEM    == SCRIPT_CTRL_ERR_OPEN_FILE_NO_MEM    );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_FILE_READ_ERR       == SCRIPT_CTRL_ERR_FILE_READ_ERR       );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_LOAD_FILE_ERR       == SCRIPT_CTRL_ERR_LOAD_FILE_ERR       );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_OUT_OF_CODE_MEM     == SCRIPT_CTRL_ERR_OUT_OF_CODE_MEM     );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_FILE_REWIND_FAIL    == SCRIPT_CTRL_ERR_FILE_REWIND_FAIL    );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_LOAD_FAIL           == SCRIPT_CTRL_ERR_LOAD_FAIL           );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_SETUP_FAIL          == SCRIPT_CTRL_ERR_SETUP_FAIL          );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_SETUP_FUN_TABLE_FAIL== SCRIPT_CTRL_ERR_SETUP_FUN_TABLE_FAIL);
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_SETUP_PARAMS_FAIL   == SCRIPT_CTRL_ERR_SETUP_PARAMS_FAIL   );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_PROCESSES_NOT_FOUND == SCRIPT_CTRL_ERR_PROCESSES_NOT_FOUND );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_START_FAILED        == SCRIPT_CTRL_ERR_START_FAILED        );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_STOP_FAILED         == SCRIPT_CTRL_ERR_STOP_FAILED         );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_SPI_BUSY            == SCRIPT_CTRL_ERR_SPI_BUSY            );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_PROCESS_NOT_STOPPED == SCRIPT_CTRL_ERR_PROCESS_NOT_STOPPED );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_PROCESS_NOT_RUNNING == SCRIPT_CTRL_ERR_PROCESS_NOT_RUNNING );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_ENVVAR_NOT_FOUND    == SCRIPT_CTRL_ERR_ENVVAR_NOT_FOUND    );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_UNKNOWN_COMMAND     == SCRIPT_CTRL_ERR_UNKNOWN_COMMAND     );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_PROCESS_NOT_LOADED  == SCRIPT_CTRL_ERR_PROCESS_NOT_LOADED  );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_COMPILER_VERSION    == SCRIPT_CTRL_ERR_COMPILER_VERSION    );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_INVALID_PARAMETER   == SCRIPT_CTRL_ERR_INVALID_PARAMETER   );
CompilerAssert(KCANIO_SCRIPT_CTRL_ERR_NOT_IMPLEMENTED     == SCRIPT_CTRL_ERR_NOT_IMPLEMENTED     );
//-----------------------------------------------------------------------------------------------

/*****************************************************************************/
/* defines */
/*****************************************************************************/

#define DEVICE_NAME_STRING            "mhydra"
// Maximum number of channels for a device of this type.
#define HYDRA_MAX_CARD_CHANNELS       5U
#define HYDRA_MAX_DRIVER_CHANNELS     128U

#define KV_MHYDRA_MAIN_RCV_BUF_SIZE   16U
#define KV_MHYDRA_TX_CMD_BUF_SIZE     16U
#define MHYDRA_CMD_RESP_WAIT_TIME     5000U
#define HYDRA_MAX_OUTSTANDING_TX      200U


// Bits in the CxSTRH register in the M32C.
#define M32C_BUS_RESET                0x01U    // Chip is in Reset state
#define M32C_BUS_ERROR                0x10U    // Chip has seen a bus error
#define M32C_BUS_PASSIVE              0x20U    // Chip is error passive
#define M32C_BUS_OFF                  0x40U    // Chip is bus off

#define MAX_HE_COUNT                  64U

#if DEBUG
#   define MHYDRA_Q_CMD_WAIT_TIME     800
#else
#   define MHYDRA_Q_CMD_WAIT_TIME     200
#endif

/* Channel specific data */
typedef struct MhydraChanData {
  /* These are the number of outgoing packets residing in the device */
  uint32_t       outstanding_tx;
  spinlock_t     outTxLock;
  uint64_t       timestamp_correction_value;
  OBJECT_BUFFER *objbufs;

  uint32_t       current_tx_message_index;
  CAN_MSG        current_tx_message[HYDRA_MAX_OUTSTANDING_TX];

} MhydraChanData;

#define NUM_IN_PIPES 3

typedef struct MhydraUsbPipeInfo {
  uint8_t   *buffer;            // the buffer to receive data
  size_t    size;               // the size of the receive buffer
  uint8_t   endpointAddr;       // the address of the bulk in endpoint
  uint32_t  maxPacketSize;
} MhydraUsbPipeInfo;

typedef struct MhydraMemoInfo {
  struct workqueue_struct  *bulkQ;
  struct work_struct        bulkWork;
  struct completion         completion;
  int                       status;
  uint32_t                  n_bytes_read;
  unsigned char            *buffer;
} MhydraMemoInfo;

typedef struct MhydraWaitNode {
  unsigned char      error_event;    //used to detect that we have got an "CMD_ERROR_EVENT"
  unsigned int       data_count;     //number of received bytes, used with multiple CMD_MEMO_GET_DATA responses
  unsigned char     *memo_buffer;
} MhydraWaitNode;

/*  Cards specific data */
typedef struct MhydraCardData {
  // Map channel (0,1,2,...) to HE (6-bit number meaningful only to fw)
  uint8_t   channel2he[HYDRA_MAX_CARD_CHANNELS];
  uint8_t   he2channel[MAX_HE_COUNT];
  uint8_t   sysdbg_he;

  uint32_t  max_outstanding_tx;
  int32_t   autoTxBufferCount;
  int32_t   autoTxBufferResolution;

  spinlock_t  replyWaitListLock;
  struct list_head replyWaitList;

  /* Structure to hold all of our device specific stuff */

  struct workqueue_struct  *txTaskQ;
  struct work_struct       txWork;

  hydraHostCmdExt txCmdBuffer[KV_MHYDRA_TX_CMD_BUF_SIZE]; /* Control messages */
  Queue           txCmdQueue;

  struct usb_device       *udev;        // save off the usb device pointer
  struct usb_interface    *interface;   // the interface for this device

  MhydraUsbPipeInfo  bulk_in[NUM_IN_PIPES];
  MhydraMemoInfo     memo;

  uint8_t   bulk_in_endpointAddrDiag;   // the address of the diag bulk in endpoint

  uint8_t   *bulk_out_buffer;           // the buffer to send data
  size_t    bulk_out_size;              // the size of the send buffer

  uint32_t  bulk_out_MaxPacketSize;

  struct urb *write_urb;                // the urb used to send data
  uint8_t    bulk_out_endpointAddr;    // the address of the bulk out endpoint
  struct completion   write_finished;     // wait for the write to finish

  VCanCardData  *vCard;

  // General data (from Windows version)
  // Time stamping timer frequency in MHz
  uint64_t  hires_timer_fq;
  uint64_t  time_offset_valid;

  uint32_t max_bitrate;
  uint8_t  rxCmdBuffer[sizeof(hydraHostCmdExt)];
  uint32_t rxCmdBufferLevel;

  spinlock_t  transIdLock;
  uint32_t    transId;

} MhydraCardData;


//sets default timeout, which is MHYDRA_CMD_RESP_WAIT_TIME
int mhydra_send_and_wait_reply (VCanCardData  *vCard,
                                hydraHostCmd  *cmd,
                                hydraHostCmd  *replyPtr,
                                unsigned char  replyCmdNo,
                                uint16_t       transId,
                                unsigned char  error_event);

//you can decide the timeout for your self.
//if resp_timeout_ms is 0, you will get timeout MHYDRA_CMD_RESP_WAIT_TIME
int mhydra_send_and_wait_reply_timeout (VCanCardData *vCard,
                                        hydraHostCmd *cmd,
                                        hydraHostCmd *replyPtr,
                                        unsigned char replyCmdNo,
                                        uint16_t      transId,
                                        unsigned char error_event,
                                        uint32_t      resp_timeout_ms);

int mhydra_queue_cmd(VCanCardData *vCard,
                     hydraHostCmd *cmd,
                     unsigned int  timeout_ms);


#define TRP_DEST_CANHE 1
uint16_t cmd_create_transId (hydraHostCmd *cmd);
int32_t device_trp (VCanCardData *vCard, uint32_t dest, uint16_t pipe, uint8_t *buffer, uint32_t buflen);

#ifdef MHYDRA_DEBUG
  extern int pc_debug;
#endif /* MHYDRA_DEBUG */

#endif  /* _MHYDRA_HW_IF_H_ */
