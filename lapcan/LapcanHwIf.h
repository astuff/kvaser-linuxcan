/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

/*  Canlib Linux LAPcan specific data */

#ifndef _LAPCAN_HW_IF_H_
#define _LAPCAN_HW_IF_H_


#if LINUX
#   include <linux/list.h>
#   include <linux/types.h>
// new
#   include <linux/completion.h>
#   include <linux/spinlock.h>
#else

#endif

#include "lapcmds.h"
#include "osif_kernel.h"
#include "osif_functions_pcmcia.h"

/*****************************************************************************/
/* defines */
/*****************************************************************************/

#define DEVICE_NAME_STRING "lapcan_cs"
#define MAX_CHANNELS 2
#define LP_FIFO_SIZE 64
#define MAIN_RCV_BUF_SIZE 16
#define TX_CMD_BUF_SIZE 16
#define TX_CHAN_BUF_SIZE 500
#define LAPCAN_TX_BUF_SIZE 200
#define LAPCAN_CMD_RESP_WAIT_TIME 200
#define LAPCAN_Q_CMD_WAIT_TIME 200



/* Channel specific data */
typedef struct LapcanChanData
{    
    OS_IF_DEVICE_CONTEXT_NODE node;
    /* Number of messages remaining in the sendbuffer  */ 
    atomic_t                sentTXflagCount;              
    atomic_t                recvTXflagCount;              
    /* These are the outgoing channelqueues */
    //unsigned char txQChipFull; /* Used for flow control */
    atomic_t txQChipFull; /* Used for flow control */
} LapcanChanData;

/*  Cards specific data */
typedef struct LapcanCardData {
    OS_IF_CARD_CONTEXT link;
    int           stop;
    struct bus_operations *bus;

    unsigned           portAddress;
    unsigned char      isInit;

    //volatile int       recIsBusy;
    volatile unsigned long recIsBusy;

    OS_IF_TASK_QUEUE_HANDLE   txTaskQ;

    // usage counter for Lapcan TX FIFO
    volatile unsigned char      fifoCount;
#if LINUX   
    struct list_head replyWaitList;
    rwlock_t replyWaitListLock;
    spinlock_t replyWaitListSpinLock;
#endif
    // Buffer structures
    unsigned char      curCmdLen;
    unsigned char      curCmdPos;
    lpcCmd             mainRcvBuffer[MAIN_RCV_BUF_SIZE];
    int                mainRcvBufHead; /* Where we write incoming messages */
    int                mainRcvBufTail; /* Where we read incoming messages  */

    lpcCmd             txCmdBuffer[TX_CMD_BUF_SIZE]; /* Control messages */
    unsigned int       txCmdBufHead;   /* Where we write outgoing control messages */
    unsigned int       txCmdBufTail;   /* The messages are sent from this end */
    OS_IF_WAITQUEUE_HEAD  txCmdWaitQ;    /* WaitQ for sending commands */

    // Some data about the card; these are obtained when initializing the card.
    unsigned char      hwInfoIsValid;
    unsigned char      swInfoIsValid;
    unsigned char      useDCD;
    unsigned char      useOUT1Toggle;
    unsigned int       hwRevision;
    unsigned int       swRevision;
    unsigned int       swOptions;
    unsigned int       clockUsPerTick;
    unsigned int       clockHz;

} LapcanCardData;

typedef struct LapcanWaitNode {
#if LINUX
    struct list_head list;
#endif
    OS_IF_SEMAPHORE   waitSemaphore;
    lpcCmd *replyPtr;
    unsigned char cmdNr;
    unsigned char transId;
    unsigned char timedOut;
} LapcanWaitNode;

int hwIfInitDriver(void);
int hwIfSetBusParams (VCanChanData *vChd, VCanBusParams *par);
int hwIfGetBusParams (VCanChanData *vChd, VCanBusParams *par);
int hwIfSetOutputMode (VCanChanData *vChd, int silent);
int hwIfSetTranceiverMode (VCanChanData *vChd, int linemode, int resnet);
int hwIfBusOn (VCanChanData *vChd);
int hwIfBusOff (VCanChanData *vChd);
int hwIfResetCard (VCanCardData *vCd);
int hwIfInitHW (VCanCardData *vCd);
int hwIfGetTxErr(VCanChanData *vChd);
int hwIfGetRxErr(VCanChanData *vChd);
int hwIfTxAvailable (VCanChanData *vChd);
int hwIfCloseAllDevices(void);
int hwIfProcRead (char *buf, char **start, off_t offset,
                  int count, int *eof, void *data);
int hwIfRequestChipState (VCanChanData *vChd);
unsigned long hwIfTime(VCanCardData *vCard);
unsigned long hwIfTimeStamp(VCanCardData *vCard, unsigned long timeLo);
int hwIfFlushSendBuffer (VCanChanData *vChan);
int hwIfRequestSend (VCanCardData *vCard, VCanChanData *vChan);
unsigned long hwIfHwRxQLen(VCanChanData *vChan); 
unsigned long hwIfHwTxQLen(VCanChanData *vChan); 
int hwIfWaitResponse(VCanCardData *vCard, lpcCmd *cmd, lpcCmd *replyPtr, unsigned char cmdNr, unsigned char transId);
int hwIfQCmd (VCanCardData *vCard, lpcCmd *cmd, unsigned int timeout);
int hwIfPrepareAndTransmit(VCanChanData *vChan, CAN_MSG *m);

/*
  The event() function is this driver's Card Services event handler.
  It will be called by Card Services when an appropriate card status
  event is received.  The config() and release() entry points are
  used to configure or release a socket, in response to card
  insertion and ejection events.  They are invoked from the lapcan
  event handler. 
*/
void hwIfConfig(OS_IF_CARD_CONTEXT *link);
void hwIfRelease(unsigned long arg);
int hwIfEvent(OS_IF_EVENT event, int priority,
              OS_IF_EVENT_PARAM *args);
/*
  The attach() and detach() entry points are used to create and destroy
  "instances" of the driver, where each instance represents everything
  needed to manage one actual PCMCIA card.
*/

OS_IF_CARD_CONTEXT* hwIfAttach(void);
void hwIfDetach(OS_IF_CARD_CONTEXT *link);
void hwIfTransmit (VCanCardData *vCard, lpcCmd *msg);

void hwIfReceive (VCanCardData *vCard);
static void hwIfSend (void *void_localPtr);
int waitTransmit (VCanCardData *vCard);
int hwIfInit(VCanCardData *vCard);




/*
  The devInfo variable is the "key" that is used to match up this
  device driver with appropriate cards, through the card configuration
  database.
*/


#endif  /* _LAPCAN_HW_IF_H_ */
