/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

/*
** File: 
**   vcan.h
** Project:
**   linuxcan
** Desciption: 
**   common driver datastructures.
*/

#ifndef _VCAN_OS_IF_H_
#define _VCAN_OS_IF_H_


#if LINUX

#else // WIN32
#   include "windows.h"
#endif
#include <asm/atomic.h>

#include "canIfData.h"
#include "vcanevt.h"

#include "osif_common.h"
#include "osif_kernel.h"



/*****************************************************************************/
/* defines */
/*****************************************************************************/

#define MAIN_RCV_BUF_SIZE  16
#define FILE_RCV_BUF_SIZE 500
#define TX_CHAN_BUF_SIZE  500

/*****************************************************************************/
/*  From canlib30/Src/CANLIB32/2K/include/vcanio.h                           */
/*****************************************************************************/

#define CAN_EXT_MSG_ID          0x80000000

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
/*  data structures                                                          */
/*****************************************************************************/

typedef union {
    unsigned long L;
    struct { unsigned short w0, w1; } W;
    struct { unsigned char b0, b1, b2, b3; } B;
} WL;

typedef struct CanChipState {
    int state;  /* buson / busoff / error passive / warning */
    int txerr;  /* tx error counter */
    int rxerr;  /* rx error counter */
} CanChipState;

/* File pointer specific data */
typedef struct VCanOpenFileNode
{
    int                     rcvBufHead;
    int                     rcvBufTail;
    VCAN_EVENT              fileRcvBuffer[FILE_RCV_BUF_SIZE];
    unsigned char           transId;
    OS_IF_WAITQUEUE_HEAD    rxWaitQ;
    struct file             *filp;
    struct VCanChanData     *chanData;
    int                     chanNr;
    unsigned char           writeIsBlock;
    unsigned char           readIsBlock;
    unsigned char           modeTx;
    unsigned char           modeTxRq;
    unsigned char           channelOpen;
    unsigned char           channelLocked;    
    long                    writeTimeout;
    long                    readTimeout;
    VCanMsgFilter           filter;
    unsigned long           overruns;
    struct VCanOpenFileNode *next;

} VCanOpenFileNode;


/* Channel specific data */
typedef struct VCanChanData 
{
    unsigned char            channel;
    unsigned char            chipType;
    unsigned char            ean[6];
    unsigned long            serialHigh;
    unsigned long            serialLow;

    /* Status */
    unsigned char            isOnBus;
    unsigned char            transType;   // TRANSCEIVER_TYPE_xxx
    unsigned char            lineMode;    // TRANSCEIVER_LINEMODE_xxx
    unsigned char            resNet;      // TRANSCEIVER_RESNET_xxx
    atomic_t                 transId;
    unsigned int             overrun;
    CanChipState             chipState;
    unsigned int             errorCount;
    unsigned long            errorTime;
    unsigned char            rxErrorCounter;
    unsigned char            txErrorCounter;
    int                      minorNr;

    CAN_MSG                  txChanBuffer[TX_CHAN_BUF_SIZE];
    CAN_MSG                  *currentTxMsg;

    atomic_t                 txChanBufHead;
    atomic_t                 txChanBufTail;
    OS_IF_WAITQUEUE_HEAD     txChanWaitQ; 

    /* Processes waiting for all messages to be sent */
    OS_IF_WAITQUEUE_HEAD     flushQ;

    struct VCanOpenFileNode *openFileList;

    unsigned short          fileOpenCount;

    OS_IF_LOCK              openLock;
    struct VCanCardData     *vCard;
    void                    *hwChanData;
    atomic_t                waitEmpty;    
} VCanChanData;


/*  Cards specific data */
typedef struct VCanCardData 
{
    unsigned int       nrChannels;
    unsigned long      serialNumber;
    unsigned char      ean[6];
    unsigned int       firmwareVersionMajor;
    unsigned int       firmwareVersionMinor;
    unsigned int       firmwareVersionBuild;

    unsigned long timeHi;
    unsigned long timeOrigin;
    unsigned long usPerTick;

    /* Ports and addresses */
    int                irq;
    unsigned char      cardPresent;
    VCanChanData       **chanData;
    void               *hwCardData;
    struct VCanCardData *next;
    struct completion           open;
    OS_IF_LOCK              memQLock;
} VCanCardData;


typedef struct VCanDriverData 
{
    int minorNr;
    int majorDevNr;
    struct timeval startTime;
} VCanDriverData;

#define VCAN_STAT_OK         0
#define VCAN_STAT_FAIL      -1
#define VCAN_STAT_TIMEOUT   -2

/*****************************************************************************/
/*  Function definitions                                                     */
/*****************************************************************************/

int vCanOpen  (struct inode *inode, struct file *filp);
int vCanClose (struct inode *inode, struct file *filp);
int vCanIOCtl (struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);

#define put_user_ret(x,ptr,ret) { if (os_if_set_int(x,ptr)) return ret; }
#define get_user_long_ret(x,ptr,ret) { if (os_if_get_long(x,ptr)) return ret; }
#define copy_to_user_ret(to,from,n,retval) { if (os_if_get_user_data(to,from,n)) return retval; }
#define copy_from_user_ret(to,from,n,retval) { if (os_if_set_user_data(to,from,n)) return retval; }

typedef struct VCanHWInterface {

    int (*initAllDevices)       (void);
    int (*setBusParams)         (VCanChanData *chd, VCanBusParams *par);
    int (*getBusParams)         (VCanChanData *chd, VCanBusParams *par);
    int (*setOutputMode)        (VCanChanData *chd, int silent);
    int (*setTranceiverMode)    (VCanChanData *chd, int linemode, int resnet);
    int (*busOn)                (VCanChanData *chd);
    int (*busOff)               (VCanChanData *chd);    
    int (*txAvailable)          (VCanChanData *chd);
    int (*transmitMessage)      (VCanChanData *chd, CAN_MSG *m);
    int (*procRead)             (char *buf, char **start, OS_IF_OFFSET offset, int count, int *eof, void *data);
    int (*closeAllDevices)      (void);
    unsigned long (*getTime)    (VCanCardData*);
    int (*flushSendBuffer)      (VCanChanData*);
    int (*getRxErr)             (VCanChanData*); 
    int (*getTxErr)             (VCanChanData*); 
    unsigned long (*rxQLen)     (VCanChanData*); 
    unsigned long (*txQLen)     (VCanChanData*); 
    int (*requestChipState)     (VCanChanData*); 
    int (*requestSend)          (VCanCardData*, VCanChanData*);
    unsigned int (*getVersion)  (int);

} VCanHWInterface;


/* Functions */
int             vCanInitData(VCanCardData *chd);
unsigned long   vCanTime(VCanCardData *vCard);
int             vCanDispatchEvent(VCanChanData *chd, VCAN_EVENT *e);
int             vCanFlushSendBuffer (VCanChanData *chd);
unsigned long   getQLen(unsigned long head, unsigned long tail, unsigned long size);

/* Shared data structures */
extern VCanDriverData  driverData;
extern VCanCardData    *canCards;
extern const char      *device_name;
extern VCanHWInterface hwIf;
extern OS_IF_LOCK      canCardsLock;
extern struct file_operations fops;

#endif /* _VCAN_OS_IF_H_ */


