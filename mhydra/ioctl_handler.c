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

#include <linux/slab.h>
#include "ioctl_handler.h"
#include "debug.h"
#include "kcan_ioctl.h"
#include "mhydraHWIf.h"

#include <linux/slab.h>

//----------------------------------------------------------------------------
// If you do not define MHYDRA_DEBUG at all, all the debug code will be
// left out.  If you compile with MHYDRA_DEBUG=0, the debug code will
// be present but disabled -- but it can then be enabled for specific
// modules at load time with a 'pc_debug=#' option to insmod.
//
#ifdef MHYDRA_DEBUG
#   define DEBUGPRINT(n, arg)     if (pc_debug >= (n)) { DEBUGOUT(n, arg); }
#else
#   define DEBUGPRINT(n, arg)     if ((n) == 1) { DEBUGOUT(n, arg); }
#endif /* MHYDRA_DEBUG */

//----------------------------------------------------------------------------
int mhydra_special_ioctl_handler(VCanOpenFileNode *fileNodePtr, unsigned int ioctl_cmd, unsigned long arg)
{
  int            status = VCAN_STAT_NOT_IMPLEMENTED;
  VCanChanData   *chd   = fileNodePtr->chanData;
  VCanCardData   *ccd   = chd->vCard;
  MhydraCardData *hwdata;

  if ((chd == NULL) || (ccd == NULL)) {
    return VCAN_STAT_BAD_PARAMETER;
  }
  hwdata = (MhydraCardData*) ccd->hwCardData;
  if (hwdata == NULL) {
    return VCAN_STAT_BAD_PARAMETER;
  }

  DEBUGPRINT(3, ("mhydra_special_ioctl_handler (%u)\n", ioctl_cmd));
  switch (ioctl_cmd) {
    default:
      DEBUGPRINT(1, ("mhydra_special_ioctl_handler unk: %u\n", ioctl_cmd));
      status = VCAN_STAT_NOT_IMPLEMENTED;
  }
  return status;
}
