/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

/*
 * Kvaser Linux Canlib
 * Read CAN messages and print out their contents
 */

#include <canlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

int i = 0;
unsigned char willExit = 0;

void sighand (int sig) {
    static int last;
    switch (sig){
    case SIGINT:
    willExit = 1;
    alarm(0);
    break;
    case SIGALRM:
    if (i != last) printf("rx : %d total: %d\n", i-last, i);
    last = i;
    alarm(1);
    break;
    }
    return;
}

 
int main (int argc, char *argv[])

{
    canHandle h;
    int ret = -1;
    long id; 
    unsigned char msg[8];
    unsigned int dlc;
    unsigned int flag;
    unsigned long time;  
    int channel = 0;
    int bitrate = BAUD_1M;
    int j;

    errno = 0;
    if (argc != 2 || (channel = atoi(argv[1]), errno) != 0) {
    printf("usage %s channel\n", argv[0]);
    exit(1);
    } else {
    printf("Reading messages on channel %d\n", channel);
    }

    /* Use sighand as our signal handler */
    signal(SIGALRM, sighand);
    signal(SIGINT, sighand);
    alarm(1);

    /* Allow signals to interrupt syscalls(in canReadBlock) */
    siginterrupt(SIGINT, 1);
  
    /* Open channels, parameters and go on bus */
    h = canOpenChannel(channel, canWANT_EXCLUSIVE | canWANT_EXTENDED);
    if (h < 0) {
    printf("canOpenChannel %d failed\n", channel);
    return -1;
    }
    
    canSetBusParams(h, bitrate, 4, 3, 1, 1, 0);
    canBusOn(h);

    i = 0;
    while(!willExit){
     
    do { 
        ret = canReadWait(h, &id, &msg, &dlc, &flag, &time, -1);
        switch (ret){
        case 0:
        printf("(%d) id:%ld dlc:%d data: ", i, id, dlc);
        if (dlc > 8) dlc = 8;
        for (j = 0; j < dlc; j++){
            printf("%2.2x ", msg[j]);
        }
        printf(" flags:0x%x time:%ld\n", flag, time);
        i++;
        break;
        case canERR_NOMSG:
        break;
        default:
        perror("canReadBlock error");
        break;
        }
    } while (ret == canOK);
    willExit = 1;
    }
   
    canClose(h);
   
    sighand(SIGALRM);
    printf("Ready\n");
    return 0;
}





