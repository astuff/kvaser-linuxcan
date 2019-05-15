/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

/* Kvaser CAN driver PCIcan hardware specific parts                    
** PCIcan definitions                                                    
*/

#ifndef _PCICAN_HW_IF_H_
#define _PCICAN_HW_IF_H_

#include "osif_kernel.h"

/*****************************************************************************/
/* defines */
/*****************************************************************************/

#define DEVICE_NAME_STRING "pcican"
#define MAX_CHANNELS 4
#define PCICAN_MAX_DEV 16
#define PCICAN_VENDOR  0x10e8
#define PCICAN_ID 0x8406
// Standard value: Pushpull  (OCTP1|OCTN1|OCTP0|OCTN0|OCM1)
#define OCR_DEFAULT_STD 0xDA
// For Galathea piggyback.
#define OCR_DEFAULT_GAL 0xDB

#define MAX_ERROR_COUNT 128
#define ERROR_RATE 30000
#define PCICAN_BYTES_PER_CIRCUIT 0x20


/*****************************************************************************/
/* Xilinx                                                                    */
/*****************************************************************************/

//
// These register values are valid for revision 14 of the Xilinx logic.
//
#define XILINX_OUTA         0   // Bit 7 used to write bits to serial memory DS2430.
#define XILINX_INA          1   // Bit 7 used to read back bits from serial memory DS2430.
#define XILINX_CTRLA        2   // Sets the function of the Xilinx pins normally set to zero.
#define XILINX_UNUSED       3
#define XILINX_OUTB         4   // Bit 7 used to reset the SJA-1000.
#define XILINX_INB          5   // Bit 7 used to read back the reset line value.
#define XILINX_CTRLB        6   // Sets the function of the Xilinx pins normally set to zero.
#define XILINX_VERINT       7   // Lower nibble simulate interrupts, high nibble version number.

#define XILINX_PRESUMED_VERSION     14


/* Channel specific data */
typedef struct PciCanChanData
{
    /* Ports and addresses */
    unsigned           sja1000;
    unsigned           xilinxAddressOut;
    unsigned           xilinxAddressCtrl;
    unsigned           xilinxAddressIn;

    OS_IF_TASK_QUEUE_HANDLE txTaskQ;
    DALLAS_CONTEXT          chanEeprom;    
} PciCanChanData;

/*  Cards specific data */
typedef struct PciCanCardData {
    /* Ports and addresses */
    unsigned           sjaBase;
    unsigned           xilinx;
    unsigned           pciIf;
    DALLAS_CONTEXT     cardEeprom;
} PciCanCardData;

int pciCanInitAllDevices(void);
int pciCanSetBusParams (VCanChanData *vChd, VCanBusParams *par);
static int pciCanGetBusParams (VCanChanData *vChd, VCanBusParams *par);
int pciCanSetOutputMode (VCanChanData *vChd, int silent);
int pciCanSetTranceiverMode (VCanChanData *vChd, int linemode, int resnet);
int pciCanBusOn (VCanChanData *vChd);
int pciCanBusOff (VCanChanData *vChd);
int pciCanResetCard (VCanCardData *vCd);
int pciCanInitHW (VCanCardData *vCd);
int pciCanGetTxErr(VCanChanData *vChd);
int pciCanGetRxErr(VCanChanData *vChd);
int pciCanTxAvailable (VCanChanData *vChd);
static int pciCanTransmitMessage (VCanChanData *vChd, CAN_MSG *m);
int pciCanCloseAllDevices(void);
int pciCanProcRead (char *buf, char **start, off_t offset,
                    int count, int *eof, void *data);
int pciCanRequestChipState (VCanChanData *vChd);
unsigned long pciCanRxQLen(VCanChanData *vChd);
unsigned long pciCanTxQLen(VCanChanData *vChd); 
int pciCanRequestSend (VCanCardData *vCard, VCanChanData *vChan);


#endif  /* _PCICAN_HW_IF_H_ */
