/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

/*
 * Kvaser Linux Canlib
 * Count the number of messages/second on a CAN channel
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

int i = 0;
int std = 0, ext = 0, rtr = 0, err = 0, over = 0;
unsigned char willExit = 0;

void sighand (int sig) {
    static int last;
    switch (sig){
    case SIGINT:
    willExit = 1;
    alarm(0);
    break;
    case SIGALRM:
    if (i-last) {
        printf("msg/s = %d, total=%d, std=%d, ext=%d, err=%d, over=%d\n", 
           i-last, i, std, ext, err, over);
    }
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
    long id = 27; 
    unsigned char msg[8];
    unsigned int dlc;
    unsigned int flag;
    unsigned long time;  
    int channel = 0;
    int bitrate = BAUD_1M;

    /* Use sighand as our signal handler */
    signal(SIGALRM, sighand);
    signal(SIGINT, sighand);
    alarm(1);

    /* Allow signals to interrupt syscalls(in canReadBlock) */
    siginterrupt(SIGINT, 1);

    errno = 0;
    if (argc != 2 || (channel = atoi(argv[1]), errno) != 0) {
    printf("usage %s channel\n", argv[0]);
    exit(1);
    } else {
    printf("Counting messages on channel %d\n", channel);
    }

  
    /* Open channels, parameters and go on bus */
    h = canOpenChannel(channel, canWANT_EXCLUSIVE | canWANT_EXTENDED);
    if (h < 0) {
    printf("canOpenChannel %d failed\n", channel);
    return -1;
    }
    check("parameters", canSetBusParams(h, bitrate, 4, 3, 1, 1, 0));
    canBusOn(h);

    while(!willExit){
     
    do { 
        ret = canReadWait(h, &id, &msg, &dlc, &flag, &time, -1);
        switch (ret){
        case 0:
            if (flag & canMSG_ERROR_FRAME) err++;
        else {
            if (flag & canMSG_STD) std++;
            if (flag & canMSG_EXT) ext++;
            if (flag & canMSG_RTR) rtr++;
            if (flag & canMSGERR_OVERRUN) over++;
        }
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
   
   
    sighand(SIGALRM);
    printf("Ready\n");
    return 0;
}





