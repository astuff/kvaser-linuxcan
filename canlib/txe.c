/*
**             Copyright 2018 by Kvaser AB, Molndal, Sweden
**                         http://www.kvaser.com
**
** This software is dual licensed under the following two licenses:
** BSD-new and GPLv2. You may use either one. See the included
** COPYING file for details.
**
** License: BSD-new
** ==============================================================================
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of the <organization> nor the
**       names of its contributors may be used to endorse or promote products
**       derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
** BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
** IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
** POSSIBILITY OF SUCH DAMAGE.
**
**
** License: GPLv2
** ==============================================================================
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
**
**
** IMPORTANT NOTICE:
** ==============================================================================
** This source code is made available for free, as an open license, by Kvaser AB,
** for use with its applications. Kvaser AB does not accept any liability
** whatsoever for any third party patent or other immaterial property rights
** violations that may result from any usage of this source code, regardless of
** the combination of source code and various applications that it can be used
** in, or with.
**
** -----------------------------------------------------------------------------
*/

/**
 * Module containing compiled t-script .txe access functionality.
 * \note little-endian cpu architecture is assumed.
 */

#include <assert.h>
#include <limits.h>
#include <memory.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "canlib.h"
#include "txe_fopen.h"


#if DEBUG
#   define DEBUGPRINT(args) printf args
#   define TXE_DBG_PRINTF(fmt) DEBUGPRINT(("%s:%d " #fmt "\n", __FILE__, __LINE__))
#   define TXE_DBG_PPRINTF(path, fmt) DEBUGPRINT(("%s:%d:%s " #fmt "\n", __FILE__, __LINE__, path))
#   define TXE_DBG_VPPRINTF(path, fmt, ...) DEBUGPRINT(("%s:%d:%s " #fmt "\n", __FILE__, __LINE__, path, __VA_ARGS__))
#else
#   define DEBUGPRINT(args)
#   define TXE_DBG_PRINTF(path, fmt)
#   define TXE_DBG_PPRINTF(path, fmt)
#   define TXE_DBG_VPPRINTF(path, fmt, ...)
#endif



#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))

struct ltv_hdr {
  uint32_t length;
  uint32_t tag;
};

enum ltv_stream_status {
  LTV_STREAM_OK = 0,
  LTV_STREAM_FORMAT_ERROR,
  LTV_STREAM_IO_ERROR,
  LTV_STREAM_OUT_OF_MEMORY
};

struct ltv_stream_iterator {
  FILE *f;
  uint32_t length;
  uint32_t tag;
  void *value;
  enum ltv_stream_status status;
};

/**
 * Create an instance of a length tag value block stream iterator.
 *
 * The iterator assumes little-endian cpu architecture.
 * Length of any block is limited to LONG_MAX.
 *
 * \param f stream to traverse.
 * \return Pointer to initialized ltv_stream_iterator instance.
 * \return NULL if allocation failure.
 */
struct ltv_stream_iterator *ltv_create(FILE *f)
{
  struct ltv_stream_iterator *ltv;

  ltv = (struct ltv_stream_iterator *) malloc(sizeof(*ltv));
  if (!ltv) {
    return NULL;
  }

  ltv->f = f;
  ltv->length = 0;
  ltv->tag = 0;
  ltv->value = NULL;
  ltv->status = LTV_STREAM_OK;

  return ltv;
}

/**
 * Destroy instance of ltv_stream_iterator.
 * \param ltv instance pointer.
 */
void ltv_destroy(struct ltv_stream_iterator *ltv)
{
  free(ltv->value);
  free(ltv);
}

/**
 * Return 1 if there are no more entries.
 * \param ltv instance pointer.
 * \return 1 if no more entries.
 */
int ltv_get_eof(struct ltv_stream_iterator *ltv)
{
  return feof(ltv->f);
}

/**
 * Return current stream status.
 * \param ltv instance pointer.
 * \return error code
 */
enum ltv_stream_status ltv_get_status(struct ltv_stream_iterator *ltv)
{
  return ltv->status;
}

/**
 * Retrieve the current block tag value.
 *
 * \param ltv instance pointer.
 * \return current block tag value.
 */
uint32_t ltv_get_tag(struct ltv_stream_iterator *ltv)
{
  return ltv->tag;
}

/**
 * Get the size of the current block's data in bytes.
 *
 * \param ltv instance pointer.
 * \return value data size in bytes.
 */
uint32_t ltv_get_length(struct ltv_stream_iterator *ltv)
{
  return ltv->length;
}

/**
 * Internal utility function to reset values and set error status.
 * \param ltv instance pointer
 * \param error error status to set.
 */
static void _ltv_reset_to_empty(struct ltv_stream_iterator *ltv, enum ltv_stream_status error)
{
  ltv->status = error;
  ltv->length = 0;
  ltv->tag = 0;
  if (ltv->value) {
    free(ltv->value);
    ltv->value = NULL;
  }
}

/**
 * Traverse to the next block.
 *
 * If the current entry is the last entry of the stream then
 * get_length() and get_tag() will return 0 and get_eof() will return 1 after
 * completion.
 *
 * \param ltv instance pointer
 */
void ltv_next(struct ltv_stream_iterator *ltv)
{
  size_t count;
  struct ltv_hdr hdr = {0};

  if (ltv->status || feof(ltv->f)) {
    return;
  }

  if (ltv->value) {
    free(ltv->value);
    ltv->value = NULL;
  } else {
    int status = fseek(ltv->f, (long)ltv->length, SEEK_CUR);
    if (status == -1) {
      _ltv_reset_to_empty(ltv, LTV_STREAM_IO_ERROR);
      return;
    }
  }

  count = fread(&hdr, 1, sizeof(hdr), ltv->f);
  if (count != sizeof(hdr)) {
    if (ferror(ltv->f)) {
      _ltv_reset_to_empty(ltv, LTV_STREAM_IO_ERROR);
    } else if (count != 0 && feof(ltv->f)) {
      _ltv_reset_to_empty(ltv, LTV_STREAM_FORMAT_ERROR);
    } else {
      _ltv_reset_to_empty(ltv, LTV_STREAM_OK);
    }
    return;
  }

  // sanity check, the length sometimes travels through int32_t
  if (hdr.length < sizeof(struct ltv_hdr) || hdr.length > INT_MAX) {
    _ltv_reset_to_empty(ltv, LTV_STREAM_FORMAT_ERROR);
    return;
  }

  ltv->length = hdr.length - sizeof(hdr);
  ltv->tag = hdr.tag;
}

/**
 * Returns a pointer to a memory area containing the contents of the current block's value.
 * The ltv_stream_iterator retains ownership of the allocated memory and the pointer remains
 * valid until ltv_next() or ltv_destroy() is invoked.
 *
 * \param ltv instance pointer.
 * \return Pointer to an allocated buffer containing the data of the current entry.
 * \return NULL if the error value of the stream isn't LTV_STREAM_OK.
 * \return NULL if current block is of length 0
 * \return NULL if out of memory, ltv_get_error() returns LTV_STREAM_OUT_OF_MEMORY.
 * \return NULL if read of data fails, ltv_get_error() returns LTV_STREAM_FORMAT_ERROR.
 */
void *ltv_get_value(struct ltv_stream_iterator *ltv)
{
  size_t count;

  if (ltv->status || ltv->length == 0) {
    return NULL;
  }

  if (ltv->value) {
    return ltv->value;
  }

  ltv->value = malloc(ltv->length);
  if (!ltv->value) {
    _ltv_reset_to_empty(ltv, LTV_STREAM_OUT_OF_MEMORY);
    return NULL;
  }

  count = fread(ltv->value, (size_t) ltv->length, 1, ltv->f);
  if (count != 1) {
    if (ferror(ltv->f)) {
      _ltv_reset_to_empty(ltv, LTV_STREAM_IO_ERROR);
    } else {
      _ltv_reset_to_empty(ltv, LTV_STREAM_FORMAT_ERROR);
    }
    return NULL;
  }

  return ltv->value;
}

/**
 * Traverse stream until a ltv triplet is found with matching tag.
 * If no matching triplet is found ltv_get_tag and ltv_get_length
 * will both return 0 and ltv_get_eof will return 1.
 * If the current triplet has a tag value matching the requested tag
 * then the state of the stream_iterator isn't modified.
 *
 * \param ltv instance pointer
 * \param block_type tag value to search for
 */
void ltv_search(struct ltv_stream_iterator *ltv, uint32_t tag)
{
  while (!ltv->status && !ltv_get_eof(ltv) && ltv->tag != tag) {
    ltv_next(ltv);
  }
}

struct ltv_triple {
  uint32_t length;
  uint32_t tag;
  void *value;
};

enum txe_magic_numbers {
  TXE_CONTAINER_V1_0_MAGIC_NUMBER = 0xEBA7ACA4
};

// block types
enum txe_block_tags {
  VERSION_TAG = 1,      // file version, compiler version
  DESCRIPTION_TAG = 2,  // variable size, file description, user defined at compile time
  DATE_TAG = 3,         // file creation date, defined at compile time
  SOURCE_CODE_TAG = 4,  // variable size, optional, defined at compile time
  PCODE_TAG = 5,        // variable size, compiled code
  KEY_TAG = 6,          // Indicates if compiled code is encrypted
  END_TAG = 0xff,       // no more blocks block (must be placed last)
};

struct txe_version_block {
  uint32_t magic_number;
  uint32_t file_major;
  uint32_t file_minor;
  uint32_t file_build;
  uint32_t compiler_major;
  uint32_t compiler_minor;
  uint32_t compiler_build;
};


struct txe_variable_sized_block {
  uint32_t use_crc;
  uint32_t crc32;
  uint32_t data[1];
};

struct txe_key_block {
  uint32_t key_id;
  uint32_t encryption_type;
  uint32_t data[1];
};

struct txe_date_block {
  uint32_t year;
  uint32_t mon;
  uint32_t day;
  uint32_t hour;
  uint32_t min;
  uint32_t sec;
};

struct txe_end_block {
  uint32_t zero0;
  uint32_t zero1;
};

#if DEBUG
static const char *tag_to_str(uint32_t tag)
{
  switch (tag) {
    case VERSION_TAG:
      return "VERSION";
    case DESCRIPTION_TAG:
      return "DESCRIPTION_TAG";
    case DATE_TAG:
      return "DATE_TAG";
    case SOURCE_CODE_TAG:
      return "SOURCE_CODE_TAG";
    case PCODE_TAG:
      return "PCODE_TAG";
    case KEY_TAG:
      return "KEY_TAG";
    case END_TAG:
      return "END_TAG";
    default:
      return "UNKNOWN";
  }
}
#endif /* DEBUG */

static int validate_txe_block_size(uint32_t tag, uint32_t length)
{
  switch (tag) {
    case VERSION_TAG:
      return length == sizeof(struct txe_version_block);
    case DATE_TAG:
      return length == sizeof(struct txe_date_block);
    case DESCRIPTION_TAG:
    case SOURCE_CODE_TAG:
    case PCODE_TAG:
      return length >= offsetof(struct txe_variable_sized_block, data);
    case KEY_TAG:
      return length >= offsetof(struct txe_key_block, data);
    case END_TAG:
      return length == sizeof(struct txe_end_block);
    default:
      return 0;
  }
}

static uint32_t get_offset_of_block_data(uint32_t tag)
{

  assert(tag == PCODE_TAG || tag == SOURCE_CODE_TAG || tag == DESCRIPTION_TAG || tag == KEY_TAG);

  switch (tag) {
    case PCODE_TAG:
    case SOURCE_CODE_TAG:
    case DESCRIPTION_TAG:
      return (uint32_t) offsetof(struct txe_variable_sized_block, data);
    case KEY_TAG:
      return (uint32_t) offsetof(struct txe_key_block, data);
    default:
      return 0;
  }
}

static uint32_t get_size_of_block_data(struct ltv_triple *block)
{
  assert(
    block->tag == PCODE_TAG || block->tag == SOURCE_CODE_TAG || block->tag == DESCRIPTION_TAG || block->tag == KEY_TAG);
  assert(validate_txe_block_size(block->tag, block->length));

  switch (block->tag) {
    case DESCRIPTION_TAG: {
      // Detect if txe_variable_sized_block contains an empty description.
      // sc.exe adds a '\x00' to the end of any description, even an empty one and then
      // inserts 3 zero bytes for padding when writing the description block to the .txe file.
      // An emtpy description should result in *bufsize = zero not 4("\x00\x00\x00\x00").
      struct txe_variable_sized_block *description = (struct txe_variable_sized_block *) block->value;
      if (block->length == sizeof(struct txe_variable_sized_block) && description->data[0] == 0x00000000) {
        return 0;
      } else {
        return block->length - (uint32_t) offsetof(struct txe_variable_sized_block, data);
      }
    }
    case PCODE_TAG:
    case SOURCE_CODE_TAG:
      return block->length - (uint32_t) offsetof(struct txe_variable_sized_block, data);
    case KEY_TAG:
      return block->length - (uint32_t) offsetof(struct txe_key_block, data);
    default:
      return 0;
  }
}

static canStatus handle_file_version_requests(struct ltv_triple *block, void *buffer, unsigned int *bufsize)
{
  struct txe_version_block *version = (struct txe_version_block *) block->value;

  assert(block->length == sizeof(struct txe_version_block) && *bufsize >= sizeof(uint32_t[3]));

  ((uint32_t *) buffer)[0] = version->file_major;
  ((uint32_t *) buffer)[1] = version->file_minor;
  ((uint32_t *) buffer)[2] = version->file_build;

  return canOK;
}

static canStatus handle_compiler_version_requests(struct ltv_triple *block, void *buffer, unsigned int *bufsize)
{
  struct txe_version_block *version = (struct txe_version_block *) block->value;

  assert(block->length == sizeof(struct txe_version_block) && *bufsize >= sizeof(uint32_t[3]));

  ((uint32_t *) buffer)[0] = version->compiler_major;
  ((uint32_t *) buffer)[1] = version->compiler_minor;
  ((uint32_t *) buffer)[2] = version->compiler_build;

  return canOK;
}

static canStatus handle_date_requests(struct ltv_triple *block, void *buffer, unsigned int *bufsize)
{
  struct txe_date_block *date = (struct txe_date_block *) block->value;

  assert(block->length == sizeof(struct txe_date_block) && *bufsize >= sizeof(uint32_t[6]));

  ((uint32_t *) buffer)[0] = date->year;
  ((uint32_t *) buffer)[1] = date->mon;
  ((uint32_t *) buffer)[2] = date->day;
  ((uint32_t *) buffer)[3] = date->hour;
  ((uint32_t *) buffer)[4] = date->min;
  ((uint32_t *) buffer)[5] = date->sec;

  return canOK;
}

static canStatus handle_size_of_code_requests(struct ltv_triple *block, void *buffer, unsigned int *bufsize)
{
  assert(block->length >= offsetof(
           struct txe_variable_sized_block, data) && *bufsize >= sizeof(uint32_t[1]));

  ((uint32_t *) buffer)[0] = get_size_of_block_data(block);
  return canOK;
}

/**
 * \note The use_crc field of txe_variable_sized_block is ignored.
 * No CRC32 verification is performed.
 */
static canStatus handle_get_data_requests(struct ltv_triple *block, void *buffer, unsigned int *bufsize)
{
  uint32_t offset_of_data = get_offset_of_block_data(block->tag);
  uint32_t required_bufsize = get_size_of_block_data(block);

  if (!buffer || *bufsize < required_bufsize) {
    *bufsize = required_bufsize;
    return !buffer ? canOK : canERR_BUFFER_TOO_SMALL;
  }

  memcpy(buffer, ((uint8_t *) block->value) + offset_of_data, required_bufsize);
  *bufsize = required_bufsize;

  return canOK;
}

/**
 * The source code block is optional, return 0 bytes (*bufsize=0) if missing.
 */
static canStatus handle_source_code_requests(struct ltv_triple *block, void *buffer, unsigned int *bufsize)
{
  if (block->length == 0) {
    *bufsize = 0;
    return canOK;
  }

  return handle_get_data_requests(block, buffer, bufsize);
}

static canStatus handle_is_encrypted_requests(struct ltv_triple *block, void *buffer, unsigned int *bufsize)
{
  struct txe_key_block *key_blk = (struct txe_key_block *) block->value;

  assert(block->length >= offsetof(
           struct txe_key_block, data) && *bufsize >= sizeof(uint32_t[1]));

  ((uint32_t *) buffer)[0] = key_blk->encryption_type == 0 ? 0 : 1;

  return canOK;
}

typedef canStatus (*handler_func)(struct ltv_triple *block, void *buffer, unsigned int *bufsize);

struct request_handler {
  int item;
  handler_func func;
  enum txe_block_tags tag;
  /**
   * Contains the minimal required size of the response buffer.
   * A zero indicates that the buffer size was not known at CANlib compile time and or
   * is dependent on the content of the .txe file.
   */
  unsigned int required_bufsize;
  /**
   * This handler will accept a "not found" ltv_triple { 0, 0, NULL}
   */
  uint32_t block_is_optional : 1;
};

struct request_handler request_handlers[] = {
  {canTXEDATA_FILE_VERSION,     handle_file_version_requests,     VERSION_TAG,     sizeof(uint32_t[3]), 0},
  {canTXEDATA_COMPILER_VERSION, handle_compiler_version_requests, VERSION_TAG,     sizeof(uint32_t[3]), 0},
  {canTXEDATA_DATE,             handle_date_requests,             DATE_TAG,        sizeof(uint32_t[6]), 0},
  {canTXEDATA_SIZE_OF_CODE,     handle_size_of_code_requests,     PCODE_TAG,       sizeof(uint32_t),    0},
  {canTXEDATA_IS_ENCRYPTED,     handle_is_encrypted_requests,     KEY_TAG,         sizeof(uint32_t),    0},
  {canTXEDATA_DESCRIPTION,      handle_get_data_requests,         DESCRIPTION_TAG, 0,                   0},
  {canTXEDATA_SOURCE,           handle_source_code_requests,      SOURCE_CODE_TAG, 0,                   1},
};

static struct request_handler *find_handler(struct request_handler *handler_table, size_t entries, int item)
{
  struct request_handler *current;
  struct request_handler *end = handler_table + entries;
  for (current = handler_table; current < end; current++) {
    if (current->item == item) {
      return current;
    }
  }
  return NULL;
}

static canStatus
find_block(struct ltv_stream_iterator *ltv, uint32_t tag, int optional, struct ltv_triple *block, const char *path)
{
  uint32_t length;

  block->length = 0;
  block->tag = 0;
  block->value = NULL;

#if !DEBUG
  (void) path;
#endif

  ltv_search(ltv, tag);
  if (ltv_get_status(ltv)!=LTV_STREAM_OK) {
    TXE_DBG_VPPRINTF(path, "An error occurred while searching for %s block.", tag_to_str(tag));
    return canERR_SCRIPT_TXE_CONTAINER_FORMAT;
  }

  if (ltv_get_eof(ltv)) {
    if (optional) {
      return canOK;
    }else{
      TXE_DBG_VPPRINTF(path, "Could not find required %s block.", tag_to_str(tag));
      return canERR_SCRIPT_TXE_CONTAINER_FORMAT;
    }
  }

  length = ltv_get_length(ltv);
  if (!validate_txe_block_size(tag, length)) {
    TXE_DBG_VPPRINTF(path, "%s block has unexpected size %u.", tag_to_str(tag), length);
    return canERR_SCRIPT_TXE_CONTAINER_FORMAT;
  }

  ltv_get_value(ltv);
  if (ltv_get_status(ltv)!=LTV_STREAM_OK) {
    TXE_DBG_VPPRINTF(path, "Failed to read block %s value of length %u.", tag_to_str(tag), length);
    return canERR_SCRIPT_TXE_CONTAINER_FORMAT;
  }

  block->length = ltv_get_length(ltv);
  block->tag = ltv_get_tag(ltv);
  block->value = ltv_get_value(ltv);

  return canOK;
}

/**
 * Verify that the next stream block is of type VERSION and contains the expected container file format version.
 * \note assumes little endian platform
 * \param ltv instance pointer ltv stream
 * \param major expected major container version
 * \param minor expected minor container version
 * \param path name of container file (only used for debugging output)
 * \return canOK if version is ok
 * \return canERR_SCRIPT_TXE_CONTAINER_VERSION If unexpected version was found
 * \return canERR_SCRIPT_TXE_CONTAINER_FORMAT If format or I/O error
 *
 */
static canStatus
verify_container_version(struct ltv_stream_iterator *ltv, uint32_t major, uint32_t minor, const char *path)
{

  struct txe_version_block *version;

#if !DEBUG
  (void) path;
#endif

  ltv_next(ltv);
  if (ltv_get_status(ltv)!=LTV_STREAM_OK || ltv_get_eof(ltv)) {
    TXE_DBG_PPRINTF(path, "Failed to read header of version block.");
    return canERR_SCRIPT_TXE_CONTAINER_FORMAT;
  }

  if (ltv_get_tag(ltv) != VERSION_TAG || ltv_get_length(ltv) != sizeof(struct txe_version_block)) {
    TXE_DBG_PPRINTF(path, "Not a compatible version block.");
    return canERR_SCRIPT_TXE_CONTAINER_FORMAT;
  }

  version = (struct txe_version_block *) ltv_get_value(ltv);
  if (!version) {
    TXE_DBG_PPRINTF(path, "Failed to read value of version block.");
    return canERR_SCRIPT_TXE_CONTAINER_FORMAT;
  }

  if (version->magic_number != TXE_CONTAINER_V1_0_MAGIC_NUMBER) {
    TXE_DBG_VPPRINTF(path, "Expected magic 0x%08x but was 0x%08x.", TXE_CONTAINER_V1_0_MAGIC_NUMBER,
                    version->magic_number);
    return canERR_SCRIPT_TXE_CONTAINER_FORMAT;
  }

  if (version->file_major != major || version->file_minor != minor) {
    TXE_DBG_VPPRINTF(path, "Expected version %i.%i but was %i.%i", major, minor, version->file_major,
                    version->file_minor);
    return canERR_SCRIPT_TXE_CONTAINER_VERSION;
  }

  return canOK;
}


kvStatus CANLIBAPI kvScriptTxeGetData(const char *filePathOnPC,
                                      int item,
                                      void *buffer,
                                      unsigned int *bufsize)
{
  canStatus status = canOK;
  FILE *stream;
  struct ltv_stream_iterator *ltv;
  struct request_handler *handler;
  struct ltv_triple block = {0};

  if (!filePathOnPC || !bufsize || strlen(filePathOnPC)==0) {
    return canERR_PARAM;
  }

  stream = txe_fopen(filePathOnPC);
  if (stream == NULL) {
    TXE_DBG_PPRINTF(filePathOnPC, "Could not open .txe file.");
    return canERR_HOST_FILE;
  }

  ltv = ltv_create(stream);
  if (!ltv) {
    TXE_DBG_PPRINTF(filePathOnPC, "Failed to create stream iterator.");
    status = canERR_NOMEM;
    goto out_close_stream;
  }

  status = verify_container_version(ltv, 1, 0, filePathOnPC);
  if (status != canOK) {
    TXE_DBG_PPRINTF(filePathOnPC, "Failed to verify .txe file.");
    goto out_destroy_ltv;
  }

  handler = find_handler(request_handlers, ARRAY_SIZE(request_handlers), item);
  if (!handler) {
    TXE_DBG_VPPRINTF(filePathOnPC, "Unknown item %i requested.", item);
    status = canERR_PARAM;
    goto out_destroy_ltv;
  }

  status = find_block(ltv, handler->tag, handler->block_is_optional, &block, filePathOnPC);
  if (status != canOK) {
    goto out_destroy_ltv;
  }

  // If the required output buffer size is known then bufsize can be validated and updated.
  if (handler->required_bufsize) {
    if (!buffer || *bufsize < handler->required_bufsize) {
      *bufsize = handler->required_bufsize;
      return !buffer ? canOK : canERR_BUFFER_TOO_SMALL;
    }
    *bufsize = handler->required_bufsize;
  }

  status = handler->func(&block, buffer, bufsize);

  out_destroy_ltv:
  ltv_destroy(ltv);
  out_close_stream:
  fclose(stream);

  return status;
}
