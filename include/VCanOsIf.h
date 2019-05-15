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

/*
** Project:
**   linuxcan
** Description:
**   common driver data structures.
*/

#ifndef _VCAN_OS_IF_H_
#define _VCAN_OS_IF_H_


#include <linux/poll.h>
#include <asm/atomic.h>
#include <linux/types.h>
#include <linux/tty.h>
#include <linux/completion.h>

#include "canIfData.h"
#include "kcan_ioctl.h"
#include "vcanevt.h"
#include "objbuf.h"

#include "queue.h"

#include "softsync.h"
#include "ticks.h"


/*****************************************************************************/
/*  Defines                                                                  */
/*****************************************************************************/

#define MAIN_RCV_BUF_SIZE  16
#define FILE_RCV_BUF_SIZE 500
#define TX_CHAN_BUF_SIZE  500

/*****************************************************************************/
/*  From vcanio.h                                                            */
/*****************************************************************************/

#define CAN_EXT_MSG_ID                  0x80000000

#define CAN_BUSSTAT_BUSOFF              0x01
#define CAN_BUSSTAT_ERROR_PASSIVE       0x02
#define CAN_BUSSTAT_ERROR_WARNING       0x04
#define CAN_BUSSTAT_ERROR_ACTIVE        0x08
#define CAN_BUSSTAT_BUSOFF_RECOVERY     0x10
#define CAN_BUSSTAT_IGNORING_ERRORS     0x20

#define CAN_CHIP_TYPE_UNKNOWN           0
#define CAN_CHIP_TYPE_VIRTUAL           1
#define CAN_CHIP_TYPE_SJA1000           2
#define CAN_CHIP_TYPE_527               3
#define CAN_CHIP_TYPE_C200              4


/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/

#define put_user_ret(x,ptr,ret)              \
  { if (put_user(x,ptr)) return ret; }
#define get_user_int_ret(x,ptr,ret)          \
  { if (get_user(x,ptr)) return ret; }
#define get_user_long_ret(x,ptr,ret)         \
  { if (get_user(x,ptr)) return ret; }
#define copy_to_user_ret(to,from,n,retval)   \
  { if (copy_to_user(to,from,n)) return retval; }
#define copy_from_user_ret(to,from,n,retval) \
  { if (copy_from_user(to,from,n)) return retval; }

#define ArgPtrIn(s)
#define ArgPtrOut(s)
#define ArgIntIn     do {                                   \
                       int argh;                            \
                       get_user_int_ret(argh, (int *)arg,   \
                                        -EFAULT);           \
                       arg = argh;                          \
                     } while (0)



#define VCAN_STAT_OK                 0
#define VCAN_STAT_FAIL              -1    // -EIO
#define VCAN_STAT_TIMEOUT           -2    // -EAGAIN (TIMEDOUT)?
#define VCAN_STAT_NO_DEVICE         -3    // -ENODEV
#define VCAN_STAT_NO_RESOURCES      -4    // -EAGAIN
#define VCAN_STAT_NO_MEMORY         -5    // -ENOMEM
#define VCAN_STAT_SIGNALED          -6    // -ERESTARTSYS
#define VCAN_STAT_BAD_PARAMETER     -7    // -EINVAL
#define VCAN_STAT_NOT_IMPLEMENTED   -8    // -EAGAIN

#define OPEN_AS_CAN           0
#define OPEN_AS_CANFD_ISO     1
#define OPEN_AS_CANFD_NONISO  2

/*****************************************************************************/
/*  Data structures                                                          */
/*****************************************************************************/

typedef union {
    uint32_t L;
    struct { unsigned short w0, w1; } W;
    struct { unsigned char b0, b1, b2, b3; } B;
} WL;

typedef struct CanChipState {
    int state;  /* buson / busoff / error passive / warning */
    int txerr;  /* tx error counter */
    int rxerr;  /* rx error counter */
} CanChipState;


/* Channel specific data */
typedef struct VCanChanData
{
    int                      minorNr;
    unsigned char            channel;
    unsigned char            chipType;
    unsigned char            ean[8];
    uint32_t                 serialHigh;
    uint32_t                 serialLow;

    /* Status */
    unsigned char            isOnBus;
    unsigned char            transType;   // TRANSCEIVER_TYPE_xxx
    unsigned char            lineMode;    // TRANSCEIVER_LINEMODE_xxx
    unsigned char            resNet;      // TRANSCEIVER_RESNET_xxx
    atomic_t                 transId;
    atomic_t                 chanId;
    unsigned int             overrun;
    CanChipState             chipState;
    unsigned int             errorCount;
    unsigned long            errorTime;
    unsigned char            rxErrorCounter;
    unsigned char            txErrorCounter;
    unsigned char            openMode;
    unsigned char            driverMode;
    unsigned char            analyzerAttached;
    int                      linMode;  // _STATUS_LIN_MASTER or _STATUS_LIN_SLAVE

    /* Transmit buffer */
    CAN_MSG                  txChanBuffer[TX_CHAN_BUF_SIZE];
    Queue                    txChanQueue;

    /* Processes waiting for all messages to be sent */
    wait_queue_head_t        flushQ;

    atomic_t                 fileOpenCount;
    unsigned int             busOnCount;
    struct VCanOpenFileNode *openFileList;

    struct completion       busOnCountCompletion;

    spinlock_t              openLock;
    void                   *hwChanData;
    unsigned long           waitEmpty;

    unsigned int            capabilities;
    unsigned int            capabilities_mask;

    VCanBusStatistics       busStats;

    struct VCanCardData    *vCard;
    struct completion       ioctl_completion;
} VCanChanData;


// For VCanCardData->card_flags
#define DEVHND_CARD_FIRMWARE_BETA         0x01  // Firmware is beta
#define DEVHND_CARD_FIRMWARE_RC           0x02  // Firmware is release candidate
#define DEVHND_CARD_AUTO_RESP_OBJBUFS     0x04  // Firmware supports auto-response object buffers
#define DEVHND_CARD_REFUSE_TO_RUN         0x08  // Major problem detected
#define DEVHND_CARD_REFUSE_TO_USE_CAN     0x10  // Major problem detected
#define DEVHND_CARD_AUTO_TX_OBJBUFS       0x20  // Firmware supports periodic transmit object buffers
#define DEVHND_CARD_DELAY_MSGS            0x40  // Firmware supports delay messages
#define DEVHND_CARD_HYDRA_EXT             0x80  // Firmware supports extended Hydra commands
#define DEVHND_CARD_CANFD_CAP             0x100 // Firmware supports CAN-FD.
#define DEVHND_CARD_EXTENDED_CAPABILITIES 0x200 // Firmware supports reading capabilities.

struct VCanHWInterface;
struct VCanCardData;

typedef struct VCanDriverData
{
    int                     noOfDevices;
    struct timeval          startTime;
    char                   *deviceName;
    struct VCanHWInterface *hwIf;
    struct VCanCardData    *canCards;
    spinlock_t              canCardsLock;
    struct cdev             cdev;
} VCanDriverData;

/*  Cards specific data */
typedef struct VCanCardData
{
    uint32_t                hw_type;
    uint32_t                card_flags;
    unsigned int            nrChannels;
    uint32_t                serialNumber;
    unsigned char           ean[8];
    unsigned int            firmwareVersionMajor;
    unsigned int            firmwareVersionMinor;
    unsigned int            firmwareVersionBuild;
    unsigned int            hwRevisionMajor;
    unsigned int            hwRevisionMinor;

    uint32_t                timeHi;
    uint32_t                usPerTick;

    /* Ports and addresses */
    volatile unsigned int    cardPresent;
    VCanChanData           **chanData;
    void                    *hwCardData;
    VCanDriverData          *driverData;

    SOFTSYNC_DATA          *softsync_data;
    int                     enable_softsync;
    int                     softsync_running;
    unsigned int            usb_root_hub_id;

    ticks_class             ticks;
    uint32_t                default_max_bitrate;
    uint32_t                current_max_bitrate;

    struct VCanCardData    *next;
} VCanCardData;

typedef struct
{
    spinlock_t              rcvLock;
    int                     bufHead;
    int                     bufTail;
#if DEBUG
    int                     lastEmpty;
    int                     lastNotEmpty;
#endif
    VCAN_EVENT              fileRcvBuffer[FILE_RCV_BUF_SIZE];
    uint8_t                 valid[FILE_RCV_BUF_SIZE];
    int                     size;
    wait_queue_head_t       rxWaitQ;
} VCanReceiveData;

/* File pointer specific data */
typedef struct VCanOpenFileNode {
    struct completion        ioctl_completion;
    VCanReceiveData          rcv;
    unsigned char            transId;
    struct file             *filp;
    struct VCanChanData     *chanData;
    int                      chanNr;
    unsigned char            modeTx;
    unsigned char            modeTxRq;
    unsigned char            modeNoTxEcho;
    unsigned char            channelOpen;
    unsigned char            channelLocked;
    VCanRequestChipStatus    chip_status;
    long                     writeTimeout;
    VCanMsgFilter            filter;
    struct work_struct       objbufWork;
    struct workqueue_struct *objbufTaskQ;
    OBJECT_BUFFER           *objbuf;
    atomic_t                 objbufActive;
    VCanOverrun              overrun;
    uint8_t                  isBusOn;
    uint8_t                  notify;
    struct VCanOpenFileNode *next;
    uint8_t                  init_access;
    uint64_t                 time_start_10usec;
} VCanOpenFileNode;


/* Dispatch call structure */
typedef struct VCanHWInterface {
    int (*initAllDevices)       (void);
    int (*setBusParams)         (VCanChanData *chd, VCanBusParams *par);
    int (*getBusParams)         (VCanChanData *chd, VCanBusParams *par);
    int (*setOutputMode)        (VCanChanData *chd, int silent);
    int (*setTranceiverMode)    (VCanChanData *chd, int linemode, int resnet);
    int (*busOn)                (VCanChanData *chd);
    int (*busOff)               (VCanChanData *chd);
    int (*reqBusStats)          (VCanChanData *chd);
    int (*txAvailable)          (VCanChanData *chd);
    int (*procRead)             (struct seq_file* m, void* v);
    int (*closeAllDevices)      (void);
    int (*getTime)              (VCanCardData*, uint64_t *time);
    int (*flushSendBuffer)      (VCanChanData*);
    int (*getRxErr)             (VCanChanData*);
    int (*getTxErr)             (VCanChanData*);
    unsigned long (*txQLen)     (VCanChanData*);
    int (*requestChipState)     (VCanChanData*);
    void (*requestSend)         (VCanCardData*, VCanChanData*);
    unsigned int (*getVersion)  (int);
    int (*objbufExists)         (VCanChanData *chd, int bufType, int bufNo);
    int (*objbufFree)           (VCanChanData *chd, int bufType, int bufNo);
    int (*objbufAlloc)          (VCanChanData *chd, int bufType, int *bufNo);
    int (*objbufWrite)          (VCanChanData *chd, int bufType, int bufNo,
                                 int id, int flags, int dlc, unsigned char *data);
    int (*objbufEnable)         (VCanChanData *chd, int bufType, int bufNo,
                                 int enable);
    int (*objbufSetFilter)      (VCanChanData *chd, int bufType, int bufNo,
                                 int code, int mask);
    int (*objbufSetFlags)       (VCanChanData *chd, int bufType, int bufNo,
                                 int flags);
    int (*objbufSetPeriod)      (VCanChanData *chd, int bufType, int bufNo,
                                 int period);
    int (*objbufSetMsgCount)    (VCanChanData *chd, int bufType, int bufNo,
                                 int count);
    int (*objbufSendBurst)      (VCanChanData *chd, int bufType, int bufNo,
                                 int burstLen);

    int (*getCardInfo)          (VCanCardData *vCard, VCAN_IOCTL_CARD_INFO *ci);
    int (*getCardInfo2)         (VCanCardData *vCard, KCAN_IOCTL_CARD_INFO_2 *ci2);
    int (*tx_interval)          (VCanChanData *chd, unsigned int *interval);
    int (*get_transceiver_type) (VCanChanData *chd, unsigned int *transceiver_type);
    int (*getCustChannelName)   (const VCanChanData * const chd,
                                 unsigned char * const data,
                                 const unsigned int data_size,
                                 unsigned int *status);
    int (*getCardInfoMisc)      (const VCanChanData *chd, KCAN_IOCTL_MISC_INFO *cardInfoMisc);
    int (*flashLeds)            (const VCanChanData *chd, int action, int timeout);
    int (*special_ioctl_handler) (VCanOpenFileNode *fileNodePtr, unsigned int ioctl_cmd, unsigned long arg);
    int (*memoConfigMode)       (const VCanChanData *chd, int interval);
    int (*kvDeviceGetMode)      (const VCanChanData *chd, int *mode);
    int (*kvDeviceSetMode)      (const VCanChanData *chd, int mode);
    int (*kvFileGetCount)       (const VCanChanData *chd, int *count);
    int (*kvFileGetName)        (const VCanChanData *chd, int fileNo, char *name, int namelen);
    int (*kvScriptControl)      (const VCanChanData *chd, KCAN_IOCTL_SCRIPT_CONTROL_T *scriptControl);
    int (*memoGetData)          (const VCanChanData *chd, int subcmd,
                                void *buf, int bufsiz,
                                unsigned long data1, unsigned short data2,
                                int *stat, int *dstat, int *lstat, unsigned int timeout_ms);
    int (*memoPutData)          (const VCanChanData *chd, int subcmd,
                                 void *buf, int bufsiz,
                                 unsigned long data1, unsigned short data2,
                                 int *stat, int *dstat, int *lstat, unsigned int timeout_ms);
    int (*memoDiskIo)           (const VCanChanData *chd);
    int (*memoDiskIoFast)       (const VCanChanData *chd);
    int (*cleanUpHnd)           (VCanChanData *vChan);
} VCanHWInterface;

#define SKIP_ERROR_EVENT 0
#define DETECT_ERROR_EVENT 1
#define ERROR_EVENT_DETECTED 2

typedef struct WaitNode {
  struct list_head   list;
  struct completion  waitCompletion;
  void              *replyPtr;
  unsigned char      cmdNr;
  uint16_t           transId;
  unsigned char      timedOut;
  void               *driver; //driver specific data
} WaitNode;



/*****************************************************************************/
/*  Shared data structures                                                   */
/*****************************************************************************/

extern struct file_operations fops;




/*****************************************************************************/
/*  Function definitions                                                     */
/*****************************************************************************/



/* Functions */
int             vCanInitData(VCanCardData *chd);
int             vCanTime(VCanCardData *vCard, uint64_t *time);
int             vCanDispatchEvent(VCanChanData *chd, VCAN_EVENT *e);
int             vCanFlushSendBuffer(VCanChanData *chd);
unsigned long   getQLen(unsigned long head, unsigned long tail, unsigned long size);
int             vCanInit(VCanDriverData *, unsigned);
void            vCanCleanup(VCanDriverData *);
int             vCanGetCardInfo(VCanCardData *, VCAN_IOCTL_CARD_INFO *);
int             vCanGetCardInfo2(VCanCardData *, KCAN_IOCTL_CARD_INFO_2 *);
struct timeval  vCanCalc_dt(struct timeval *start); //returns now-start
void            vCanCardRemoved(VCanChanData *chd);

#endif /* _VCAN_OS_IF_H_ */
