
#include "ringbuf.h"
#include "winlindrv.h"

int rd_ring_buf(RING_BUFFER_HEADER *rbuffer, unsigned char *read_to, int num_bytes_toread) {
  int cur_rd_pos = rbuffer->rd_pos, cur_wr_pos = rbuffer->wr_pos;
  int can_read = cur_wr_pos - cur_rd_pos, \
      left_to_wrap = rbuffer->num_bytes - cur_rd_pos;

  if (can_read == 0) return 0; // nothing to read

  if (can_read < 0) can_read += rbuffer->num_bytes;

  if (can_read < num_bytes_toread) {
    num_bytes_toread = can_read; //1*
  }

  if (left_to_wrap > num_bytes_toread) {
    if (copy_memory_to(read_to, &rbuffer->start[cur_rd_pos], num_bytes_toread)) { //MEMORY RANGES CANNOT OVERLAP
      return -1;
    }
    cur_rd_pos += num_bytes_toread;
  }
  else {
    if (copy_memory_to(read_to, &rbuffer->start[cur_rd_pos], left_to_wrap)) {
      return -1;
    }
    read_to += left_to_wrap;
    if (copy_memory_to (read_to, rbuffer->start, num_bytes_toread - left_to_wrap)) {
      return -1;
    }
    cur_rd_pos = num_bytes_toread - left_to_wrap;
    // rd_pos <= wr_pos because of 1*
    // rd_pos < rbuf_head->num_bytes because of invariant wr_pos < rbuf_head->num_bytes (see wr_ring_buf, 2*)
  }
  rbuffer->rd_pos = cur_rd_pos;
  return num_bytes_toread;
}

int wr_ring_buf(RING_BUFFER_HEADER *rbuffer, unsigned char *write_from, int num_bytes_towrite) {
  /* We do NOT use this for kdi ringbuffer */
  int left_to_wrap;
  int can_write;
  int cur_rd_pos = rbuffer->rd_pos, cur_wr_pos = rbuffer->wr_pos;

  can_write = cur_rd_pos - cur_wr_pos;
  if (can_write <= 0) can_write += rbuffer->num_bytes;

  if (can_write < num_bytes_towrite) num_bytes_towrite = can_write;

  left_to_wrap = rbuffer->num_bytes - cur_wr_pos;
  if (left_to_wrap > num_bytes_towrite) {
    if (copy_memory_from (&rbuffer->start[cur_wr_pos], write_from, num_bytes_towrite)) { //MEMORY RANGES CANNOT OVERLAP
      return -1;
    }
    cur_wr_pos += num_bytes_towrite;
  }
  else {
    if (copy_memory_from (&rbuffer->start[cur_wr_pos], write_from, left_to_wrap)) {
      return -1;
    }
    num_bytes_towrite -= left_to_wrap;
    write_from += left_to_wrap;
    if (copy_memory_from (rbuffer->start, write_from, num_bytes_towrite)) {
      return -1;
    }
    cur_wr_pos = num_bytes_towrite;
    // wr_pos < rbuf_head->num_bytes because of 1* //2*
  }
  rbuffer->wr_pos = cur_wr_pos;
  return num_bytes_towrite;
}
