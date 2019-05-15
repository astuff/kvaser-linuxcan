/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

#ifndef _CANSTAT_H_
#define _CANSTAT_H_
//
// Don't forget to update canGetErrorText in canlib.c if this is changed!
//
typedef enum {
    canOK                  = 0,
    canERR_PARAM           = -1,     // Error in parameter
    canERR_NOMSG           = -2,     // No messages available
    canERR_NOTFOUND        = -3,     // Specified hw not found
    canERR_NOMEM           = -4,     // Out of memory
    canERR_NOCHANNELS      = -5,     // No channels avaliable
    canERR_INTERRUPTED     = -6,     // Interrupted by signal
    canERR_TIMEOUT         = -7,     // Timeout ocurred
    canERR_NOTINITIALIZED  = -8,     // Lib not initialized
    canERR_NOHANDLES       = -9,     // Can't get handle
    canERR_INVHANDLE       = -10,    // Handle is invalid
    canERR_DRIVER          = -12,    // CAN driver type not supported
    canERR_TXBUFOFL        = -13,    // Transmit buffer overflow
    canERR_RESERVED_1      = -14,
    canERR_HARDWARE        = -15,    // Some hardware error has occurred
    canERR_RESERVED_4      = -19,
    canERR_RESERVED_5      = -20,
    canERR_RESERVED_6      = -21,
    canERR_RESERVED_2      = -22,
    canERR_DRIVERLOAD      = -23,    // Can't find/load driver
    canERR_NOCONFIGMGR     = -25,    // Can't find req'd config s/w (e.g. CS/SS)
    canERR_NOCARD          = -26,    // The card was removed or not inserted
    canERR_RESERVED_7      = -27,
    // The last entry - a dummy so we know where NOT to place a comma.
    canERR__RESERVED       = -31
} canStatus;


#define CANSTATUS_SUCCESS(X) ((X) == canOK)
#define CANSTATUS_FAILURE(X) ((X) != canOK)


//
// Notification codes; appears in the notification message.
//
#define canEVENT_RX             32000       // Receive event
#define canEVENT_TX             32001       // Transmit event
#define canEVENT_ERROR          32002       // Error event
#define canEVENT_STATUS         32003       // Change-of-status event

//
// These are used in the call to canSetNotify().
//
#define canNOTIFY_RX            0x0001      // Notify on receive
#define canNOTIFY_TX            0x0002      // Notify on transmit
#define canNOTIFY_ERROR         0x0004      // Notify on error
#define canNOTIFY_STATUS        0x0008      // Notify on (some) status changes

//
// Circuit status flags.
//
#define canSTAT_ERROR_PASSIVE   0x00000001  // The circuit is error passive
#define canSTAT_BUS_OFF         0x00000002  // The circuit is Off Bus
#define canSTAT_ERROR_WARNING   0x00000004  // At least one error counter > 96
#define canSTAT_ERROR_ACTIVE    0x00000008  // The circuit is error active.
#define canSTAT_TX_PENDING      0x00000010  // There are messages pending transmission
#define canSTAT_RX_PENDING      0x00000020  // There are messages in the receive buffer
#define canSTAT_RESERVED_1      0x00000040
#define canSTAT_TXERR           0x00000080  // There has been at least one TX error
#define canSTAT_RXERR           0x00000100  // There has been at least one RX error of some sort
#define canSTAT_HW_OVERRUN      0x00000200  // The has been at least one HW buffer overflow
#define canSTAT_SW_OVERRUN      0x00000400  // The has been at least one SW buffer overflow

//
// Message information flags, < 0x100
//
#define canMSG_MASK             0x00ff      // Used to mask the non-info bits
#define canMSG_RTR              0x0001      // Message is a remote request
#define canMSG_STD              0x0002      // Message has a standard ID
#define canMSG_EXT              0x0004      // Message has a extended ID
#define canMSG_WAKEUP           0x0008      // Message was received in wakeup mode
#define canMSG_NERR             0x0010      // NERR was active during the message
#define canMSG_ERROR_FRAME      0x0020      // Message is an error frame
#define canMSG_TXACK            0x0040      // Message is a TX ACK (msg is really sent)
#define canMSG_TXRQ             0x0080      // Message is a TX REQUEST (msg is transfered to the chip)

//
// Message error flags, >= 0x0100
//
#define canMSGERR_MASK          0xff00      // Used to mask the non-error bits
// 0x0100 reserved
#define canMSGERR_HW_OVERRUN    0x0200      // HW buffer overrun
#define canMSGERR_SW_OVERRUN    0x0400      // SW buffer overrun
#define canMSGERR_STUFF         0x0800      // Stuff error
#define canMSGERR_FORM          0x1000      // Form error
#define canMSGERR_CRC           0x2000      // CRC error
#define canMSGERR_BIT0          0x4000      // Sent dom, read rec
#define canMSGERR_BIT1          0x8000      // Sent rec, read dom

//
// Convenience values for the message error flags.
//
#define canMSGERR_OVERRUN       0x0600      // Any overrun condition.
#define canMSGERR_BIT           0xC000      // Any bit error.
#define canMSGERR_BUSERR        0xF800      // Any RX error

#endif






