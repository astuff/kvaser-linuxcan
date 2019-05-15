/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

/*
 * Kvaser Linux Canlib
 * Read timer test
 */

#include <stdio.h>
#include <canlib.h>
#include <unistd.h>
#include <sys/time.h>

int main(int argc, char *argv[])
{
  unsigned long time, last=0, lastsys=0;
  char errorString[50];
  canHandle h;
  struct timeval tv;
  
  h = canOpenChannel(1, 0);
  if (h < 0) {
    canGetErrorText(h, errorString, 50);
    printf("%s\n", errorString);
  } 
  while(1){
    if (canReadTimer(h, &time) == canOK) printf("Time=%ld ms (%ld)\n", time, time-last);
    else break;
    gettimeofday(&tv, NULL);
    printf("system:%ld\n", tv.tv_sec * 1000 + tv.tv_usec / 1000 - lastsys);
    last = time;
    lastsys = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    sleep(1);
  }
  printf("canReadTimer failed\n");
  canClose(h);
  return 0;
}
