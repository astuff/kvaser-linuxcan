/*
**                Copyright 2012 by Kvaser AB, Mölndal, Sweden
**                        http://www.kvaser.com
**
** This software is dual licensed under the following two licenses:
** BSD-new and GPLv2. You may use either one. See the included
** COPYING file for details.
**
** License: BSD-new
** ===============================================================================
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
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
** WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
** DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
** (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
** LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
** ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
**
** License: GPLv2
** ===============================================================================
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**
** ---------------------------------------------------------------------------
**/

/*  Kvaser Linux Canlib LAPcan helper functions */

#include <canlib_util.h>







//====================================================================
// addFilter - Add a filter to a specific handle, type=pass
//====================================================================
int addFilter (OS_IF_FILE_HANDLE fd,
          unsigned int cmdNr,  int cmdNrMask,
          unsigned char chanNr, char chanNrMask,
          unsigned char flags,  char flagsMask,
          int isPass)
{
  FilterData filterData;

  filterData.cmdNr = cmdNr;
  filterData.cmdNrMask = cmdNrMask;
  filterData.chanNr = chanNr;
  filterData.chanNrMask = chanNrMask;
  filterData.flags = flags;
  filterData.flagsMask = flagsMask;
  filterData.isPass = isPass;

  return os_if_ioctl_write(fd, LAPCAN_IOC_ADD_FILTER, &filterData, sizeof(FilterData));
}


//====================================================================
// clearFilters - Remove all filters on a handle
//====================================================================
int clearFilters (OS_IF_FILE_HANDLE fd)
{
  return os_if_ioctl_write(fd, LAPCAN_IOC_CLEAR_FILTERS, NULL, 0);
}


//====================================================================
unsigned char chanMap [CMD__HIGH + 1] = \
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
 0, 0, 0, 0, 0, 0, 2, 3, 3, 5, \
 3, 3, 0, 0, 3, 0, 3, 3, 3, 3, \
 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, \
 0, 0, 3, 3, 0, 0, 0, 3, 0, 3, \
 3, 0, 3, 3, 3, 0, 0, 0, 3, 3, \
 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, \
 0, 0, 0, 0, 0, 0, 0, 3, 3, 2, \
 3, 3, 0, 0, 0, 3, 0, 0, 0, 3, \
 3, 3, 3};

unsigned char flagMap [CMD__HIGH + 1] = \
{2, 2, 0, 0, 0, 0, 0, 0, 0, 0, \
 0, 0, 0, 0, 0, 0, 0, 4, 6, 0, \
 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, \
 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, \
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \

 0, 0, 0, 6, 5, 0, 0, 0, 0, 0, \
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
 4, 0, 0};


//====================================================================
// getCmdNr
//====================================================================
OS_IF_INLINE int getCmdNr (lpcCmd *cmd)
{
  unsigned char *b;

  b = (unsigned char*) cmd;

  // Infrequent command:bit 7 is set
  if (b [0] & 0x80) {
    return b [1] & 0x7f;
  }
  else {
    return b [0] >> 5;
  }
}


//====================================================================
// getCmdLen
//====================================================================
OS_IF_INLINE int getCmdLen (lpcCmd *cmd)
{
  unsigned char *b;

  b = (unsigned char*) cmd;
  if (b [0] & 0x80) { // Infrequent command:bit 7 is set
    return 1 + (b [0] & 0x7f); // Length is lower 7 bits
  }
  else {
    return 1 + (b [0] & 0x1f); // Length is lower 5 bits
  }
}


//====================================================================
// getCmdChannel
//====================================================================
OS_IF_INLINE int getCmdChan (lpcCmd *cmd)
{
  unsigned char *b;
  b = (unsigned char*) cmd;
  if (b [0] & 0x80) { // Infrequent command:bit 7 is set
    return b [chanMap [getCmdNr(cmd)] ];
  }
  else {
    return b [1] >> 4; // channel is upper 4 bits
  }
}


//====================================================================
// getFlags
//====================================================================
OS_IF_INLINE unsigned char getFlags (lpcCmd *cmd)
{
  // flagMap[getCmdNr(cmd)] is the position of the flags in the command
  return ((unsigned char*) cmd) [flagMap [getCmdNr(cmd)] ];
  // OLD return ((unsigned char*) cmd) [2];
}


//====================================================================
// setFlags
//====================================================================
OS_IF_INLINE void setFlags (lpcCmd *cmd, unsigned char flags)
{
  ((unsigned char*) cmd) [flagMap [getCmdNr(cmd)] ] = flags;
  // OLD ((unsigned char*) cmd) [2] = flags;
  return;
}


//====================================================================
// copyLapcanCmd
//====================================================================
OS_IF_INLINE void copylpcCmd (lpcCmd *cmd_to, lpcCmd *cmd_from)
{
  int cmdLen;
  unsigned char *from, *to;

  from = (unsigned char*) cmd_from;
  to = (unsigned char*) cmd_to;

  for (cmdLen = getCmdLen(cmd_from); cmdLen > 0; cmdLen--) {
    *to++ = *from++;
  }
  return;
}

