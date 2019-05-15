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
**  Defines in common for Memorators (both kcanl and kcany)
**
** -----------------------------------------------------------------------------
*/

#ifndef KCANIO_MEMORATOR_H
#define KCANIO_MEMORATOR_H

//---------------------------------------------------------------------------
// NOTE that these defines HAVE to exactly match the defines in both hydra_host_cmds.h
// and filo_cmds.h, if you add a define add a compiler assert in /drv/
//---------------------------------------------------------------------------

#define KCANIO_MEMO_STATUS_SUCCESS                    0
#define KCANIO_MEMO_STATUS_MORE_DATA                  1
#define KCANIO_MEMO_STATUS_UNKNOWN_COMMAND            2
#define KCANIO_MEMO_STATUS_FAILED                     3 // .data[] contains more info
#define KCANIO_MEMO_STATUS_EOF                        4

// *All*K subcommands to be in one number series
#define KCANIO_MEMO_SUBCMD_GET_FS_INFO                1 // Get DOS filesys info; for get_data
#define KCANIO_MEMO_SUBCMD_GET_DISK_INFO_A            2 // Get disk info; for get_data
#define KCANIO_MEMO_SUBCMD_GET_DISK_INFO_B            3 // Get logio info; for get_data

#define KCANIO_MEMO_SUBCMD_READ_PHYSICAL_SECTOR       4
#define KCANIO_MEMO_SUBCMD_WRITE_PHYSICAL_SECTOR      5
#define KCANIO_MEMO_SUBCMD_ERASE_PHYSICAL_SECTOR      6
#define KCANIO_MEMO_SUBCMD_READ_LOGICAL_SECTOR        7
#define KCANIO_MEMO_SUBCMD_WRITE_LOGICAL_SECTOR       8
#define KCANIO_MEMO_SUBCMD_ERASE_LOGICAL_SECTOR       9

#define KCANIO_MEMO_SUBCMD_FORMAT_DISK               10 // Format disk (FAT16 or -32) asyncop
#define KCANIO_MEMO_SUBCMD_INIT_DISK                 11 // Create logdata.kmf, asyncop
#define KCANIO_MEMO_SUBCMD_CLEAR_DATA                12 // Clear logdata.kmf, asyncop

#define KCANIO_MEMO_SUBCMD_GET_MISC_INFO             13 // for get_data
#define KCANIO_MEMO_SUBCMD_GET_RTC_INFO              14 // for get_data
#define KCANIO_MEMO_SUBCMD_PUT_RTC_INFO              15 // for put_data

#define KCANIO_MEMO_SUBCMD_GET_FS_INFO_B             16 // Get various filesystem info

#define KCANIO_MEMO_SUBCMD_FASTREAD_PHYSICAL_SECTOR  17
#define KCANIO_MEMO_SUBCMD_FASTREAD_LOGICAL_SECTOR   18

#define KCANIO_MEMO_SUBCMD_OPEN_FILE                 19
#define KCANIO_MEMO_SUBCMD_READ_FILE                 20
#define KCANIO_MEMO_SUBCMD_CLOSE_FILE                21
#define KCANIO_MEMO_SUBCMD_WRITE_FILE                22
#define KCANIO_MEMO_SUBCMD_DELETE_FILE               23


/*****************************************************************************/

#endif /* KCANIO_MEMORATOR_H */
