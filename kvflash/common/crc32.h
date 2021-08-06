#ifndef CRC32_H___
#define CRC32_H___

#include <stdint.h>

uint32_t crc32Calc(const uint8_t *buf, int bufsize);
uint32_t crc32Calc_be(const uint8_t *buf, uint32_t bufsiz);
#endif /* CRC32_H___ */
