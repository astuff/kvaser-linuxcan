/******************************************************************************
 *                                                                             *
 * License Agreement                                                           *
 *                                                                             *
 * Copyright (c) 2008 Altera Corporation, San Jose, California, USA.           *
 * All rights reserved.                                                        *
 *                                                                             *
 * Permission is hereby granted, free of charge, to any person obtaining a     *
 * copy of this software and associated documentation files (the "Software"),  *
 * to deal in the Software without restriction, including without limitation   *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,    *
 * and/or sell copies of the Software, and to permit persons to whom the       *
 * Software is furnished to do so, subject to the following conditions:        *
 *                                                                             *
 * The above copyright notice and this permission notice shall be included in  *
 * all copies or substantial portions of the Software.                         *
 *                                                                             *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR  *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,    *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE *
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER      *
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING     *
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER         *
 * DEALINGS IN THE SOFTWARE.                                                   *
 *                                                                             *
 * This agreement shall be governed in all respects by the laws of the State   *
 * of California and by the laws of the United States of America.              *
 *                                                                             *
 ******************************************************************************/

#include <asm/io.h>

#define epcs_read    0x03
#define epcs_pp      0x02
#define epcs_wren    0x06
#define epcs_wrdi    0x04
#define epcs_rdsr    0x05
#define epcs_wrsr    0x01
#define epcs_se      0xD8
#define epcs_be      0xC7
#define epcs_dp      0xB9
#define epcs_res     0xAB
#define epcs_rdid    0x9F

uint8_t epcs_read_device_id(volatile void * base);
uint8_t epcs_read_electronic_signature(volatile void * base);
uint8_t epcs_read_status_register(volatile void * base);
void epcs_sector_erase(volatile void * base, uint32_t offset);
int32_t epcs_read_buffer(volatile void * base, int offset, uint8_t *dest_addr, int length);
void epcs_write_enable(volatile void * base);
void epcs_write_status_register(volatile void * base, uint8_t value);
int32_t epcs_write_buffer(volatile void * base, int offset, const uint8_t *src_addr, int length);
