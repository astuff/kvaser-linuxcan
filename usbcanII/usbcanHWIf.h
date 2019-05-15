/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

//
//  Linux USBcanII driver
//

#ifndef _USBCAN_HW_IF_H_
#define _USBCAN_HW_IF_H_


#if LINUX
//#   include <linux/list.h>
#   include <linux/types.h>
// new
#   include <linux/completion.h>
#else

#endif

#include <linux/workqueue.h>

#include "osif_kernel.h"
#include "helios_cmds.h"

/*****************************************************************************/
/* defines */
/*****************************************************************************/

#define DEVICE_NAME_STRING              "usbcanII"
#define MAX_CHANNELS                             2
#define KV_USBCAN_MAIN_RCV_BUF_SIZE             16
#define KV_USBCAN_TX_CMD_BUF_SIZE               16
#define USBCAN_CMD_RESP_WAIT_TIME              200
#define USBCANII_TICKS_PER_MS                  100
//#define DEMETER_MAX_OUTSTANDING_TX              64


// Bits in the CxSTRH register in the M16C.
#define M16C_BUS_RESET    0x01    // Chip is in Reset state
#define M16C_BUS_ERROR    0x10    // Chip has seen a bus error
#define M16C_BUS_PASSIVE  0x20    // Chip is error passive
#define M16C_BUS_OFF      0x40    // Chip is bus off



#if DEBUG
#   define USBCAN_Q_CMD_WAIT_TIME                 800
#else
#   define USBCAN_Q_CMD_WAIT_TIME                 200
#endif


typedef struct UsbcanWaitNode {
#if LINUX
    struct list_head list;
#endif
    OS_IF_SEMAPHORE   waitSemaphore;
    heliosCmd *replyPtr;
    unsigned char cmdNr;
    unsigned char transId;
    unsigned char timedOut;
} UsbcanWaitNode;



/* Channel specific data */
typedef struct UsbcanChanData
{    
    //OS_IF_DEVICE_CONTEXT_NODE node;
    /* Number of messages remaining in the sendbuffer  */ 
    //unsigned long sentTXflagCount;
    //unsigned long recvTXflagCount;
    /* These are the outgoing channelqueues */
    unsigned char txQChipFull; /* Used for flow control */
    //unsigned int transId;
    //unsigned int outstanding_tx;
    atomic_t outstanding_tx;
} UsbcanChanData;



/*  Cards specific data */
typedef struct UsbcanCardData {

    CAN_MSG current_tx_message[128]; // qqq max_outstanding_tx is received
                                     // from the card
    int max_outstanding_tx;
    
    spinlock_t      timeHi_lock;
        
    struct list_head replyWaitList;
        
    struct completion rx_needed;

    
    /* Structure to hold all of our device specific stuff */
    int rx_thread_running;
    int tx_thread_running;
    
    //OS_IF_TASK_QUEUE_HANDLE   txTaskQ;
    //OS_IF_TASK_QUEUE_HANDLE   rxTaskQ;
    struct workqueue_struct *txTaskQ;
    struct work_struct      txWork;
    
    struct workqueue_struct *rxTaskQ;
    struct work_struct      rxWork;
    
        
    heliosCmd          mainRcvBuffer[KV_USBCAN_MAIN_RCV_BUF_SIZE ];
    int                mainRcvBufHead; /* Where we write incoming messages */
    int                mainRcvBufTail; /* Where we read incoming messages  */

    heliosCmd          txCmdBuffer[KV_USBCAN_TX_CMD_BUF_SIZE]; /* Control messages */
    unsigned int       txCmdBufHead;   /* Where we write outgoing control messages */
    unsigned int       txCmdBufTail;   /* The messages are sent from this end */
    
    OS_IF_WAITQUEUE_HEAD  txCmdWaitQ;    /* WaitQ for sending commands */
    
    // busparams
    unsigned long freq;
    unsigned char sjw;
    unsigned char tseg1;
    unsigned char tseg2;
    unsigned char samples;
        
    
        struct usb_device       *udev;               // save off the usb device pointer 
        struct usb_interface    *interface;          // the interface for this device 
//        unsigned char         minor;               // the starting minor number for this device 
        unsigned char           num_ports;           // the number of ports this device has 
        char                    num_interrupt_in;    // number of interrupt in endpoints we have 
        char                    num_bulk_in;         // number of bulk in endpoints we have 
        char                    num_bulk_out;        // number of bulk out endpoints we have 

        unsigned char *         bulk_in_buffer;      // the buffer to receive data 
        size_t                  bulk_in_size;        // the size of the receive buffer 
        __u8                    bulk_in_endpointAddr;// the address of the bulk in endpoint 

        unsigned char *         bulk_out_buffer;     // the buffer to send data 
        size_t                  bulk_out_size;       // the size of the send buffer 
        struct urb *            write_urb;           // the urb used to send data
        struct urb *            read_urb;            // the urb used to receive data 
        __u8                    bulk_out_endpointAddr;//the address of the bulk out endpoint 
        atomic_t                write_busy;           // true iff write urb is busy 
        struct completion       write_finished;       // wait for the write to finish 

        int                     open;                 // if the port is open or not 
        volatile int            present;              // if the device is not disconnected 
        struct semaphore        sem;                  // locks this structure 

        VCanCardData           *vCard;
} UsbcanCardData;


int usbcan_init_driver(void);
int usbcan_set_busparams (VCanChanData *vChd, VCanBusParams *par);
int usbcan_get_busparams (VCanChanData *vChd, VCanBusParams *par);
int usbcan_set_silent (VCanChanData *vChd, int silent);
int usbcan_set_trans_type (VCanChanData *vChd, int linemode, int resnet);
int usbcan_bus_on (VCanChanData *vChd);
int usbcan_bus_off (VCanChanData *vChd);
int hwIfResetCard (VCanCardData *vCd);
int hwIfInitHW (VCanCardData *vCd);
int usbcan_get_tx_err(VCanChanData *vChd);
int usbcan_get_rx_err(VCanChanData *vChd);
//int usbcan_tx_available (VCanChanData *vChd);
int usbcan_outstanding_sync (VCanChanData *vChan);
int usbcan_close_all(void);
int usbcan_proc_read (char *buf, char **start, off_t offset,
                    int count, int *eof, void *data);
int usbcan_get_chipstate (VCanChanData *vChd);
unsigned long usbcan_get_time(VCanCardData *vCard);
unsigned long hwIfTimeStamp(VCanCardData *vCard, unsigned long timeLo);
int usbcan_flush_tx_buffer (VCanChanData *vChan);
int usbcan_schedule_send (VCanCardData *vCard, VCanChanData *vChan);
unsigned long usbcan_get_hw_rx_q_len(VCanChanData *vChan); 
unsigned long usbcan_get_hw_tx_q_len(VCanChanData *vChan); 
//int hwIfWaitResponse(VCanCardData *vCard, lpcCmd *cmd, lpcCmd *replyPtr, unsigned char cmdNr, unsigned char transId);
//int hwIfQCmd (VCanCardData *vCard, lpcCmd *cmd, unsigned int timeout);
int usbcan_translate_and_send_message(VCanChanData *vChan, CAN_MSG *m);

/*
  The event() function is this driver's Card Services event handler.
  It will be called by Card Services when an appropriate card status
  event is received.  The config() and release() entry points are
  used to configure or release a socket, in response to card
  insertion and ejection events.  They are invoked from the lapcan
  event handler. 
*/
//void hwIfConfig(OS_IF_CARD_CONTEXT *link);
void hwIfRelease(unsigned long arg);
/*int hwIfEvent(OS_IF_EVENT event, int priority,
                       OS_IF_EVENT_PARAM *args);
                       */
/*
  The attach() and detach() entry points are used to create and destroy
  "instances" of the driver, where each instance represents everything
  needed to manage one actual PCMCIA card.
*/

//OS_IF_CARD_CONTEXT* hwIfAttach(void);
//void hwIfDetach(OS_IF_CARD_CONTEXT *link);
//void hwIfTransmit (VCanCardData *vCard, lpcCmd *msg);

void hwIfReceive (VCanCardData *vCard);
//static void hwIfSend (void *void_localPtr);
int waitTransmit (VCanCardData *vCard);
int hwIfInit(VCanCardData *vCard);




/*
  The devInfo variable is the "key" that is used to match up this
  device driver with appropriate cards, through the card configuration
  database.
*/


#endif  /* _USBCAN_HW_IF_H_ */
