/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

/* Kvaser CAN driver PCIcan hardware specific parts */
/* Definition of certain (not all) registers and bits in the AMCC 5920. */

#ifndef S5920_H
#define S5920_H

//
// Chip register offsets
//
#define S5920_OMB    0x0C
#define S5920_IMB    0x1C
#define S5920_MBEF   0x34
#define S5920_INTCSR 0x38
#define S5920_RCR    0x3C
#define S5920_PTCR   0x60

//
// PCI Interrupt Control/status Register (INTCSR)
//
#define INTCSR_INTERRUPT_ASSERTED_V     23
#define INTCSR_INTERRUPT_ASSERTED_M     0x800000
#define INTCSR_ADDON_INTSTAT_V          22
#define INTCSR_ADDON_INTSTAT_M          0x400000
#define INTCSR_ADDON_INTENABLE_V        13
#define INTCSR_ADDON_INTENABLE_M        0x2000


//
// PCI Reset Control Register (RCR)
//
#define RCR_ADDON_RESET_V   24
#define RCR_ADDON_RESET_M   0x1000000



#endif

