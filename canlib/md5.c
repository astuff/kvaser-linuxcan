/*
Copyright (C) 1999, 2000, 2002 Aladdin Enterprises.  All rights reserved.

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

L. Peter Deutsch
ghost@aladdin.com

*/
/* $Id$ */
/*
Independent implementation of MD5 (RFC 1321).

This code implements the MD5 Algorithm defined in RFC 1321, whose
text is available at
http://www.ietf.org/rfc/rfc1321.txt
The code is derived from the text of the RFC, including the test suite
(section A.5) but excluding the rest of Appendix A.  It does not include
any code or documentation that is identified in the RFC as being
copyrighted.

The original and principal author of md5.c is L. Peter Deutsch
<ghost@aladdin.com>.  Other authors are noted in the change history
that follows (in reverse chronological order):

2002-04-13 lpd Clarified derivation from RFC 1321; now handles byte order
either statically or dynamically; added missing #include <string.h>
in library.
2002-03-11 lpd Corrected argument list for main(), and added int return
type, in test program and T value program.
2002-02-21 lpd Added missing #include <stdio.h> in test program.
2000-07-03 lpd Patched to eliminate warnings about "constant is
unsigned in ANSI C, signed in traditional"; made test program
self-checking.
1999-11-04 lpd Edited comments slightly for automatic TOC extraction.
1999-10-18 lpd Fixed typo in header comment (ansi2knr rather than md5).
1999-05-03 lpd Original version.
*/

#include "md5.h"
#include <string.h>

#define FAR

#define ROTATE_LEFT(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

static const FAR uint32_t k[64] = {
  0xd76aa478L, 0xe8c7b756L, 0x242070dbL, 0xc1bdceeeL,
  0xf57c0fafL, 0x4787c62aL, 0xa8304613L, 0xfd469501L,
  0x698098d8L, 0x8b44f7afL, 0xffff5bb1L, 0x895cd7beL,
  0x6b901122L, 0xfd987193L, 0xa679438eL, 0x49b40821L,
  0xf61e2562L, 0xc040b340L, 0x265e5a51L, 0xe9b6c7aaL,
  0xd62f105dL, 0x02441453L, 0xd8a1e681L, 0xe7d3fbc8L,
  0x21e1cde6L, 0xc33707d6L, 0xf4d50d87L, 0x455a14edL,
  0xa9e3e905L, 0xfcefa3f8L, 0x676f02d9L, 0x8d2a4c8aL,
  0xfffa3942L, 0x8771f681L, 0x6d9d6122L, 0xfde5380cL,
  0xa4beea44L, 0x4bdecfa9L, 0xf6bb4b60L, 0xbebfbc70L,
  0x289b7ec6L, 0xeaa127faL, 0xd4ef3085L, 0x04881d05L,
  0xd9d4d039L, 0xe6db99e5L, 0x1fa27cf8L, 0xc4ac5665L,
  0xf4292244L, 0x432aff97L, 0xab9423a7L, 0xfc93a039L,
  0x655b59c3L, 0x8f0ccc92L, 0xffeff47dL, 0x85845dd1L,
  0x6fa87e4fL, 0xfe2ce6e0L, 0xa3014314L, 0x4e0811a1L,
  0xf7537e82L, 0xbd3af235L, 0x2ad7d2bbL, 0xeb86d391L};

static const FAR uint32_t r[64] = {
  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21};

void md5 (const uint8_t *data, int32_t dataLen, uint8_t *digest)
{
  MD5_CTX md5_ctx;
  md5_init(&md5_ctx);
  md5_append(&md5_ctx, data, dataLen); 
  md5_finish(&md5_ctx, digest);
}

void md5DualInput (const uint8_t *data1, int32_t data1Len,
                   const uint8_t *data2, int32_t data2Len, uint8_t *digest) 
{
  MD5_CTX md5_ctx;
  md5_init(&md5_ctx);
  md5_append(&md5_ctx, data1, data1Len);
  md5_append(&md5_ctx, data2, data2Len);
  md5_finish(&md5_ctx, digest);
}

void md5Vector (const uint8_t *dataInput[], const int32_t *dataLength,
                int32_t numberOfElements, uint8_t *digest) 
{
  MD5_CTX md5_ctx;
  int i;
  md5_init(&md5_ctx);
  for(i = 0; i < numberOfElements; i++) {
    md5_append(&md5_ctx, dataInput[i], dataLength[i]);
  }
  md5_finish(&md5_ctx, digest);
}

static void
md5_process (md5_state_t *pms, const md5_byte_t *data /*[64]*/)
{
  md5_word_t
    a = pms->abcd[0], b = pms->abcd[1],
    c = pms->abcd[2], d = pms->abcd[3];
  md5_word_t f,g;
  uint32_t i;    
    
  uint32_t temp;
  uint32_t w[16];
  /* Initialize w */
  for(i = 0; i < 16; i++) {
    w[i] =  (uint32_t)data[i * 4] ;
    w[i] |= (uint32_t)data[i * 4 + 1] << 8;
    w[i] |= (uint32_t)data[i * 4 + 2] << 16;
    w[i] |= (uint32_t)data[i * 4 + 3] << 24;
  }
  
  
  for(i = 0; i < 64; i++) {
    if(i < 16) {
      f = (b & c) | ((~b) & d);
      g = i;         
    } else if (i < 32) {
      f = (d & b) | ((~d) & c);  
      g = (5 * i + 1) % 16;
    } else if (i < 48) {
      f = b ^ c ^ d;
      g = (3 * i + 5) % 16;            
    } else if (i < 64) {
      f = c ^ (b | (~d));
      g = (7 * i) % 16;                     
    }
    
    temp = d;
    d = c;
    c = b;
    b = ROTATE_LEFT((a + f + k[i] + w[g]), r[i]) + b;
    
    a = temp;
  }    
  /* Then perform the following additions. (That is increment each
     of the four registers by the value it had before this block
     was started.) */
  pms->abcd[0] += a;
  pms->abcd[1] += b;
  pms->abcd[2] += c;
  pms->abcd[3] += d;
}

void
md5_init (md5_state_t *pms)
{
  pms->count[0] = pms->count[1] = 0;
  pms->abcd[0] = 0x67452301L;
  pms->abcd[1] = 0xefcdab89L;
  pms->abcd[2] = 0x98badcfeL;
  pms->abcd[3] = 0x10325476L;
}

void
md5_append (md5_state_t *pms, const md5_byte_t *data, uint32_t nbytes)
{
  const md5_byte_t *p = data;
  int left = nbytes;
  int offset = (pms->count[0] >> 3) & 63;
  md5_word_t nbits = (md5_word_t)(nbytes << 3);
  
  if (nbytes <= 0) {
    return;
  }
  
  /* Update the message length. */
  pms->count[1] += nbytes >> 29;
  pms->count[0] += nbits;
  if (pms->count[0] < nbits) {
    pms->count[1]++;
  }
  
  /* Process an initial partial block. */
  if (offset) {
    int copy = (offset + nbytes > 64 ? 64 - offset : (int)nbytes);
    
    memcpy(pms->buf + offset, p, copy);
    if (offset + copy < 64) {
      return;
    }
    p += copy;
    left -= copy;
    md5_process(pms, pms->buf);
  }
  
  /* Process full blocks. */
  for (; left >= 64; p += 64, left -= 64) {
    md5_process(pms, p);
  }

  /* Process a final partial block. */
  if (left) {
    memcpy(pms->buf, p, left);
  }    
}

void
md5_finish (md5_state_t *pms, md5_byte_t digest[16])
{
  md5_byte_t pad[64];
  md5_byte_t data[8];
  int i;
  
  memset(pad, 0, 64);
  pad[0] = 0x80;

  /* Save the length before padding. */
  for (i = 0; i < 8; ++i) {
    data[i] = (md5_byte_t)(pms->count[i >> 2] >> ((i & 3) << 3));
  }
  /* Pad to 56 bytes mod 64. */
  md5_append(pms, pad, ((55 - (pms->count[0] >> 3)) & 63) + 1);
  /* Append the length. */
  md5_append(pms, data, 8);
  for (i = 0; i < 16; ++i) {
    digest[i] = (md5_byte_t)(pms->abcd[i >> 2] >> ((i & 3) << 3));
  }
}
