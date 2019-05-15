/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

/*
 * Kvaser Linux Canlib
 * Send out CAN messages as fast as possible
 */


#include <canlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

int i = 0;
int willExit = 0;

void sighand (int sig) {
  switch (sig){
  case SIGINT:
    willExit = 1;
    break;
  }
  return;
}

/*
 * Send messages until ctrl-c is pressed 
 */
 
int main (int argc, char *argv[])

{
  canHandle h;
  int ret = -1;
  int k = 0;
  int channel;
  int bitrate = BAUD_1M;

  errno = 0;
  if (argc != 2 || (channel = atoi(argv[1]), errno) != 0) {
    printf("usage %s channel\n", argv[0]);
    exit(1);
  } else {
    printf("Sending messages on channel %d\n", channel);
  }


  /* Use sighand as our signal handler */
  signal(SIGINT, sighand);

  /* Allow signals to interrupt syscalls(in canReadBlock) */
  siginterrupt(SIGINT, 1);
  
  /* Open channel, set parameters and go on bus */

  h = canOpenChannel(channel, canWANT_EXCLUSIVE | 0*canWANT_EXTENDED);
  if (h < 0) {
    printf("canOpenChannel %d failed\n", k);
    return -1;
  }
  canSetBusParams(h, bitrate, 4, 3, 1, 1, 0);
  canBusOn(h);
  
   i = 0;
   while(!willExit){
     /* Send some messages */
     for (k = 0 ; k < 10000 ; k++){
       ret = canWriteWait(h, channel + 100, "Kvaser!", 8, 0, -1);       
       if (ret != canOK || willExit) break;       
       else i++;
     }
     printf("Total sent=%d\n", i);
     //sleep(1);
   }
   
   if (canWriteSync(h, 1000) != canOK) printf("Sync failed!\n");
   canBusOff(h);
   canClose(h);

   printf("---------------\n");
   printf("Total sent=%d\n", i);
  return 0;
}





