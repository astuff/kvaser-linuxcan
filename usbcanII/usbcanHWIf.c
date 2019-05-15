/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

//
//  Linux USBcanII driver
//

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/usb.h>

// non system headers
#include "osif_kernel.h" 
#include "osif_functions_kernel.h"
#include "VCanOsIf.h"
#include "usbcanHWIf.h"
#include "helios_cmds.h"

// Get a minor range for your devices from the usb maintainer
// qqq dont know what this is for...
#define USB_USBCAN_MINOR_BASE   32

#if LINUX
    MODULE_LICENSE("GPL");
    MODULE_AUTHOR("KVASER");
    MODULE_DESCRIPTION("USBcanII CAN module.");

    
//---------------------------------------------------------------------------------------
// If you do not define USBCAN_DEBUG at all, all the debug code will be
// left out.  If you compile with USBCAN_DEBUG=0, the debug code will
// be present but disabled -- but it can then be enabled for specific
// modules at load time with a 'pc_debug=#' option to insmod.
//
#   ifdef USBCAN_DEBUG
        static int pc_debug = USBCAN_DEBUG;
        MODULE_PARM(pc_debug, "i");
#       define DEBUGPRINT(n, args...) if (pc_debug>=(n)) printk("<" #n ">" args)
#   else
#       define DEBUGPRINT(n, args...)

#   endif
//----------------------------------------------------------------------------------------

#endif // LINUX



//======================================================================
// HW function pointers                                                 
//======================================================================

#if LINUX
VCanHWInterface hwIf = {
    .initAllDevices    = usbcan_init_driver,    
    .setBusParams      = usbcan_set_busparams,     
    .getBusParams      = usbcan_get_busparams,     
    .setOutputMode     = usbcan_set_silent,    
    .setTranceiverMode = usbcan_set_trans_type,
    .busOn             = usbcan_bus_on,            
    .busOff            = usbcan_bus_off,           
    .txAvailable       = usbcan_outstanding_sync,            // this isn't really a function thats checks if tx is available!
    .transmitMessage   = usbcan_translate_and_send_message,  // qqq shouldn't be here! 
    .procRead          = usbcan_proc_read,         
    .closeAllDevices   = usbcan_close_all,  
    .getTime           = usbcan_get_time,
    .flushSendBuffer   = usbcan_flush_tx_buffer, 
    .getTxErr          = usbcan_get_tx_err,
    .getRxErr          = usbcan_get_rx_err,
    .rxQLen            = usbcan_get_hw_rx_q_len,
    .txQLen            = usbcan_get_hw_tx_q_len,
    .requestChipState  = usbcan_get_chipstate,
    .requestSend       = usbcan_schedule_send
};
#endif





//======================================================================
// static declarations
#define DEVICE_NAME_STRING "usbcanII"
const char      *device_name    = DEVICE_NAME_STRING;

// prevent races between open() and disconnect() 
static DECLARE_MUTEX (disconnect_sem);

// usb packet size
#define MAX_PACKET_OUT      3072        // To device
#define MAX_PACKET_IN       3072        // From device



//======================================================================
// prototypes
static int          usbcan_plugin(struct usb_interface *interface, const struct usb_device_id *id);
static void         usbcan_remove(struct usb_interface *interface);

static void         usbcan_write_bulk_callback(struct urb *urb, struct pt_regs *regs);


static int          usbcan_allocate(struct VCanCardData **vCard);
static void         usbcan_deallocate(struct VCanCardData *vCard);

static int          usbcan_start(VCanCardData *vCard);

static int          usbcan_tx_available (VCanChanData *vChan);
static int          usbcan_transmit(VCanCardData *vCard); 
static int          usbcan_send_and_wait_reply(VCanCardData *vCard, heliosCmd *cmd, heliosCmd *replyPtr, unsigned char cmdNr, unsigned char transId);
static int          usbcan_queue_cmd(VCanCardData *vCard, heliosCmd *cmd, unsigned int timeout);

static int          usbcan_handle_command(heliosCmd *cmd, VCanCardData *vCard);
static int          usbcan_get_trans_id(heliosCmd *cmd);

static int          usbcan_fill_usb_buffer(VCanCardData *vCard, unsigned char  *buffer, int maxlen);
static void         usbcan_translate_can_msg(VCanChanData *vChan, heliosCmd *helios_msg, CAN_MSG *can_msg);

static void         usbcan_get_card_info(VCanCardData* vCard);
//----------------------------------------------------------------------



//------------------------------------------------------------------------------------------------------------
// Supported KVASER hardware
#define KVASER_VENDOR_ID            0x0bfd
#define USB_USBCAN2_PRODUCT_ID      0x0004
#define USB_USBCAN_REVB_PRODUCT_ID  0x0002
#define USB_MEMORATOR_PRODUCT_ID    0x0005


// table of devices that work with this driver 
static struct usb_device_id usbcan_table [] = {
    { USB_DEVICE(KVASER_VENDOR_ID, USB_USBCAN2_PRODUCT_ID) },
    { USB_DEVICE(KVASER_VENDOR_ID, USB_USBCAN_REVB_PRODUCT_ID) },
    { USB_DEVICE(KVASER_VENDOR_ID, USB_MEMORATOR_PRODUCT_ID) },
    { }  // Terminating entry 
};

MODULE_DEVICE_TABLE (usb, usbcan_table);

// 
// usb class driver info in order to get a minor number from the usb core,
// and to have the device registered with devfs and the driver core
//
static struct usb_class_driver usbcan_class = {
    // there will be a special file in /dev/usb called the below
    .name =         "usb/usbcanII",
    .fops =         &fops,
    .mode =         S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH,
    .minor_base =   USB_USBCAN_MINOR_BASE,
};


// usb specific object needed to register this driver with the usb subsystem 
static struct usb_driver usbcan_driver = {
    .owner      =    THIS_MODULE,
    .name       =    "KVASER usbcanII",
    .probe      =    usbcan_plugin,
    .disconnect =    usbcan_remove,
    .id_table   =    usbcan_table,
};

//------------------------------------------------------------------------------------------------------------





//------------------------------------------------------
//
//    ---- CALLBACKS ----
//
//------------------------------------------------------

//============================================================================
//  usbcan_write_bulk_callback
//
static void usbcan_write_bulk_callback (struct urb *urb, struct pt_regs *regs)
{
    VCanCardData *vCard = (VCanCardData*)urb->context;
    struct UsbcanCardData *dev = (struct UsbcanCardData *)vCard->hwCardData;

    // sync/async unlink faults aren't errors 
    if (urb->status && !(urb->status == -ENOENT ||
                         urb->status == -ECONNRESET)) {
        DEBUGPRINT(2, "%s - nonzero write bulk status received: %d\n",
              __FUNCTION__, urb->status);
    }

    // notify anyone waiting that the write has finished 
    atomic_set (&dev->write_busy, 0);
    os_if_up_sema (&dev->write_finished);
    
} // _write_bulk_callback




//----------------------------------------------------------------------------
//
//    ---- THREADS ----
//
//----------------------------------------------------------------------------


//============================================================================
// usbcan_rx_thread
//
static int usbcan_rx_thread (void *context)
{
    VCanCardData *vCard = (VCanCardData*)context;
    struct UsbcanCardData *dev = (struct UsbcanCardData *)vCard->hwCardData;
    int len;
    int loopmax = 100;
    int ret;
    
    // initialize
    dev->rx_thread_running = 1;
    
    DEBUGPRINT(1, "rx thread started\n");
    
    dev->read_urb = usb_alloc_urb(0, GFP_KERNEL);
    
    dev->write_urb->transfer_flags = (URB_NO_TRANSFER_DMA_MAP/* |
                                      URB_ASYNC_UNLINK*/);
        
    
    while (dev->present) {

        // verify that the device wasn't unplugged 
        if (!dev->present) {
            DEBUGPRINT(1, "rx_thread: device removed\n");
            DEBUGPRINT(1, "rx thread Ended\n");
            dev->rx_thread_running = 0;
            return -ENODEV;
        }
   
        // do a blocking bulk read to get data from the device
        len = 0;
        ret = usb_bulk_msg (dev->udev,
                   usb_rcvbulkpipe (dev->udev,
                        dev->bulk_in_endpointAddr),
                   dev->bulk_in_buffer,
                   dev->bulk_in_size,
                   &len, HZ*30);
        if (ret) {
            // save if runaway
            if (ret != -ETIMEDOUT) {
                #if DEBUG
                if (loopmax %10 == 0)
                    DEBUGPRINT(2, "usb_bulk_msg error - closing down in: %d\n", loopmax/10);
                #endif
                if (--loopmax == 0) {
                    DEBUGPRINT(1, "rx thread Ended\n");
                    
                    // since this has faild so many times
                    // stop transfers to device
                    dev->present = 0;

                    return -ENODEV;
                }
            }
        }
        else {
            
            #   if 0 //DEBUG //@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
                if (len) {
                    char *ptr = dev->bulk_in_buffer;
                    int i;
                    DEBUGPRINT(1, "rx buf dump: ");
                    for (i=0;i<len;i++) {
                        DEBUGPRINT(1, " %x", *ptr);
                        ptr++;
                    }       
                    DEBUGPRINT(1, "\n");
                }
            #   endif //@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
            

            heliosCmd      *cmd;
            char           *buffer   = dev->bulk_in_buffer;
            int            loopMax   = 1000;
            unsigned int   count     = 0;
            
            while (count < len) {
                cmd = (heliosCmd *)&buffer[count];
                if (cmd->head.cmdLen == 0) {
                  count += 64;
                  count &= 0xFFFFFFC0;
                  continue;
                } 
                else {
                  count += cmd->head.cmdLen;
                }
            
                // A loop counter as a safetly measure.
                if (--loopMax == 0) {
                  DEBUGPRINT(1, "ERROR demeter_process_usb_data() LOOPMAX. \n");
                  break;
                }
                usbcan_handle_command(cmd, vCard);
            }   
        }
    } // while (dev->present)
    
    dev->rx_thread_running = 0;
    DEBUGPRINT(1, "rx thread Ended\n");
    return 0;

} // _rx_thread



//============================================================================
// _handle_command
//
int usbcan_handle_command (heliosCmd *cmd, VCanCardData *vCard) 
{
    struct UsbcanCardData  *dev = (struct UsbcanCardData *)vCard->hwCardData;
    struct list_head *currHead;
    struct list_head *tmpHead;
    UsbcanWaitNode   *currNode;
    VCAN_EVENT  e;
    
    switch (cmd->head.cmdNo) {
        case CMD_RX_STD_MESSAGE:
            {   
                //DEBUGPRINT(1, "CMD_RX_STD_MESSAGE");
                
                char dlc;
                unsigned char flags;
                unsigned int chan = cmd->rxCanMessage.channel;
        
                if (chan < (unsigned)vCard->nrChannels) {
                    VCanChanData *vChan = vCard->chanData[cmd->rxCanMessage.channel];

                    e.tag               = V_RECEIVE_MSG;
                    e.transId           = 0;
                    os_if_irq_disable(&dev->timeHi_lock);
                    e.timeStamp         = (cmd->rxCanMessage.time + vCard->timeHi)/USBCANII_TICKS_PER_MS;
                    os_if_irq_enable(&dev->timeHi_lock);
                    e.tagData.msg.id    =  (cmd->rxCanMessage.rawMessage[0] & 0x1F) << 6;
                    e.tagData.msg.id    += (cmd->rxCanMessage.rawMessage[1] & 0x3F);
                    flags = cmd->rxCanMessage.flags;
                    e.tagData.msg.flags = 0;
                    if (flags & MSGFLAG_OVERRUN) 
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_OVERRUN;
                    if (flags & MSGFLAG_REMOTE_FRAME) 
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_REMOTE_FRAME;
                    if (flags & MSGFLAG_ERROR_FRAME) 
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_ERROR_FRAME;
                    if (flags & MSGFLAG_TX)
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_TXACK;
                    if (flags & MSGFLAG_TXRQ)
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_TXRQ;
        
                    dlc = cmd->rxCanMessage.rawMessage[5] & 0x0F;
                    e.tagData.msg.dlc = dlc;
        
                    memcpy(e.tagData.msg.data, &cmd->rxCanMessage.rawMessage[6], 8);
        
                    //DEBUGPRINT(1, " - vCanDispatchEvent id: %d (ch:%d), time %d\n", e.tagData.msg.id, vChan->channel, e.timeStamp);
                    vCanDispatchEvent(vChan, &e);
                }
            }
            break;
        
        case CMD_RX_EXT_MESSAGE:
            {
                char dlc;
                unsigned char flags;
                unsigned int chan = cmd->rxCanMessage.channel;

                if (chan < (unsigned)vCard->nrChannels) {
                    // ext?
                    VCanChanData *vChan  = vCard->chanData[cmd->rxCanMessage.channel];

                    e.tag               = V_RECEIVE_MSG;
                    e.transId           = 0;
                    os_if_irq_disable(&dev->timeHi_lock);
                    e.timeStamp         = (cmd->rxCanMessage.time + vCard->timeHi)/USBCANII_TICKS_PER_MS;
                    os_if_irq_enable(&dev->timeHi_lock);
                    e.tagData.msg.id    =  (cmd->rxCanMessage.rawMessage[0] & 0x1F) << 24;
                    e.tagData.msg.id    += (cmd->rxCanMessage.rawMessage[1] & 0x3F) << 18;
                    e.tagData.msg.id    += (cmd->rxCanMessage.rawMessage[2] & 0x0F) << 14;
                    e.tagData.msg.id    += (cmd->rxCanMessage.rawMessage[3] & 0xFF) <<  6;
                    e.tagData.msg.id    += (cmd->rxCanMessage.rawMessage[4] & 0x3F);
                    e.tagData.msg.id    += EXT_MSG;

                    flags = cmd->rxCanMessage.flags;
                    e.tagData.msg.flags = 0;
                    if (flags & MSGFLAG_OVERRUN) 
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_OVERRUN;
                    if (flags & MSGFLAG_REMOTE_FRAME) 
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_REMOTE_FRAME;
                    if (flags & MSGFLAG_ERROR_FRAME) 
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_ERROR_FRAME;
                    if (flags & MSGFLAG_TX)
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_TXACK;
                    if (flags & MSGFLAG_TXRQ)
                        e.tagData.msg.flags |= VCAN_MSG_FLAG_TXRQ;

                    dlc = cmd->rxCanMessage.rawMessage[5] & 0x0F;
                    e.tagData.msg.dlc = dlc;

                    memcpy(e.tagData.msg.data, &cmd->rxCanMessage.rawMessage[6], 8);

                    //DEBUGPRINT(1, " - vCanDispatchEvent id: x%d (ch:%d), time %d\n", e.tagData.msg.id, vChan->channel, e.timeStamp);
                    vCanDispatchEvent(vChan, &e);
                }
            }
            break;
        
        case CMD_GET_BUSPARAMS_RESP:
            DEBUGPRINT(1, "CMD_GET_BUSPARAMS_RESP\n");
            break;
        
        case CMD_CHIP_STATE_EVENT:
            
            {                    
                unsigned int chan = cmd->chipStateEvent.channel;
                VCanChanData *vChd = vCard->chanData[chan];
                
                if (chan < (unsigned)vCard->nrChannels) {
                    vChd->chipState.txerr = cmd->chipStateEvent.txErrorCounter;
                    vChd->chipState.rxerr = cmd->chipStateEvent.rxErrorCounter;
                    if(cmd->chipStateEvent.txErrorCounter || cmd->chipStateEvent.rxErrorCounter) {
                        //DEBUGPRINT(1,"CMD_CHIP_STATE_EVENT, chan %d - ", chan);
                        //DEBUGPRINT(1,"txErr = %d/rxErr = %d\n",
                        //      cmd->chipStateEvent.txErrorCounter,
                        //      cmd->chipStateEvent.rxErrorCounter);
                    }
                }

                // ".busStatus" is the contents of the CnSTRH register.
                switch (cmd->chipStateEvent.busStatus & (M16C_BUS_PASSIVE|M16C_BUS_OFF)) {
                    case 0:
                        vChd->chipState.state = CHIPSTAT_ERROR_ACTIVE;
                        break;
                    case M16C_BUS_PASSIVE:
                        vChd->chipState.state = CHIPSTAT_ERROR_PASSIVE|CHIPSTAT_ERROR_WARNING;
                        break;
                    case M16C_BUS_OFF:
                        vChd->chipState.state = CHIPSTAT_BUSOFF;
                        break;
                    case (M16C_BUS_PASSIVE|M16C_BUS_OFF):
                        vChd->chipState.state = CHIPSTAT_BUSOFF|CHIPSTAT_ERROR_PASSIVE|CHIPSTAT_ERROR_WARNING;
                        break;
                }
                // Reset is treated like bus-off
                if (cmd->chipStateEvent.busStatus & M16C_BUS_RESET) {
                    vChd->chipState.state = CHIPSTAT_BUSOFF;
                    vChd->chipState.txerr = 0;
                    vChd->chipState.rxerr = 0;
                }

                //if(hCd->waitForChipState)
                //    wake_up(&hCd->waitResponse);
                
                e.tag       = V_CHIP_STATE;
                os_if_irq_disable(&dev->timeHi_lock);
                e.timeStamp = (cmd->chipStateEvent.time + vCard->timeHi)/USBCANII_TICKS_PER_MS;
                os_if_irq_enable(&dev->timeHi_lock);
                e.transId   = 0;
                e.tagData.chipState.busStatus      = (unsigned char) vChd->chipState.state;
                e.tagData.chipState.txErrorCounter = (unsigned char) vChd->chipState.txerr;
                e.tagData.chipState.rxErrorCounter = (unsigned char) vChd->chipState.rxerr;

                vCanDispatchEvent(vChd, &e); 

                break;
            }
        
        case CMD_GET_DRIVERMODE_RESP:
            DEBUGPRINT(1, "CMD_GET_DRIVERMODE_RESP\n");
            break;
        
        case CMD_START_CHIP_RESP:
            DEBUGPRINT(1, "CMD_START_CHIP_RESP\n");
            break;
        
        case CMD_STOP_CHIP_RESP:
            DEBUGPRINT(1, "CMD_STOP_CHIP_RESP\n");
            break;
        
        case CMD_READ_CLOCK_RESP:
            //DEBUGPRINT(1, "CMD_READ_CLOCK_RESP\n");
            os_if_irq_disable(&dev->timeHi_lock);
            vCard->timeHi = cmd->readClockResp.time[1] << 16;
            os_if_irq_enable(&dev->timeHi_lock);
            //DEBUGPRINT(1, "C %x\n", vCard->timeHi);            
            break;
        
        case CMD_CLOCK_OVERFLOW_EVENT:
            //DEBUGPRINT(1, "CMD_CLOCK_OVERFLOW_EVENT\n");
            os_if_irq_disable(&dev->timeHi_lock);
            vCard->timeHi = cmd->clockOverflowEvent.currentTime & 0xFFFF0000;
            os_if_irq_enable(&dev->timeHi_lock);
            //DEBUGPRINT(1, "O %x\n", vCard->timeHi);            
            break;      
        
        case CMD_GET_CARD_INFO_RESP:
            DEBUGPRINT(1, "CMD_GET_CARD_INFO_RESP\n");
            break;      
        
        case CMD_GET_INTERFACE_INFO_RESP:
            DEBUGPRINT(1, "CMD_GET_INTERFACE_INFO_RESP\n");
            break;      
        
        case CMD_GET_SOFTWARE_INFO_RESP:
            DEBUGPRINT(1, "CMD_GET_SOFTWARE_INFO_RESP\n");
            break;      
        
        case CMD_GET_BUSLOAD_RESP:
            DEBUGPRINT(1, "CMD_GET_BUSLOAD_RESP\n");
            break;      
        
        case CMD_RESET_STATISTICS:
            DEBUGPRINT(1, "CMD_RESET_STATISTICS\n");
            break;      
        
        case CMD_ERROR_EVENT:
            DEBUGPRINT(1, "CMD_ERROR_EVENT");
            
            break;  
        
        case CMD_RESET_ERROR_COUNTER:
            DEBUGPRINT(1, "CMD_RESET_ERROR_COUNTER\n");
            break;

        case CMD_TX_REQUEST:
        {
            unsigned int transId;
            unsigned int    chan    = cmd->txRequest.channel;
            VCanChanData   *vChan   = vCard->chanData[cmd->txRequest.channel];
            //UsbcanChanData *usbChan = vChan->hwChanData;

            if (chan < (unsigned)vCard->nrChannels) {
                    // A TxReq. Take the current tx message, modify it to a
                    // receive message and send it back.
                transId = cmd->txRequest.transId;
                if ((transId == 0) || (transId > dev->max_outstanding_tx)) {
                    DEBUGPRINT(1,"CMD_TX_REQUEST chan %d ERROR transid to high %d\n", chan, transId);
                    break;
                }

                if (dev->current_tx_message[transId - 1].flags & VCAN_MSG_FLAG_TXRQ) {
                    VCAN_EVENT *e = (VCAN_EVENT*) &dev->current_tx_message[transId - 1];
                    e->tag = V_RECEIVE_MSG;
                    os_if_irq_disable(&dev->timeHi_lock);
                    e->timeStamp = (cmd->txRequest.time + vCard->timeHi)/USBCANII_TICKS_PER_MS;
                    os_if_irq_enable(&dev->timeHi_lock);
                    e->tagData.msg.flags &= ~VCAN_MSG_FLAG_TXACK;  // qqq TXACK/RQ???
                    vCanDispatchEvent(vChan, e);
                }
            }                
            break;
        }
        
        case CMD_TX_ACKNOWLEDGE:
            {
                unsigned int       transId;
                VCanChanData       *vChan    = vCard->chanData[cmd->txAck.channel];
                UsbcanChanData     *usbChan  = vChan->hwChanData;    
                UsbcanCardData     *dev      = (struct UsbcanCardData *)vCard->hwCardData;
                
                //DEBUGPRINT(1, "CMD_TX_ACKNOWLEDGE on ch %d", cmd->txAck.channel);

                transId = cmd->txAck.transId;
                if ((transId == 0) || (transId > dev->max_outstanding_tx)) {
                    DEBUGPRINT(1,"CMD_TX_ACKNOWLEDGE chan %d ERROR transid %d\n", cmd->txAck.channel, transId);
                    break;
                }
                // outstanding are changing from *full* to at least one open slot
                if (atomic_read(&usbChan->outstanding_tx) >= dev->max_outstanding_tx) {
                    //queue_work(dev->txTaskQ, &dev->txWork);
                    os_if_queue_task_not_default_queue(dev->txTaskQ, &dev->txWork);
                }
                atomic_sub(1, &usbChan->outstanding_tx);
                
                // check if we should *wake* canwritesync
                if ((atomic_read(&usbChan->outstanding_tx) == 0) && (getQLen(atomic_read(&vChan->txChanBufHead),
                    atomic_read(&vChan->txChanBufTail), TX_CHAN_BUF_SIZE) == 0) && atomic_read(&vChan->waitEmpty)) {
                    atomic_set(&vChan->waitEmpty, 0);
                    os_if_wake_up_interruptible(&vChan->flushQ);
                    //DEBUGPRINT(1, "W%d", cmd->txAck.channel);
                }
                #if DEBUG
                else {
                    if (atomic_read(&usbChan->outstanding_tx) < 4)
                        DEBUGPRINT(1, "o%d ql%d we%d ", atomic_read(&usbChan->outstanding_tx),
                                   (int)getQLen(atomic_read(&vChan->txChanBufHead), atomic_read(&vChan->txChanBufTail),
                                   TX_CHAN_BUF_SIZE), atomic_read(&vChan->waitEmpty));
                }
                #endif

                // qqq dispatch txack!
                
                if (dev->current_tx_message[transId - 1].flags & VCAN_MSG_FLAG_TXACK) {
                    VCAN_EVENT *e = (VCAN_EVENT*) &dev->current_tx_message[transId - 1];
                    e->tag = V_RECEIVE_MSG;
                    os_if_irq_disable(&dev->timeHi_lock);
                    e->timeStamp = (cmd->txAck.time + vCard->timeHi)/USBCANII_TICKS_PER_MS;
                    os_if_irq_enable(&dev->timeHi_lock);
                    e->tagData.msg.flags &= ~VCAN_MSG_FLAG_TXRQ; // qqq TXRQ???
                    vCanDispatchEvent(vChan, e);
                }
                
                //DEBUGPRINT(1, "X%d ", cmd->txAck.channel);
                break;
            }
        
        case CMD_CAN_ERROR_EVENT:
            //DEBUGPRINT(1, "CMD_CAN_ERROR_EVENT");
            {
                int errorCounterChanged;
                
                // <windows> Known problem: if the error counters of both channels
                // are max then there is no way of knowing which channel got an errorframe 
                // </windows>
                VCanChanData *vChd = vCard->chanData[0]; // qqq chan??

                // It's an error frame if any of our error counters has
                // increased..
                errorCounterChanged =  (cmd->canErrorEvent.txErrorCounterCh0 > vChd->chipState.txerr);
                errorCounterChanged |= (cmd->canErrorEvent.rxErrorCounterCh0 > vChd->chipState.rxerr);
                // It's also an error frame if we have seen a bus error while
                // the other channel hasn't seen any bus errors at all.
                errorCounterChanged |= ((cmd->canErrorEvent.busStatusCh0 & M16C_BUS_ERROR) &&
                                        !(cmd->canErrorEvent.busStatusCh1 & M16C_BUS_ERROR));

                vChd->chipState.txerr = cmd->canErrorEvent.txErrorCounterCh0;
                vChd->chipState.rxerr = cmd->canErrorEvent.rxErrorCounterCh0;


                switch (cmd->canErrorEvent.busStatusCh0 & (M16C_BUS_PASSIVE|M16C_BUS_OFF)) {
                    case 0:
                        vChd->chipState.state = CHIPSTAT_ERROR_ACTIVE;
                        break;

                    case M16C_BUS_PASSIVE:
                        vChd->chipState.state = CHIPSTAT_ERROR_PASSIVE|CHIPSTAT_ERROR_WARNING;
                        break;

                    case M16C_BUS_OFF:
                        vChd->chipState.state = CHIPSTAT_BUSOFF;
                        errorCounterChanged = 0;
                        break;

                    case (M16C_BUS_PASSIVE|M16C_BUS_OFF):
                        vChd->chipState.state = CHIPSTAT_BUSOFF|CHIPSTAT_ERROR_PASSIVE|CHIPSTAT_ERROR_WARNING;
                        errorCounterChanged = 0;
                        break;

                    default:
                        break;
                }
                
                // new FO 041217
                // Reset is treated like bus-off
                if (cmd->canErrorEvent.busStatusCh0 & M16C_BUS_RESET) {
                    vChd->chipState.state = CHIPSTAT_BUSOFF;
                    vChd->chipState.txerr = 0;
                    vChd->chipState.rxerr = 0;
                    errorCounterChanged = 0;
                }
                 // next channel
                if ((unsigned)vCard->nrChannels > 0) {

                    VCanChanData *vChd = vCard->chanData[1];

                    // It's an error frame if any of our error counters has
                    // increased..
                    errorCounterChanged =  (cmd->canErrorEvent.txErrorCounterCh1 > vChd->chipState.txerr);
                    errorCounterChanged |= (cmd->canErrorEvent.rxErrorCounterCh1 > vChd->chipState.rxerr);

                    // It's also an error frame if we have seen a bus error while
                    // the other channel hasn't seen any bus errors at all.
                    errorCounterChanged |= ((cmd->canErrorEvent.busStatusCh1 & M16C_BUS_ERROR) &&
                                            !(cmd->canErrorEvent.busStatusCh0 & M16C_BUS_ERROR));

                    vChd->chipState.txerr = cmd->canErrorEvent.txErrorCounterCh1;
                    vChd->chipState.rxerr = cmd->canErrorEvent.rxErrorCounterCh1;

                    switch (cmd->canErrorEvent.busStatusCh1 & (M16C_BUS_PASSIVE|M16C_BUS_OFF)) {
                        case 0:
                            vChd->chipState.state = CHIPSTAT_ERROR_ACTIVE;
                            break;

                        case M16C_BUS_PASSIVE:
                            vChd->chipState.state = CHIPSTAT_ERROR_PASSIVE|CHIPSTAT_ERROR_WARNING;
                            break;

                        case M16C_BUS_OFF:
                            vChd->chipState.state = CHIPSTAT_BUSOFF;
                            errorCounterChanged = 0;
                            break;

                        case (M16C_BUS_PASSIVE|M16C_BUS_OFF):
                            vChd->chipState.state = CHIPSTAT_BUSOFF|CHIPSTAT_ERROR_PASSIVE|CHIPSTAT_ERROR_WARNING;
                            errorCounterChanged = 0;
                            break;

                        default:
                            break;
                    }

                    // Reset is treated like bus-off
                    if (cmd->canErrorEvent.busStatusCh1 & M16C_BUS_RESET) {
                        vChd->chipState.state = CHIPSTAT_BUSOFF;
                        vChd->chipState.txerr = 0;
                        vChd->chipState.rxerr = 0;                            
                        errorCounterChanged = 0;
                    }     
            }
            
            break;
            }
        
        default:
            DEBUGPRINT(1, "UNKNOWN COMMAND - %d\n", cmd->head.cmdNo);
    }

    // wake up those who are waiting for a resp
    list_for_each_safe(currHead, tmpHead, &dev->replyWaitList) 
    {
        currNode = list_entry(currHead, UsbcanWaitNode, list); 
        
        if (currNode->cmdNr == cmd->head.cmdNo && usbcan_get_trans_id(cmd) == currNode->transId) {
            memcpy(currNode->replyPtr, cmd, cmd->head.cmdLen);
            //DEBUGPRINT(1, "wakeup!!!\n");
            os_if_up_sema(&currNode->waitSemaphore);                       
        }
        #if 0 //DEBUG
        else {
        DEBUGPRINT(1, "currNode->cmdNr(%d) == cmd->head.cmdNo(%d) && usbcan_get_trans_id(%d) == currNode->transId(%d)\n",  currNode->cmdNr , cmd->head.cmdNo, usbcan_get_trans_id(cmd),currNode->transId);
        }
        #endif
    }
    
    return 0;
} // _handle_command


//============================================================================
// _get_trans_id
//
static int usbcan_get_trans_id (heliosCmd *cmd)
{
    if (cmd->head.cmdNo > CMD_TX_EXT_MESSAGE) {
        
        // any of the commands
        return cmd->getBusparamsReq.transId;
    }
    else {
        DEBUGPRINT(1, "WARNING wont give a correct transid\n");
        return 0;   
    }
} // _get_trans_id


//============================================================================
// _send 
//
void usbcan_send (void * context)
{
    int              i;
    VCanCardData     *vCard       = (VCanCardData*)context;
    UsbcanCardData   *dev         = (struct UsbcanCardData *)vCard->hwCardData;
    VCanChanData     *vChan       = NULL;
   
    //while (dev->present)
    {
        //wait_for_completion(&dev->rx_needed);
        
        if (!dev->present) {
            // the device was unplugged before the file was released 

            // we cannot deallocate here it is to early
            // and handled elsewhere
            return;
        }
        
        // wait for a previous write to finish up; we don't use a timeout
        // and so a nonresponsive device can delay us indefinitely.
        if (atomic_read (&dev->write_busy))
            os_if_down_sema(&dev->write_finished);
    
            
        // Do we have any cmd to send
        //DEBUGPRINT(1, "cmd queue length: %d (h: %d|t: %d)\n", usbcan_getQLen(dev->txCmdBufHead, dev->txCmdBufTail, KV_USBCAN_TX_CMD_BUF_SIZE), dev->txCmdBufHead, dev->txCmdBufTail );
        if (getQLen(dev->txCmdBufHead, dev->txCmdBufTail, KV_USBCAN_TX_CMD_BUF_SIZE) != 0) {
    
            //DEBUGPRINT(1, "Send cmd nr %d from %d\n", (&dev->txCmdBuffer [dev->txCmdBufTail])->head.cmdNo, dev->txCmdBufTail);                 
            usbcan_transmit(vCard);

            // wake up those who are waiting to send a cmd?
            os_if_wake_up_interruptible(&dev->txCmdWaitQ);
        }
        
        // Process the channel queues (send can-messages)
        for (i = 0; i < vCard->nrChannels; i++){
    
            // Alternate between channels
            vChan = vCard->chanData[i];
            
            // Test if queue is empty or USBcan has sent "queue high"qqq? (TX_CHAN_BUF_SIZE is from vcanosif)
            
            if (getQLen(atomic_read(&vChan->txChanBufHead), atomic_read(&vChan->txChanBufTail), TX_CHAN_BUF_SIZE) == 0) {
                continue;
            }
            usbcan_transmit(vCard);

            // wake up those who are waiting to send a msg
            // (canWriteWait)
            os_if_wake_up_interruptible(&vChan->txChanWaitQ);
        }
        
        //init_completion(&dev->rx_needed);

    }

    return;
} // _send



//============================================================================
//
// _translate_can_msg
// translate from CAN_MSG to heliosCmd 
//
static void usbcan_translate_can_msg (VCanChanData *vChan, heliosCmd *helios_msg, CAN_MSG *can_msg) 
{

    UsbcanCardData     *dev = vChan->vCard->hwCardData;
    // UsbcanChanData     *usbChan  = vChan->hwChanData;    

    // Save a copy of the message.
    dev->current_tx_message[atomic_read(&vChan->transId) - 1] = *can_msg;

    helios_msg->txCanMessage.cmdLen       = sizeof(cmdTxCanMessage);
    helios_msg->txCanMessage.channel      = (unsigned char)vChan->channel;
    helios_msg->txCanMessage.transId      = (unsigned char)atomic_read(&vChan->transId);

    //DEBUGPRINT(1, "can mesg channel:%d transid %d\n", helios_msg->txCanMessage.channel, helios_msg->txCanMessage.transId);
    
    if (can_msg->id & VCAN_EXT_MSG_ID) { // Extended CAN 
        helios_msg->txCanMessage.cmdNo         = CMD_TX_EXT_MESSAGE;
        helios_msg->txCanMessage.rawMessage[0] = (unsigned char) ((can_msg->id >> 24) & 0x1F);
        helios_msg->txCanMessage.rawMessage[1] = (unsigned char) ((can_msg->id >> 18) & 0x3F);
        helios_msg->txCanMessage.rawMessage[2] = (unsigned char) ((can_msg->id >> 14) & 0x0F);
        helios_msg->txCanMessage.rawMessage[3] = (unsigned char) ((can_msg->id >>  6) & 0xFF);
        helios_msg->txCanMessage.rawMessage[4] = (unsigned char) ((can_msg->id      ) & 0x3F);
    }
    else { // Standard CAN 
        helios_msg->txCanMessage.cmdNo         = CMD_TX_STD_MESSAGE;
        helios_msg->txCanMessage.rawMessage[0] = (unsigned char) ((can_msg->id >>  6) & 0x1F);
        helios_msg->txCanMessage.rawMessage[1] = (unsigned char) ((can_msg->id      ) & 0x3F);
    }
    helios_msg->txCanMessage.rawMessage[5]     = can_msg->length & 0x0F;
    memcpy(&helios_msg->txCanMessage.rawMessage[6], can_msg->data, 8);

    //usbChan->outstanding_tx++; // removed because calling fkt sometimes breaks b4 actually queueing 
    //DEBUGPRINT(1,"outstanding(%d)++ id: %d\n", usbChan->outstanding_tx, can_msg->id);
    //DEBUGPRINT(1,"Trans %d, jif %d\n", hChd->outstanding_tx, jiffies);
    
    helios_msg->txCanMessage.flags = can_msg->flags & (VCAN_MSG_FLAG_TX_NOTIFY|
                                                      VCAN_MSG_FLAG_TX_START|
                                                      VCAN_MSG_FLAG_ERROR_FRAME|
                                                      VCAN_MSG_FLAG_REMOTE_FRAME);    
} // _translate_can_msg



//============================================================================
// Fill the buffer with commands from the sw-command-q (for transfer to USB)
// The firmware requires that no command straddle a 64-byte boundary.
// This is because the bulk transfer sends 64 bytes per stage.
//
static int usbcan_fill_usb_buffer (VCanCardData *vCard, unsigned char  *buffer, int maxlen)
{
    int             cmd_bwp    = 0;
    int             msg_bwp    = 0;
    int j;
    
    UsbcanCardData  *dev   = (struct UsbcanCardData *)vCard->hwCardData;
    VCanChanData    *vChan;
    int more_messages_to_send;
    heliosCmd   command;
    int         len;

    // fill buffer with commands
    while (dev->txCmdBufTail != dev->txCmdBufHead) {
        heliosCmd   *commandPtr;
        int         len;
    
        commandPtr = &dev->txCmdBuffer[dev->txCmdBufTail];
        len = commandPtr->head.cmdLen;
        
        DEBUGPRINT(4, "fill buf with cmd nr %d\n", commandPtr->head.cmdNo); 
    
    
        // any space left in the usb buffer?
        if (len > (maxlen - cmd_bwp)) break;
    
        // will this command straddle a 64-byte boundry?
        if ((cmd_bwp & 0XFFFFFFC0) != ((cmd_bwp + len) & 0XFFFFFFC0)) {
              // yes. write a zero here and move the pointer to the next
              // 64-byte boundry
              buffer[cmd_bwp] = 0;
              cmd_bwp = (cmd_bwp + 64) & 0XFFFFFFC0;
              continue;
        }

        memcpy(&buffer[cmd_bwp], commandPtr, len);
        cmd_bwp += len;

#if 0//DEBUG // DEBUG -------------------------------------------------------------------
    {
      int tmp = cmd_bwp - len;
      DEBUGPRINT(1, " SEND [CMD: ");
      for (i = 0; i < len; i++) {
        DEBUGPRINT(1, "%X ", buffer[tmp++]);
      }   
      DEBUGPRINT(1, "]");
      DEBUGPRINT(1, "(%d,%d) ", dev->txCmdBufTail, dev->txCmdBufHead);
      DEBUGPRINT(1, "\n");
    }
#endif       // DEBUG ------------------------------------------------------------------

        dev->txCmdBufTail = (dev->txCmdBufTail + 1) % KV_USBCAN_TX_CMD_BUF_SIZE;
    } // end while
    
    msg_bwp = cmd_bwp;
    
    //DEBUGPRINT(1, "bwp: (%d)\n", msg_bwp);
    
    // add the messages
    for (j=0; j<vCard->nrChannels; j++) {

        UsbcanChanData *usbChan;
        vChan  = (VCanChanData *)vCard->chanData[j];
        usbChan = vChan->hwChanData;
        
        more_messages_to_send = 1;
        
        while (more_messages_to_send) {
            more_messages_to_send = atomic_read(&vChan->txChanBufTail) != atomic_read(&vChan->txChanBufHead);
            
            // make sure we dont write more messages than 
            // we are aloud to the usbcan
            if (!usbcan_tx_available(vChan)) {
                DEBUGPRINT(1, "URB FULL\n");
                return msg_bwp;
            }
            
            if (more_messages_to_send == 0) break;
            
            //heliosCmd   command;
            //int         len;
        
            // get and translate message
            usbcan_translate_can_msg(vChan, &command, &vChan->txChanBuffer[atomic_read(&vChan->txChanBufTail)]);
            
            //command = &vChan->txChanBuffer[vChan->txChanBufTail];
            len = command.head.cmdLen;
        
            // any space left in the usb buffer?
            if (len > (maxlen - msg_bwp)) break;
        
            // will this command straddle a 64-byte boundry?
            if ((msg_bwp & 0XFFFFFFC0) != ((msg_bwp + len) & 0XFFFFFFC0)) {
                  // yes. write a zero here and move the pointer to the next
                  // 64-byte boundry
                  buffer[msg_bwp] = 0;
                  msg_bwp = (msg_bwp + 64) & 0XFFFFFFC0;
                  continue;
            }
    
            
            memcpy(&buffer[msg_bwp], &command, len);
            msg_bwp += len;
            //DEBUGPRINT(1, "memcpy cmdno %d, len %d (%d)\n", command.head.cmdNo, len, msg_bwp);
            //DEBUGPRINT(1, "x\n");
            
            if((atomic_read(&vChan->transId)+1) > dev->max_outstanding_tx) {
                atomic_set(&vChan->transId,1);
            }
            else {
                atomic_add(1, &vChan->transId);
            }

            // have to be here (after all the breaks and continues
            atomic_add(1, &usbChan->outstanding_tx);
            //DEBUGPRINT(1, "t");
            
            atomic_read(&vChan->txChanBufTail) = (atomic_read(&vChan->txChanBufTail) + 1) % TX_CHAN_BUF_SIZE;            
        } // while (more_messages_to_send)
    } 
    
    return msg_bwp;
    
} // _fill_usb_buffer 



//============================================================================
// the actual sending
//
static int usbcan_transmit (VCanCardData *vCard /*, void *cmd*/) 
{
    UsbcanCardData   *dev     = (struct UsbcanCardData *)vCard->hwCardData;
    int              retval   = 0;
    int              fill     = 0;
   
    fill = usbcan_fill_usb_buffer(vCard, dev->write_urb->transfer_buffer, MAX_PACKET_OUT);
    
    if (fill == 0) {
        // no data to send...
        return 0;//qqq return what?   
    }
    
    dev->write_urb->transfer_buffer_length = fill;
    
    os_if_init_sema(&dev->write_finished);
    atomic_set (&dev->write_busy, 1);
    
    if (!dev->present) {
        // the device was unplugged before the file was released 
        // we cannot deallocate here it shouldn't be done from here
        return -ENODEV;
    }

    retval = usb_submit_urb(dev->write_urb, GFP_KERNEL);
    if (retval) {
        atomic_set (&dev->write_busy, 0);
        DEBUGPRINT(1, "%s - failed submitting write urb, error %d",
              __FUNCTION__, retval);
        retval = -1;
    } 
    else {
        retval = sizeof(heliosCmd);
    }
    
    return retval;
} // _transmit



//============================================================================
// _get_card_info
//
static void usbcan_get_card_info (VCanCardData* vCard)
{
    struct UsbcanCardData *dev = (struct UsbcanCardData *)vCard->hwCardData;
    cmdGetCardInfoReq card_cmd;
    heliosCmd reply;

    cmdGetSoftwareInfoReq cmd;
    cmd.cmdLen  = sizeof(cmdGetSoftwareInfoReq);
    cmd.cmdNo   = CMD_GET_SOFTWARE_INFO_REQ;
    cmd.transId = CMD_GET_SOFTWARE_INFO_REQ;
    
    usbcan_send_and_wait_reply(vCard, (heliosCmd*)&cmd, &reply, CMD_GET_SOFTWARE_INFO_RESP, CMD_GET_SOFTWARE_INFO_REQ);
    dev->max_outstanding_tx     =  reply.getSoftwareInfoResp.maxOutstandingTx;
    
    vCard->firmwareVersionMajor = (reply.getSoftwareInfoResp.applicationVersion >> 24) & 0xff;
    vCard->firmwareVersionMinor = (reply.getSoftwareInfoResp.applicationVersion >> 16) & 0xff;
    vCard->firmwareVersionBuild = (reply.getSoftwareInfoResp.applicationVersion) & 0xffff;
    
    //DEBUGPRINT(1, "Using fw version: %d.%d.%d\n", (reply.getSoftwareInfoResp.applicationVersion >> 24) & 0xff, (reply.getSoftwareInfoResp.applicationVersion >> 16) & 0xff, (reply.getSoftwareInfoResp.applicationVersion) & 0xffff);
    DEBUGPRINT(1, "Using fw version: %d.%d.%d\n", vCard->firmwareVersionMajor, vCard->firmwareVersionMinor, vCard->firmwareVersionBuild);

    card_cmd.cmdLen  = sizeof(cmdGetCardInfoReq);
    card_cmd.cmdNo   = CMD_GET_CARD_INFO_REQ;
    card_cmd.transId = CMD_GET_CARD_INFO_REQ;
    
    usbcan_send_and_wait_reply(vCard, (heliosCmd*)&card_cmd, &reply, CMD_GET_CARD_INFO_RESP, CMD_GET_CARD_INFO_REQ);
    DEBUGPRINT(1, "channels: %d, s/n: %d, hwrev: %u\n", reply.getCardInfoResp.channelCount, (int)reply.getCardInfoResp.serialNumberLow, (unsigned int)reply.getCardInfoResp.hwRevision);
    vCard->nrChannels   = reply.getCardInfoResp.channelCount;
    vCard->serialNumber = reply.getCardInfoResp.serialNumberLow;
        
} // _get_card_info



//============================================================================
//  response_timeout
//  used in usbcan_send_and_wait_reply
static void usbcan_response_timer (unsigned long voidWaitNode)
{
    UsbcanWaitNode *waitNode = (UsbcanWaitNode*) voidWaitNode;
    waitNode->timedOut = 1;
    os_if_up_sema(&waitNode->waitSemaphore);
    return;
} // response_timeout



//============================================================================
//  usbcan_send_and_wait_reply
//  send a heliosCmd and wait for the usbcan to answear. 
//
static int usbcan_send_and_wait_reply (VCanCardData *vCard, heliosCmd *cmd, heliosCmd *replyPtr, unsigned char cmdNr, unsigned char transId)
{    
    UsbcanCardData     *dev = vCard->hwCardData;
    struct timer_list  waitTimer;
    UsbcanWaitNode     waitNode;
    int                ret;

    // maybe return something different...
    if (vCard == NULL) return -ENODEV;
 
    // se if dev is present
    if (!dev->present) {
        return -ENODEV;   
    }
          
    os_if_init_sema(&waitNode.waitSemaphore);
    waitNode.replyPtr  = replyPtr;
    waitNode.cmdNr     = cmdNr;
    waitNode.transId   = transId;
    waitNode.timedOut  = 0;
 
       // Add to card's list of expected responses 
    //spin_lock_irqsave(&hCard->replyWaitListLock, flags);
    list_add(&waitNode.list, &dev->replyWaitList);
    //spin_unlock_irqrestore(&hCard->replyWaitListLock, flags);
 
    ret = usbcan_queue_cmd(vCard, cmd, USBCAN_Q_CMD_WAIT_TIME);
    if (ret != 0) {
        // qqq write lock?  
        //write_lock_irqsave(&hCard->replyWaitListLock, flags);
        list_del(&waitNode.list);
        //write_unlock_irqrestore(&hCard->replyWaitListLock, flags);            
        return ret;
    }

    //DEBUGPRINT(1, "b4 init timer\n");
    init_timer(&waitTimer);
    waitTimer.function  = usbcan_response_timer;
    waitTimer.data      = (unsigned long) &waitNode;
    waitTimer.expires   = jiffies + (USBCAN_CMD_RESP_WAIT_TIME * HZ)/1000;
    add_timer(&waitTimer);

    os_if_down_sema(&waitNode.waitSemaphore);
    // Now we either got a response or a timeout
    // qqq spinlock?
    //spin_lock_irqsave(&vCard->replyWaitListLock, flags);
    list_del(&waitNode.list);
    //spin_unlock_irqrestore(&vCard->replyWaitListLock, flags);
    del_timer_sync(&waitTimer);
    
    //DEBUGPRINT(1, "after del timer\n");
    if (waitNode.timedOut) {
        DEBUGPRINT(1, "WARNING: waiting for response(%d) timed out! \n", waitNode.cmdNr);
        return VCAN_STAT_TIMEOUT;
    }

    return VCAN_STAT_OK;    
} // _send_and_wait_reply



//============================================================================
//  usbcan_queue_cmd
//  put the command in the command queue
//
static int usbcan_queue_cmd (VCanCardData *vCard, heliosCmd *cmd, unsigned int timeout)
{
    struct UsbcanCardData *dev = (struct UsbcanCardData *)vCard->hwCardData;
    OS_IF_WAITQUEUE wait;
    os_if_init_waitqueue_entry(&wait);
    os_if_add_wait_queue(&dev->txCmdWaitQ, &wait);
    
    // Sleep when buffer is full and timeout > 0
    while(1) {
        //DEBUGPRINT(1, "cmd queue len: %d (h: %u|t: %u)\n", getQLen(dev->txCmdBufHead, dev->txCmdBufTail, KV_USBCAN_TX_CMD_BUF_SIZE), (unsigned int)dev->txCmdBufHead, (unsigned int)dev->txCmdBufTail );
        // buffer full
        if (getQLen(dev->txCmdBufHead, dev->txCmdBufTail, KV_USBCAN_TX_CMD_BUF_SIZE) >= KV_USBCAN_TX_CMD_BUF_SIZE - 1) {
            
            // Do we want a timeout ?
            if (timeout == 0){
                os_if_remove_wait_queue(&dev->txCmdWaitQ, &wait);         
                DEBUGPRINT(1, "ERROR 1 -EAGAIN\n");
                return -EAGAIN;
            } else {
                os_if_set_task_interruptible();
                
                //
                //if(!signal_pending(current)) {
                //
                
                if (os_if_wait_for_event_timeout(timeout*HZ/1000, &wait) == 0) {
                    // Sleep was interrupted by timer
                    // set task running?
                    os_if_remove_wait_queue(&dev->txCmdWaitQ, &wait);         
                    DEBUGPRINT(1, "ERROR 2 -EAGAIN\n");
                    return -EAGAIN;
                }
                
                //
                /*
                continue;
                }
                os_if_remove_wait_queue(&vCard->txCmdWaitQ, &wait);
                return -ERESTARTSYS;
                */
                //
            }
            //
            if(signal_pending(current)){
                os_if_remove_wait_queue(&dev->txCmdWaitQ, &wait);
                DEBUGPRINT(1, "ERROR 3 -ERESTARTSYS\n");
                return -ERESTARTSYS;
            }
            continue;
            
            //
        } 
        else { // buffer NOT full
            // Get a pointer to the right bufferspace 
            // Lock needed for SMP 
            heliosCmd *bufCmdPtr = (heliosCmd*)&dev->txCmdBuffer[dev->txCmdBufHead];
            memcpy (bufCmdPtr, cmd, cmd->head.cmdLen);
            dev->txCmdBufHead++;
            if (dev->txCmdBufHead >= KV_USBCAN_TX_CMD_BUF_SIZE)
                dev->txCmdBufHead = 0;
            //os_if_set_task_running(); // qqq
            
            // wake up the rx-thread
            //complete(&dev->rx_needed);
            //queue_work(dev->txTaskQ, &dev->txWork); //mh dec08
            os_if_queue_task_not_default_queue(dev->txTaskQ, &dev->txWork);
            os_if_remove_wait_queue(&dev->txCmdWaitQ, &wait);
            
            break;
        }
    }

    return VCAN_STAT_OK;
} // _queue_cmd


//============================================================================
//  usbcan_plugin
//
//  Called by the usb core when a new device is connected that it thinks
//  this driver might be interested in.
//  Also allocates card info struct mem space and starts workqueues
//
static int usbcan_plugin (struct usb_interface *interface, const struct usb_device_id *id)
{
    struct usb_device               *udev          = interface_to_usbdev(interface);
    struct usb_host_interface       *iface_desc;
    struct usb_endpoint_descriptor  *endpoint;
    size_t                          buffer_size;
    int                             i;
    int                             retval         = -ENOMEM;
    VCanCardData                    *vCard;
    UsbcanCardData                  *dev;

    // See if the device offered us matches what we can accept 
    // add here for more devices
    if (
    (udev->descriptor.idVendor != KVASER_VENDOR_ID) || 
        (
        (udev->descriptor.idProduct != USB_USBCAN2_PRODUCT_ID) && 
        (udev->descriptor.idProduct != USB_USBCAN_REVB_PRODUCT_ID) && 
        (udev->descriptor.idProduct != USB_MEMORATOR_PRODUCT_ID) 
        )
    ) 
    {
        return -ENODEV;
    }
    
    #if DEBUG
     DEBUGPRINT(1, "\nKVASER ");            
     switch (udev->descriptor.idProduct) {
        case USB_USBCAN2_PRODUCT_ID:
        DEBUGPRINT(1, "USBcanII plugged in\n");            
        break;
        case USB_USBCAN_REVB_PRODUCT_ID:
        DEBUGPRINT(1, "USBcan rev B plugged in\n");            
        break;
        case USB_MEMORATOR_PRODUCT_ID:
        DEBUGPRINT(1, "Memorator plugged in\n");            
        break;
        default:
        DEBUGPRINT(1, "UNKNOWN product plugged in\n");            
        break;
    }
    #endif
    
    // allocate datastructures for the card
    usbcan_allocate(&vCard);
   
    dev = vCard->hwCardData;
    init_MUTEX (&((UsbcanCardData*)vCard->hwCardData)->sem);
    dev->udev = udev;
    dev->interface = interface;
    
    // set up the endpoint information 
    // check out the endpoints 
    // use only the first bulk-in and bulk-out endpoints 
    iface_desc = &interface->altsetting[0];
    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
        endpoint = &iface_desc->endpoint[i].desc;

        if (!dev->bulk_in_endpointAddr &&
              (endpoint->bEndpointAddress & USB_DIR_IN) &&
              ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
               == USB_ENDPOINT_XFER_BULK)) {
            // we found a bulk in endpoint 
            //buffer_size = endpoint->wMaxPacketSize;
            buffer_size = MAX_PACKET_OUT;
            dev->bulk_in_size = buffer_size;
            dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
            dev->bulk_in_buffer = kmalloc (buffer_size, GFP_KERNEL);
            DEBUGPRINT(1, "MALLOC bulk_in_buffer\n");
            if (!dev->bulk_in_buffer) {
                DEBUGPRINT(1, "Couldn't allocate bulk_in_buffer");
                goto error;
            }
            memset(dev->bulk_in_buffer, 0, sizeof(dev->bulk_in_buffer));
        }

        if (!dev->bulk_out_endpointAddr &&
              !(endpoint->bEndpointAddress & USB_DIR_IN) &&
              ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
               == USB_ENDPOINT_XFER_BULK)) {
            // we found a bulk out endpoint 
            // a probe() may sleep and has no restrictions on memory allocations 
            dev->write_urb = usb_alloc_urb(0, GFP_KERNEL);
            if (!dev->write_urb) {
                DEBUGPRINT(1, "No free urbs available");
                goto error;
            }
            dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;

            // on some platforms using this kind of buffer alloc
            // call eliminates a dma "bounce buffer".
            //
            // NOTE: you'd normally want i/o buffers that hold
            // more than one packet, so that i/o delays between
            // packets don't hurt throughput.
            //
            buffer_size = endpoint->wMaxPacketSize;
            dev->bulk_out_size = buffer_size;
            dev->write_urb->transfer_flags = (URB_NO_TRANSFER_DMA_MAP/* |
                                              URB_ASYNC_UNLINK*/);
            dev->bulk_out_buffer = usb_buffer_alloc (udev,
                buffer_size, GFP_KERNEL,
                &dev->write_urb->transfer_dma);
            if (!dev->bulk_out_buffer) {
                DEBUGPRINT(1, "Couldn't allocate bulk_out_buffer");
                goto error;
            }
            usb_fill_bulk_urb(dev->write_urb, udev,
                              usb_sndbulkpipe(udev,
                                              endpoint->bEndpointAddress),
                              dev->bulk_out_buffer, buffer_size,
                              usbcan_write_bulk_callback, vCard);
        }
    }
    if (!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)) {
        DEBUGPRINT(1, "Couldn't find both bulk-in and bulk-out endpoints");
        goto error;
    }

    // allow device read, write and ioctl 
    dev->present = 1;

    // we can register the device now, as it is ready 
    usb_set_intfdata (interface, vCard);
    retval = usb_register_dev (interface, &usbcan_class);
    if (retval) {
        // something prevented us from registering this driver 
        DEBUGPRINT(1, "Not able to get a minor for this device.");
        usb_set_intfdata (interface, NULL);
        goto error;
    }

    // set the number on the channels
     for (i = 0; i < MAX_CHANNELS; i++) {
        VCanChanData   *vChd = vCard->chanData[i];
        vChd->channel  = i;  
     }
    
    
    // let the user know what node this device is now attached to 
    DEBUGPRINT(1, "------------------------------\n");
    DEBUGPRINT(1, "USBcanII device now attached\n");
    DEBUGPRINT(1, "using driver built %s\n", __TIME__);
    DEBUGPRINT(1, "on %s\n", __DATE__);
    DEBUGPRINT(1, "------------------------------\n");

    // start up vital stuff    
    usbcan_start(vCard);
    
    return 0;

error:
    DEBUGPRINT(1, "_deallocate from usbcan_plugin\n");
    usbcan_deallocate(vCard);
    return retval;

} // usbcan_plugin



//========================================================================
//
// init stuff, called from end of _plugin
//
static int usbcan_start (VCanCardData *vCard)
{
    struct UsbcanCardData *dev = (struct UsbcanCardData *)vCard->hwCardData;
    int ret;
    
    // initialize queqes/waitlists for commands
    INIT_LIST_HEAD(&dev->replyWaitList);
    os_if_init_waitqueue_head(&dev->txCmdWaitQ);
    
    // default taskqueue used when sending
    //os_if_init_task(&dev->txTaskQ, &usbcan_send, vCard);
    //init_completion(&dev->rx_needed);
    
    // init the lock for the hi part of the timestamps
    dev->timeHi_lock    = SPIN_LOCK_UNLOCKED;

                
    os_if_init_task(&dev->txWork, &usbcan_send, vCard);
    //dev->txTaskQ = create_workqueue("usbcan_tx");
    dev->txTaskQ = os_if_declare_task("usbcan_tx");
    //queue_delayed_work(dev->txTaskQ, &dev->txWork, 1);
    
    /*
    // not default workqueue used as receive tread
    INIT_WORK(&dev->rxWork, &usbcan_rx_thread, vCard);
    dev->rxTaskQ = create_workqueue("usbcan_rx");
    queue_delayed_work(dev->rxTaskQ, &dev->rxWork, 1);
    */
 
    //ret = kernel_thread(usbcan_send, vCard, /*CLONE_FS | CLONE_FILES*/ CLONE_KERNEL);  
    ret = kernel_thread(usbcan_rx_thread, vCard, /*CLONE_FS | CLONE_FILES*/ CLONE_KERNEL);  
      
    // gather some card info
    usbcan_get_card_info(vCard);    
    DEBUGPRINT(1, "vcard chnr: %d\n", vCard->nrChannels);
    vCanInitData(vCard);
    
    return 0;
} // _start


//========================================================================
//
// allocates space for card structs
//
static int usbcan_allocate (VCanCardData **in_vCard)
{
    // Helper struct for allocation 
    typedef struct {
        VCanChanData    *dataPtrArray[MAX_CHANNELS];
        VCanChanData    vChd[MAX_CHANNELS];
        UsbcanChanData  hChd[MAX_CHANNELS];
    } ChanHelperStruct;
    
    int              chNr;
    ChanHelperStruct *chs;
    VCanCardData     *vCard;

    DEBUGPRINT(1, "_allocate\n");
    
    // Allocate data area for this card 
    vCard  = kmalloc(sizeof(VCanCardData) + sizeof(UsbcanCardData), GFP_KERNEL);
    DEBUGPRINT(1, "MALLOC _allocate\n");
    if (!vCard) {
        goto card_alloc_err;
    }
    memset(vCard, 0, sizeof(VCanCardData) + sizeof(UsbcanCardData));

    // hwCardData is directly after VCanCardData 
    vCard->hwCardData = vCard + 1;    

    // Allocate memory for n channels 
    chs = kmalloc(sizeof(ChanHelperStruct), GFP_KERNEL);
    DEBUGPRINT(1, "MALLOC _alloc helperstruct\n");
    if (!chs) {
        goto chan_alloc_err;
    }
    memset(chs, 0, sizeof(ChanHelperStruct));

    // Init array and hwChanData 
    for (chNr = 0; chNr < MAX_CHANNELS; chNr++){
        chs->dataPtrArray[chNr] = &chs->vChd[chNr];
        chs->vChd[chNr].hwChanData = &chs->hChd[chNr];
        chs->vChd[chNr].vCard = vCard;
    }
    vCard->chanData = chs->dataPtrArray;
    //UsbcanCardData* dev = vCard->hwCardData;

    os_if_spin_lock(&canCardsLock);
    // Insert into list of cards 
    vCard->next = canCards;
    canCards = vCard;
    os_if_spin_unlock(&canCardsLock);

    *in_vCard = vCard;
    
    return 0;
    
chan_alloc_err:
    DEBUGPRINT(1, "alloc error");
    kfree(vCard);
    DEBUGPRINT(1, "KFREE alloc_err\n");

card_alloc_err:
    DEBUGPRINT(1, "alloc error");
    return 1;
} // _allocate




//============================================================================
// usbcan_deallocate
//

static void usbcan_deallocate (struct VCanCardData *vCard)
{
    struct UsbcanCardData *dev = (struct UsbcanCardData *)vCard->hwCardData;
    VCanCardData *local_vCard;

    DEBUGPRINT(1, "_deallocate\n");

    // make sure all workqueues are finished
    //flush_workqueue(&dev->rxTaskQ);
    //flush_workqueue(&dev->txTaskQ);

    if(dev->bulk_in_buffer != NULL) {
        kfree (dev->bulk_in_buffer);
        DEBUGPRINT(1, "KFREE bulk_in_buffer\n");
        dev->bulk_in_buffer = NULL;
    }
    usb_buffer_free (dev->udev, dev->bulk_out_size,
                     dev->bulk_out_buffer,
                     dev->write_urb->transfer_dma);
    usb_free_urb (dev->write_urb);

    os_if_spin_lock(&canCardsLock);
    
    // qqq check for open files 
    local_vCard = canCards;
    
    while (local_vCard) {
        local_vCard = canCards->next;
        if(canCards->chanData != NULL) {
            kfree(canCards->chanData);
            DEBUGPRINT(1, "KFREE cancards->chanData\n");
            canCards->chanData = NULL;
        }
        if(canCards != NULL) {
            kfree(canCards);
            DEBUGPRINT(1, "KFREE cancards\n");
            canCards = NULL;
        }
        canCards = local_vCard;
    }
    os_if_spin_unlock(&canCardsLock);
    return;

} // _deallocate


//============================================================================
//     usbcan_remove
// 
//     Called by the usb core when the device is removed from the system.
// 
//     This routine guarantees that the driver will not submit any more urbs
//     by clearing dev->udev.  It is also supposed to terminate any currently
//     active urbs.  Unfortunately, usb_bulk_msg(), does not provide any way 
//     to do this.  But at least we can cancel an active write.
// 
static void usbcan_remove (struct usb_interface *interface)
{
    VCanCardData *vCard;
    VCanChanData *vChan;
    UsbcanCardData *dev;

    DEBUGPRINT(1, "_remove\n");
    
    vCard = usb_get_intfdata (interface);
    usb_set_intfdata (interface, NULL);
    vChan = vCard->chanData[0]; // any channel
    while (vChan->fileOpenCount > 0) {
        schedule_timeout(10);   
    }
    
    dev = vCard->hwCardData;

    // prevent device read, write and ioctl 
    // this is usually set in _rx_thread when we 
    // fail to read an urb, but we do it here to make sure
    dev->present = 0;
    
    // terminate workqueues
    flush_scheduled_work();
    //flush_workqueue(&dev->txTaskQ);

    // give back our minor 
    usb_deregister_dev (interface, &usbcan_class);

    // terminate an ongoing write 
    if (atomic_read (&dev->write_busy)) {
        usb_unlink_urb (dev->write_urb);
        os_if_down_sema (&dev->write_finished);
    }

    // flush and destroy rx workqueue
    //destroy_workqueue(dev->rxTaskQ);
    DEBUGPRINT(1, "destroy_workqueue\n");
    os_if_destroy_task(dev->txTaskQ);
    
    // deallocate datastructures
    usbcan_deallocate(vCard);

    DEBUGPRINT(1, "USBcanII  now disconnected");

} // _remove




//======================================================================
//
// Set bit timing   
//
int usbcan_set_busparams (VCanChanData *vChan, VCanBusParams *par)
{
    heliosCmd        cmd;
    UsbcanCardData   *dev = vChan->hwChanData;
    unsigned long    tmp;
    VCanCardData *vCard;

    DEBUGPRINT(1, "_set_busparam\n");
    
    cmd.setBusparamsReq.cmdNo   = CMD_SET_BUSPARAMS_REQ;
    cmd.setBusparamsReq.cmdLen  = sizeof(cmdSetBusparamsReq);
    cmd.setBusparamsReq.bitRate = par->freq;
    cmd.setBusparamsReq.sjw     = (unsigned char) par->sjw;
    cmd.setBusparamsReq.tseg1   = (unsigned char) par->tseg1;
    cmd.setBusparamsReq.tseg2   = (unsigned char) par->tseg2;
    cmd.setBusparamsReq.channel = (unsigned char)vChan->channel;
    cmd.setBusparamsReq.noSamp  = 1; // qqq can't be trusted: (BYTE) pi->chip_param.samp3
    
    // Check bus parameters
    tmp = par->freq * (par->tseg1 + par->tseg2 + 1);
    if (tmp == 0) {
      DEBUGPRINT(1, "usbcan: _set_busparams() tmp==0!\n");
      return -1;
    }
    if ((8000000/tmp) > 16) {
      DEBUGPRINT(1, "usbcan: _set_busparams() prescaler wrong\n");
      return -1;
    }
    
     // store locally since getBusParams not correct
    dev->freq    = (unsigned long) par->freq;
    dev->sjw     = (unsigned char) par->sjw;
    dev->tseg1   = (unsigned char) par->tseg1;
    dev->tseg2   = (unsigned char) par->tseg2;
    dev->samples = 1; // always 1
    
    vCard = vChan->vCard;
    usbcan_queue_cmd(vCard, &cmd, 5 /* there is no response */);
    
    return VCAN_STAT_OK;
} // _set_busparams 



//======================================================================
//
//  Get bit timing                                                      
//
int usbcan_get_busparams (VCanChanData *vChan, VCanBusParams *par)
{
    UsbcanCardData   *dev = vChan->hwChanData;

    DEBUGPRINT(1, "_getbusparam\n");
    
    par->freq    = dev->freq; 
    par->sjw     = dev->sjw;
    par->tseg1   = dev->tseg1;
    par->tseg2   = dev->tseg2;
    par->samp3   = dev->samples; // always 1

    // this returns wrong values...
    // ret = usbcan_send_and_wait_reply(vCard, (heliosCmd*)&cmd, &reply, CMD_GET_SOFTWARE_INFO_RESP, CMD_GET_SOFTWARE_INFO_REQ);    
    
    return VCAN_STAT_OK;

} // _get_busparams 


//======================================================================
//
//  Set silent or normal mode                                           
//
int usbcan_set_silent (VCanChanData *vChan, int silent)
{
    //    UsbcanCardData  *vCard   =  vChan->hwChanData;
    heliosCmd       cmd;

    DEBUGPRINT(1, "_set_silent\n");

    cmd.setDrivermodeReq.cmdNo      = CMD_SET_DRIVERMODE_REQ;
    cmd.setDrivermodeReq.cmdLen     = sizeof(cmdSetDrivermodeReq);
    cmd.setDrivermodeReq.channel    = (unsigned char)vChan->channel;
    cmd.setDrivermodeReq.driverMode = silent? DRIVERMODE_SILENT : DRIVERMODE_NORMAL;

    usbcan_queue_cmd(vChan->vCard, &cmd, 5 /* there is no response */);

    return VCAN_STAT_OK;
} // _set_silent 


//======================================================================
//
//  Line mode                                                           
//
int usbcan_set_trans_type (VCanChanData *vChan, int linemode, int resnet)
{
    int ret = 0;
    //qqq not implemented
    DEBUGPRINT(1, "usbcan_set_trans_type is NOT implemented!\n");

    return ret;
} // _set_trans_type 




//======================================================================
//
//  Query chip status                                                   
//
int usbcan_get_chipstate (VCanChanData *vChan)
{
    VCanCardData   *vCard = vChan->vCard;
    //VCAN_EVENT     msg;
    heliosCmd      cmd;
    heliosCmd      reply;
    int            ret = 0;
    // UsbcanCardData *dev = vCard->hwCardData;

    DEBUGPRINT(1, "_getchipstate\n");
    
    cmd.head.cmdNo              = CMD_GET_CHIP_STATE_REQ;
    cmd.getChipStateReq.cmdLen  = sizeof(cmdGetChipStateReq);
    cmd.getChipStateReq.channel = (unsigned char)vChan->channel;
    cmd.getChipStateReq.transId = (unsigned char)vChan->channel;

    ret = usbcan_send_and_wait_reply(vCard, (heliosCmd*)&cmd, &reply, CMD_CHIP_STATE_EVENT, CMD_GET_CHIP_STATE_REQ);    
    if (ret) return ret;
        
    /*    
    msg.tag = V_CHIP_STATE;
    msg.timeStamp = (reply.chipStateEvent.time + vCard->timeHi) / USBCANII_TICKS_PER_MS;
    msg.transId = 0;
    msg.tagData.chipState.busStatus = (unsigned char) vChan->chipState.state;
    msg.tagData.chipState.txErrorCounter = (unsigned char) vChan->chipState.txerr;
    msg.tagData.chipState.rxErrorCounter = (unsigned char) vChan->chipState.rxerr;
    
    vCanDispatchEvent(vChan, &msg); // qqq
    */
    return VCAN_STAT_OK;
} // _get_chipstate 



//======================================================================
//
//  Go bus on                                                           
//
int usbcan_bus_on (VCanChanData *vChan)
{
    VCanCardData    *vCard    = vChan->vCard;
    UsbcanChanData  *usbChan  = vChan->hwChanData;
    UsbcanCardData  *dev      = vCard->hwCardData;
    heliosCmd cmd;
    heliosCmd reply;
    int ret = 0;

    memset (dev->current_tx_message, 0, sizeof (dev->current_tx_message));
    atomic_set(&usbChan->outstanding_tx, 0);
    atomic_set(&vChan->transId, 1);
    
    cmd.head.cmdNo            = CMD_START_CHIP_REQ;
    cmd.startChipReq.cmdLen   = sizeof(cmdStartChipReq);
    cmd.startChipReq.channel  = (unsigned char)vChan->channel;
    cmd.startChipReq.transId  = (unsigned char)vChan->channel;
    
    DEBUGPRINT(1, "bus on called - ch %d\n", cmd.startChipReq.channel);
    
    ret = usbcan_send_and_wait_reply(vCard, (heliosCmd*)&cmd, &reply, CMD_START_CHIP_RESP, cmd.startChipReq.transId);    
    if (ret) return ret;
    
    vChan->isOnBus = 1;

    usbcan_get_chipstate(vChan);

    return VCAN_STAT_OK;
} // _bus_on 


//======================================================================
//
//  Go bus off                                                           
//
int usbcan_bus_off (VCanChanData *vChan)
{
    VCanCardData *vCard = vChan->vCard;
    UsbcanChanData *usbChan  = vChan->hwChanData;
    UsbcanCardData *dev = vCard->hwCardData;
    
    heliosCmd cmd;
    heliosCmd reply;
    int ret = 0;

    cmd.head.cmdNo            = CMD_STOP_CHIP_REQ;
    cmd.startChipReq.cmdLen   = sizeof(cmdStartChipReq);
    cmd.startChipReq.channel  = (unsigned char)vChan->channel;
    cmd.startChipReq.transId  = (unsigned char)vChan->channel;
    
    ret = usbcan_send_and_wait_reply(vCard, (heliosCmd*)&cmd, &reply, CMD_STOP_CHIP_RESP, cmd.startChipReq.transId);    
    if (ret) return ret;

    usbcan_get_chipstate(vChan);

    DEBUGPRINT(1, "bus off channel %d\n", cmd.startChipReq.channel);

    vChan->isOnBus = 0;
    vChan->chipState.state = CHIPSTAT_BUSOFF;
    memset(dev->current_tx_message, 0, sizeof (dev->current_tx_message));

    atomic_set(&usbChan->outstanding_tx, 0);
    atomic_set(&vChan->transId, 1);
    
    return VCAN_STAT_OK;

} // _bus_off 



//======================================================================
//
//  Clear send buffer on card                                           
//
int usbcan_flush_tx_buffer (VCanChanData *vChan)
{
    UsbcanChanData     *usbChan  = vChan->hwChanData;
    //UsbcanCardData *dev      = vChan->vCard->hwCardData;
    //VCanCardData   *vCard    = vChan->vCard;
    heliosCmd cmd;
    
    DEBUGPRINT(1, "usbcan_flush_tx_buffer - %d\n", vChan->channel);
    
    cmd.head.cmdNo         = CMD_FLUSH_QUEUE;
    cmd.flushQueue.cmdLen  = sizeof (cmd.flushQueue);
    cmd.flushQueue.channel = (unsigned char)vChan->channel;
    cmd.flushQueue.flags   = 0;

    usbcan_queue_cmd(vChan->vCard, &cmd, 5 /* there is no response */);
    
    atomic_set(&vChan->transId, 1);    
    atomic_set(&usbChan->outstanding_tx, 0);
    atomic_set(&vChan->txChanBufHead, 0);
    atomic_set(&vChan->txChanBufTail, 0);
    return VCAN_STAT_OK;
    
} // _flush_tx_buffer 


//======================================================================
//
// Request send                                                         
//
int usbcan_schedule_send (VCanCardData *vCard, VCanChanData *vChan)
{
    UsbcanCardData *dev = vCard->hwCardData;

    //DEBUGPRINT(1, "_shcedule_send\n");

    if (usbcan_tx_available(vChan) && dev->present) {
        //complete(&dev->rx_needed);
        os_if_queue_task_not_default_queue(dev->txTaskQ, &dev->txWork);
    }
    #if DEBUG
    else {
        DEBUGPRINT(1, "SEND FAILED \n");
        return -1;   
    }
    #endif
    
    return 0;
} // _schedule_send



//======================================================================
//  Read transmit error counter                                         
//
int usbcan_get_tx_err(VCanChanData *vChan) 
{
    usbcan_get_chipstate(vChan);
    return vChan->chipState.txerr;
    //return vChan->txErrorCounter;
} //_get_tx_err


//======================================================================
//  Read transmit error counter                                         
//
int usbcan_get_rx_err(VCanChanData *vChan) 
{
    usbcan_get_chipstate(vChan);
    return vChan->chipState.rxerr;
    //return vChan->rxErrorCounter;
} // _get_rx_err


//======================================================================
//  Read receive queue length in hardware/firmware
//
unsigned long usbcan_get_hw_rx_q_len (VCanChanData *vChan) 
{
    return getQLen(atomic_read(&vChan->txChanBufHead),
        atomic_read(&vChan->txChanBufTail), TX_CHAN_BUF_SIZE);
} // _get_hw_rx_q_len 


//======================================================================
//  Read transmit queue length in hardware/firmware                     
//
unsigned long usbcan_get_hw_tx_q_len (VCanChanData *vChan) 
{  
    UsbcanChanData *hChd  = vChan->hwChanData;
    return atomic_read(&hChd->outstanding_tx);
} //_get_hw_tx_q_len



//======================================================================
// compose msg and transmit                                         
//
int usbcan_translate_and_send_message(VCanChanData *vChan, CAN_MSG *m)
{
    // not used
    DEBUGPRINT(1, "usbcan_translate_and_send_message PLEASE, PLEASE implement me!!\n");
    return VCAN_STAT_OK;
}


//======================================================================
//
// Run when driver is loaded                                            
//
int usbcan_init_driver (void)
{
    int result = 0;

    // register this driver with the USB subsystem 
    result = usb_register(&usbcan_driver);
    if (result) {
        DEBUGPRINT(1, "usb_register failed. Error number %d",
              result);
        return result;
    }
    return 0;

} // _init_driver



//======================================================================
// Run when driver is unloaded                                          
//
int usbcan_close_all (void)
{
    DEBUGPRINT(1, "_close_all (deregister driver..)");
    usb_deregister(&usbcan_driver);
    
    return 0;    
} // _close_all



//======================================================================
// proc read function                                                  
//
int usbcan_proc_read (char *buf, char **start, off_t offset, int count, int *eof, void *data)
{
    int len = 0;
    len += sprintf(buf+len,"\ntotal channels %d\n", driverData.minorNr);
    *eof = 1;
    return len;
} // _proc_read


//======================================================================
//  Can we send now?                                                    
//
static int usbcan_tx_available (VCanChanData *vChan)
{
    UsbcanChanData     *usbChan  = vChan->hwChanData;
    VCanCardData       *vCard    = vChan->vCard;
    UsbcanCardData     *dev      = vCard->hwCardData;
    
    //DEBUGPRINT(1, "usbcan_tx_available %d!\n", usbChan->outstanding_tx);
    return (atomic_read(&usbChan->outstanding_tx) < dev->max_outstanding_tx);   
} // _tx_available 


//======================================================================
//  are all sent msg's received?
//
int usbcan_outstanding_sync (VCanChanData *vChan)
{
    UsbcanChanData     *usbChan  = vChan->hwChanData;
    
    return (atomic_read(&usbChan->outstanding_tx) == 0);    
} // _tx_available 



//======================================================================
// Get time
//
unsigned long usbcan_get_time(VCanCardData *vCard)
{
    heliosCmd cmd;
    heliosCmd reply;
    int ret = 0;
    unsigned long time;

    // DEBUGPRINT(1, "_get_time\n");

    memset(&cmd, 0, sizeof(cmd));
    cmd.head.cmdNo           = CMD_READ_CLOCK_REQ;
    cmd.readClockReq.cmdLen  = sizeof (cmd.readClockReq);
    cmd.readClockReq.flags   = 0;

    // CMD_READ_CLOCK_RESP seem to always return 0 as transid
    ret = usbcan_send_and_wait_reply(vCard, (heliosCmd*)&cmd, &reply, CMD_READ_CLOCK_RESP, 0);    
    if (ret) return ret;

    time = (reply.readClockResp.time[1] << 16) + reply.readClockResp.time[0];
    //DEBUGPRINT(1, "time %d\n", (time/USBCANII_TICKS_PER_MS));

    return (time/USBCANII_TICKS_PER_MS);
}





