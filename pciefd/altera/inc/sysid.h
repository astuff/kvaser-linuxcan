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

#ifndef __SYSID_H__
#define __SYSID_H__

#include "io.h"
#include "inc/bits.h"

// Baudrate
#define SYSID_DATE_REG                  0
#define SYSID_TIME_REG                  1
#define SYSID_REVISION_REG              2
#define SYSID_FREQUENCY_REG             3
#define SYSID_BUS_FREQUENCY_REG         4
#define SYSID_BUILD_L_REG               5
#define SYSID_BUILD_H_REG               6

#define IOADDR_SYSID_DATE(base)               __IO_CALC_ADDRESS_NATIVE(base, SYSID_DATE_REG)
#define IORD_SYSID_DATE(base)                 IORD(base, SYSID_DATE_REG)
#define IOWR_SYSID_DATE(base, data)           IOWR(base, SYSID_DATE_REG, data)

#define IOADDR_SYSID_TIME(base)               __IO_CALC_ADDRESS_NATIVE(base, SYSID_TIME_REG)
#define IORD_SYSID_TIME(base)                 IORD(base, SYSID_TIME_REG)
#define IOWR_SYSID_TIME(base, data)           IOWR(base, SYSID_TIME_REG, data)

#define IOADDR_SYSID_REVISION(base)           __IO_CALC_ADDRESS_NATIVE(base, SYSID_REVISION_REG)
#define IORD_SYSID_REVISION(base)             IORD(base, SYSID_REVISION_REG)
#define IOWR_SYSID_REVISION(base, data)       IOWR(base, SYSID_REVISION_REG, data)

#define IOADDR_SYSID_FREQUENCY(base)          __IO_CALC_ADDRESS_NATIVE(base, SYSID_FREQUENCY_REG)
#define IORD_SYSID_FREQUENCY(base)            IORD(base, SYSID_FREQUENCY_REG)
#define IOWR_SYSID_FREQUENCY(base, data)      IOWR(base, SYSID_FREQUENCY_REG, data)

#define IOADDR_SYSID_BUS_FREQUENCY(base)      __IO_CALC_ADDRESS_NATIVE(base, SYSID_BUS_FREQUENCY_REG)
#define IORD_SYSID_BUS_FREQUENCY(base)        IORD(base, SYSID_BUS_FREQUENCY_REG)
#define IOWR_SYSID_BUS_FREQUENCY(base, data)  IOWR(base, SYSID_BUS_FREQUENCY_REG, data)

#define IOADDR_SYSID_BUILD_L(base)            __IO_CALC_ADDRESS_NATIVE(base, SYSID_BUILD_L_REG)
#define IORD_SYSID_BUILD_L(base)              IORD(base, SYSID_BUILD_L_REG)
#define IOWR_SYSID_BUILD_L(base, data)        IOWR(base, SYSID_BUILD_L_REG, data)

#define IOADDR_SYSID_BUILD_H(base)            __IO_CALC_ADDRESS_NATIVE(base, SYSID_BUILD_H_REG)
#define IORD_SYSID_BUILD_H(base)              IORD(base, SYSID_BUILD_H_REG)
#define IOWR_SYSID_BUILD_H(base, data)        IOWR(base, SYSID_BUILD_H_REG, data)

#define SYSID_VERSION_NUM_CONT_LSHIFT       24
#define SYSID_VERSION_NUM_CONT_NBITS        8
#define SYSID_VERSION_NUM_CONT_MSK          mask(SYSID_VERSION_NUM_CONT)
#define SYSID_VERSION_NUM_CONT_GET(value)   get(SYSID_VERSION_NUM_CONT, value)
#define SYSID_VERSION_NUM_CONT(value)       field(SYSID_VERSION_NUM_CONT, value)

#define SYSID_VERSION_MAJOR_LSHIFT          16
#define SYSID_VERSION_MAJOR_NBITS           8
#define SYSID_VERSION_MAJOR_MSK             mask(SYSID_VERSION_MAJOR)
#define SYSID_VERSION_MAJOR_GET(value)      get(SYSID_VERSION_MAJOR, value)
#define SYSID_VERSION_MAJOR(value)          field(SYSID_VERSION_MAJOR, value)

#define SYSID_VERSION_MINOR_LSHIFT          0
#define SYSID_VERSION_MINOR_NBITS           8
#define SYSID_VERSION_MINOR_MSK             mask(SYSID_VERSION_MINOR)
#define SYSID_VERSION_MINOR_GET(value)      get(SYSID_VERSION_MINOR, value)
#define SYSID_VERSION_MINOR(value)          field(SYSID_VERSION_MINOR, value)

#define SYSID_BUILD_UC_LSHIFT               0
#define SYSID_BUILD_UC_NBITS                1
#define SYSID_BUILD_UC_MSK                  mask(SYSID_BUILD_UC)
#define SYSID_BUILD_UC_GET(value)           get(SYSID_BUILD_UC, value)
#define SYSID_BUILD_UC(value)               field(SYSID_BUILD_UC, value)

#define SYSID_BUILD_SEQNO_LSHIFT            1
#define SYSID_BUILD_SEQNO_NBITS             15
#define SYSID_BUILD_SEQNO_MSK               mask(SYSID_BUILD_SEQNO)
#define SYSID_BUILD_SEQNO_GET(value)        get(SYSID_BUILD_SEQNO, value)
#define SYSID_BUILD_SEQNO(value)            field(SYSID_BUILD_SEQNO, value)

#define SYSID_BUILD_L_HG_ID_LSHIFT          16
#define SYSID_BUILD_L_HG_ID_NBITS           16
#define SYSID_BUILD_L_HG_ID_MSK             mask(SYSID_BUILD_L_HG_ID)
#define SYSID_BUILD_L_HG_ID_GET(value)      get(SYSID_BUILD_L_HG_ID, value)
#define SYSID_BUILD_L_HG_ID(value)          field(SYSID_BUILD_L_HG_ID, value)

#endif
