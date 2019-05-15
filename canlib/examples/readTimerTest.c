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
 * Kvaser Linux Canlib
 * Read timer test
 */

#include <stdio.h>
#include <canlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <errno.h>

static int willExit = 0;

static void check(char* id, canStatus stat)
{
  if (stat != canOK) {
    char buf[50];
    buf[0] = '\0';
    canGetErrorText(stat, buf, sizeof(buf));
    printf("%s: failed, stat=%d (%s)\n", id, (int)stat, buf);
  }
}

static void printUsageAndExit(char *prgName)
{
  printf("Usage: '%s <channel>'\n", prgName);
  exit(1);
}

static void sighand(int sig)
{
  (void)sig;
  willExit = 1;
}

int main(int argc, char *argv[])
{
  unsigned long time, last = 0, lastsys = 0;
  canStatus stat;
  canHandle hnd;
  struct timeval tv;
  int channel;

  if (argc != 2) {
    printUsageAndExit(argv[0]);
  }

  {
    char *endPtr = NULL;
    errno = 0;
    channel = strtol(argv[1], &endPtr, 10);
    if ( (errno != 0) || ((channel == 0) && (endPtr == argv[1])) ) {
      printUsageAndExit(argv[0]);
    }
  }

  /* Allow signals to interrupt syscalls */
  signal(SIGINT, sighand);
  siginterrupt(SIGINT, 1);

  canInitializeLibrary();

  hnd = canOpenChannel(channel, 0);
  if (hnd < 0) {
    printf("canOpenChannel %d", channel);
    check("", hnd);
    return -1;
  }

  while (!willExit) {
    stat = canReadTimer(hnd, &time);
    if (stat != canOK) {
      check("canReadTimer", stat);
      break;
    }
    printf("Time=%lu ms (%lu)\n", time, time - last);
    gettimeofday(&tv, NULL);
    printf("system:%lu\n", tv.tv_sec * 1000 + tv.tv_usec / 1000 - lastsys);
    last = time;
    lastsys = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    sleep(1);
  }
  canClose(hnd);

  stat = canUnloadLibrary();
  if (stat != canOK) {
    check("canUnloadLibrary", stat);
    return -1;
  }

  return 0;
}
