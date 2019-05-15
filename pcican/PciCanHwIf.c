/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

//
// Kvaser CAN driver PCIcan hardware specific parts                    
// PCIcan functions                                                    
//

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#if LINUX_2_6
#   include <linux/workqueue.h>
#else
#   include <linux/tqueue.h>
#endif
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>

// Module versioning 
#define EXPORT_SYMTAB
#include <linux/module.h>

// retrieve the CONFIG_* macros 
#include <linux/autoconf.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#   define MODVERSIONS
#endif

#ifdef MODVERSIONS
#   if LINUX_2_6
#      include <config/modversions.h>
#   else
#      include <linux/modversions.h>
#   endif
#endif

// Kvaser definitions 
#include "VCanOsIf.h"
#include "dallas.h"
#include "PciCanHwIf.h"
#include "osif_kernel.h"
#include "osif_functions_kernel.h"

#include "sja1000.h"
#include "amcc5920.h"
#include "hwnames.h"

//
// If you do not define PCICAN_DEBUG at all, all the debug code will be
// left out.  If you compile with PCICAN_DEBUG=0, the debug code will
// be present but disabled -- but it can then be enabled for specific
// modules at load time with a 'debug_level=#' option to insmod.
// i.e. >insmod kvpcican debug_level=#
//

#ifdef PCICAN_DEBUG
static int debug_level = PCICAN_DEBUG;
MODULE_PARM(debug_level, "i");
#   define DEBUGPRINT(n, args...) if (debug_level>=(n)) printk("<" #n ">" args)
#else
#   define DEBUGPRINT(n, args...)
#endif

//======================================================================
// HW function pointers                                                 
//======================================================================
#if LINUX_2_6
VCanHWInterface hwIf = {
    .initAllDevices     = pciCanInitAllDevices,    
    .setBusParams       = pciCanSetBusParams,     
    .getBusParams       = pciCanGetBusParams,     
    .setOutputMode      = pciCanSetOutputMode,    
    .setTranceiverMode  = pciCanSetTranceiverMode,
    .busOn              = pciCanBusOn,            
    .busOff             = pciCanBusOff,           
    .txAvailable        = pciCanTxAvailable,      
    .transmitMessage    = pciCanTransmitMessage,  
    .procRead           = pciCanProcRead,         
    .closeAllDevices    = pciCanCloseAllDevices,  
    .getTime            = vCanTime,
    .flushSendBuffer    = vCanFlushSendBuffer, 
    .getTxErr           = pciCanGetTxErr,
    .getRxErr           = pciCanGetRxErr,
    .rxQLen             = pciCanRxQLen,
    .txQLen             = pciCanTxQLen,
    .requestChipState   = pciCanRequestChipState,
    .requestSend        = pciCanRequestSend
};
#elif LINUX
VCanHWInterface hwIf = {
     initAllDevices:      pciCanInitAllDevices,
     setBusParams:        pciCanSetBusParams,     
     getBusParams:        pciCanGetBusParams,     
     setOutputMode:       pciCanSetOutputMode,    
     setTranceiverMode:   pciCanSetTranceiverMode,
     busOn:               pciCanBusOn,            
     busOff:              pciCanBusOff,           
     txAvailable:         pciCanTxAvailable,      
     transmitMessage:     pciCanTransmitMessage,  
     procRead:            pciCanProcRead,         
     closeAllDevices:     pciCanCloseAllDevices,  
     getTime:             vCanTime,
     flushSendBuffer:     vCanFlushSendBuffer, 
     getTxErr:            pciCanGetTxErr,
     getRxErr:            pciCanGetRxErr,
     rxQLen:              pciCanRxQLen,
     txQLen:              pciCanTxQLen,
     requestChipState:    pciCanRequestChipState,
     requestSend:         pciCanRequestSend
};
#else // wince
VCanHWInterface hwIf = {
    /*initAllDevices:*/     hwIfInitDriver,    
    /*setBusParams:  */     hwIfSetBusParams,     
    /*getBusParams:  */     hwIfGetBusParams,     
    /*setOutputMode: */     hwIfSetOutputMode,    
    /*setTranceiverMode: */ hwIfSetTranceiverMode,
    /*busOn:           */   hwIfBusOn,            
    /*busOff:          */   hwIfBusOff,           
    /*txAvailable:      */  hwIfTxAvailable,      
    /*transmitMessage:  */  hwIfPrepareAndTransmit,  
    /*procRead:         */  hwIfProcRead,         
    /*closeAllDevices:  */  hwIfCloseAllDevices,  
    /*getTime:          */  hwIfTime,
    /*flushSendBuffer:  */  hwIfFlushSendBuffer, 
    /*getTxErr:         */  hwIfGetTxErr,
    /*getRxErr:         */  hwIfGetRxErr,
    /*rxQLen:           */  hwIfHwRxQLen,
    /*txQLen:           */  hwIfHwTxQLen,
    /*requestChipState: */  hwIfRequestChipState,
    /*requestSend:      */  hwIfRequestSend
};
#endif

const char *device_name = DEVICE_NAME_STRING;


//======================================================================
// /proc read function                                                  
//======================================================================
int pciCanProcRead (char *buf, char **start, off_t offset,
                    int count, int *eof, void *data)
{
    int len = 0;
    len += sprintf(buf+len,"\ntotal channels %d\n", driverData.minorNr);
    *eof = 1;
    return len;
}


//======================================================================
//  Can we send now?                                                    
//======================================================================
int pciCanTxAvailable (VCanChanData *vChd)
{
    PciCanChanData *hChd = vChd->hwChanData;
    return ((inb(hChd->sja1000 + PCAN_SR) & PCAN_TBS) == PCAN_TBS);
} // pciCanTxAvailable 


//======================================================================
//  Check sja1000 health                                                
//======================================================================
static int pciCanProbeChannel (VCanChanData *chd)
{
    unsigned int port, tmp;

    PciCanChanData *hChd = chd->hwChanData;

    // First, reset the chip.
    outb (0x01, hChd->sja1000 + PCAN_MOD);
    port = inb (hChd->sja1000 + PCAN_MOD);

    // If we don't read 0x01 back, then it isn't an sja1000.
    if ((port & 0x01) == 0) return -1;

    // 0xff is not a valid answer.
    if (port == 0xFF) return -1;

    // Try to set the Pelican bit.
    port = inb (hChd->sja1000 + PCAN_CDR);
    outb ((unsigned char) (port | PCAN_PELICAN), hChd->sja1000 + PCAN_CDR);
    port = inb (hChd->sja1000 + PCAN_CDR);
    if ((port & PCAN_PELICAN) == 0) return -1;

    // Reset it..
    outb ((unsigned char) (port & ~PCAN_PELICAN), hChd->sja1000 + PCAN_CDR);
    port = inb(hChd->sja1000 + PCAN_CDR);
    if ((port & PCAN_PELICAN) != 0) return -1;

    // Check that bit 5 in the 82c200 control register is always 1
    tmp = inb (hChd->sja1000 + 0);
    if ((tmp & 0x20) == 0) return -1;
    outb ( (unsigned char) (tmp | 0x20), hChd->sja1000 + 0);
    tmp = inb (hChd->sja1000 + 0);
    if ((tmp & 0x20) == 0) return -1;

    // The 82c200 command register is always read as 0xFF
    tmp = inb (hChd->sja1000 + 1);
    if (tmp != 0xFF) return -1;
    outb ( 0, hChd->sja1000 + 1);
    tmp = inb (hChd->sja1000 + 1);
    if (tmp != 0xFF) return -1;

    // Set the Pelican bit.
    port = inb (hChd->sja1000 + PCAN_CDR);
    outb ( (unsigned char) (port | PCAN_PELICAN), hChd->sja1000 + PCAN_CDR);
    port = inb (hChd->sja1000 + PCAN_CDR);
    if ((port & PCAN_PELICAN) == 0) return -1;
    return 0;
} // pciCanProbeChannel 


//======================================================================
// Find out some info about the H/W                                     
// (*cd) must have pciIf, xilinx and sjaBase initialized                
//======================================================================
int pciCanProbe (VCanCardData *vCd)
{
    PciCanCardData *hCd = vCd->hwCardData;
    int i;
    unsigned addr;
    int xilinxRev;

    // Set (one) Wait State needed(?) by the CAN chip on the ADDON bus of S5920
    // WritePortUlong(ci->cc.s5920_address + S5920_PTCR, 0x81818181L );

    // Assert PTADR# - we're in passive mode so the other bits are not important
    outl (0x80808080L, hCd->pciIf + S5920_PTCR);

    xilinxRev = inb(hCd->xilinx + XILINX_VERINT) >> 4;

    vCd->firmwareVersionMajor = xilinxRev;
    vCd->firmwareVersionMinor = 0;
    vCd->firmwareVersionBuild = 0;

    hCd->cardEeprom.address_out = hCd->xilinx + XILINX_OUTA;
    hCd->cardEeprom.address_in  = hCd->xilinx + XILINX_INA;
    hCd->cardEeprom.in_mask = 0x80;
    hCd->cardEeprom.out_mask = 0x80;


    // Check for piggybacks and other data stored in the Dallas memories. 
    ds_init (&(hCd->cardEeprom));

    if (ds_check_for_presence_loop (&(hCd->cardEeprom))) {
        unsigned char buf[32];
        unsigned long boardSerialNumber;

        ds_read_memory(&(hCd->cardEeprom), 0, buf, sizeof(buf));
        memcpy(&boardSerialNumber, &buf[6], sizeof(boardSerialNumber));

        if (boardSerialNumber != 0xffffffff) {
            vCd->serialNumber = boardSerialNumber;
        }
#if 0      
        memset(vCd->ean, 0, sizeof(vCd->ean));
        if (memcmp(&buf[1], "\xff\xff\xff\xff\xff\xff\xff\xff", 6) != 0) {
            packed_EAN_to_BCD_with_csum(&buf[1], vCd->ean);
        }
#endif      
#if 0
        {
            unsigned int sum = 0, n;

            DbgPrint ("EEPROM Board: ");
            for (n = 0; n < 32; ++n) {
                DbgPrint ("%02x ", buf [n]);
                sum += buf [n];
            }
            DbgPrint (", sum=%02x\n", sum);

            DbgPrint ("serial_number = 0x%04x (%u)\n",
                      ci->serial_number,
                      ci->serial_number);

            DbgPrint ("ean_code = ");
            for (n=0; n < sizeof(ci->ean_code); n++) {
                DbgPrint("%02x", ci->ean_code[n]);
            }
            DbgPrint ("\n");
        }
#endif
    }

    for (i = 0; i < MAX_CHANNELS; i++) {
        VCanChanData *vChd = vCd->chanData[i];
        PciCanChanData *hChd = vChd->hwChanData;

        if (vChd->chipType == CAN_CHIP_TYPE_UNKNOWN) {
            // This does not work, if the card is probed the second time (e.g. after
            // wakeup from sleep mode), and there are less than MAX_CHANNELS available.
            
            // Each controller has PCICAN_BYTES_PER_CIRCUIT bytes.
            // This is "hardcoded" on the PCB and in the Xilinx.
            addr                         = hCd->sjaBase + (i * PCICAN_BYTES_PER_CIRCUIT);
            vChd->channel                = i;
            hChd->sja1000                = addr;
            hChd->chanEeprom.address_out = 0;
            hChd->chanEeprom.address_in  = 0;
            hChd->chanEeprom.in_mask     = 0;
            hChd->chanEeprom.out_mask    = 0;

            hChd->xilinxAddressOut       = 0;
            hChd->xilinxAddressCtrl      = 0;
            hChd->xilinxAddressIn        = 0;

            if (vChd->channel == 0) {
                hChd->chanEeprom.address_out = hCd->xilinx + XILINX_OUTA;
                hChd->chanEeprom.address_in  = hCd->xilinx + XILINX_INA;
                hChd->chanEeprom.in_mask     = 0x01;
                hChd->chanEeprom.out_mask    = 0x01;

                hChd->xilinxAddressOut       = hCd->xilinx + XILINX_OUTA;
                hChd->xilinxAddressCtrl      = hCd->xilinx + XILINX_CTRLA;
                hChd->xilinxAddressIn        = hCd->xilinx + XILINX_INA;
            }
            else if (vChd->channel == 1) {
                hChd->chanEeprom.address_out = hCd->xilinx + XILINX_OUTB;
                hChd->chanEeprom.address_in  = hCd->xilinx + XILINX_INB;
                hChd->chanEeprom.in_mask     = 0x01;
                hChd->chanEeprom.out_mask    = 0x01;

                hChd->xilinxAddressOut       = hCd->xilinx + XILINX_OUTB;
                hChd->xilinxAddressCtrl      = hCd->xilinx + XILINX_CTRLB;
                hChd->xilinxAddressIn        = hCd->xilinx + XILINX_INB;
            }
        }
        else {
            DEBUGPRINT(1, "Channel %d already detected - don't calculate address\n", vChd->channel);
        }

        if (pciCanProbeChannel (vChd) == 0) {
            vChd->chipType = CAN_CHIP_TYPE_SJA1000;
        } else {
            // Exit from loop 
            break;
        }
    }

    vCd->nrChannels = i;
    DEBUGPRINT(1, "Kvaser PCIcan with %d channels found\n", vCd->nrChannels);

    // Init Dallas ports so we can read the memories on the piggybacks, if
    // they are present.

    outb(1, hCd->xilinx + XILINX_CTRLA);
    outb(1, hCd->xilinx + XILINX_CTRLB);

    for (i = 0; i < vCd->nrChannels; ++i) {
        VCanChanData *vChd = vCd->chanData[i];
        PciCanChanData *hChd = vChd->hwChanData;

        vChd->transType = VCAN_TRANSCEIVER_TYPE_251;

        ds_init(&hChd->chanEeprom);

        memset(vChd->ean, 0, sizeof(vChd->ean));
        vChd->serialLow = 0;
        vChd->serialHigh = 0;

        if (ds_check_for_presence_loop (&hChd->chanEeprom)) {

            unsigned char buf[32];

            ds_read_memory (&hChd->chanEeprom, 0, buf, sizeof(buf));

            if ((buf[9] == VCAN_TRANSCEIVER_TYPE_SWC) ||
                  (buf[9] == VCAN_TRANSCEIVER_TYPE_251) ||
                  (buf[9] == VCAN_TRANSCEIVER_TYPE_GAL) ||
                  (buf[9] == VCAN_TRANSCEIVER_TYPE_252)) {
                vChd->transType = buf[9];
            }

            // qqq The following fields should probably also be filled in:
            // unsigned int  transceiver_type;
            // unsigned int  transceiver_state;
            // unsigned long serial_number_low,
            // serial_number_high;
            // unsigned long transceiver_capabilities;
            // char          ean[16];
            // unsigned int  linemode,
            // resnet;

            memset(vChd->ean, 0, sizeof(vChd->ean));
#if 0
            packed_EAN_to_BCD_with_csum(&buf[0], vChd->ean);
#endif
#if 0
            if (DBGF(DEBUG_INIT)) {
                unsigned int sum = 0, n;
                DbgPrint ("EEPROM Channel %d: ", vChd->channel);
                for (n = 0; n < 32; ++n) {
                    DbgPrint ("%02x ", buf [n]);
                    sum += buf [n];
                }
                DbgPrint (", sum=%02x\n", sum);
                DbgPrint ("%s: channel %d piggy %02x (%d)\n",
                          hwif.hw_name, vChd->channel,
                          vChd->transceiver_info.transceiver_type,
                          vChd->transceiver_info.transceiver_type);
            }
#endif
        }

        switch (vChd->transType) {

            case VCAN_TRANSCEIVER_TYPE_SWC:
                vChd->lineMode = VCAN_TRANSCEIVER_LINEMODE_SWC_NORMAL;
                break;

            case VCAN_TRANSCEIVER_TYPE_251:
            case VCAN_TRANSCEIVER_TYPE_NONE:
                vChd->lineMode = VCAN_TRANSCEIVER_LINEMODE_NORMAL;
                break;

            default:
                vChd->lineMode = VCAN_TRANSCEIVER_LINEMODE_NA;
                break;
        }
    }

    // Now restore the control registers in the Xilinx to the default config;
    // needed for talking to a PCIcan-Q. If we have piggys present, they will
    // be setup again in hermes_setup_transceiver().

    if (vCd->nrChannels <= 2) {
        // PCIcan-S, -D. Disable the ununsed interrupts so Galathea will work.
        // This is done by writing 0x40 to the control ports.
        // Also, for Galathea, set bit 1 to 1 in the control registers.
        outb(0x42, hCd->xilinx + XILINX_CTRLA);
        outb(0x42, hCd->xilinx + XILINX_CTRLB);
    } else {
        // Xilinx setup for PCIcan-Q.
        outb(0, hCd->xilinx + XILINX_CTRLA);
        outb(0, hCd->xilinx + XILINX_CTRLB);
    }

#if 0
    // Useful for debug.
    { int i, j;

    unsigned  x = hCd->sjaBase;

    for (i=0; i<64; i++) {
        printk("<1>%04x  ", x + i*16);
        for (j=0; j<16; j++) {
            printk("%02x ", inb(x + i*16 + j));
        }
        printk("\n");
    }
    }
#endif

    if (i == 0) {

        // no channels found 
        vCd->cardPresent = 0;
        return -1;
    }

    vCd->cardPresent = 1;

    return 0;
} // pciCanProbe 


//======================================================================
// Perform transceiver-specific setup on PCIcan with piggybacks         
//======================================================================
static void pciCanSetupTransceiver (VCanChanData *vChd)
{
    PciCanChanData *hChd = vChd->hwChanData;

    if (hChd->xilinxAddressCtrl == 0) return;

    switch (vChd->transType) {

        case VCAN_TRANSCEIVER_TYPE_NONE:
        case VCAN_TRANSCEIVER_TYPE_251:
            // Piggyback pins all inputs. 
            outb (0, hChd->xilinxAddressCtrl);
            break;

        case VCAN_TRANSCEIVER_TYPE_SWC:
            outb (0x18, hChd->xilinxAddressCtrl);
            // qqq we should implement a "sticky" input register in the Xilinx
            // qqq so we can see if HVOLT has gone low.
            break;

            //
            // The 252/1053 code is not yet tested. qqq
            //
        case VCAN_TRANSCEIVER_TYPE_252:
            outb (0x0e, hChd->xilinxAddressCtrl);
            // qqq we should implement a "sticky" input register in the Xilinx
            // qqq so we can see if NERR has gone low.
            break;
        case VCAN_TRANSCEIVER_TYPE_GAL:
        {
            // Activate the transceiver. OUTx register bit 1 high then low

            unsigned char tmp;
            tmp = inb(hChd->xilinxAddressOut);
            outb((unsigned char)(tmp | 2), hChd->xilinxAddressOut);
            outb((unsigned char)(tmp & ~2), hChd->xilinxAddressOut);

        }
        break;
        default:
            // qqq unsupported piggy
            printk ("<1> PCIcan: unknown piggy on chan %d\n", vChd->channel);
            break;
    }
} // pciCanSetupTransceiver 


//======================================================================
// This sets the transceiver to the specified line mode.                
// It's a no-op for 251-type tranceivers, but for e.g. SWCs the current 
// line mode could be WAKEUP.                                           
//======================================================================
static void pciCanActivateTransceiver (VCanChanData *vChd, int linemode)
{
    PciCanChanData *hChd = vChd->hwChanData;

    if (hChd->xilinxAddressOut == 0) return;

    switch (vChd->transType) {

        case VCAN_TRANSCEIVER_TYPE_SWC:
            switch (linemode) {

                case VCAN_TRANSCEIVER_LINEMODE_SWC_SLEEP:
                    outb (0x00, hChd->xilinxAddressOut);
                    break;

                case VCAN_TRANSCEIVER_LINEMODE_SWC_NORMAL:
                    outb (0x18, hChd->xilinxAddressOut);
                    break;

                case VCAN_TRANSCEIVER_LINEMODE_SWC_FAST:
                    outb (0x08, hChd->xilinxAddressOut);
                    break;

                case VCAN_TRANSCEIVER_LINEMODE_SWC_WAKEUP:
                    outb (0x10, hChd->xilinxAddressOut);
                    break;
            }
            break;

        case VCAN_TRANSCEIVER_TYPE_252:
            // EN=1, STB#=1, WAK#=1 
            outb ( 0x0e, hChd->xilinxAddressOut);
            break;
        case VCAN_TRANSCEIVER_TYPE_GAL:

            break;
        default:
            break;
    }
} // pciCanActivateTransceiver 


//======================================================================
// Enable bus error interrupts, and reset the                           
// counters which keep track of the error rate                          
//======================================================================
static void pciCanResetErrorCounter (VCanChanData *vChd)
{
    PciCanChanData *hChd = vChd->hwChanData;
    unsigned char ier;
    ier = inb(hChd->sja1000 + PCAN_IER);
    outb(ier | PCAN_BEIE, hChd->sja1000 + PCAN_IER);
    vChd->errorCount = 0;

    vChd->errorTime = hwIf.getTime(vChd->vCard);
} // pciCanResetErrorCounter 


//======================================================================
//  Set bit timing                                                      
//======================================================================
int pciCanSetBusParams (VCanChanData *vChd, VCanBusParams *par)
{
    PciCanChanData  *hChd = vChd->hwChanData;
    unsigned        quantaPerCycle;
    unsigned long   brp;
    unsigned char   cbt0;
    unsigned char   cbt1;
    unsigned char   tmp;
    int             resetStatus;
    unsigned long   freq;
    unsigned char   tseg1;
    unsigned char   tseg2;
    unsigned char   sjw;
    unsigned char   sam;
    unsigned        circAddr = hChd->sja1000;

    freq  = par->freq;
    sjw   = par->sjw;
    tseg1 = par->tseg1;
    tseg2 = par->tseg2;
    sam   = par->samp3;
    sjw--;

    quantaPerCycle = tseg1 + tseg2 + 1;
    if (quantaPerCycle == 0 || freq == 0) return -1;

    brp = (8000000L * 64) / (freq * quantaPerCycle);
    if ((brp & 0x3F) != 0) {
        // Fraction != 0 : not divisible.
        return -1;
    }
    brp = (brp >> 6) - 1;
    if (brp > 64 || sjw > 3 || quantaPerCycle < 3) {
        return -1;
    }

    cbt0 = (sjw << 6) + brp;
    cbt1 = ((sam==3?1:0)<<7) + ((tseg2-1)<<4) + (tseg1-1);


    // Put the circuit in Reset Mode 
    tmp = inb(circAddr + PCAN_MOD);

    // Always set the AFM bit.
    tmp |= PCAN_AFM;
    resetStatus = tmp & PCAN_RM;
    outb( tmp | PCAN_RM | PCAN_AFM, circAddr + PCAN_MOD);

    outb(cbt0, circAddr + PCAN_BTR0);
    outb(cbt1, circAddr + PCAN_BTR1);

    if (!resetStatus) {
        tmp = inb(circAddr + PCAN_MOD);
        outb (tmp & ~PCAN_RM, circAddr + PCAN_MOD);
    }
    return 0;
} // pciCanSetBusParams 


//======================================================================
//  Get bit timing                                                      
//======================================================================
static int pciCanGetBusParams (VCanChanData *vChd, VCanBusParams *par)
{    
    PciCanChanData *hChd = vChd->hwChanData;

    unsigned quantaPerCycle;
    unsigned char cbt0;
    unsigned char cbt1;
    unsigned long brp;

    cbt0 = inb(hChd->sja1000 + PCAN_BTR0);
    cbt1 = inb(hChd->sja1000 + PCAN_BTR1);

    par->sjw = 1 + (cbt0 >> 6);
    par->samp3 = (cbt1 >> 7) == 1 ? 3 : 1;

    par->tseg1 = 1 + (cbt1 & 0xf);
    par->tseg2 = 1 + ((cbt1 >> 4) & 0x7);

    quantaPerCycle = par->tseg1 + par->tseg2 + 1;    
    brp = 1 +(cbt0 & 0x3f);

    par->freq = 8000000L/(quantaPerCycle * brp);

    return 0;
} // pciCanGetBusParams 


//======================================================================
//  Set silent or normal mode                                           
//======================================================================
int pciCanSetOutputMode (VCanChanData *vChd, int silent)
{
    PciCanChanData *hChd = vChd->hwChanData;

    unsigned char driver;
    unsigned char tmp;

    // Save control register
    tmp = inb (hChd->sja1000 + PCAN_MOD);
    // Put the circuit in Reset Mode
    outb (tmp | PCAN_RM , hChd->sja1000 + PCAN_MOD);
    // Always set the AFM bit.
    tmp |= PCAN_AFM;

    if (vChd->transType == VCAN_TRANSCEIVER_TYPE_GAL) {
        driver = OCR_DEFAULT_GAL;
    } else {
        driver = OCR_DEFAULT_STD;
    }

    if(silent) {
        tmp |= PCAN_LOM;
    } else {
        tmp &= ~PCAN_LOM;
    }

    // Set the output control
    outb (driver, hChd->sja1000 + PCAN_OCR);
    // Restore control register
    outb (tmp, hChd->sja1000 + PCAN_MOD);

    return 0;
} // pciCanSetOutputMode 


//======================================================================
//  Line mode                                                           
//======================================================================
int pciCanSetTranceiverMode (VCanChanData *vChd, int linemode, int resnet)
{
    vChd->lineMode = linemode;
    pciCanActivateTransceiver (vChd, vChd->lineMode);
    return 0;
} // pciCanSetTranceiverMode 


//======================================================================
//  Query chip status                                                   
//======================================================================
int pciCanRequestChipState (VCanChanData *vChd)
{
    PciCanChanData *hChd = vChd->hwChanData;
    VCAN_EVENT msg;
    unsigned char sr, cr;
    
    sr = inb(hChd->sja1000 + PCAN_SR);
    cr = inb(hChd->sja1000 + PCAN_MOD);

    vChd->chipState.txerr = inb(hChd->sja1000 + PCAN_TXERR);
    vChd->chipState.rxerr = inb(hChd->sja1000 + PCAN_RXERR);

    switch (sr & (PCAN_BS|PCAN_ES)) {
        case PCAN_BS:
            vChd->chipState.state = CHIPSTAT_BUSOFF;
            break;

        case PCAN_BS|PCAN_ES:          
            vChd->chipState.state = CHIPSTAT_BUSOFF;
            break;

        case PCAN_ES:
            vChd->chipState.state = CHIPSTAT_ERROR_WARNING;
            if ((vChd->chipState.txerr > 127) ||
                  (vChd->chipState.rxerr > 127)) {
                vChd->chipState.state |= CHIPSTAT_ERROR_PASSIVE;
            }
            break;

        case 0:
            vChd->chipState.state = CHIPSTAT_ERROR_ACTIVE;
            break;
    }

    if (cr & PCAN_RM) {
        // It's in reset mode. We report BUSOFF but should really be
        // reporting "inactive" or so. qqq
        vChd->chipState.state = CHIPSTAT_BUSOFF;
    }

    msg.tag = V_CHIP_STATE;
    msg.timeStamp = hwIf.getTime(vChd->vCard);
    msg.transId = 0;
    msg.tagData.chipState.busStatus = (unsigned char) vChd->chipState.state;
    msg.tagData.chipState.txErrorCounter = (unsigned char) vChd->chipState.txerr;
    msg.tagData.chipState.rxErrorCounter = (unsigned char) vChd->chipState.rxerr;
    vCanDispatchEvent(vChd, &msg);

    return 0;
} // pciCanRequestChipState 


//======================================================================
//  Go bus on                                                           
//======================================================================
int pciCanBusOn (VCanChanData *vChd)
{
    PciCanChanData *hChd = vChd->hwChanData;
    unsigned tmp;

    switch (vChd->transType) {

        case VCAN_TRANSCEIVER_TYPE_SWC:
            vChd->lineMode = VCAN_TRANSCEIVER_LINEMODE_SWC_NORMAL;
            break;

        case VCAN_TRANSCEIVER_TYPE_251:
        case VCAN_TRANSCEIVER_TYPE_NONE:
            vChd->lineMode = VCAN_TRANSCEIVER_LINEMODE_NORMAL;
            break;

        default:
            vChd->lineMode = VCAN_TRANSCEIVER_LINEMODE_NA;
            break;
    }

    pciCanSetupTransceiver(vChd);
    pciCanActivateTransceiver(vChd, vChd->lineMode);

    vChd->isOnBus = 1;
    vChd->overrun = 0;
    pciCanResetErrorCounter(vChd);
    
    // Go on bus 
    tmp = inb (hChd->sja1000 + PCAN_MOD);
    // Always set the AFM bit.
    tmp |= PCAN_AFM;
    outb(PCAN_CDO, hChd->sja1000 + PCAN_CMR);
    outb(tmp | PCAN_RM, hChd->sja1000 + PCAN_MOD);
    outb(0, hChd->sja1000 + PCAN_TXERR);
    outb(0, hChd->sja1000 + PCAN_RXERR);
    (void)inb (hChd->sja1000 + PCAN_ECC);

    // Write the hardware filters once again. They might have been corrupted
    // if we tried to write a message to the transmit buffer at the same
    // time as the sja1000 decided to go bus off due to e.g. excessive errors.
    outb(0, hChd->sja1000 + PCAN_ACR0);
    outb(0, hChd->sja1000 + PCAN_ACR1);
    outb(0, hChd->sja1000 + PCAN_ACR2);
    outb(0, hChd->sja1000 + PCAN_ACR3);
    outb(0xFF, hChd->sja1000 + PCAN_AMR0);
    outb(0xFF, hChd->sja1000 + PCAN_AMR1);
    outb(0xFF, hChd->sja1000 + PCAN_AMR2);
    outb(0xFF, hChd->sja1000 + PCAN_AMR3);

    outb(tmp & ~PCAN_RM, hChd->sja1000 + PCAN_MOD);

    vChd->chipState.state = CHIPSTAT_ERROR_ACTIVE;

    pciCanRequestChipState(vChd);
    return 0;
} // pciCanBusOn 


//======================================================================
//  Go bus off                                                          
//======================================================================
int pciCanBusOff (VCanChanData *vChd)
{  
    PciCanChanData *hChd = vChd->hwChanData;
    unsigned tmp;

    vChd->isOnBus = 0;

    tmp = inb(hChd->sja1000 + PCAN_MOD);
    outb(tmp | PCAN_RM, hChd->sja1000 + PCAN_MOD);

    pciCanRequestChipState(vChd);
    return 0;
} // pciCanBusOff 


//======================================================================
//  Disable Card                                                        
//======================================================================
int pciCanResetCard (VCanCardData *vChd)
{
    PciCanCardData *hChd = vChd->hwCardData;
    unsigned tmp;

    // The card must be present! 
    if (!vChd->cardPresent) return -1;

    // Disable interrupts from card 
    tmp = inl(hChd->pciIf + S5920_INTCSR);
    tmp &= ~INTCSR_ADDON_INTENABLE_M;
    outl(tmp, hChd->pciIf + S5920_INTCSR);

    return 0;
} // pciCanResetCard 


//======================================================================
//  Interrupt handling functions                                        
//======================================================================
static void pciCanReceiveIsr (VCanChanData *vChd)
{
    PciCanChanData *hChd = vChd->hwChanData;

    unsigned           circAddr = hChd->sja1000;
    static VCAN_EVENT  e;
    int                i;
    int                r; 
    unsigned char      dlc;
    unsigned char      flags;
    unsigned char      *p;
    unsigned           data;
    unsigned char      SR;
    WL                 id;

    SR = inb(circAddr + PCAN_SR);

    while (SR & PCAN_RBS) {
        unsigned char tmp;

        e.timeStamp = hwIf.getTime(vChd->vCard);

        tmp = inb(circAddr + PCAN_MSGBUF);
        dlc = (unsigned char)(tmp & 0x0F);

        // qqq remove this?
        if (dlc > 8) dlc = 8;
        flags = (unsigned char)((tmp & PCAN_FF_REMOTE) ? VCAN_MSG_FLAG_REMOTE_FRAME : 0);
        id.L = 0;


        // Extended CAN 
        if (tmp & PCAN_FF_EXTENDED) { 
            id.B.b3 = inb (circAddr + PCAN_XID0);
            id.B.b2 = inb (circAddr + PCAN_XID1);
            id.B.b1 = inb (circAddr + PCAN_XID2);
            id.B.b0 = inb (circAddr + PCAN_XID3);
            id.L >>= 3;
            id.L |= VCAN_EXT_MSG_ID;
            data = circAddr + PCAN_XDATA;
        }
        // Standard CAN 
        else { 
            id.B.b1 = inb (circAddr + PCAN_SID0);
            id.B.b0 = inb (circAddr + PCAN_SID1);
            id.L >>= 5;
            data = circAddr + PCAN_SDATA;
        }

        p = e.tagData.msg.data;

        for (i=0; i<dlc; i++) {
            *p++ = inb (data++);
        }

        if (vChd->overrun) {
            flags |= vChd->overrun;
            vChd->overrun = 0;
        }

        e.tag               = V_RECEIVE_MSG;
        e.transId           = 0;
        e.tagData.msg.id    = id.L;
        e.tagData.msg.flags = flags;
        e.tagData.msg.dlc   = dlc;

        r = vCanDispatchEvent(vChd, &e);
        // Release receive buffer

        outb(PCAN_RRB, circAddr + PCAN_CMR);

        SR = inb (circAddr + PCAN_SR);
    }

    if (vChd->errorCount > 0) pciCanResetErrorCounter(vChd);
} // pciCanReceiveIsr 


//======================================================================
//  Transmit interrupt handler                                          
//======================================================================
static void pciCanTransmitIsr (VCanChanData *vChd)
{
    PciCanChanData *hChd = vChd->hwChanData;
    
    pciCanActivateTransceiver(vChd, vChd->lineMode);


    // "send" a transmit ack. 
    if (vChd->currentTxMsg->flags & VCAN_MSG_FLAG_TXACK) {
        VCAN_EVENT *e = (VCAN_EVENT*) vChd->currentTxMsg;
        e->tag = V_RECEIVE_MSG;
        e->timeStamp = hwIf.getTime(vChd->vCard);
        e->tagData.msg.flags &= ~VCAN_MSG_FLAG_TXRQ;
        vCanDispatchEvent(vChd, e);
    }

    // Send next message in queue
    //DEBUGPRINT(1, "pciCanTransmitIsr\n");
    os_if_queue_task(&hChd->txTaskQ);

} // pciCanTransmitIsr 


//======================================================================
// Handle error interrupts. Happens when the bus status or error status 
// bits in the status register changes.                                 
//======================================================================
static void pciCanErrorIsr (VCanChanData *vChd)
{
    pciCanRequestChipState (vChd);
} // pciCanErrorIsr


//======================================================================
//  Overrun interrupt handler                                           
//======================================================================
static void pciCanOverrunIsr (VCanChanData *vChd)
{
    PciCanChanData *hChd = vChd->hwChanData;
    outb (PCAN_CDO, hChd->sja1000 + PCAN_CMR);
    vChd->overrun = VCAN_MSG_FLAG_OVERRUN;
    pciCanReceiveIsr (vChd);
} // pciCanOverrunIsr 


//======================================================================
//  Bus error interrupt handler                                         
//======================================================================
static void pciCanBusErrorIsr (VCanChanData *vChd)
{

    PciCanChanData  *hChd = vChd->hwChanData;
    VCAN_EVENT      e;
    unsigned char   ECC;
    int             r;
    
    ECC                  = inb(hChd->sja1000 + PCAN_ECC);
    e.timeStamp          = hwIf.getTime(vChd->vCard);
    e.tag                = V_RECEIVE_MSG;
    e.transId            = 0;
    e.tagData.msg.id     = 0x800 + ECC;
    e.tagData.msg.flags  = VCAN_MSG_FLAG_ERROR_FRAME;
    e.tagData.msg.dlc    = 0;

    r = vCanDispatchEvent(vChd, &e);

    // Muffle the sja1000 if we get too many errors.
    // qqq this is not done right 
    //

    vChd->errorCount++;
    if (vChd->errorCount == MAX_ERROR_COUNT/2) {
        // Half done, store current time 
        vChd->errorTime = hwIf.getTime(vChd->vCard);
    }
    else if (vChd->errorCount > MAX_ERROR_COUNT) {
        if ((hwIf.getTime(vChd->vCard) - vChd->errorTime) > ERROR_RATE/10) {
            // Error rate reasonable, restart counters 
            vChd->errorCount = 0;
            vChd->errorTime = hwIf.getTime(vChd->vCard);
        }
        else {
            unsigned char ier;
            ier = inb (hChd->sja1000 + PCAN_IER);
            outb((unsigned char)(ier & ~PCAN_BEIE), hChd->sja1000 + PCAN_IER);
        }
    }
} // pciCanBusErrorIsr 


static void pciCanErrorPassiveIsr (VCanChanData *vChd)
{
    pciCanRequestChipState(vChd);
} // pciCanErrorPassiveIsr 


//======================================================================
//  Main ISR                                                            
//======================================================================
OS_IF_INTR_HANDLER pciCanInterrupt(int irq, void *dev_id, struct pt_regs *regs)
{

    VCanCardData    *vCard   = (VCanCardData*) dev_id;
    PciCanCardData  *hCd     = vCard->hwCardData;
    VCanChanData    *vChd;
    PciCanChanData  *hChd; 
    unsigned int    loopmax  = 1000;
    unsigned        ireg;
    int             i;
    
    // Read interrupt status 
    while(inl(hCd->pciIf + S5920_INTCSR) & INTCSR_INTERRUPT_ASSERTED_M){

        if (--loopmax == 0) {
            // Kill the card. 
            printk ("<1> PCIcan runaway, shutting down card!!");
            pciCanResetCard (vCard);
            return IRQ_HANDLED; // qqq ???
        }

        // Handle all channels 
        for (i = 0; i < vCard->nrChannels; i++) {
            vChd = vCard->chanData[i];
            hChd = vChd->hwChanData;
            // Reading clears interrupt flag 
            while ((ireg = inb(hChd->sja1000 + PCAN_IR))) {
                if (ireg & PCAN_RI) pciCanReceiveIsr(vChd);
                if (ireg & PCAN_TI) pciCanTransmitIsr(vChd);
                if (ireg & PCAN_EI) pciCanErrorIsr(vChd);
                if (ireg & PCAN_DOI) pciCanOverrunIsr(vChd);
                if (ireg & PCAN_WUI) { 
                    printk ("<1>PCIcan: Huh? Wakeup Interrupt!\n");
                }
                if (ireg & PCAN_BEI) pciCanBusErrorIsr(vChd);
                if (ireg & PCAN_EPI) pciCanErrorPassiveIsr(vChd);
            }
        }
    }
    return IRQ_HANDLED;

} // pciCanInterrupt 


//======================================================================
//  pcicanTransmit                                                      
//======================================================================
static int pciCanTransmitMessage (VCanChanData *vChd,
                                  CAN_MSG *m)
{
    PciCanChanData *hChd = vChd->hwChanData;
    unsigned       p;
    unsigned       circAddr = hChd->sja1000;
    unsigned char  *msg = m->data;
    signed long    ident = m->id;
    unsigned char  flags = m->flags;
    unsigned char  dlc = m->length;
    int            i;

    // Set special transvceiver modes 
    switch (vChd->transType) {

        case VCAN_TRANSCEIVER_TYPE_SWC:
            if (flags & VCAN_MSG_FLAG_WAKEUP) {
                pciCanActivateTransceiver (vChd, VCAN_TRANSCEIVER_LINEMODE_SWC_WAKEUP);
            }
            break;

        case VCAN_TRANSCEIVER_TYPE_251:
        case VCAN_TRANSCEIVER_TYPE_NONE:
            break;

        default:
            break;
    }

    if (ident & VCAN_EXT_MSG_ID) { // Extended CAN 
        WL id;
        unsigned char x;

        id.L = ident & ~VCAN_EXT_MSG_ID;
        id.L <<= 3;
        x = (unsigned char)(dlc | PCAN_FF_EXTENDED);
        if (flags & VCAN_MSG_FLAG_REMOTE_FRAME) x |= PCAN_FF_REMOTE;

        outb(x, circAddr + PCAN_MSGBUF);
        outb(id.B.b3, circAddr + PCAN_XID0);
        outb(id.B.b2, circAddr + PCAN_XID1);
        outb(id.B.b1, circAddr + PCAN_XID2);
        outb(id.B.b0, circAddr + PCAN_XID3);

        p = circAddr + PCAN_XDATA;
        for (i = 0; i < dlc; i++) outb (*msg++, p++);
    }
    else { // Standard CAN 
        unsigned char x;
        x = dlc;

        if (flags & VCAN_MSG_FLAG_REMOTE_FRAME) x |= PCAN_FF_REMOTE;
        outb(x, circAddr + PCAN_MSGBUF);
        outb((unsigned char) (ident >> 3), circAddr + PCAN_SID0);
        outb((unsigned char) (ident << 5), circAddr + PCAN_SID1);

        p = circAddr + PCAN_SDATA;
        for (i=0; i<dlc; i++) outb(*msg++, p++);
    }

    vChd->currentTxMsg = m;

    if (flags & VCAN_MSG_FLAG_TXRQ) {
        VCAN_EVENT e = *((VCAN_EVENT*) vChd->currentTxMsg);
        e.tagData.msg.flags &= ~(VCAN_MSG_FLAG_TX_NOTIFY | VCAN_MSG_FLAG_TX_START);

        // fix invalid messages 
        e.tagData.msg.flags &= (VCAN_MSG_FLAG_ERROR_FRAME |
                                VCAN_MSG_FLAG_REMOTE_FRAME);

        if (e.tagData.msg.dlc > 8) e.tagData.msg.dlc = 8;

        if (e.tagData.msg.id & VCAN_EXT_MSG_ID) {
            e.tagData.msg.id &= 0x1fffffff | VCAN_EXT_MSG_ID;
        }
        else {
            e.tagData.msg.id &= 0x07ff;
        }

        e.tag = V_RECEIVE_MSG;
        e.timeStamp = hwIf.getTime(vChd->vCard);

        e.tagData.msg.flags |= VCAN_MSG_FLAG_TX_START;
        vCanDispatchEvent(vChd, &e);
    }


    outb(PCAN_TR, circAddr + PCAN_CMR);

    if(vChd->errorCount > 0) pciCanResetErrorCounter (vChd);

    return 0;
} // pciCanTransmitMessage 


//======================================================================
//  Read transmit error counter                                         
//======================================================================
int pciCanGetTxErr(VCanChanData *vChd) 
{
    PciCanChanData *hChd = vChd->hwChanData;
    return inb(hChd->sja1000 + PCAN_TXERR);
}


//======================================================================
//  Read transmit error counter                                         
//======================================================================
int pciCanGetRxErr(VCanChanData *vChd) 
{
    PciCanChanData *hChd = vChd->hwChanData;
    return inb(hChd->sja1000 + PCAN_RXERR);
}


//======================================================================
//  Read receive queue length in hardware/firmware                     
//======================================================================
unsigned long pciCanRxQLen(VCanChanData *vChd) 
{
    return getQLen(atomic_read(&vChd->txChanBufHead),
        atomic_read(&vChd->txChanBufTail), TX_CHAN_BUF_SIZE);
}


//======================================================================
//  Read transmit queue length in hardware/firmware                     
//======================================================================
unsigned long pciCanTxQLen(VCanChanData *vChd) 
{
    int qLen = 0;
    //if ((vChd->chipState.state != CHIPSTAT_BUSOFF) && !hwIf.txAvailable(vChd)) qLen++;

    // return zero because we don't have any hw-buffers. 
    return qLen;
}


//======================================================================
//  Initialize H/W                                                      
//======================================================================
int pciCanInitHW (VCanCardData *vCard)
{
    int             chNr;    
    unsigned        tmp;
    PciCanCardData  *hCard = vCard->hwCardData;

    // The card must be present! 
    if (!vCard->cardPresent) return -1;

    for (chNr = 0; chNr < vCard->nrChannels; chNr++){

        PciCanChanData *hChd = vCard->chanData[chNr]->hwChanData;
        VCanChanData *vChd = vCard->chanData[chNr];

        unsigned addr = hChd->sja1000;
        if (!addr) return -1;

        // Reset the circuit...
        outb(PCAN_RM, addr + PCAN_MOD);
        // 
        // ...goto Pelican mode...
        outb(PCAN_PELICAN|PCAN_CBP, addr + PCAN_CDR);
        // 
        // ...and set the filter mode
        outb(PCAN_RM|PCAN_AFM, addr + PCAN_MOD);

        // Activate almost all interrupt sources.
        outb(PCAN_BEIE|PCAN_EPIE|PCAN_DOIE|PCAN_EIE|PCAN_TIE|PCAN_RIE, addr + PCAN_IER);

        // Accept all messages by default.

        outb(0, addr + PCAN_ACR0);
        outb(0, addr + PCAN_ACR1);
        outb(0, addr + PCAN_ACR2);
        outb(0, addr + PCAN_ACR3);
        outb(0xFF, addr + PCAN_AMR0);
        outb(0xFF, addr + PCAN_AMR1);
        outb(0xFF, addr + PCAN_AMR2);
        outb(0xFF, addr + PCAN_AMR3);

        // Default 125 kbit/s, pushpull.
        outb(0x07, addr + PCAN_BTR0);
        outb(0x23, addr + PCAN_BTR1);

        if (vChd->transType == VCAN_TRANSCEIVER_TYPE_GAL) {
            outb(OCR_DEFAULT_GAL, addr + PCAN_OCR);
        } else {
            outb(OCR_DEFAULT_STD, addr + PCAN_OCR);
        }
    }


    // enable interrupts from card 
    tmp = inl (hCard->pciIf + S5920_INTCSR);
    tmp |= INTCSR_ADDON_INTENABLE_M;
    outl(tmp, hCard->pciIf + S5920_INTCSR);

    return 0;
} 


//======================================================================
//  Find out addresses for one card                                     
//======================================================================
static int readPCIAddresses(struct pci_dev *dev, VCanCardData *vCard)
{
    PciCanCardData *hCd = vCard->hwCardData;
    int i;

    // fo removed static 040927
    /*static */u32 addresses[] = {
        PCI_BASE_ADDRESS_0,
        PCI_BASE_ADDRESS_1,
        PCI_BASE_ADDRESS_2,
        PCI_BASE_ADDRESS_3,
        PCI_BASE_ADDRESS_4,
        PCI_BASE_ADDRESS_5,
        0
    };

    for(i = 0;addresses[i];i++) {
        u32 curr,mask;

        pci_read_config_dword(dev,addresses[i],&curr);
        //cli();
        pci_write_config_dword(dev,addresses[i],~0);
        pci_read_config_dword(dev,addresses[i],&mask);
        pci_write_config_dword(dev,addresses[i],curr);
        //sti();
        if(!mask)
            continue;

        if(curr & PCI_BASE_ADDRESS_SPACE_IO) {
            curr &= PCI_BASE_ADDRESS_IO_MASK;
        }
        else {
            curr &= PCI_BASE_ADDRESS_MEM_MASK;
        }

        if(mask & PCI_BASE_ADDRESS_SPACE_IO) {
            mask &= PCI_BASE_ADDRESS_IO_MASK;
        }
        else {
            mask &= PCI_BASE_ADDRESS_MEM_MASK;
        }

        switch(i){
            case 0:
                hCd->pciIf = curr;
                break;
            case 1:
                hCd->sjaBase = curr;
                vCard->irq = dev->irq;
                break;
            case 2:
                hCd->xilinx = curr;
                break;
            default:
                break;
        }      
    }
    // we return 1 for ok 
    if (pci_enable_device(dev)){
        // Failed 
        return 0;
    }
    else {
        return 1;
    }
}    


//======================================================================
// Request send                                                         
//======================================================================
int pciCanRequestSend (VCanCardData *vCard, VCanChanData *vChan)
{
    PciCanChanData *hChan = vChan->hwChanData;
    if (pciCanTxAvailable(vChan)){
        os_if_queue_task(&hChan->txTaskQ);
    }
    return 0;
}


//======================================================================
//  Process send Q - This function is called from the immediate queue   
//======================================================================
void pciCanSend (void *void_chanData)
{
    VCanChanData *chd = (VCanChanData*) void_chanData;
    if (!hwIf.txAvailable(chd)) return;
    
    // Send Messages  
    if (getQLen(atomic_read(&chd->txChanBufHead), atomic_read(&chd->txChanBufTail),
        TX_CHAN_BUF_SIZE) != 0) {
        hwIf.transmitMessage(chd, &(chd->txChanBuffer[atomic_read(&chd->txChanBufTail)]));
        // ??? Multiple CPU race when flushing txQ
        atomic_add(1, &chd->txChanBufTail);
        
        if (atomic_read(&chd->txChanBufTail) >= TX_CHAN_BUF_SIZE)
            atomic_set(&chd->txChanBufTail, 0);

        wake_up_interruptible(&chd->txChanWaitQ);
    }
    else if(atomic_read(&chd->waitEmpty)) {
        atomic_set(&chd->waitEmpty, 0);
        wake_up_interruptible(&chd->flushQ);
    }
    return;
}


//======================================================================
//  Initialize H/W specific data                                        
//======================================================================
int pciCanInitData (VCanCardData *vCard)
{
    int chNr;
    vCanInitData(vCard);
    for (chNr = 0; chNr < vCard->nrChannels; chNr++){
        PciCanChanData *hChd = vCard->chanData[chNr]->hwChanData;
        os_if_init_task(&hChd->txTaskQ, pciCanSend, vCard->chanData[chNr]);
    }
    return 0;
}


//======================================================================
// Initialize the HW for one card                                       
//======================================================================
int pciCanInitOne(struct pci_dev *dev)
{
    // Helper struct for allocation 
    typedef struct {
        VCanChanData *dataPtrArray[MAX_CHANNELS];
        VCanChanData vChd[MAX_CHANNELS];
        PciCanChanData hChd[MAX_CHANNELS];
    } ChanHelperStruct;

    ChanHelperStruct *chs;

    int chNr;
    VCanCardData *vCard;
    
    // Allocate data area for this card 
    vCard  = kmalloc(sizeof(VCanCardData) + sizeof(PciCanCardData), GFP_KERNEL);
    if (!vCard) goto card_alloc_err;
    memset(vCard, 0, sizeof(VCanCardData) + sizeof(PciCanCardData));

    // hwCardData is directly after VCanCardData 
    vCard->hwCardData = vCard + 1;    

    // Allocate memory for n channels 
    chs = kmalloc(sizeof(ChanHelperStruct), GFP_KERNEL);
    if (!chs) goto chan_alloc_err;
    memset(chs, 0, sizeof(ChanHelperStruct));

    // Init array and hwChanData 
    for (chNr = 0; chNr < MAX_CHANNELS; chNr++){
        chs->dataPtrArray[chNr] = &chs->vChd[chNr];
        chs->vChd[chNr].hwChanData = &chs->hChd[chNr];
    }
    vCard->chanData = chs->dataPtrArray;

    // Get PCI controller, SJA1000 base and Xilinx addresses 
    if (!readPCIAddresses(dev, vCard)) {
        printk("<1>readPCIAddresses failed");
        goto pci_err;
    }

    // Find out type of card i.e. N/O channels etc 
    if (pciCanProbe(vCard)) {
        printk("<1>pciCanProbe failed");
        goto probe_err;
    }

    // Init channels 
    pciCanInitData(vCard);

    os_if_spin_lock(&canCardsLock);
    // Insert into list of cards 
    vCard->next = canCards;
    canCards = vCard;
    os_if_spin_unlock(&canCardsLock);
    
    // ISR 
    request_irq(vCard->irq, pciCanInterrupt, SA_SHIRQ, "Kvaser PCIcan", vCard);
    // Init h/w  & enable interrupts in PCI Interface 
    if (pciCanInitHW (vCard)) {
        printk("<1> pciCanInitHW failed");
        goto intr_err;
    }

    return 1;

intr_err:
    free_irq(vCard->irq, vCard);
    kfree(vCard->chanData);
chan_alloc_err:
probe_err:
pci_err:
    kfree(vCard);
card_alloc_err:
    return 0;
} // pciCanInitOne 


//======================================================================
// Find and initialize all cards                                        
//======================================================================
int pciCanInitAllDevices(void)
{
    struct pci_dev *dev = NULL;
    int found;

    // obsolete
    //if (!pci_present())
        //return -ENODEV;

    for (found=0; found < PCICAN_MAX_DEV;) {
        dev = pci_find_device(PCICAN_VENDOR, PCICAN_ID, dev);
        if (!dev) // No more PCIcans... 
            break;
        // Initialize card 
        found += pciCanInitOne(dev);
        DEBUGPRINT(1, "pciCanInitAllDevices %d\n", found);
    }
    // We need to find at least one 
    return  (found == 0) ? -ENODEV : 0;
} // pciCanInitAllDevices 


//======================================================================
// Shut down and free resources before unloading driver                 
//======================================================================
int pciCanCloseAllDevices(void)
{
    // qqq check for open files 
    VCanCardData *vCard;

    os_if_spin_lock(&canCardsLock);
    vCard = canCards;
    while (vCard) {
        DEBUGPRINT(1, "pciCanCloseAllDevices\n");
        free_irq(vCard->irq, vCard);
        vCard = canCards->next;
        kfree(canCards->chanData);
        kfree(canCards);
        canCards = vCard;
    }
    os_if_spin_unlock(&canCardsLock);
    return 0;
} // pciCanCloseAllDevices 

