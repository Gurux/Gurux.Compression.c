/*
 * --------------------------------------------------------------------------
 * Gurux Ltd
 *
 * Copyright (c) Gurux Ltd.
 *
 * This file is part of Gurux Device Framework.
 *
 * Commit: $Format:%H$
 * Date:   $Format:%cI$
 *
 * This software is available under either of the following licenses:
 *
 * 1. GNU General Public License, version 2 only (GPL-2.0-only).
 *    See the LICENSE file included with this distribution.
 *
 * 2. A commercial license granted by Gurux Ltd under a separate written
 *    agreement. Use under this option requires a valid commercial license
 *    and is governed by the terms of that agreement.
 *
 * If you have not obtained a commercial license from Gurux Ltd,
 * use of this software is governed by GPL-2.0-only.
 *
 * Commercial licensing: https://www.gurux.fi/
 *
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Gurux-Commercial
 * --------------------------------------------------------------------------
 */
#ifndef GX_COMPRESSION_BYTE_BUFFER_H
#define GX_COMPRESSION_BYTE_BUFFER_H

#ifdef GX_COMPRESSION_CONFIG_HEADER
#include GX_COMPRESSION_CONFIG_HEADER
#endif

#include <stddef.h>
#include <limits.h>

/** @file gxbytebuffer.h
 * @brief Synchronous byte storage subset used by the V.44 codec.
 * Define GX_COMPRESSION_EXTERNAL_BUFFER_HEADER for all translation units to
 * include the application's Gurux buffer instead; exclude gxbytebuffer.c.
 * Buffers own allocated storage or borrow attached storage. Distinct buffers
 * with disjoint storage are reentrant; shared buffers require caller locking.
 * Allocation-enabled calls are unsuitable for interrupt handlers.
 */

#ifdef GX_COMPRESSION_EXTERNAL_BUFFER_HEADER
#include "gxexternalbuffer.h"
#endif

#if defined(GX_DLMS_BYTE_BUFFER_SIZE_32) || \
    (!defined(GX_DLMS_MICROCONTROLLER) && \
     (defined(_WIN32) || defined(_WIN64) || defined(__linux__)))
#ifdef GX_COMPRESSION_EXTERNAL_BUFFER_HEADER
typedef uint32_t gxV44BufferSize;
#elif UINT_MAX == 0xFFFFFFFFUL
/** @brief Unsigned buffer count; 32 or 16 bits, matching the selected layout. */
typedef unsigned int gxV44BufferSize;
#elif ULONG_MAX == 0xFFFFFFFFUL
typedef unsigned long gxV44BufferSize;
#else
#error A 32-bit unsigned integer type is required for this buffer layout.
#endif
#else
#ifdef GX_COMPRESSION_EXTERNAL_BUFFER_HEADER
typedef uint16_t gxV44BufferSize;
#elif USHRT_MAX == 0xFFFFUL
typedef unsigned short gxV44BufferSize;
#elif UINT_MAX == 0xFFFFUL
typedef unsigned int gxV44BufferSize;
#else
#error A 16-bit unsigned integer type is required for this buffer layout.
#endif
#endif

#ifndef GX_COMPRESSION_EXTERNAL_BUFFER_HEADER
#ifdef __cplusplus
extern "C" {
#endif

/** @brief Buffer result codes. External buffers retain their own error codes. */
typedef enum
{
    GX_BYTE_BUFFER_OK = 0, /**< Success. */
    GX_BYTE_BUFFER_INVALID_ARGUMENT = -1, /**< Invalid pointer, count or state. */
    GX_BYTE_BUFFER_OUT_OF_MEMORY = -3 /**< Capacity limit or allocation failure. */
} GX_BYTE_BUFFER_ERROR;

/** @brief Gurux-compatible layout. The capacity high bit marks borrowed data.
 * Initialize before use. Do not copy an owning buffer or serialize this struct.
 * Rebuild all consumers with matching layout and allocation configuration.
 */
typedef struct
{
    unsigned char* data; /**< Allocated or borrowed bytes; no implicit NUL. */
    gxV44BufferSize capacity; /**< Capacity plus the attachment flag. */
    gxV44BufferSize size; /**< Stored byte count. */
    gxV44BufferSize position; /**< Read cursor, from zero through size. */
} gxByteBuffer;

/** @brief Initialization entry point; bb_Init avoids ESP-IDF's bb_init name. */
#ifndef ESP_PLATFORM
#define BYTE_BUFFER_INIT bb_init
#else
#define BYTE_BUFFER_INIT bb_Init
#endif

/** @brief Initialize uninitialized storage; does not free previous storage.
 * @param[out] bb Buffer. @return 0, or -1 for NULL.
 */
int BYTE_BUFFER_INIT(gxByteBuffer* bb);
/** @brief Test attachment. @param[in] bb Buffer. @return Nonzero if borrowed. */
char bb_isAttached(gxByteBuffer* bb);
/** @brief Read capacity without flag. @param[in] bb Buffer. @return Capacity, or 0 for NULL. */
gxV44BufferSize bb_getCapacity(gxByteBuffer* bb);
/** @brief Read size. @param[in] bb Buffer. @return Size, or 0 for NULL. */
gxV44BufferSize bb_size(gxByteBuffer* bb);
/** @brief Read remaining bytes. @param[in] bb Buffer. @return Remaining bytes, or 0 for invalid state. */
gxV44BufferSize bb_available(gxByteBuffer* bb);
/** @brief Resize owned storage; attached capacity is fixed.
 * Shrinking clamps size and position. Zero frees owned storage. Failure leaves
 * the buffer unchanged. DLMS_IGNORE_MALLOC disables allocation.
 * @param[in,out] bb Initialized buffer. @param[in] capacity Requested bytes,
 * at most 32767 or 2147483647 for the selected layout.
 * @return 0, -1 for invalid state, or -3 for unavailable capacity.
 */
int bb_capacity(gxByteBuffer* bb, gxV44BufferSize capacity);
/** @brief Attach caller-owned storage without freeing an earlier allocation.
 * Clear owned storage first. Failure leaves the buffer unchanged.
 * @param[out] bb Buffer. @param[in] value Borrowed storage, valid until detached.
 * @param[in] count Initial size. @param[in] capacity Storage size, without flag.
 * NULL storage is allowed only when count and capacity are zero.
 * @return 0, -1 for invalid arguments, or -3 for excessive capacity.
 */
int bb_attach(gxByteBuffer* bb, unsigned char* value,
              gxV44BufferSize count, gxV44BufferSize capacity);
/** @brief Append bytes, supporting a source within the buffer's stored bytes.
 * Failure preserves buffer state. Does not advance position.
 * @param[in,out] bb Initialized buffer. @param[in] value Source bytes; may be
 * NULL only for count zero. @param[in] count Source length.
 * @return 0, -1 for invalid arguments, or -3 for unavailable capacity.
 */
int bb_set(gxByteBuffer* bb, const unsigned char* value, gxV44BufferSize count);
/** @brief Append one byte. @param[in,out] bb Buffer. @param[in] value Byte.
 * @return Same result codes as bb_set.
 */
int bb_setUInt8(gxByteBuffer* bb, unsigned char value);
/** @brief Reset size/cursor, retaining storage. @param[in,out] bb Buffer.
 * @return 0, or -1 for NULL.
 */
int bb_empty(gxByteBuffer* bb);
/** @brief Free owned storage; retain attached storage and reset size/cursor.
 * @param[in,out] bb Initialized buffer. @return 0, or -1 for NULL.
 */
int bb_clear(gxByteBuffer* bb);

#ifdef __cplusplus
}
#endif
#endif
#endif
