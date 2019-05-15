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
** Functions to let devices appear synchronized by means of software
** translations of all time stamps to and from the dito.
**
** ---------------------------------------------------------------------------
*/

// Toughts behind the mutexes...
//
// There are two kinds of mutexes:
//    * one for each member (including the master). Should be grabbed before
//      accesing member specific data (i.e. TRef list, nMatch list or
//      precomputed values such as loc-, glob- and masterdiff.)
//
//    * one master mutex to be grabbed before accessing any other data (i.e.
//      either other members data or following any pointers (next, prev or
//      masterData->master.))
//
// Consequently to be allowed access to, for instance, an other members data,
// both the master mutex and the member specific mutex must be owned. To avoid
// deadlocks the philosphy is to always grab the master mutex before a member
// specific mutex, and never to let go of the member specific before the master.
// If more than one member specific mutex is needed, start off by grabbing the
// master mutex
// And finally never try to acquire an already owned mutex.
//
// The translation functions loc2Glob, glob2Loc and instab are for performance
// reasons intentionally kept clean of attempts to access master mutex
// protected data... Try adhering to this if making changes.


// Who owns, allocates and frees what data...
//
// softSyncInitialize allocates the first masterData structure and the master
// mutex.
//
// softSyncDeinitialize frees the master mutex and all masterData structs it
// finds (which should be exactly one)
//
// AddMember allocates non_paged memory for each member's SOFTSYNC_DATA struct.
// AddMember also allocates non_paged memory for all masterData structures but
// the first when needed.
//
// RemoveMember deallocates member data after safely linking the member out of
// the softsync time domain.
// When the last member of a domain is removed the masterData is also freed
// (unless it's the last, then it's just reinitialized).






#include <linux/types.h>
#include <linux/slab.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/version.h>
#include "softsync.h"
#include "VCanOsIf.h"
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
#define div64_s64 div64_u64
#endif /* KERNEL_VERSION < 2.6.37 */
#define PRINTF(X) printk X
#ifdef MAGISYNC_DEBUG
#define PRINTF_SOFTSYNC(X) printk X
#else
#define PRINTF_SOFTSYNC(X)
#endif /* MAGISYNC_DEBUG */
#define HWNAME "softsync"
#define ASSERT(X) BUG_ON(!(X))
#define malloc_non_paged(X) kmalloc(X, GFP_KERNEL)
#define free_non_paged(X) kfree(X)

// Defines

#define MATCH_OK              0
#define MATCH_WARNING         1
#define MATCH_DROPPED         2

#define myAcquireMutex(X)   spin_lock(X)
#define myReleaseMutex(X)   spin_unlock(X)
#define myInitMutex(X)      spin_lock_init(X)




// Forward declarations
static int newMatchedPair (SOFTSYNC_DATA *p,
                           uint64_t loc,
                           uint64_t glob);
static int existInTRefList (SOFTSYNC_DATA *p,
                            unsigned id,
                            uint64_t *tRef);

static SOFTSYNC_MASTER *MASTERXXX = NULL;
static FAST_MUTEX      *MASTERMUTEX = NULL;


// Function implementations
int softSyncInitialize (void)
{
  MASTERXXX = malloc_non_paged(sizeof(SOFTSYNC_MASTER));
  if (MASTERXXX == NULL) {
    PRINTF(("softSyncInit() failed due to lack of memory!\n"));
    return -1;
  }
  MASTERXXX->master = NULL;
  MASTERXXX->next = NULL;
  MASTERXXX->prev = NULL;

  MASTERMUTEX = malloc_non_paged(sizeof(FAST_MUTEX));
  if (MASTERMUTEX == NULL) {
    free_non_paged(MASTERXXX);
    MASTERXXX = NULL;
    PRINTF(("softSyncInit() failed due to lack of memory!\n"));
    return -1;
  }
  myInitMutex(MASTERMUTEX);
  return 0;
}


void softSyncDeinitialize (void)
{
  if (MASTERXXX == NULL) return;

  // First free the mutex (only one mutex common to all masters assumed)
  free_non_paged(MASTERMUTEX);
  MASTERMUTEX = NULL;

  // Then free all master structs (there should only be one though)
  while (MASTERXXX != NULL) {
    SOFTSYNC_MASTER *p = MASTERXXX->next;
    free_non_paged(MASTERXXX);
    MASTERXXX = p;
  }
}

uint64_t softSyncLoc2Glob (CARD_INFO *ci, uint64_t stamp)
{
  uint64_t retVal;
  SOFTSYNC_DATA *p;

  ASSERT(ci != NULL && ci->softsync_data != NULL);
  p = ci->softsync_data;

  myAcquireMutex(&p->softSyncAccessMutex);

  retVal = p->masterDiff + stamp + p->newDiff;
  if (p->locDiff) {
    retVal += div64_s64((stamp - p->matchList[0].loc)*p->diffDiff, p->locDiff);
  }

  myReleaseMutex(&p->softSyncAccessMutex);

  return retVal;
}
EXPORT_SYMBOL(softSyncLoc2Glob);


long softSyncInstab (CARD_INFO *ci)
{
  int64_t tmp;
  ASSERT(ci != NULL && ci->softsync_data != NULL);
  tmp = ci->softsync_data->instab;
  if (tmp > LONG_MAX) {
    return LONG_MAX;
  }
  else if (tmp < LONG_MIN) {
    return LONG_MIN;
  }
  else {
    return (long) tmp;
  }
}


void softSyncInstabMaxMin (CARD_INFO *ci, long *max, long *min)
{
  int64_t tmp;
  ASSERT(ci != NULL && ci->softsync_data != NULL);
  if (max) {
    tmp = ci->softsync_data->instabMax;
    if (tmp > LONG_MAX) {
      *max = LONG_MAX;
    }
    else if (tmp < 0) {
      *max = 0;
    }
    else {
      *max = (long) tmp;
    }
  }
  if (min) {
    tmp = ci->softsync_data->instabMin;
    if (tmp < LONG_MIN) {
      *min = LONG_MIN;
    }
    else if (tmp > 0) {
      *min = 0;
    }
    else {
      *min = (long) tmp;
    }
  }
}


uint64_t softSyncGlob2Loc(CARD_INFO *ci, uint64_t stamp)
{
  uint64_t retVal;
  SOFTSYNC_DATA *p;

  ASSERT(ci != NULL && ci->softsync_data != NULL);
  p = ci->softsync_data;

  myAcquireMutex(&p->softSyncAccessMutex);

  retVal = stamp - p->masterDiff - p->newDiff;
  if (p->locDiff) {
    retVal -= div64_s64((stamp - p->masterDiff - p->matchList[0].glob)
                         *p->diffDiff,
                        p->locDiff);
  }

  myReleaseMutex(&p->softSyncAccessMutex);

  return retVal;
}

// Make sure p->softSyncAccessMutex is owned before calling!
static int existInTRefList (SOFTSYNC_DATA *p, unsigned id, uint64_t *tRef)
{
  int i;
  for (i=0; i<p->nTRef; i++) {
    if (p->tRefList[i].id==id) {
      *tRef = p->tRefList[i].tRef;
      return 1;
    }
  }
  return 0;
}


// Make sure p->softSyncAccessMutex is owned before calling!
static int newMatchedPair (SOFTSYNC_DATA *p,
                           uint64_t loc,
                           uint64_t glob)
{
  int i;
  int64_t diff;

  diff = glob - loc - p->newDiff;
  if (p->locDiff) {
    diff -= div64_s64((loc - p->matchList[0].loc)*p->diffDiff, p->locDiff);
  }

  if (abs(diff) < SOFTSYNC_DIFF_ALLOWED || p->nMatched < SOFTSYNC_MAXNMATCHED) {

    // Keep track of how many matched pairs we have
    if (p->nMatched < SOFTSYNC_MAXNMATCHED) {
      p->nMatched++;
    }

    // Update the list
    for (i=SOFTSYNC_MAXNMATCHED-1; i>0; i--) {
      p->matchList[i].loc  = p->matchList[i-1].loc;
      p->matchList[i].glob = p->matchList[i-1].glob;
    }
    p->matchList[0].loc  = loc;
    p->matchList[0].glob = glob;

    p->newDiff  = glob - loc;

    // Update precomputed variables
    if (p->nMatched > 1) {
      p->locDiff  = loc  - p->matchList[p->nMatched - 1].loc;
      p->globDiff = glob - p->matchList[p->nMatched - 1].glob;
      p->diffDiff = p->globDiff - p->locDiff;
      p->instab   = p->nMatched>=SOFTSYNC_MAXNMATCHED ? diff : 0;
      p->instabMax = p->instab > p->instabMax ? p->instab : p->instabMax;
      p->instabMin = p->instab < p->instabMin ? p->instab : p->instabMin;
    }

    return abs(p->instab) < SOFTSYNC_DIFF_WARNING ? MATCH_OK : MATCH_WARNING;

  } else {
    PRINTF_SOFTSYNC(("%s: SoftSync member 0x%p dropped matched pair because"
                     " diff=abs(%ld) >= %d\n",
                     HWNAME, p->ci, (long) diff, SOFTSYNC_DIFF_ALLOWED));

    p->nMatched = 0; //try to sync again

    return MATCH_DROPPED;
  }
}


void softSyncHandleTRef (CARD_INFO *ci, uint64_t tRef, unsigned id)
{
  int i, existed=0;
  SOFTSYNC_DATA *p;
  SOFTSYNC_DATA *master;

  ASSERT(ci != NULL &&
         ci->softsync_data != NULL &&
         ci->softsync_data->masterData != NULL);

  p = ci->softsync_data;

  myAcquireMutex(&p->softSyncAccessMutex);
  // Making sure we don't have duplicate id's in the list
  for (i=0; i<p->nTRef; i++) {
    if (p->tRefList[i].id==id) {
      p->tRefList[i].tRef = tRef;
      existed = 1;
      break;
    }
  }

  // Updating the tRefList with the new entry
  if (!existed) {
    if (p->nTRef < SOFTSYNC_MAXNTREFS)
      p->nTRef++;
    p->tRefList[p->oldestTRef].tRef = tRef;
    p->tRefList[p->oldestTRef].id = id;
    p->oldestTRef = (p->oldestTRef + 1) % SOFTSYNC_MAXNTREFS;
  }
  myReleaseMutex(&p->softSyncAccessMutex);

  // Trying to match entries
  // newMatchedPair() now filters out bad matches... i.e. the fact that SOF's
  // distanced N*2048 ms gets matched shouldn't be a problem.
  myAcquireMutex(MASTERMUTEX);
  master = p->masterData->master;
  if (p==master) {
    // Let all members know the master's TRef
    uint64_t tmpTRef;
    SOFTSYNC_DATA *member;
    for (member=master->next; member!=NULL; member=member->next) {
      myAcquireMutex(&member->softSyncAccessMutex);
      if (existInTRefList(member, id, &tmpTRef)) {
        newMatchedPair(member, tmpTRef, tRef);
      }
      myReleaseMutex(&member->softSyncAccessMutex);
    }
  } else {
    // See if the master already has received this TRef
    uint64_t tmpTRef = 0;
    int found;
    myAcquireMutex(&master->softSyncAccessMutex);
    found = existInTRefList(master, id, &tmpTRef);
    myReleaseMutex(&master->softSyncAccessMutex);
    if (found) {
      myAcquireMutex(&p->softSyncAccessMutex);
      newMatchedPair(p, tRef, tmpTRef);
      myReleaseMutex(&p->softSyncAccessMutex);
    }
  }
  myReleaseMutex(MASTERMUTEX);
}
EXPORT_SYMBOL(softSyncHandleTRef);


int softSyncAddMember (CARD_INFO *ci, int id)
{
  ASSERT(ci != NULL);

  if (ci->enable_softsync) {

    SOFTSYNC_DATA *thisMember;

    if (ci->softsync_data == NULL) {
      ci->softsync_data = malloc_non_paged(sizeof(SOFTSYNC_DATA));
      if (ci->softsync_data == NULL) {
        PRINTF(("%s: softSyncAddMember() failed due to lack of memory"
                ", 0x%p!\n", HWNAME, ci));
        return -1;
      }
    } else {
      PRINTF(("%s: softSyncAddMember() called for an already added member"
              ", 0x%p!\n", HWNAME, ci));
      return ci->softsync_running;
    }

    thisMember = ci->softsync_data;

    thisMember->id = id;

    thisMember->oldestTRef = 0;
    thisMember->nTRef = 0;
    thisMember->nMatched = 0;
    thisMember->ci = ci;

    thisMember->locDiff = 0;
    thisMember->globDiff = 0;
    thisMember->newDiff = 0;
    thisMember->diffDiff = 0;
    thisMember->instab = 0;
    thisMember->instabMax = 0;
    thisMember->instabMin = 0;

    thisMember->masterDiff = 0;

    ASSERT(MASTERXXX != NULL && MASTERMUTEX != NULL);

    myInitMutex(&thisMember->softSyncAccessMutex);

    myAcquireMutex(MASTERMUTEX);
    thisMember->masterData = MASTERXXX;
    // First added member gets to be master...
    if (thisMember->masterData->master == NULL) {
      thisMember->masterData->master = thisMember;
      thisMember->prev = NULL;
      thisMember->next = NULL;
      PRINTF_SOFTSYNC(("%s: Softsync master 0x%p with id=0x%08x added.\n", HWNAME, ci, id));

    } else {
      // Other members looks for a master with the same id as themselves
      while (thisMember->masterData->master->id != thisMember->id) {
        if (thisMember->masterData->next == NULL) {
          // No master with the same id found. Make this member the master for this id.
          SOFTSYNC_MASTER *newMaster = malloc_non_paged(sizeof(SOFTSYNC_MASTER));
          if (newMaster == NULL) {
            free_non_paged(ci->softsync_data);
            ci->softsync_data = NULL;
            PRINTF(("%s: softSyncAddMember() failed due to lack of memory"
                    ", 0x%p!\n", HWNAME, ci));
            return -1;
          }
          newMaster->prev = thisMember->masterData;
          newMaster->prev->next = newMaster;
          newMaster->next = NULL;
          newMaster->master = thisMember;

          thisMember->masterData = newMaster;

          thisMember->prev = NULL;
          thisMember->next = NULL;
          PRINTF_SOFTSYNC(("%s: Softsync another master 0x%p with id=0x%08x added.\n",
                           HWNAME, ci, id));

          break;

        } else {
          thisMember->masterData = thisMember->masterData->next;
        }
      }

      // If a master was found the member orders itself in a linked list
      // starting with the master
      if (thisMember->masterData->master != thisMember) {
        SOFTSYNC_DATA *master = thisMember->masterData->master;
        if (master->next != NULL) {
          master->next->prev = thisMember;
        }
        thisMember->next = master->next;
        thisMember->prev = master;
        master->next = thisMember;
        thisMember->masterDiff = master->masterDiff;
        PRINTF_SOFTSYNC(("%s: Softsync member 0x%p with id=0x%08x added.\n",
                         HWNAME, ci, id));
      }
    }

    myReleaseMutex(MASTERMUTEX);

    ci->softsync_running = 1;

  } else {
    ci->softsync_running = 0;
    PRINTF_SOFTSYNC(("%s: Member NOT added to softsync\n", HWNAME));
  }
  return ci->softsync_running;
}
EXPORT_SYMBOL(softSyncAddMember);


void softSyncRemoveMember (CARD_INFO *ci)
{
  SOFTSYNC_DATA *thisMember;

  ASSERT(ci != NULL);

  ci->softsync_running = 0;
  thisMember = ci->softsync_data;

  if (thisMember == NULL) {
    // Already removed... just return...
    return;
  }

  myAcquireMutex(MASTERMUTEX);
  if (thisMember == thisMember->masterData->master) {
    // This is a master
    if (thisMember->next == NULL) {
      // No new master available in the domain...
      SOFTSYNC_MASTER *p = thisMember->masterData;

      if (p->prev) {
        // There is a master before in the master list:
        // Link out of the list and free this master struct.
        if (p->next) p->next->prev = p->prev;
        p->prev->next = p->next;
        free_non_paged(thisMember->masterData);
        PRINTF_SOFTSYNC(("%s: Softsync master 0x%p removed. "
                         "No new master available.\n", HWNAME, ci));

      } else if (p->next) {
        // There is a master after in the master list:
        // Link out of the list and free this master struct.
        MASTERXXX = p->next;
        p->next->prev = NULL;
        free_non_paged(thisMember->masterData);
        PRINTF_SOFTSYNC(("%s: Softsync master 0x%p removed. "
                         "No new master available.\n", HWNAME, ci));

      } else {
        // No masters what so ever. Don't free the struct, just initialize it.
        p->master = NULL;
        PRINTF_SOFTSYNC(("%s: Softsync master 0x%p removed. "
                         "No new master available at all.\n", HWNAME, ci));
      }

      thisMember->masterData = NULL;

    } else {
      // Transfer the master role to the next member
      SOFTSYNC_DATA *member;
      SOFTSYNC_DATA *master = thisMember->next;

      myAcquireMutex(&master->softSyncAccessMutex);

      master->masterDiff += master->newDiff;
      if (master->locDiff) {
        master->masterDiff += div64_s64(master->newDiff * master->diffDiff,
                                        master->locDiff);
      }

      master->nMatched  = 0;
      master->locDiff   = 0;
      master->globDiff  = 0;
      master->newDiff   = 0;
      master->diffDiff  = 0;
      master->instab    = 0;
      master->instabMax = 0;
      master->instabMin = 0;

      thisMember->masterData->master = master;
      master->prev = NULL;

      // Now let all other members know the new master's tRefs
      for (member=master->next; member!=NULL; member=member->next) {
        int i, j;
        uint64_t tmpTRef;
        myAcquireMutex(&member->softSyncAccessMutex);
        member->masterDiff = master->masterDiff;
        member->nMatched = 0;
        member->newDiff  = 0;
        member->locDiff  = 0;
        for (j=0; j<master->nTRef; j++) {
          i = (j + master->oldestTRef) % SOFTSYNC_MAXNTREFS;
          if (existInTRefList(member, master->tRefList[i].id, &tmpTRef)) {
            newMatchedPair(member, tmpTRef, master->tRefList[i].tRef);
          }
        }
        myReleaseMutex(&member->softSyncAccessMutex);
      }
      myReleaseMutex(&master->softSyncAccessMutex);
      PRINTF_SOFTSYNC(("%s: Softsync master 0x%p removed. "
                       "Master role transfered to 0x%p.\n",
                       HWNAME, ci, master->ci));
    }

  } else {
    if (thisMember->next != NULL) {
      thisMember->next->prev = thisMember->prev;
    }
    ASSERT(thisMember->prev != NULL); // shouldn't occur since we've just
                                      // established that this is not the
                                      // first member in the list...
    thisMember->prev->next = thisMember->next;
    PRINTF_SOFTSYNC(("%s: Softsync member 0x%p removed.\n", HWNAME, ci));
  }
  myReleaseMutex(MASTERMUTEX);

  free_non_paged(ci->softsync_data);
  ci->softsync_data = NULL;
}
EXPORT_SYMBOL(softSyncRemoveMember);


int softSyncGetTRefList (CARD_INFO *ci, void *buf, int bufsiz)
{
  SOFTSYNC_DATA *sd;

  if (!ci) return -1;
  if (!ci->softsync_data) return -1;

  sd = ci->softsync_data;

  if (bufsiz < sizeof(sd->tRefList)) return -2;

  memcpy(buf, sd->tRefList, sizeof(sd->tRefList));
  PRINTF_SOFTSYNC(("gettRefList: tRef=%llu, id=%x\n",
                   sd->tRefList[0].tRef, sd->tRefList[0].id));

  return 0;
}
EXPORT_SYMBOL(softSyncGetTRefList);
