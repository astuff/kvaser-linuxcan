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

#include <linux/module.h>
#include "capabilities.h"
#include "vcan_ioctl.h"
#include "hydra_host_cmds.h"

static void set_capability (uint32_t *self, uint32_t cap, uint32_t to)
{
  if (to) {
    *self |= cap;
  } else {
    *self &= ~cap;
  }
}

void set_capability_value (VCanCardData *vCard, uint32_t cap, uint32_t to, uint32_t channel_mask, uint32_t n_channels_max)
{
  uint32_t i;
  for (i = 0; i < n_channels_max; i++) {
    if ((1 << i) & channel_mask) {
      VCanChanData *vChd = vCard->chanData[i];
      set_capability (&vChd->capabilities, cap, to);
    }
  }
}
EXPORT_SYMBOL(set_capability_value);

void set_capability_mask (VCanCardData *vCard, uint32_t cap, uint32_t to, uint32_t channel_mask, uint32_t n_channels_max)
{
  uint32_t i;
  for (i = 0; i < n_channels_max; i++) {
    if ((1 << i) & channel_mask) {
      VCanChanData *vChd = vCard->chanData[i];
      set_capability (&vChd->capabilities_mask, cap, to);
    }
  }
}
EXPORT_SYMBOL(set_capability_mask);

uint8_t convert_vcan_to_hydra_cmd (uint32_t vcan_cmd) {
  switch (vcan_cmd) {
    case VCAN_CHANNEL_CAP_SILENTMODE:          return CAP_SUB_CMD_SILENT_MODE; break;
    case VCAN_CHANNEL_CAP_SEND_ERROR_FRAMES:   return CAP_SUB_CMD_ERRFRAME; break;
    case VCAN_CHANNEL_CAP_BUSLOAD_CALCULATION: return CAP_SUB_CMD_BUS_STATS; break;
    case VCAN_CHANNEL_CAP_ERROR_COUNTERS:      return CAP_SUB_CMD_ERRCOUNT_READ; break;
    case VCAN_CHANNEL_CAP_SINGLE_SHOT:         return CAP_SUB_CMD_SINGLE_SHOT; break;
    case VCAN_CHANNEL_CAP_SYNC_TX_FLUSH:       return CAP_SUB_CMD_SYNC_TX_FLUSH; break;
    case VCAN_CHANNEL_CAP_LIN_HYBRID:          return CAP_SUB_CMD_LIN_HYBRID; break;
    case VCAN_CHANNEL_CAP_HAS_LOGGER:          return CAP_SUB_CMD_HAS_LOGGER; break;
    case VCAN_CHANNEL_CAP_HAS_REMOTE:          return CAP_SUB_CMD_HAS_REMOTE; break;
    case VCAN_CHANNEL_CAP_HAS_SCRIPT:          return CAP_SUB_CMD_HAS_SCRIPT; break;
    case VCAN_CHANNEL_CAP_HAS_IO_API:          return CAP_SUB_CMD_HAS_IO_API; break;
    case VCAN_CHANNEL_CAP_DIAGNOSTICS:         return CAP_SUB_CMD_HAS_KDI; break;
    default: return 0; break;
  }
}
EXPORT_SYMBOL(convert_vcan_to_hydra_cmd);

int card_has_capability (VCanCardData *vCard, uint32_t cap, uint32_t n_channels_max)
{
  uint32_t i;
  for (i = 0; i < n_channels_max; i++) {
    VCanChanData *vChd = vCard->chanData[i];
    if (vChd->capabilities & cap) return 1;
  }
  return 0;
}
EXPORT_SYMBOL(card_has_capability);
