/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

/*
 * Kvaser Linux Canlib
 * Send a CAN message
 */

#include <canlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>


void check(char* id, canStatus stat)
{
   char buf[50];

   buf[0] = '\0';
   canGetErrorText(stat, buf, sizeof(buf));
   if (stat != canOK) printf("%s: failed, stat=%d (%s)\n", id, (int)stat, buf);
   else printf("%s: OK\n", id);
   return;
}

/*
 * Send messages until ctrl-c is pressed 
 */
 
int main (int argc, char *argv[])

{
  canHandle h;
  int channel;
  int bitrate = BAUD_100K;

  errno = 0;
  if (argc != 2 || (channel = atoi(argv[1]), errno) != 0) {
    printf("usage %s channel\n", argv[0]);
    exit(1);
  } else {
    printf("Sending a message on channel %d\n", channel);
  }


  /* Allow signals to interrupt syscalls(e.g in canReadBlock) */
  siginterrupt(SIGINT, 1);
  
  /* Open channel, set parameters and go on bus */

  h = canOpenChannel(channel, canWANT_EXCLUSIVE | canWANT_EXTENDED);
  if (h < 0) {
    printf("canOpenChannel %d failed\n", channel);
    return -1;
  }

  canBusOff(h);
  check("canSetBusParams",  canSetBusParams(h, bitrate, 4, 3, 1, 1, 0));
  check("canBusOn", canBusOn(h));

  
  check("canWrite", canWrite(h, 10000, "Kvaser!", 8, 0));       
  check("canWriteSync", canWriteSync(h, 1000));

  
  //check("canBusOff", canBusOff(h));
  check("canClose", canClose(h));

  return 0;
}





