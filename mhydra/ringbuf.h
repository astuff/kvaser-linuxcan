#ifndef RINGBUF_H
#define RINGBUF_H

/*
 Ringbuffer implementation.
 Requires valid CopyMemoryTo/From definitions

 CopyMemoryTo/From construct is made for sake of possibility to writing directly to/from user mode (from/to kernel mode).

 Copyright 2016 by Kvaser AB
 */

typedef struct {
  unsigned char *start;
  int num_bytes;
  volatile int rd_pos, wr_pos;
} RING_BUFFER_HEADER;

int rd_ring_buf(RING_BUFFER_HEADER *rbuffer, unsigned char *read_to, int num_bytes_toread);

int wr_ring_buf(RING_BUFFER_HEADER *rbuffer, unsigned char *write_from, int num_bytes_towrite);

#endif
