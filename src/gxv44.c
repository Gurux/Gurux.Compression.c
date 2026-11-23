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
#include "gxv44.h"
#include "gxv44memory.h"
#include "gxv44bufferlimits.h"
#include <string.h>
#include <limits.h>

#if CHAR_BIT != 8
#error Gurux V.44 requires eight-bit bytes.
#endif

/** @brief Kind of the preceding data token; control codes do not change it. */
typedef enum
{
    GX_V44_TOKEN_NONE = 0,
    GX_V44_TOKEN_ORDINAL = 1,
    GX_V44_TOKEN_CODEWORD = 2,
    GX_V44_TOKEN_STRING_EXTENSION = 3
} GX_V44_TOKEN_TYPE;

/** @brief Reserved wire codewords and the dictionary's starting codeword. */
typedef enum
{
    GX_V44_CONTROL_TRANSPARENT = 0,
    GX_V44_CONTROL_FLUSH = 1,
    GX_V44_CONTROL_STEPUP = 2,
    GX_V44_CONTROL_REINIT = 3,
    GX_V44_CONTROL_COUNT = 4
} GX_V44_CONTROL_CODE;

/** @brief LSB-first bit cursor. The first error is sticky until rollback. */
typedef struct
{
    gxByteBuffer* buffer;
    size_t index;
    unsigned int bit;
    GX_V44_ERROR error;
#ifdef COMPRESSION_TRACE
    const gxByteBuffer* uncompressed;
    size_t input_position;
    int compress;
#endif
} gxV44Bits;

#ifdef COMPRESSION_TRACE
void gxv44_set_trace(gxV44* c, gxV44TraceCallback callback, void* user_data)
{
    if (c != NULL)
    {
        c->on_trace = callback;
        c->trace_user_data = user_data;
    }
}

/** @brief Publish transient views; a pending error suppresses non-error events. */
static void trace_event(gxV44* c, gxV44Bits* b, GX_V44_TRACE_OPERATION operation,
                        unsigned int value)
{
    CGXV44TraceArgs args;
    if ((c->on_trace == NULL) || ((b->error != GX_V44_OK) && (operation != GX_V44_TRACE_ERROR)))
    {
        return;
    }
    memset(&args, 0, sizeof(args));
    args.compress = b->compress;
    args.operation = operation;
    args.value = value;
    args.error = b->error;
    args.input_position = b->compress ? b->input_position : b->index;
    args.output_position = b->compress ? b->buffer->size : b->uncompressed->size;
    args.compressed_position =
        b->compress ? (size_t)b->buffer->size - (b->bit != 0 ? 1 : 0) : b->index;
    args.compressed_bit = b->bit;
    args.codeword_size = c->codeword_size;
    args.ordinal_size = c->ordinal_size;
    args.compressed = b->buffer;
    args.uncompressed = b->uncompressed;
    args.dictionary = c->entries;
    args.dictionary_count = c->count;
    c->on_trace(&args, c->trace_user_data);
    if (args.stop && (b->error == GX_V44_OK))
    {
        b->error = GX_V44_CANCELLED;
    }
}

#define TRACE_EVENT(c, b, operation, value) trace_event(c, b, operation, value)
#else
/* Fixed arguments keep this macro C90-compatible. Nothing is evaluated. */
#define TRACE_EVENT(c, b, operation, value) ((void)0)
#endif

void gxv44_options_init(gxV44Options* o)
{
    if (o != NULL)
    {
        o->default_codeword_size = GX_V44_CODEWORD_SIZE_6;
        o->default_ordinal_size = GX_V44_ORDINAL_SIZE_7;
        o->max_codewords = 1024;
        o->maximum_string_length = GX_V44_MAXIMUM_STRING_LENGTH_255;
        o->history_limit = 3072;
    }
}

int gxv44_options_validate(const gxV44Options* o)
{
    if ((o == NULL) || (o->default_codeword_size < GX_V44_CODEWORD_SIZE_6) ||
        (o->default_codeword_size > GX_V44_CODEWORD_SIZE_16) ||
        (o->default_ordinal_size < GX_V44_ORDINAL_SIZE_7) ||
        (o->default_ordinal_size > GX_V44_ORDINAL_SIZE_8) || (o->max_codewords < 256) ||
        (o->max_codewords > 65535) || (o->history_limit < 512) ||
        ((o->maximum_string_length != GX_V44_MAXIMUM_STRING_LENGTH_46) &&
         (o->maximum_string_length != GX_V44_MAXIMUM_STRING_LENGTH_78) &&
         (o->maximum_string_length != GX_V44_MAXIMUM_STRING_LENGTH_142) &&
         (o->maximum_string_length != GX_V44_MAXIMUM_STRING_LENGTH_255)))
    {
        return GX_V44_INVALID_ARGUMENT;
    }
    return 0;
}

void gxv44_reset(gxV44* c)
{
    if (c != NULL)
    {
        memset(c->roots, 0, sizeof(c->roots));
        c->count = 0;
        c->codeword_size = c->options.default_codeword_size;
        c->ordinal_size = c->options.default_ordinal_size;
    }
}

int gxv44_attach(gxV44* c, const gxV44Options* options, gxV44Entry* entries, size_t capacity)
{
    gxV44Options o;
    if (c == NULL)
    {
        return GX_V44_INVALID_ARGUMENT;
    }
    memset(c, 0, sizeof(*c));
    gxv44_options_init(&o);
    if (options != NULL)
    {
        o = *options;
    }
    if ((gxv44_options_validate(&o) != 0) || (entries == NULL) ||
        (capacity < (size_t)o.max_codewords - GX_V44_CONTROL_COUNT) ||
        ((size_t)o.max_codewords - GX_V44_CONTROL_COUNT > (size_t)-1 / sizeof(gxV44Entry)))
    {
        return GX_V44_INVALID_ARGUMENT;
    }
    c->options = o;
    c->entries = entries;
    c->entry_capacity = capacity;
    gxv44_reset(c);
    return 0;
}

int gxv44_init(gxV44* c, const gxV44Options* options)
{
    gxV44Options o;
#ifndef DLMS_IGNORE_MALLOC
    gxV44Entry* entries;
    size_t count;
    int ret;
#endif
    if (c == NULL)
    {
        return GX_V44_INVALID_ARGUMENT;
    }
    memset(c, 0, sizeof(*c));
    gxv44_options_init(&o);
    if (options != NULL)
    {
        o = *options;
    }
    if (gxv44_options_validate(&o) != 0)
    {
        return GX_V44_INVALID_ARGUMENT;
    }
#ifdef DLMS_IGNORE_MALLOC
    return GX_V44_OUT_OF_MEMORY;
#else
    count = (size_t)o.max_codewords - GX_V44_CONTROL_COUNT;
    if (count > (size_t)-1 / sizeof(gxV44Entry))
    {
        return GX_V44_OUT_OF_MEMORY;
    }
    entries = (gxV44Entry*)GX_COMPRESSION_MALLOC(count * sizeof(gxV44Entry));
    if (entries == NULL)
    {
        return GX_V44_OUT_OF_MEMORY;
    }
    ret = gxv44_attach(c, &o, entries, count);
    c->owns_entries = 1;
    return ret;
#endif
}

void gxv44_clear(gxV44* c)
{
    if (c != NULL)
    {
#ifndef DLMS_IGNORE_MALLOC
        if (c->owns_entries)
        {
            GX_COMPRESSION_FREE(c->entries);
        }
#endif
        memset(c, 0, sizeof(*c));
    }
}

static int append(gxByteBuffer* b, unsigned char value)
{
    size_t capacity;
    if (b->size >= gxv44_buffer_limit())
    {
        return GX_V44_OUT_OF_MEMORY;
    }
    capacity = bb_getCapacity(b);
    if (b->size == capacity)
    {
        size_t next;
        next = gxv44_buffer_growth(capacity, (size_t)b->size + 1, gxv44_buffer_limit());
        if (bb_capacity(b, (gxV44BufferSize)next) != 0)
        {
            return GX_V44_OUT_OF_MEMORY;
        }
    }
    b->data[b->size] = value;
    ++b->size;
    return 0;
}

static void put(gxV44Bits* b, unsigned int value, unsigned int width)
{
    unsigned int i;
    for (i = 0; (i < width) && (b->error == 0); ++i)
    {
        if (b->bit == 0)
        {
            b->index = b->buffer->size;
            b->error = append(b->buffer, 0);
            if (b->error != 0)
            {
                return;
            }
        }
        if ((value >> i) & 1U)
        {
            b->buffer->data[b->index] |= (unsigned char)(1U << b->bit);
        }
        b->bit = (b->bit + 1) & 7U;
    }
}

static unsigned int get(gxV44Bits* b, unsigned int width)
{
    unsigned int value, i;
    value = 0;
    for (i = 0; (i < width) && (b->error == 0); ++i)
    {
        if (b->index >= b->buffer->size)
        {
            b->error = GX_V44_INVALID_DATA;
            return 0;
        }
        value |= ((unsigned int)(b->buffer->data[b->index] >> b->bit) & 1U) << i;
        ++b->bit;
        if (b->bit == 8)
        {
            ++b->index;
            b->bit = 0;
        }
    }
    return value;
}

static unsigned int extension_width(GX_V44_MAXIMUM_STRING_LENGTH maximum)
{
    if (maximum == GX_V44_MAXIMUM_STRING_LENGTH_46)
    {
        return 5;
    }
    if (maximum == GX_V44_MAXIMUM_STRING_LENGTH_78)
    {
        return 6;
    }
    if (maximum == GX_V44_MAXIMUM_STRING_LENGTH_142)
    {
        return 7;
    }
    return 8;
}

static void put_extension(gxV44Bits* b, unsigned int length, GX_V44_MAXIMUM_STRING_LENGTH maximum)
{
    if (length == 1)
    {
        put(b, 1, 1);
    }
    else if (length <= 4)
    {
        put(b, 0, 1);
        put(b, length - 1, 2);
    }
    else if (length <= 12)
    {
        put(b, (length - 5) << 4, 7);
    }
    else
    {
        put(b, 8, 4);
        put(b, length - 13, extension_width(maximum));
    }
}

static unsigned int get_extension(gxV44Bits* b, GX_V44_MAXIMUM_STRING_LENGTH maximum)
{
    unsigned int n;
    if (get(b, 1) != 0)
    {
        return 1;
    }
    n = get(b, 2);
    if (n != 0)
    {
        return n + 1;
    }
    if (get(b, 1) == 0)
    {
        return get(b, 3) + 5;
    }
    n = get(b, extension_width(maximum)) + 13;
    if (n > (unsigned int)maximum - 2)
    {
        b->error = GX_V44_INVALID_DATA;
    }
    return n;
}

static void write_code(gxV44* c, gxV44Bits* b, unsigned int code)
{
    put(b, 1, 1);
    put(b, code, c->codeword_size);
}

static void write_control(gxV44* c, gxV44Bits* b, GX_V44_CONTROL_CODE code)
{
    write_code(c, b, (unsigned int)code);
}

static void ordinal(gxV44* c, gxV44Bits* b, unsigned int ch, GX_V44_TOKEN_TYPE previous)
{
    if ((ch > 127) && (c->ordinal_size == GX_V44_ORDINAL_SIZE_7))
    {
        write_control(c, b, GX_V44_CONTROL_STEPUP);
        ++c->ordinal_size;
        TRACE_EVENT(c, b, GX_V44_TRACE_STEPUP_ORDINAL, c->ordinal_size);
    }
    else if (previous == GX_V44_TOKEN_CODEWORD)
    {
        put(b, 0, 1);
    }
    put(b, 0, 1);
    put(b, ch, c->ordinal_size);
}

static void string_code(gxV44* c, gxV44Bits* b, unsigned int code, unsigned int ext)
{
    while ((unsigned long)code >= (1UL << c->codeword_size))
    {
        write_control(c, b, GX_V44_CONTROL_STEPUP);
        ++c->codeword_size;
        TRACE_EVENT(c, b, GX_V44_TRACE_STEPUP_CODEWORD, c->codeword_size);
    }
    write_code(c, b, code);
    if (ext != 0)
    {
        put(b, 2, 2);
        put_extension(b, ext, c->options.maximum_string_length);
    }
}

/** @brief Append one history segment to a root/child sibling chain.
 * Callers reserve one entry at each token boundary. Segment coordinates refer
 * to input bytes, including the implicit root represented by the chain.
 */
static void add_encoder(gxV44* c, unsigned int* head, size_t pos, unsigned int len)
{
    gxV44Entry* n;
    unsigned int code;
    code = c->count + GX_V44_CONTROL_COUNT;
    n = &c->entries[c->count];
    ++c->count;
    n->position = pos;
    n->length = len;
    n->down = n->side = 0;
    while (*head != 0)
    {
        head = &c->entries[*head - GX_V44_CONTROL_COUNT].side;
    }
    *head = code;
}

static int valid_buffer(gxByteBuffer* b)
{
    return (b != NULL) && (b->position <= b->size) && (b->size <= bb_getCapacity(b)) &&
           ((b->data != NULL) || (bb_getCapacity(b) == 0));
}

/** @brief Reject overlapping allocations using portable pointer equality.
 * Pointer equality is portable even for unrelated objects. Scan allocations to
 * reject overlapping storage without ordering unrelated pointers (undefined C). */
static int overlaps(const unsigned char* a, size_t an, const unsigned char* b, size_t bn)
{
    size_t i;
    if ((a == NULL) || (b == NULL) || (an == 0) || (bn == 0))
    {
        return 0;
    }
    for (i = 0; i < an; ++i)
    {
        if (a + i == b)
        {
            return 1;
        }
    }
    for (i = 0; i < bn; ++i)
    {
        if (b + i == a)
        {
            return 1;
        }
    }
    return 0;
}

static int prepare(gxV44* c, gxByteBuffer* input, gxByteBuffer* output)
{
    size_t bytes;
    if ((c == NULL) || (c->entries == NULL) || (gxv44_options_validate(&c->options) != 0) ||
        (c->entry_capacity < (size_t)c->options.max_codewords - GX_V44_CONTROL_COUNT) ||
        !valid_buffer(input) || !valid_buffer(output) || (input == output))
    {
        return GX_V44_INVALID_ARGUMENT;
    }
    bytes = ((size_t)c->options.max_codewords - GX_V44_CONTROL_COUNT) * sizeof(gxV44Entry);
    if (overlaps(input->data, bb_getCapacity(input), output->data, bb_getCapacity(output)) ||
        overlaps(input->data, bb_getCapacity(input), (const unsigned char*)c->entries, bytes) ||
        overlaps(output->data, bb_getCapacity(output), (const unsigned char*)c->entries, bytes))
    {
        return GX_V44_INVALID_ARGUMENT;
    }
    gxv44_reset(c);
    return 0;
}

int gxv44_compress(gxV44* c, gxByteBuffer* input, gxByteBuffer* output)
{
    gxV44Bits b;
    size_t i, end, original, cursor, history, pos, source;
    unsigned int head, best, tail, length, ext, ch;
    GX_V44_TOKEN_TYPE previous;
    int ret;
    ret = prepare(c, input, output);
    if (ret != 0)
    {
        return ret;
    }
    memset(&b, 0, sizeof(b));
    b.buffer = output;
    original = output->size;
    i = input->position;
    end = i +
          (input->size - i < c->options.history_limit ? input->size - i : c->options.history_limit);
    previous = GX_V44_TOKEN_ORDINAL;
#ifdef COMPRESSION_TRACE
    b.compress = 1;
    b.uncompressed = input;
    b.input_position = i;
#endif
    TRACE_EVENT(c, &b, GX_V44_TRACE_START, 0);
    while ((i < input->size) && (b.error == 0))
    {
        if ((i == end) || (c->count + GX_V44_CONTROL_COUNT >= c->options.max_codewords))
        {
            write_control(c, &b, GX_V44_CONTROL_REINIT);
            gxv44_reset(c);
            end = i + (input->size - i < c->options.history_limit ? input->size - i
                                                                  : c->options.history_limit);
            previous = GX_V44_TOKEN_NONE;
            TRACE_EVENT(c, &b, GX_V44_TRACE_REINIT, 0);
        }
        ch = input->data[i];
        history = i + 1;
        head = c->roots[ch];
        best = 0;
        tail = 0;
        cursor = i + 1;
        /* A dictionary path represents the root byte plus complete matching
         * segments. Source bytes must already be in the current history. */
        while (head != 0)
        {
            gxV44Entry* n;
            n = &c->entries[head - GX_V44_CONTROL_COUNT];
            if ((1 + tail + n->length <= (unsigned int)c->options.maximum_string_length) &&
                (n->length <= end - cursor) && (n->position <= history) &&
                (n->length <= history - n->position) &&
                (memcmp(input->data + cursor, input->data + n->position, n->length) == 0))
            {
                best = head;
                tail += n->length;
                cursor += n->length;
                head = n->down;
            }
            else
            {
                head = n->side;
            }
        }
        if (best == 0)
        {
            ordinal(c, &b, ch, previous);
            add_encoder(c, &c->roots[ch], i + 1, 1);
            previous = GX_V44_TOKEN_ORDINAL;
            ++i;
#ifdef COMPRESSION_TRACE
            b.input_position = i;
#endif
            TRACE_EVENT(c, &b, GX_V44_TRACE_ORDINAL, ch);
            continue;
        }
        length = 1 + tail;
        history = i + length;
        pos = i + length;
        source = c->entries[best - GX_V44_CONTROL_COUNT].position +
                 c->entries[best - GX_V44_CONTROL_COUNT].length;
        ext = 0;
        while ((ext < (unsigned int)c->options.maximum_string_length - length) &&
               (ext < end - pos) && (source + ext < history) &&
               (input->data[pos + ext] == input->data[source + ext]))
        {
            ++ext;
            ++history;
        }
        string_code(c, &b, best, ext);
        if ((ext != 0) || (length < (unsigned int)c->options.maximum_string_length))
        {
            add_encoder(c, &c->entries[best - GX_V44_CONTROL_COUNT].down, pos, ext == 0 ? 1 : ext);
        }
        previous = ext != 0 ? GX_V44_TOKEN_STRING_EXTENSION : GX_V44_TOKEN_CODEWORD;
        i += length + ext;
#ifdef COMPRESSION_TRACE
        b.input_position = i;
#endif
        TRACE_EVENT(c, &b, GX_V44_TRACE_CODEWORD, best);
        TRACE_EVENT(c, &b, GX_V44_TRACE_PROCESSING, best);
#ifdef COMPRESSION_TRACE
        if (ext != 0)
        {
            TRACE_EVENT(c, &b, GX_V44_TRACE_STRING_EXTENSION, ext);
        }
#endif
    }
    write_control(c, &b, GX_V44_CONTROL_FLUSH);
    TRACE_EVENT(c, &b, GX_V44_TRACE_FLUSH, 0);
    TRACE_EVENT(c, &b, GX_V44_TRACE_END, 0);
    if (b.error != 0)
    {
        TRACE_EVENT(c, &b, GX_V44_TRACE_ERROR, 0);
        output->size = (gxV44BufferSize)original;
        gxv44_reset(c);
        return b.error;
    }
    input->position = input->size;
    return 0;
}

static void add_decoder(gxV44* c, size_t position, unsigned int length)
{
    gxV44Entry* n;
    if ((length > (unsigned int)c->options.maximum_string_length) ||
        (c->count + GX_V44_CONTROL_COUNT >= c->options.max_codewords))
    {
        return;
    }
    n = &c->entries[c->count];
    ++c->count;
    n->position = position;
    n->length = length;
    n->down = n->side = 0;
}

/** @brief Expand history one byte at a time, allowing source/output overlap.
 * The source must precede the next output byte, but may enter newly expanded
 * data. This implements repeated runs and the special next-codeword case.
 * Use offsets rather than pointers because append() can reallocate output.
 */
static int copy_history(gxV44* c, gxByteBuffer* out, size_t start, size_t source,
                        unsigned int length)
{
    int ret;
    unsigned int j;
    if ((out->size - start > c->options.history_limit) ||
        (length > c->options.history_limit - (out->size - start)))
    {
        return GX_V44_INVALID_DATA;
    }
    for (j = 0; j < length; ++j)
    {
        if ((source < start) || (source >= out->size))
        {
            return GX_V44_INVALID_DATA;
        }
        ret = append(out, out->data[source]);
        ++source;
        if (ret != 0)
        {
            return ret;
        }
    }
    return 0;
}

int gxv44_decompress(gxV44* c, gxByteBuffer* input, gxByteBuffer* output)
{
    gxV44Bits b;
    size_t original, start, output_start, source;
    unsigned int prefix, second, code, previous_code, previous_length, length, ext, ch, max_bits;
    GX_V44_TOKEN_TYPE previous;
    int after_codeword, ended, ret;
    ret = prepare(c, input, output);
    if (ret != 0)
    {
        return ret;
    }
    memset(&b, 0, sizeof(b));
    b.buffer = input;
    b.index = input->position;
    original = start = output->size;
    previous = GX_V44_TOKEN_NONE;
    after_codeword = ended = 0;
    previous_code = previous_length = ch = 0;
#ifdef COMPRESSION_TRACE
    b.uncompressed = output;
#endif
    TRACE_EVENT(c, &b, GX_V44_TRACE_START, 0);
    while ((b.index < input->size) && (b.error == 0))
    {
        ended = 0;
        prefix = get(&b, 1);
        if (after_codeword && (prefix == 0))
        {
            second = get(&b, 1);
            if (second == 1)
            {
                ext = get_extension(&b, c->options.maximum_string_length);
                if (b.error != 0)
                {
                    break;
                }
                if (ext > (unsigned int)c->options.maximum_string_length - previous_length)
                {
                    b.error = GX_V44_INVALID_DATA;
                    break;
                }
                source = c->entries[previous_code - GX_V44_CONTROL_COUNT].position + 1;
                b.error = copy_history(c, output, start, source, ext);
                if (b.error != 0)
                {
                    break;
                }
                add_decoder(c, output->size - 1, previous_length + ext);
                previous = GX_V44_TOKEN_STRING_EXTENSION;
                after_codeword = 0;
                TRACE_EVENT(c, &b, GX_V44_TRACE_STRING_EXTENSION, ext);
                continue;
            }
        }
        after_codeword = 0;
        if (prefix == 0)
        {
            ch = get(&b, c->ordinal_size);
            if (b.error != 0)
            {
                break;
            }
            if (output->size - start >= c->options.history_limit)
            {
                b.error = GX_V44_INVALID_DATA;
                break;
            }
            b.error = append(output, (unsigned char)ch);
            if (b.error != 0)
            {
                break;
            }
            if (previous == GX_V44_TOKEN_ORDINAL)
            {
                add_decoder(c, output->size - 1, 2);
            }
            else if (previous == GX_V44_TOKEN_CODEWORD)
            {
                add_decoder(c, output->size - 1, previous_length + 1);
            }
            previous = GX_V44_TOKEN_ORDINAL;
            TRACE_EVENT(c, &b, GX_V44_TRACE_ORDINAL, ch);
            continue;
        }
        code = get(&b, c->codeword_size);
        if (b.error != 0)
        {
            break;
        }
        if (code == GX_V44_CONTROL_TRANSPARENT)
        {
            b.error = GX_V44_INVALID_DATA;
        }
        else if (code == GX_V44_CONTROL_FLUSH)
        {
            if ((b.bit != 0) && (get(&b, 8 - b.bit) != 0))
            {
                b.error = GX_V44_INVALID_DATA;
            }
            ended = 1;
            TRACE_EVENT(c, &b, GX_V44_TRACE_FLUSH, 0);
        }
        else if (code == GX_V44_CONTROL_STEPUP)
        {
            if (b.index >= input->size)
            {
                b.error = GX_V44_INVALID_DATA;
                break;
            }
            prefix = (input->data[b.index] >> b.bit) & 1U;
            if (prefix == 0)
            {
                if (c->ordinal_size == GX_V44_ORDINAL_SIZE_8)
                {
                    b.error = GX_V44_INVALID_DATA;
                }
                else
                {
                    ++c->ordinal_size;
                    TRACE_EVENT(c, &b, GX_V44_TRACE_STEPUP_ORDINAL, c->ordinal_size);
                }
            }
            else
            {
                max_bits = 0;
                code = c->options.max_codewords - 1;
                while (code != 0)
                {
                    ++max_bits;
                    code >>= 1;
                }
                if (max_bits < (unsigned int)c->options.default_codeword_size)
                {
                    max_bits = c->options.default_codeword_size;
                }
                if ((unsigned int)c->codeword_size >= max_bits)
                {
                    b.error = GX_V44_INVALID_DATA;
                }
                else
                {
                    ++c->codeword_size;
                    TRACE_EVENT(c, &b, GX_V44_TRACE_STEPUP_CODEWORD, c->codeword_size);
                }
            }
        }
        else if (code == GX_V44_CONTROL_REINIT)
        {
            gxv44_reset(c);
            start = output->size;
            previous = GX_V44_TOKEN_NONE;
            previous_code = previous_length = 0;
            TRACE_EVENT(c, &b, GX_V44_TRACE_REINIT, 0);
        }
        else
        {
            if ((code >= c->options.max_codewords) || (code > c->count + GX_V44_CONTROL_COUNT) ||
                ((code == c->count + GX_V44_CONTROL_COUNT) &&
                 ((previous != GX_V44_TOKEN_ORDINAL) && (previous != GX_V44_TOKEN_CODEWORD))))
            {
                b.error = GX_V44_INVALID_DATA;
                break;
            }
            output_start = output->size;
            if (code == c->count + GX_V44_CONTROL_COUNT)
            {
                length = previous == GX_V44_TOKEN_ORDINAL ? 2 : previous_length + 1;
                if (length > (unsigned int)c->options.maximum_string_length)
                {
                    b.error = GX_V44_INVALID_DATA;
                    break;
                }
                source = previous == GX_V44_TOKEN_ORDINAL
                             ? (size_t)output->size - 1
                             : c->entries[previous_code - GX_V44_CONTROL_COUNT].position -
                                   previous_length + 1;
            }
            else
            {
                length = c->entries[code - GX_V44_CONTROL_COUNT].length;
                source = c->entries[code - GX_V44_CONTROL_COUNT].position + 1 - length;
            }
            b.error = copy_history(c, output, start, source, length);
            if (b.error != 0)
            {
                break;
            }
            if (previous == GX_V44_TOKEN_ORDINAL)
            {
                add_decoder(c, output_start, 2);
            }
            else if (previous == GX_V44_TOKEN_CODEWORD)
            {
                add_decoder(c, output_start, previous_length + 1);
            }
            previous = GX_V44_TOKEN_CODEWORD;
            previous_code = code;
            previous_length = length;
            after_codeword = 1;
            TRACE_EVENT(c, &b, GX_V44_TRACE_CODEWORD, code);
            TRACE_EVENT(c, &b, GX_V44_TRACE_PROCESSING, code);
        }
    }
    if ((b.error == 0) && !ended)
    {
        b.error = GX_V44_INVALID_DATA;
    }
    TRACE_EVENT(c, &b, GX_V44_TRACE_END, 0);
    if (b.error != 0)
    {
        TRACE_EVENT(c, &b, GX_V44_TRACE_ERROR, 0);
        output->size = (gxV44BufferSize)original;
        gxv44_reset(c);
        return b.error;
    }
    input->position = input->size;
    return 0;
}
