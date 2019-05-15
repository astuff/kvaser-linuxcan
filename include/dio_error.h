/*
**                    Copyright 2010 by KVASER AB, SWEDEN      
**                        WWW: http://www.kvaser.com
**
** This software is furnished under a license and may be used and copied
** only in accordance with the terms of such license.
**
** Description:
**   These are the dio results from diskio.h in minihydra and m32firm
** ---------------------------------------------------------------------------
*/
#ifndef DIO_ERROR_H
#define DIO_ERROR_H

typedef enum {
  dio_OK = 0,
  dio_CRCError,         // 1: Our own checksum failed. May be OK for some operations.
  dio_SectorErased,     // 2: The sector is empty. Probably OK at all times.
  dio_Pending,          // 3:
  dio_IllegalRequest,   // 4:
  dio_QueueFull,        // 5:
  dio_FileNotFound,     // 6:
  dio_FileError,        // 7:
  dio_NotImplemented,   // 8:
  dio_Custom1,          // 9: Just if another package wants to store someting
                        // in a status variable. This value is
                        // (normally) not assigned by a diskio call.
  dio_NotFormatted,     // 10:
  dio_Timeout,          // 11:
  dio_WrongDiskType,    // 12:
  dio_DiskError,        // 13: The disk reported an error.
  dio_DiskCommError,    // 14: A disk communication problem was detected.
  dio_NoDisk,           // 15: The disk is missing
  dio_NoMemory,         // 16: Memory allocation failed; or other resource shortage
  dio_UserCancel,       // 17: User cancelled the operation
  dio_CpldError,        // 18:
  dio_ConfigError,      // 19: Some type of configuration error (e.g. corrupt config file)
  dio_DiskIdle,         // 20: The R1 response says the disk is in idle state.
  dio_EOF,              // 21: End Of File while reading.
  dio_SPI               // 22: SPI bus in use by another module; wait
} DioResult;

#endif // DIO_ERROR_H
