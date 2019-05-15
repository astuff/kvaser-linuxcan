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

#include "dlc.h"

#ifdef __KERNEL__
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)
#include <linux/module.h>
#else
#include <linux/export.h>
#endif /* LINUX_VERSION_CODE */
#endif /* __KERNEL */

uint32_t dlc_bytes_to_dlc_fd (uint32_t n_bytes)
{
  if (n_bytes > 48) return 15;
  else if (n_bytes > 32) return 14;
  else if (n_bytes > 24) return 13;
  else if (n_bytes > 20) return 12;
  else if (n_bytes > 16) return 11;
  else if (n_bytes > 12) return 10;
  else if (n_bytes >  8) return 9;
  else return n_bytes;
}
#ifdef __KERNEL__
EXPORT_SYMBOL(dlc_bytes_to_dlc_fd);
#endif

uint32_t dlc_dlc_to_bytes_fd (uint32_t dlc)
{
  switch (dlc & 0x0000000FU) {
    case 9:   return 12;
    case 10:  return 16;
    case 11:  return 20;
    case 12:  return 24;
    case 13:  return 32;
    case 14:  return 48;
    case 15:  return 64;
    default:  return dlc;
  }
}
#ifdef __KERNEL__
EXPORT_SYMBOL(dlc_dlc_to_bytes_fd);
#endif

uint32_t dlc_is_dlc_ok (uint32_t accept_large_dlc, uint32_t is_fd, uint32_t dlc)
{
  if (is_fd)  {
    return ((dlc <= 8)  ||
            (dlc == 12) || (dlc == 16) ||
            (dlc == 20) || (dlc == 24) ||
            (dlc == 32) || (dlc == 48) ||
            (dlc == 64));
  } else if (accept_large_dlc) {
    return 1;
  } else {
    return (dlc <= 8);
  }
}
#ifdef __KERNEL__
EXPORT_SYMBOL(dlc_is_dlc_ok);
#endif

uint32_t dlc_dlc_to_bytes_classic (uint32_t dlc)
{
  if ((dlc & 0x0000000FU) > 8) {
    return 8;
  } else {
    return dlc;
  }
}
#ifdef __KERNEL__
EXPORT_SYMBOL(dlc_dlc_to_bytes_classic);
#endif
