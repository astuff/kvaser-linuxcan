/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

#ifndef DALLAS_H
#define DALLAS_H

/*****************************************************************************/

/* Commands to Dallas-type Memories */
#define READ_STATUS                 0x66
#define READ_MEMORY                 0xf0
#define WRITE_SCRATCHPAD            0x0f
#define READ_SCRATCHPAD             0xaa
#define COPY_SCRATCHPAD             0x55
#define WRITE_APPLICATION           0x99
#define READ_APPLICATION            0xc3
#define COPY_AND_LOCK_APPLICATION   0x5a
#define READ_ROM                    0x33
#define MATCH_ROM                   0x55
#define SKIP_ROM                    0xcc
#define SEARCH_ROM                  0xf0

/* Device Types */
#define DS1494_CODE                 0x04
#define DS2430_CODE                 0x14

#define COPY_PASSWORD               0xa5
#define STATUS_PASSWORD             0x00

#define MAXMEMORY                   0x04
#define MAXHITLEVEL                 0x04

#define ONE_MEMORY          0x01
#define ALL_FOUND           0x02
#define NOMEMORY            0x03

/*****************************************************************************/

typedef struct s_dallas_context DALLAS_CONTEXT;

struct s_dallas_context {
         unsigned       address_out,
                        address_in;
         unsigned int   in_mask;
         unsigned int   out_mask;
};

typedef enum {
          dsError_None = 0,           // 0
          dsError_NoDevice,           // 1
          dsError_WrongDevice,        // 2
          dsError_ScratchPadError,    // 3
          dsError_ROMError,           // 4
          dsError_EEPROMError,        // 5
          dsError_BusStuckLow,        // 6
          dsError_NotProgrammed,      // 7
          dsError_ApplicationLocked   // 8
} dsStatus;

/*****************************************************************************/

dsStatus ds_init (DALLAS_CONTEXT *dc);

dsStatus ds_reset (DALLAS_CONTEXT *dc);

int ds_check_for_presence (DALLAS_CONTEXT *dc);

int ds_check_for_presence_loop (DALLAS_CONTEXT *dc);

dsStatus ds_read_string (DALLAS_CONTEXT *dc,
                         unsigned int command,
                         unsigned int address,
                         unsigned char *data,
                         int bufsiz);

dsStatus ds_read_memory (DALLAS_CONTEXT *dc,
                         unsigned int address,
                         unsigned char *data,
                         int bufsiz);

dsStatus ds_read_application_area (DALLAS_CONTEXT *dc,
                                   unsigned int address,
                                   unsigned char *data,
                                   int bufsiz);

dsStatus ds_read_rom_64bit (DALLAS_CONTEXT *dc, int port, unsigned char *data);

int ds_verify_area (unsigned char *data, int bufsiz);


/*****************************************************************************/

#endif // DALLAS_H

