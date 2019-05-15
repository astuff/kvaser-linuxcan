/*
**             Copyright 2017 by Kvaser AB, Molndal, Sweden
**                         http://www.kvaser.com
**
** This software is dual licensed under the following two licenses:
** BSD-new and GPLv2. You may use either one. See the included
** COPYING file for details.
**
** License: BSD-new
** ==============================================================================
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
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
** BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
** IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
** POSSIBILITY OF SUCH DAMAGE.
**
**
** License: GPLv2
** ==============================================================================
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
**
**
** IMPORTANT NOTICE:
** ==============================================================================
** This source code is made available for free, as an open license, by Kvaser AB,
** for use with its applications. Kvaser AB does not accept any liability
** whatsoever for any third party patent or other immaterial property rights
** violations that may result from any usage of this source code, regardless of
** the combination of source code and various applications that it can be used
** in, or with.
**
** -----------------------------------------------------------------------------
*/

/*
** Description:
**   These are the dio results from diskio.h in minihydra and m32firm
** -----------------------------------------------------------------------------
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
