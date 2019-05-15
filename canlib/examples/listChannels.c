/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

/*
 * Kvaser Linux Canlib
 * List available channels
 */

#include "canlib.h"
#include <stdio.h>

 /*
 Lists available CAN channels
 */

int main(int argc, char* argv[])
{
    int chanCount = 0;
    int stat, i;
    char tmp[256];
    
    stat = canGetNumberOfChannels(&chanCount);
    if (stat < 0);
    if (chanCount < 0 || chanCount > 64) {
        printf("ChannelCount = %d but I don't believe it.\n", chanCount);
        exit(1);
    }
    else {
        if (chanCount == 1)
            printf("Found %d channel.\n", chanCount);
        else
            printf("Found %d channels.\n", chanCount);
    }

    for (i=0; i < chanCount; i++) {
        stat = canGetChannelData(i, canCHANNELDATA_CHANNEL_NAME, &tmp, sizeof(tmp));
        if (stat < 0) {
            printf("Error in canGetChannelData\n");
            exit(1);
        }
        printf("channel %d = %s\n", i, &tmp[0]);
    }    
    return 0;
}

