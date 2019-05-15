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

#include <linux/seq_file.h>
#include <linux/math64.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)
#include <linux/module.h>
#else
#include <linux/export.h>
#endif /* LINUX_VERSION_CODE */
#include "ticks.h"

#define TIMESTAMP_WRAP_STATE_LO       0
#define TIMESTAMP_WRAP_STATE_NORMAL   1
#define TIMESTAMP_WRAP_STATE_HIGH     2

#define WRAP_BIT              (uint64_t)0x0001000000000000ULL
#define WRAP_MAX              (uint64_t)0x0000ffffffffffffULL
#define WRAP_LO_LIMIT         (uint64_t)0x0000400000000000ULL
#define WRAP_HIGH_LIMIT       (uint64_t)0x0000800000000000ULL

void ticks_init  (ticks_class* self)
{
  self->high16 = 0;
  self->state  = TIMESTAMP_WRAP_STATE_NORMAL;
}
EXPORT_SYMBOL(ticks_init);

uint64_t ticks_to_64bit_ns (ticks_class* self, uint64_t n_ticks, uint32_t freq_mhz)
{
  uint64_t retval;

  n_ticks &= (WRAP_MAX);

  switch (self->state)
  {
    case TIMESTAMP_WRAP_STATE_LO:
      if (n_ticks > WRAP_HIGH_LIMIT) {
        retval = (self->high16-WRAP_BIT) + n_ticks;
      } else {
        if(n_ticks > WRAP_LO_LIMIT) {
          self->state = TIMESTAMP_WRAP_STATE_NORMAL;
          printk (KERN_INFO "ticks_to_64bit_ns N n_ticks=%16llx, w_n_ticks=%16llx\n", n_ticks, self->high16 + n_ticks);
        }
        retval = self->high16 + n_ticks;
      }
      break;

    case TIMESTAMP_WRAP_STATE_NORMAL:
      if (n_ticks > WRAP_HIGH_LIMIT) {
        self->state = TIMESTAMP_WRAP_STATE_HIGH;
        printk (KERN_INFO "ticks_to_64bit_ns H n_ticks=%16llx, w_n_ticks=%16llx\n", n_ticks, self->high16 + n_ticks);
      }
      retval = self->high16 + n_ticks;
      break;

    case TIMESTAMP_WRAP_STATE_HIGH:
      if (n_ticks < WRAP_LO_LIMIT) {
        self->state = TIMESTAMP_WRAP_STATE_LO;
        self->high16 += WRAP_BIT;
        printk (KERN_INFO "ticks_to_64bit_ns L n_ticks=%16llx, w_n_ticks=%16llx\n", n_ticks, self->high16 + n_ticks);
      }
      retval = self->high16 + n_ticks;
      break;

    default:
      printk (KERN_INFO "Error: ticks_to_64bit_ns Unknown state %d\n", self->state);
      retval = 0;
      break;
  }

  return div_u64 (retval * 1000, freq_mhz); //convert to nano seconds
}
EXPORT_SYMBOL(ticks_to_64bit_ns);
