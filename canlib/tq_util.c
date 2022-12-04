/*
**             Copyright 2018 by Kvaser AB, Molndal, Sweden
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

#include "tq_util.h"
#include "kvdebug.h"
#include "stdio.h"

/*returns -1 if bad parameters, otherwize 0*/
int tqu_check_nominal (const kvBusParamsTq self)
{
  if ((self.tq < 0) || (self.prop < 0) || (self.phase1 < 0) || (self.phase2 < 0) || (self.sjw < 0) || (self.prescaler < 0)) {
    DEBUGPRINTF(("canERR_PARAM (nom): All arguments must be positive: tq=%d, prop=%d,"
                 " phase1=%d, phase2=%d, sjw=%d, prescaler=%d\n", self.tq, self.prop,
                 self.phase1, self.phase2, self.sjw, self.prescaler));
    return -1;
  }

  if (self.tq != (1 + self.prop + self.phase1 + self.phase2)) {
    DEBUGPRINTF(("canERR_PARAM (nom): Mismatch in sum: 1 (sync) + %d (prop) +"
                 " %d (phase1) + %d (phase2) != %d (tq)", self.prop, self.phase1,
                 self.phase2, self.tq));
    return -1;
  }

  if (self.tq <= 2) {
    DEBUGPRINTF(("canERR_PARAM (nom): tq is too small, %d (tq) >= 3", self.tq));
    return -1;
  }

  if (self.sjw > self.phase1) {
    DEBUGPRINTF(("canERR_PARAM (nom): sjw is too big, %d (sjw) <= %d (phase1)",
                 self.sjw, self.phase1));
    return -1;
  }

  if (self.sjw > self.phase2) {
    DEBUGPRINTF(("canERR_PARAM (nom): sjw is too big, %d (sjw) <= %d (phase2)",
                 self.sjw, self.phase2));
    return -1;
  }

  return 0;
}

/*returns -1 if bad parameters, otherwize 0*/
int tqu_check_data (const kvBusParamsTq self)
{
  if ((self.tq < 0) || (self.prop < 0) || (self.phase1 < 0) || (self.phase2 < 0) || (self.sjw < 0) || (self.prescaler < 0)) {
    DEBUGPRINTF(("canERR_PARAM (data): All arguments must be positive: tq=%d, prop=%d,"
                 " phase1=%d, phase2=%d, sjw=%d, prescaler=%d\n", self.tq, self.prop,
                 self.phase1, self.phase2, self.sjw, self.prescaler));
    return -1;
  }
  if (self.tq != (1 + self.prop + self.phase1 + self.phase2)) {
    return -1;
  }

  if (self.tq <= 2) {
    DEBUGPRINTF(("canERR_PARAM (data): tq is too small, %d (tq) >= 3", self.tq));
    return -1;
  }

  if (self.sjw > self.phase1) {
    DEBUGPRINTF(("canERR_PARAM (data): sjw is too big, %d (sjw) <= %d (phase1)",
                 self.sjw, self.phase1));
    return -1;
  }
  if (self.sjw > self.phase2) {
    DEBUGPRINTF(("canERR_PARAM (data): sjw is too big, %d (sjw) <= %d (phase2)",
                 self.sjw, self.phase2));
    return -1;
  }

  return 0;
}

canStatus tqu_translate_bitrate_constant (int freq, kvBusParamsTq *nominal)
{
  switch (freq) {
    case canBITRATE_1M:
      tqu_set_busparam_values(nominal, 8, 2, 2, 1, 3, 10);
      break;
    case canBITRATE_500K:
      tqu_set_busparam_values(nominal, 8, 2, 2, 1, 3, 20);
      break;
    case canBITRATE_250K:
      tqu_set_busparam_values(nominal, 8, 2, 2, 1, 3, 40);
      break;
    case canBITRATE_125K:
      tqu_set_busparam_values(nominal, 16, 4, 4, 1, 7, 40);
      break;
    case canBITRATE_100K:
      tqu_set_busparam_values(nominal, 16, 4, 4, 1, 7, 50);
      break;
    case canBITRATE_83K:
      tqu_set_busparam_values(nominal, 8, 2, 2, 2, 3, 120);
      break;
    case canBITRATE_62K:
      tqu_set_busparam_values(nominal, 16, 4, 4, 1, 7, 80);
      break;
    case canBITRATE_50K:
      tqu_set_busparam_values(nominal, 16, 4, 4, 1, 7, 100);
      break;
    case canBITRATE_10K:
      tqu_set_busparam_values(nominal, 16, 4, 4, 1, 7, 500);
      break;
    default:
      return canERR_PARAM;
  }

  return canOK;
}

canStatus tqu_translate_bitrate_constant_fd (int freqA, int freqD, kvBusParamsTq *arbitration, kvBusParamsTq *data)
{
  switch (freqA) {
    case canFD_BITRATE_1M_80P:
      tqu_set_busparam_values(arbitration, 80, 16, 16, 16, 47, 1);
      break;
    case canFD_BITRATE_500K_80P:
      tqu_set_busparam_values(arbitration, 80, 16, 16, 16, 47, 2);
      break;
    default:
      return canERR_PARAM;
  }
  switch (freqD) {
    case canFD_BITRATE_8M_80P:
      tqu_set_busparam_values(data, 10, 7, 2, 1, 0, 1);
      break;
    case canFD_BITRATE_8M_70P:
      tqu_set_busparam_values(data, 10, 6, 3, 1, 0, 1);
      break;
    case canFD_BITRATE_8M_60P:
      tqu_set_busparam_values(data, 5, 2, 2, 1, 0, 2);
      break;
    case canFD_BITRATE_4M_80P:
      tqu_set_busparam_values(data, 10, 7, 2, 2, 0, 2);
      break;
    case canFD_BITRATE_2M_80P:
      tqu_set_busparam_values(data, 20, 8, 4, 4, 7, 2);
      break;
    case canFD_BITRATE_2M_60P:
      tqu_set_busparam_values(data, 20, 8, 8, 4, 3, 2);
      break;
    case canFD_BITRATE_1M_80P:
      tqu_set_busparam_values(data, 40, 8, 8, 8, 23, 2);
      break;
    case canFD_BITRATE_500K_80P:
      tqu_set_busparam_values(data, 40, 8, 8, 8, 23, 4);
      break;
    default:
      return canERR_PARAM;
  }

  return canOK;
}

/*returns 0 if successful*/
int tqu_set_busparam_values (kvBusParamsTq *busparam, int tq, int phase1, int phase2, int sjw, int prop, int prescaler)
{

  busparam->tq = tq;
  busparam->phase1 = phase1;
  busparam->phase2 = phase2;
  busparam->sjw = sjw;
  busparam->prop = prop;
  busparam->prescaler = prescaler;

  return 0;
}

canStatus tqu_validate_busparameters (const CanHandle hnd, kvBusParamsTq *busparam)
{
  canStatus stat;
  kvClockInfo clockInfo;
  kvBusParamLimits busParamLimits;

  // Modify prescaler if 24 or 16 MHz device
  stat = canGetHandleData(hnd, canCHANNELDATA_CLOCK_INFO, &clockInfo, sizeof(clockInfo));
  if (stat != canOK) return stat;
  if (clockInfo.numerator / clockInfo.denominator == 24) {
    busparam->prescaler = busparam->prescaler * 3 / 10;
  } else if (clockInfo.numerator / clockInfo.denominator == 16) {
    busparam->prescaler = busparam->prescaler * 2 / 10;
  } else if (clockInfo.numerator / clockInfo.denominator != 80) {
    return canERR_NOT_SUPPORTED;
  }

  stat = canGetHandleData(hnd, canCHANNELDATA_BUS_PARAM_LIMITS, &busParamLimits, sizeof(busParamLimits));

  return canOK;
}

canStatus tqu_validate_busparameters_fd (const CanHandle hnd)
{
  canStatus stat;
  kvClockInfo clockInfo;
  kvBusParamLimits busParamLimits;

  // Only supported on 80 MHz devices
  stat = canGetHandleData(hnd, canCHANNELDATA_CLOCK_INFO, &clockInfo, sizeof(clockInfo));
  if (stat != canOK) return stat;
  if (clockInfo.numerator / clockInfo.denominator != 80) {
    return canERR_NOT_SUPPORTED;
  }

  stat = canGetHandleData(hnd, canCHANNELDATA_BUS_PARAM_LIMITS, &busParamLimits, sizeof(busParamLimits));

  return canOK;
}

canStatus get_tq_limits (int hw_type, kvBusParamLimits *bus_param_limits, int has_FD)
{
  bus_param_limits->version = 2;
  switch(hw_type) {
    case canHWTYPE_U100:
      bus_param_limits->arbitration_min.tq = 0;
      bus_param_limits->arbitration_min.phase1 = 1;
      bus_param_limits->arbitration_min.phase2 = 2;
      bus_param_limits->arbitration_min.sjw = 1;
      bus_param_limits->arbitration_min.prop = 1;
      bus_param_limits->arbitration_min.prescaler = 1;

      bus_param_limits->arbitration_max.tq = 0;
      bus_param_limits->arbitration_max.phase1 = 32;
      bus_param_limits->arbitration_max.phase2 = 32;
      bus_param_limits->arbitration_max.sjw = 32;
      bus_param_limits->arbitration_max.prop = 64;
      bus_param_limits->arbitration_max.prescaler = 1024;

      if (has_FD) {
        bus_param_limits->data_min.tq = 0;
        bus_param_limits->data_min.phase1 = 1;
        bus_param_limits->data_min.phase2 = 2;
        bus_param_limits->data_min.sjw = 1;
        bus_param_limits->data_min.prop = 0;
        bus_param_limits->data_min.prescaler = 1;

        bus_param_limits->data_max.tq = 0;
        bus_param_limits->data_max.phase1 = 8;
        bus_param_limits->data_max.phase2 = 8;
        bus_param_limits->data_max.sjw = 8;
        bus_param_limits->data_max.prop = 31;
        bus_param_limits->data_max.prescaler = 1024;
      } else {
        bus_param_limits->data_min.tq = 0;
        bus_param_limits->data_min.phase1 = 0;
        bus_param_limits->data_min.phase2 = 0;
        bus_param_limits->data_min.sjw = 0;
        bus_param_limits->data_min.prop = 0;
        bus_param_limits->data_min.prescaler = 0;

        bus_param_limits->data_max = bus_param_limits->data_min;
      }
      break;

    case canHWTYPE_BLACKBIRD_V2:
    case canHWTYPE_EAGLE:
    case canHWTYPE_ETHERCAN:
      bus_param_limits->arbitration_min.tq = 0;
      bus_param_limits->arbitration_min.phase1 = 1;
      bus_param_limits->arbitration_min.phase2 = 1;
      bus_param_limits->arbitration_min.sjw = 1;
      bus_param_limits->arbitration_min.prop = 1;
      bus_param_limits->arbitration_min.prescaler = 1;

      bus_param_limits->arbitration_max.tq = 0;
      bus_param_limits->arbitration_max.phase1 = 8;
      bus_param_limits->arbitration_max.phase2 = 8;
      bus_param_limits->arbitration_max.sjw = 4;
      bus_param_limits->arbitration_max.prop = 8;
      bus_param_limits->arbitration_max.prescaler = 256;

      if (has_FD) {
        bus_param_limits->data_min = bus_param_limits->arbitration_min;
        bus_param_limits->data_max = bus_param_limits->arbitration_max;
      } else {
        bus_param_limits->data_min.tq = 0;
        bus_param_limits->data_min.phase1 = 0;
        bus_param_limits->data_min.phase2 = 0;
        bus_param_limits->data_min.sjw = 0;
        bus_param_limits->data_min.prop = 0;
        bus_param_limits->data_min.prescaler = 0;

        bus_param_limits->data_max = bus_param_limits->data_min;
      }
      break;

    case canHWTYPE_USBCAN_PRO2:
    case canHWTYPE_MEMORATOR_PRO2:
    case canHWTYPE_MEMORATOR_V2:
    case canHWTYPE_USBCAN_LIGHT:
    case canHWTYPE_LEAF2:
    case canHWTYPE_CANLINHYBRID:
    case canHWTYPE_PCIE_V2:
    case canHWTYPE_DINRAIL:
      bus_param_limits->arbitration_min.tq = 0;
      bus_param_limits->arbitration_min.phase1 = 1;
      bus_param_limits->arbitration_min.phase2 = 1;
      bus_param_limits->arbitration_min.sjw = 1;
      bus_param_limits->arbitration_min.prop = 0;
      bus_param_limits->arbitration_min.prescaler = 1;

      bus_param_limits->arbitration_max.tq = 0;
      bus_param_limits->arbitration_max.phase1 = 512;
      bus_param_limits->arbitration_max.phase2 = 32;
      bus_param_limits->arbitration_max.sjw = 16;
      bus_param_limits->arbitration_max.prop = 0;
      bus_param_limits->arbitration_max.prescaler = 8192;

      if (has_FD) {
        bus_param_limits->data_min = bus_param_limits->arbitration_min;
        bus_param_limits->data_max = bus_param_limits->arbitration_max;
      } else {
        bus_param_limits->data_min.tq = 0;
        bus_param_limits->data_min.phase1 = 0;
        bus_param_limits->data_min.phase2 = 0;
        bus_param_limits->data_min.sjw = 0;
        bus_param_limits->data_min.prop = 0;
        bus_param_limits->data_min.prescaler = 0;

        bus_param_limits->data_max = bus_param_limits->data_min;
      }
      break;

    default:
      return canERR_NOT_IMPLEMENTED;
  }
  return canOK;
}
