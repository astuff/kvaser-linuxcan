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

#ifndef SOFTSYNC_H
#define SOFTSYNC_H

// Make sure all timestamps handed to this module has 1 GHz resolution!
#define SOFTSYNC_DIFF_ALLOWED        10000  // 10 us @ 1 GHz
#define SOFTSYNC_DIFF_WARNING          500  // 0.5 us @ 1 GHz

#define SOFTSYNC_MAXNMATCHED  3  // do not set lower than 2!!!
#define SOFTSYNC_MAXNTREFS    16  // set to a power of two

typedef struct VCanCardData CARD_INFO;
typedef spinlock_t FAST_MUTEX;
typedef struct s_softsync_data SOFTSYNC_DATA;

typedef struct s_softsync_tRef_list {
    uint64_t         tRef;
    unsigned int     id;
    unsigned int     padding;
  } SOFTSYNC_TREF_LIST;

typedef struct s_softsync_match_list {
    uint64_t loc;
    uint64_t glob;
  } SOFTSYNC_MATCH_LIST;

typedef struct s_softsync_master SOFTSYNC_MASTER;

typedef struct s_softsync_master {
  SOFTSYNC_DATA            *master;
  SOFTSYNC_MASTER          *next;
  SOFTSYNC_MASTER          *prev;
}  SOFTSYNC_MASTER;


typedef struct s_softsync_data {

  int                      id;

  SOFTSYNC_TREF_LIST       tRefList[SOFTSYNC_MAXNTREFS];

  int                      oldestTRef;
  int                      nTRef;
  int                      nMatched;

  SOFTSYNC_MATCH_LIST      matchList[SOFTSYNC_MAXNMATCHED];

  int64_t                  locDiff;
  int64_t                  globDiff;
  int64_t                  newDiff;
  int64_t                  diffDiff;
  int64_t                  instab;
  int64_t                  instabMax;
  int64_t                  instabMin;

  int64_t                  masterDiff;

  FAST_MUTEX               softSyncAccessMutex;
  CARD_INFO                *ci;

  SOFTSYNC_DATA            *next;
  SOFTSYNC_DATA            *prev;

  SOFTSYNC_MASTER          *masterData;

} SOFTSYNC_DATA;

uint64_t softSyncLoc2Glob (CARD_INFO *ci, uint64_t stamp);
uint64_t softSyncGlob2Loc (CARD_INFO *ci, uint64_t stamp);
long softSyncInstab (CARD_INFO *ci);
void softSyncInstabMaxMin (CARD_INFO *ci, long *max, long *min);
void softSyncHandleTRef (CARD_INFO *ci, uint64_t tRef, unsigned id);
int  softSyncAddMember (CARD_INFO *ci, int id);
void softSyncRemoveMember (CARD_INFO *ci);
int softSyncInitialize (void);
void softSyncDeinitialize (void);
int softSyncGetTRefList (CARD_INFO *ci, void *buf, int bufsiz);


#endif
