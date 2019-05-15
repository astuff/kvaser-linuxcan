/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

#include <canlib.h>
#include <stdio.h>

void check(char* id, canStatus stat)
{
   char buf[50];

   buf[0] = '\0';
   canGetErrorText(stat, buf, sizeof(buf));
   if (stat != canOK) printf("%s: failed, stat=%d (%s)\n", id, (int)stat, buf);
   else printf("%s: OK\n", id);
   return;
}


/* Set bus parameters and read them back */

int main (int argc, char *argv[])

{
  long freq;
  unsigned int tseg1, tseg2, sjw, noSamp, syncmode;
  int ret = -1;
  int j;
  canHandle h [2];
  
  for (j = 0 ; j < 2 ; j++) {
    h [j] = canOpenChannel(j, canWANT_EXCLUSIVE | canWANT_EXTENDED);
    if (h [j] < 0) {
      printf("canOpenChannel (%d) failed\n", j);
      return -1;
    }
    ret = canSetBusParams(h [j], 500000, 4, 3, 1, 1, 0);
    check("canSetBusParams", ret);
  }

  printf("\n");
  for (j = 0; j < 2; j++){
    ret = canGetBusParams(h [j], &freq, &tseg1, &tseg2, &sjw, &noSamp, &syncmode);
    printf("H[%d]:freq %ld, tseg1 %d, tseg2 %d, sjw %d, noSamp %d, syncmode %d\n ",
       j, freq, tseg1, tseg2, sjw, noSamp, syncmode);
  }
  

  return 0;
}



