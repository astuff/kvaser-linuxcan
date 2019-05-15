/*
**                    Copyright 2010 by KVASER AB, SWEDEN      
**                        WWW: http://www.kvaser.com
**
** This software is furnished under a license and may be used and copied
** only in accordance with the terms of such license.
**
** Description:
**   These are the lio results from logio.h in minihydra and m32firm
** ---------------------------------------------------------------------------
*/
#ifndef LIO_ERROR_H
#define LIO_ERROR_H

typedef enum {
  lio_OK = 0,
  lio_queueFull = 1,
  lio_CRCError = 2,
  lio_SectorErased = 3,
  lio_FileError = 4,
  lio_DiskError = 5,
  lio_DiskFull_Dir = 6,
  lio_DiskFull_Data = 7,
  lio_EOF = 8,
  lio_SeqError = 9,
  lio_Error = 10,
  lio_FileSystemCorrupt = 11,
  lio_UnsupportedVersion = 12,
  lio_NotImplemented = 13,
  lio_FatalError = 14,
  lio_State = 15
} LioResult;

#endif // LIO_ERROR_H
