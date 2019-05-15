#include <asm/uaccess.h>

#include "winlindrv.h"

int copy_memory_to   (void* to, void* from, unsigned int n_bytes) {
  return copy_to_user (to, from, n_bytes);
}

int copy_memory_from (void* to, void* from, unsigned int n_bytes) {
  return copy_from_user (to, from, n_bytes);
}
