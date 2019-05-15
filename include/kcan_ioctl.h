/*
**             Copyright 2012-2016 by Kvaser AB, Molndal, Sweden
**                        http://www.kvaser.com
**
** This software is dual licensed under the following two licenses:
** BSD-new and GPLv2. You may use either one. See the included
** COPYING file for details.
**
** License: BSD-new
** ===============================================================================
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
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
** WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
** DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
** (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
** LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
** ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
**
** License: GPLv2
** ===============================================================================
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**
** ---------------------------------------------------------------------------
**/

/* kcan_ioctl.h: ioctls()'s specific for Kvasers CAN drivers */

#ifndef _KCAN_IOCTL_H
#define _KCAN_IOCTL_H

//#   include <linux/ioctl.h>
#   include <asm/ioctl.h>
#   include "debug.h"

#   define KCAN_IOC_MAGIC 'k'

// For compatibility with Windows #define:s below.
#define VCAN_DEVICE      0     // dummy
#define KCAN_IOCTL_START 0
#define METHOD_BUFFERED  0     // dummy
#define FILE_ANY_ACCESS  0


#define CTL_CODE(x,i,y,z) _IO(KCAN_IOC_MAGIC, (i))


#define KCAN_IOCTL_OBJBUF_FREE_ALL              CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 6, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KCAN_IOCTL_OBJBUF_ALLOCATE              CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 7, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KCAN_IOCTL_OBJBUF_FREE                  CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 8, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KCAN_IOCTL_OBJBUF_WRITE                 CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 9, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KCAN_IOCTL_OBJBUF_SET_FILTER            CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 10, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KCAN_IOCTL_OBJBUF_SET_FLAGS             CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 11, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KCAN_IOCTL_OBJBUF_ENABLE                CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 12, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KCAN_IOCTL_OBJBUF_DISABLE               CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 13, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define KCAN_IOCTL_OBJBUF_SET_PERIOD            CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 22, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KCAN_IOCTL_OBJBUF_SEND_BURST            CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 23, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define KCAN_IOCTL_OBJBUF_SET_MSG_COUNT         CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 34, METHOD_BUFFERED, FILE_ANY_ACCESS)


#define  KCAN_IOCTL_TX_INTERVAL                 CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 68, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define KCAN_IOCTL_CANFD                        CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 69, METHOD_BUFFERED, FILE_ANY_ACCESS)
// Windows code has this in vcanio.h
#define VCAN_IOCTL_GET_CARD_INFO                CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 70, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KCAN_IOCTL_GET_CARD_INFO_2              CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 71, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define KCAN_IOCTL_SET_BRLIMIT                  CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 72, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KCAN_IOCTL_GET_CUST_CHANNEL_NAME        CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 73, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KCAN_IOCTL_GET_CARD_INFO_MISC           CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 74, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define KCANY_IOCTL_MEMO_CONFIG_MODE     CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 75, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KCANY_IOCTL_MEMO_GET_DATA        CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 76, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KCANY_IOCTL_MEMO_PUT_DATA        CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 77, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KCANY_IOCTL_MEMO_DISK_IO         CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 78, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KCANY_IOCTL_MEMO_DISK_IO_FAST    CTL_CODE (VCAN_DEVICE, KCAN_IOCTL_START + 79, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define KCAN_CARDFLAG_FIRMWARE_BETA       0x01  // Firmware is beta
#define KCAN_CARDFLAG_FIRMWARE_RC         0x02  // Firmware is release candidate
#define KCAN_CARDFLAG_AUTO_RESP_OBJBUFS   0x04  // Firmware supports auto-response object buffers
#define KCAN_CARDFLAG_REFUSE_TO_RUN       0x08  // Major problem detected
#define KCAN_CARDFLAG_REFUSE_TO_USE_CAN   0x10  // Major problem detected
#define KCAN_CARDFLAG_AUTO_TX_OBJBUFS     0x20  // Firmware supports periodic transmit object buffers


#define KCAN_DRVFLAG_BETA                 0x01    // Driver is beta

#if defined(DEVHND_DRIVER_IS_BETA)
CompilerAssert(KCAN_DRVFLAG_BETA == DEVHND_DRIVER_IS_BETA);
#endif




#define CAN_CANFD_SUCCESS          0
#define CAN_CANFD_MISMATCH        -1
#define CAN_CANFD_NOT_IMPLEMENTED -2
#define CAN_CANFD_FAILURE         -3
#define CANFD 1
#define CAN   0
#define CAN_CANFD_SET          1
#define CAN_CANFD_READ         2
#define CAN_CANFD_READ_VERSION 3

typedef struct {
  unsigned int fd;   // CANFD, CAN
  unsigned int action; // CAN_CANFD_SET, CAN_CANFD_READ
  unsigned int reply; // reply from read?
  int status; // CAN_CANFD_MATCHING, CAN_CANFD_MISMATCH
  unsigned int unused[8];
} KCAN_IOCTL_CANFD_T;

typedef struct {
  unsigned int  status;     // !0 => no name found for this channel.
  unsigned char data[64];   // Transfered bytes between user space and kernel space
} KCAN_IOCTL_GET_CUST_CHANNEL_NAME_T;

#define KCAN_IOCTL_MISC_INFO_REMOTE_TYPE     0
#define KCAN_IOCTL_MISC_INFO_LOGGER_TYPE     1
#define KCAN_IOCTL_MISC_INFO_WEBSERVER_TYPE  2

/*possible values*/
#define KCAN_IOCTL_MISC_INFO_NOT_IMPLEMENTED      0
#define KCAN_IOCTL_MISC_INFO_REMOTE_TYPE_WLAN     1
#define KCAN_IOCTL_MISC_INFO_REMOTE_TYPE_LAN      2
#define KCAN_IOCTL_MISC_INFO_LOGGER_TYPE_V1       1
#define KCAN_IOCTL_MISC_INFO_LOGGER_TYPE_V2       2
#define KCAN_IOCTL_MISC_INFO_REMOTE_WEBSERVER_V1  1

typedef struct {
  unsigned int type;
  unsigned int value;
} KCAN_IOCTL_MISC_INFO;

//===========================================================================
// for KCAN_IOCTL_LED_ACTION_I
#define KCAN_LED_SUBCOMMAND_ALL_LEDS_ON    0
#define KCAN_LED_SUBCOMMAND_ALL_LEDS_OFF   1
#define KCAN_LED_SUBCOMMAND_LED_0_ON       2
#define KCAN_LED_SUBCOMMAND_LED_0_OFF      3
#define KCAN_LED_SUBCOMMAND_LED_1_ON       4
#define KCAN_LED_SUBCOMMAND_LED_1_OFF      5
#define KCAN_LED_SUBCOMMAND_LED_2_ON       6
#define KCAN_LED_SUBCOMMAND_LED_2_OFF      7
#define KCAN_LED_SUBCOMMAND_LED_3_ON       8
#define KCAN_LED_SUBCOMMAND_LED_3_OFF      9

typedef struct s_kcan_ioctl_led_action {
  unsigned long   sub_command;    // One of KCAN_LED_SUBCOMMAND_xxx
  int             timeout;
} KCAN_IOCTL_LED_ACTION_I;

typedef struct {
  int             interval;
  int             padding[10]; // for future usage.
} KCANY_CONFIG_MODE;

typedef struct {
  unsigned int   subcommand;
  int            status;          // From the driver: MEMO_STATUS_xxx
  int            dio_status;      // From the driver: DioResult
  int            lio_status;      // From the driver: LioResult
  unsigned int   buflen;
  unsigned int   timeout;         // Timeout in ms
  unsigned char  buffer[1000];    // Contents & usage dependent on subcommand
} KCANY_MEMO_INFO;


typedef struct {
  unsigned int  subcommand;
  unsigned int  first_sector;
  unsigned int  count;
  unsigned char buffer[512];
  int status;                     // From the driver: MEMO_STATUS_...
  int dio_status;                 // From the driver: DioResult
  int lio_status;                 // From the driver: LioResult
} KCANY_MEMO_DISK_IO;


typedef struct {
  unsigned int subcommand;        // To the driver: ..IO_FASTREAD..
  unsigned int first_sector;      // To the driver: first sector no
  unsigned int count;             // To the driver: sector count
  int status;                     // From the driver: MEMO_STATUS_...
  int dio_status;                 // From the driver: DioResult
  int lio_status;                 // From the driver: LioResult
  unsigned char buffer[16][512];
} KCANY_MEMO_DISK_IO_FAST;


// Must agree with MEMO_SUBCMD_xxx in filo_cmd.h
#define KCANY_MEMO_DISK_IO_READ_PHYSICAL_SECTOR         4
#define KCANY_MEMO_DISK_IO_WRITE_PHYSICAL_SECTOR        5
#define KCANY_MEMO_DISK_IO_ERASE_PHYSICAL_SECTOR        6
#define KCANY_MEMO_DISK_IO_READ_LOGICAL_SECTOR          7
#define KCANY_MEMO_DISK_IO_WRITE_LOGICAL_SECTOR         8
#define KCANY_MEMO_DISK_IO_ERASE_LOGICAL_SECTOR         9
#define KCANY_MEMO_DISK_IO_FASTREAD_PHYSICAL_SECTOR    17
#define KCANY_MEMO_DISK_IO_FASTREAD_LOGICAL_SECTOR     18

#define KCAN_USBSPEED_NOT_AVAILABLE   0
#define KCAN_USBSPEED_FULLSPEED       1
#define KCAN_USBSPEED_HISPEED         2

typedef struct s_kcan_ioctl_card_info_2 {
    unsigned char   ean[8];
    unsigned long   hardware_address;
    unsigned long   ui_number;
    unsigned long   usb_speed;            // KCAN_USBSPEED_xxx
    unsigned long   softsync_running;
    long            softsync_instab;
    long            softsync_instab_max;
    long            softsync_instab_min;
    unsigned long   card_flags;           // KCAN_CARDFLAG_xxx
    unsigned long   driver_flags;         // KCAN_DRVFLAG_xxx
    char            pcb_id[32];           // e.g. P023B002V1-2 (see doc Q023-059)
    unsigned long   mfgdate;              // Seconds since 1970-01-01
    unsigned long   usb_host_id;          // Checksum of USB host controller
    unsigned int    usb_throttle;         // Enforced delay between transmission of commands.
    unsigned char   reserved[40];
} KCAN_IOCTL_CARD_INFO_2;
#if defined(CompilerAssert)
CompilerAssert(sizeof(KCAN_IOCTL_CARD_INFO_2) == 128);
#endif
#endif /* KCANIO_H */

