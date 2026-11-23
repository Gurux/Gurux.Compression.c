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
#ifndef GX_V44_H
#define GX_V44_H

#include <stddef.h>
#include "gxbytebuffer.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @file gxv44.h
 * @brief Synchronous V.44 codec for non-overlapping binary gxByteBuffers.
 *
 * Requires eight-bit bytes and the selected standalone or external Gurux buffer
 * layout. Calls run synchronously without I/O, waits or timeouts. Optional trace
 * callbacks may cancel only when COMPRESSION_TRACE is defined.
 * Different contexts with disjoint storage may run concurrently. Sharing a
 * context or its buffers between threads or interrupts requires caller locking;
 * allocation-enabled calls are unsuitable for interrupt handlers.
 */

/** @brief Codec status values; upstream buffer errors are mapped to these codes. */
typedef enum
{
    GX_V44_OK = 0,                /**< Operation completed successfully. */
    GX_V44_INVALID_ARGUMENT = -1, /**< Invalid options, state, storage or buffer. */
    GX_V44_INVALID_DATA = -2,     /**< Malformed/truncated stream or history overflow. */
#ifdef COMPRESSION_TRACE
    GX_V44_CANCELLED = -4, /**< Trace callback requested cancellation. */
#endif
    GX_V44_OUT_OF_MEMORY = -3 /**< Allocation failed or output capacity exhausted. */
} GX_V44_ERROR;

/** @brief Negotiated initial codeword width, in bits. */
typedef enum
{
    GX_V44_CODEWORD_SIZE_6 = 6,   /**< Six bits; the default. */
    GX_V44_CODEWORD_SIZE_7 = 7,   /**< Seven bits. */
    GX_V44_CODEWORD_SIZE_8 = 8,   /**< Eight bits. */
    GX_V44_CODEWORD_SIZE_9 = 9,   /**< Nine bits. */
    GX_V44_CODEWORD_SIZE_10 = 10, /**< Ten bits. */
    GX_V44_CODEWORD_SIZE_11 = 11, /**< Eleven bits. */
    GX_V44_CODEWORD_SIZE_12 = 12, /**< Twelve bits. */
    GX_V44_CODEWORD_SIZE_13 = 13, /**< Thirteen bits. */
    GX_V44_CODEWORD_SIZE_14 = 14, /**< Fourteen bits. */
    GX_V44_CODEWORD_SIZE_15 = 15, /**< Fifteen bits. */
    GX_V44_CODEWORD_SIZE_16 = 16  /**< Sixteen bits. */
} GX_V44_CODEWORD_SIZE;

/** @brief Negotiated initial ordinal width, in bits. */
typedef enum
{
    GX_V44_ORDINAL_SIZE_7 = 7, /**< Seven bits; the default. */
    GX_V44_ORDINAL_SIZE_8 = 8  /**< Eight bits. */
} GX_V44_ORDINAL_SIZE;

/** @brief Supported negotiated maximum string lengths, in bytes. */
typedef enum
{
    GX_V44_MAXIMUM_STRING_LENGTH_46 = 46,   /**< At most 46 bytes. */
    GX_V44_MAXIMUM_STRING_LENGTH_78 = 78,   /**< At most 78 bytes. */
    GX_V44_MAXIMUM_STRING_LENGTH_142 = 142, /**< At most 142 bytes. */
    GX_V44_MAXIMUM_STRING_LENGTH_255 = 255  /**< At most 255 bytes; the default. */
} GX_V44_MAXIMUM_STRING_LENGTH;

/** @brief Negotiated bounds, copied into the context during initialization.
 * Rebuild all consumers when migrating from the former unsigned width/string
 * fields. Enum representation is compiler-dependent; library and consumers
 * must use matching enum-size/packing options. Do not serialize this structure.
 */
typedef struct
{
    GX_V44_CODEWORD_SIZE default_codeword_size; /**< Initial codeword width. */
    GX_V44_ORDINAL_SIZE default_ordinal_size;   /**< Initial ordinal width. */
    unsigned int max_codewords; /**< Codeword limit including controls, 256..65535. */
    GX_V44_MAXIMUM_STRING_LENGTH maximum_string_length; /**< Maximum decoded string length. */
    size_t history_limit; /**< Maximum history bytes between resets, >=512. */
} gxV44Options;

/** @brief Storage for one non-control codeword; fields are private codec state.
 * Allocate at least max_codewords - 4 entries for gxv44_attach(). Do not modify
 * entries during an operation. No separate history allocation is required.
 */
typedef struct
{
    size_t position;     /**< Encoder segment start or decoder inclusive end offset. */
    unsigned int length; /**< Segment/string length in bytes. */
    unsigned int down;   /**< Encoder child codeword, or zero for no child. */
    unsigned int side;   /**< Encoder sibling codeword, or zero for no sibling. */
} gxV44Entry;

#ifdef COMPRESSION_TRACE
/** @brief Optional diagnostics. Define COMPRESSION_TRACE for library and callers.
 * This macro is intentionally not defined by this header. When absent, all trace
 * types, callback fields, registration and event generation are excluded.
 */
typedef enum
{
    GX_V44_TRACE_NONE = -3,            /**< No diagnostic operation. */
    GX_V44_TRACE_START = -2,           /**< Beginning of a valid operation. */
    GX_V44_TRACE_END = -1,             /**< Complete result, before committing positions. */
    GX_V44_TRACE_ORDINAL = 0,          /**< Ordinal byte; value contains the byte. */
    GX_V44_TRACE_CODEWORD = 1,         /**< Dictionary codeword; value contains its code. */
    GX_V44_TRACE_STRING_EXTENSION = 2, /**< String extension; value contains its length. */
    GX_V44_TRACE_STEPUP_ORDINAL = 3,   /**< Increased ordinal width; value is the new width. */
    GX_V44_TRACE_STEPUP_CODEWORD = 4,  /**< Increased codeword width; value is the new width. */
    GX_V44_TRACE_FLUSH = 5,            /**< FLUSH control completed. */
    GX_V44_TRACE_REINIT = 6,           /**< Dictionaries/widths reset. */
    GX_V44_TRACE_ERROR = 7,            /**< Failed/cancelled operation before rollback. */
    GX_V44_TRACE_PROCESSING = 8        /**< Dictionary codeword processing completed. */
} GX_V44_TRACE_OPERATION;

/** @brief Allocation-free C counterpart of .NET GXV44TraceArgs.
 * All pointers are borrowed, read-only views valid only during the callback.
 * Output may be reallocated later. Copy needed diagnostics into caller storage;
 * do not retain these views or modify buffers, entries, codec state or this
 * structure except stop. No strings or dictionary snapshots are allocated.
 */
typedef struct
{
    int compress;                       /**< Nonzero for encoding; zero for decoding. */
    GX_V44_TRACE_OPERATION operation;   /**< Event kind, matching .NET operation values. */
    unsigned int value;                 /**< Byte, codeword, extension length or width. */
    GX_V44_ERROR error;                 /**< GX_V44_OK except at ERROR. */
    size_t input_position;              /**< Current absolute input byte coordinate. */
    size_t output_position;             /**< Current absolute output size in bytes. */
    size_t compressed_position;         /**< Compressed byte coordinate of the next bit. */
    unsigned int compressed_bit;        /**< Next bit within that byte, 0..7, LSB first. */
    GX_V44_CODEWORD_SIZE codeword_size; /**< Current codeword width. */
    GX_V44_ORDINAL_SIZE ordinal_size;   /**< Current ordinal width. */
    const gxByteBuffer* compressed;     /**< Compressed buffer view, including any prefix. */
    const gxByteBuffer* uncompressed;   /**< Uncompressed buffer view. */
    const gxV44Entry* dictionary;       /**< Encoder segments or decoder strings. */
    unsigned int dictionary_count;      /**< Number of populated dictionary entries. */
    int stop;                           /**< Set nonzero to cancel; ignored at ERROR. */
} CGXV44TraceArgs;

/** @brief Synchronous trace callback; it must not reenter or mutate this codec.
 * @param[in,out] args Temporary diagnostic view; only stop may be changed.
 * @param[in] user_data Caller-owned value supplied to gxv44_set_trace().
 */
typedef void (*gxV44TraceCallback)(CGXV44TraceArgs* args, void* user_data);
#endif

/** @brief Caller-owned codec context; all fields are implementation state.
 * Initialize with gxv44_init() or gxv44_attach(), and clear before initializing
 * again. Do not copy initialized contexts, alter fields or share storage between
 * concurrent operations. No global mutable state or recursive calls are used.
 */
typedef struct
{
    gxV44Options options;    /**< Validated option snapshot. */
    gxV44Entry* entries;     /**< Owned or attached dictionary, separate from payload buffers. */
    size_t entry_capacity;   /**< Available dictionary entries, not bytes. */
    unsigned int roots[256]; /**< First codeword for each root byte, or zero. */
    unsigned int count;      /**< Number of populated dictionary entries. */
    GX_V44_CODEWORD_SIZE codeword_size; /**< Current codeword width in bits. */
    GX_V44_ORDINAL_SIZE ordinal_size;   /**< Current ordinal width in bits. */
    unsigned char owns_entries;         /**< Nonzero only for allocator-owned entries. */
#ifdef COMPRESSION_TRACE
    gxV44TraceCallback on_trace; /**< Optional callback; initialized to NULL. */
    void* trace_user_data;       /**< Caller-owned callback state; never freed. */
#endif
} gxV44;

#ifdef COMPRESSION_TRACE
/** @brief Register or disable optional synchronous tracing without allocating.
 * @param[in,out] codec Initialized context, or NULL for a no-op.
 * @param[in] callback Callback, or NULL to disable events. Reset preserves it;
 * clear/reinitialization removes it. Do not call this while codec is running.
 * @param[in] user_data Borrowed state, valid until registration is replaced or
 * codec is cleared. The caller controls its lifetime and synchronization.
 * Cancellation returns GX_V44_CANCELLED with normal logical rollback. ERROR
 * events retain the original failure regardless of stop. Invalid arguments
 * rejected before codec preparation do not generate callbacks.
 */
void gxv44_set_trace(gxV44* codec, gxV44TraceCallback callback, void* user_data);
#endif

/** @brief Set defaults matching Gurux.Compression.Net.
 * @param[out] options Destination, or NULL for a no-op.
 */
void gxv44_options_init(gxV44Options* options);

/** @brief Validate negotiated bounds without modifying them.
 * @param[in] options Options to check; NULL is invalid here.
 * @return GX_V44_OK or GX_V44_INVALID_ARGUMENT.
 */
int gxv44_options_validate(const gxV44Options* options);

/** @brief Initialize a context and allocate its dictionary through the configured allocator.
 * @param[out] codec Uninitialized or previously cleared context; cannot be NULL.
 * @param[in] options Options to copy, or NULL for defaults. Must not alias codec.
 * @return GX_V44_OK, GX_V44_INVALID_ARGUMENT or GX_V44_OUT_OF_MEMORY.
 * @note DLMS_IGNORE_MALLOC always returns GX_V44_OUT_OF_MEMORY for valid options;
 * use gxv44_attach() instead. Allocation failure leaves codec safe to clear.
 * The caller owns codec and must eventually call gxv44_clear(). This call does
 * not release a previous dictionary: clear before reinitializing.
 */
int gxv44_init(gxV44* codec, const gxV44Options* options);

/** @brief Initialize without allocating or copying dictionary storage.
 * @param[out] codec Uninitialized or cleared context; cannot be NULL.
 * @param[in] options Options to copy, or NULL for defaults. Must not alias codec.
 * @param[in,out] entries Caller-owned array, separate from codec and all buffers.
 * Must remain valid until gxv44_clear(). Its previous contents are not read.
 * @param[in] entry_capacity Array length in entries, at least max_codewords - 4;
 * the required byte count must be representable by size_t.
 * @return GX_V44_OK or GX_V44_INVALID_ARGUMENT. Failure leaves codec safe to clear.
 * Neither this call nor gxv44_clear() frees caller-owned entries.
 */
int gxv44_attach(gxV44* codec, const gxV44Options* options, gxV44Entry* entries,
                 size_t entry_capacity);

/** @brief Reset dictionary links/widths, retaining options and storage.
 * @param[in,out] codec Initialized context, or NULL for a no-op.
 * Each valid compression/decompression call resets automatically. No storage is freed.
 */
void gxv44_reset(gxV44* codec);

/** @brief Free only an owned dictionary and zero the context.
 * @param[in,out] codec Initialized/cleared context or a failed initialization
 * result, or NULL for a no-op. A never-initialized object is not valid here.
 * Repeated clear calls are safe. Attached buffers and entries remain caller-owned.
 */
void gxv44_clear(gxV44* codec);

/** @brief Compress one complete independent binary stream, including FLUSH.
 * @param[in,out] codec Initialized, exclusively owned context.
 * @param[in,out] input Reads bytes [position, size); success advances position
 * to size. Data is never modified. Empty input produces a FLUSH-only stream.
 * @param[in,out] output Appends bytes after size; position and the existing prefix
 * are preserved. Capacity grows through gxByteBuffer unless attached/no-malloc.
 * @return GX_V44_OK, GX_V44_INVALID_ARGUMENT or GX_V44_OUT_OF_MEMORY.
 * Trace-enabled builds can also return GX_V44_CANCELLED.
 * Buffers, their allocations, codec and dictionary storage must be separate.
 * On failure input.position and output.size are unchanged; capacity and bytes
 * beyond the old output size may change. The context is reusable after failure.
 * Worst-case output sizing and memory assumptions are documented in README.md.
 */
int gxv44_compress(gxV44* codec, gxByteBuffer* input, gxByteBuffer* output);

/** @brief Decompress one complete V.44 stream with a terminal padded FLUSH.
 * @param[in,out] codec Initialized, exclusively owned context.
 * @param[in,out] input Compressed bytes [position, size); success advances
 * position to size. Input data is never modified. An empty stream is invalid.
 * @param[in,out] output Appends decoded bytes after size, retaining position
 * and the existing prefix. Provide space for the expected uncompressed length.
 * @return GX_V44_OK, GX_V44_INVALID_ARGUMENT, GX_V44_INVALID_DATA or
 * GX_V44_OUT_OF_MEMORY. Truncated tokens and nonzero FLUSH padding are rejected.
 * Trace-enabled builds can also return GX_V44_CANCELLED.
 * Storage separation, failure rollback and allocation rules match
 * gxv44_compress(). Processing is synchronous and has no I/O or timeout.
 */
int gxv44_decompress(gxV44* codec, gxByteBuffer* input, gxByteBuffer* output);

#ifdef __cplusplus
}
#endif
#endif
