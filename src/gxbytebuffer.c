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
#include "gxbytebuffer.h"

/* External mode deliberately exports none of the local bb_* symbols. */
#ifndef GX_COMPRESSION_EXTERNAL_BUFFER_HEADER
#include "gxv44memory.h"
#ifndef DLMS_IGNORE_MALLOC
#include "gxv44bufferlimits.h"
#endif
#include <string.h>

/* The high bit is reserved for the Gurux attachment flag. */
#define BUFFER_LIMIT ((gxV44BufferSize)(((gxV44BufferSize)-1) >> 1))
#define ATTACHED_FLAG ((gxV44BufferSize)(BUFFER_LIMIT + 1UL))

static int valid_buffer(gxByteBuffer* bb)
{
    return (bb != NULL) && (bb->position <= bb->size) &&
        (bb->size <= bb_getCapacity(bb)) &&
        ((bb->data != NULL) || (bb_getCapacity(bb) == 0));
}

int BYTE_BUFFER_INIT(gxByteBuffer* bb)
{
    if (bb == NULL)
    {
        return GX_BYTE_BUFFER_INVALID_ARGUMENT;
    }
    bb->data = NULL;
    bb->capacity = bb->size = bb->position = 0;
    return 0;
}

char bb_isAttached(gxByteBuffer* bb)
{
    return (char)((bb != NULL) && ((bb->capacity & ATTACHED_FLAG) != 0));
}

gxV44BufferSize bb_getCapacity(gxByteBuffer* bb)
{
    return bb == NULL ? 0 : (gxV44BufferSize)(bb->capacity & BUFFER_LIMIT);
}

gxV44BufferSize bb_size(gxByteBuffer* bb)
{
    return bb == NULL ? 0 : bb->size;
}

gxV44BufferSize bb_available(gxByteBuffer* bb)
{
    return valid_buffer(bb) ? (gxV44BufferSize)(bb->size - bb->position) : 0;
}

int bb_capacity(gxByteBuffer* bb, gxV44BufferSize capacity)
{
#ifndef DLMS_IGNORE_MALLOC
    unsigned char* data;
    gxV44BufferSize count;
#endif
    if (!valid_buffer(bb))
    {
        return GX_BYTE_BUFFER_INVALID_ARGUMENT;
    }
    if ((capacity > BUFFER_LIMIT) || ((size_t)capacity != capacity))
    {
        return GX_BYTE_BUFFER_OUT_OF_MEMORY;
    }
    if (bb_isAttached(bb))
    {
        return capacity <= bb_getCapacity(bb) ? 0 : GX_BYTE_BUFFER_OUT_OF_MEMORY;
    }
    if (capacity == bb->capacity)
    {
        return 0;
    }
#ifdef DLMS_IGNORE_MALLOC
    return GX_BYTE_BUFFER_OUT_OF_MEMORY;
#else
    data = NULL;
    count = bb->size < capacity ? bb->size : capacity;
    if (capacity != 0)
    {
        data = (unsigned char*)GX_COMPRESSION_MALLOC((size_t)capacity);
        if (data == NULL)
        {
            return GX_BYTE_BUFFER_OUT_OF_MEMORY;
        }
        if (count != 0)
        {
            memcpy(data, bb->data, (size_t)count);
        }
    }
    GX_COMPRESSION_FREE(bb->data);
    bb->data = data;
    bb->capacity = capacity;
    bb->size = count;
    if (bb->position > count)
    {
        bb->position = count;
    }
    return 0;
#endif
}

int bb_attach(gxByteBuffer* bb, unsigned char* value,
              gxV44BufferSize count, gxV44BufferSize capacity)
{
    if ((bb == NULL) || (count > capacity) || ((value == NULL) && (capacity != 0)))
    {
        return GX_BYTE_BUFFER_INVALID_ARGUMENT;
    }
    if ((capacity > BUFFER_LIMIT) || ((size_t)capacity != capacity))
    {
        return GX_BYTE_BUFFER_OUT_OF_MEMORY;
    }
    bb->data = value;
    bb->capacity = (gxV44BufferSize)(capacity | ATTACHED_FLAG);
    bb->size = count;
    bb->position = 0;
    return 0;
}

int bb_set(gxByteBuffer* bb, const unsigned char* value, gxV44BufferSize count)
{
    gxV44BufferSize required;
#ifndef DLMS_IGNORE_MALLOC
    size_t next;
#endif
    if (!valid_buffer(bb) || ((value == NULL) && (count != 0)))
    {
        return GX_BYTE_BUFFER_INVALID_ARGUMENT;
    }
    if (count > BUFFER_LIMIT - bb->size)
    {
        return GX_BYTE_BUFFER_OUT_OF_MEMORY;
    }
    required = (gxV44BufferSize)(bb->size + count);
    if ((size_t)required != required)
    {
        return GX_BYTE_BUFFER_OUT_OF_MEMORY;
    }
    if (required > bb_getCapacity(bb))
    {
        /* Allocate before releasing old storage: a source inside the buffer
         * remains valid until copying has completed, without pointer ordering.
         */
#ifndef DLMS_IGNORE_MALLOC
        unsigned char* data;
        if (bb_isAttached(bb))
        {
            return GX_BYTE_BUFFER_OUT_OF_MEMORY;
        }
        next = gxv44_buffer_growth((size_t)bb_getCapacity(bb),
                                   (size_t)required, gxv44_buffer_limit());
        if ((gxV44BufferSize)next != next)
        {
            return GX_BYTE_BUFFER_OUT_OF_MEMORY;
        }
        data = (unsigned char*)GX_COMPRESSION_MALLOC(next);
        if (data == NULL)
        {
            return GX_BYTE_BUFFER_OUT_OF_MEMORY;
        }
        if (bb->size != 0)
        {
            memcpy(data, bb->data, (size_t)bb->size);
        }
        memcpy(data + bb->size, value, (size_t)count);
        GX_COMPRESSION_FREE(bb->data);
        bb->data = data;
        bb->capacity = (gxV44BufferSize)next;
#else
        return GX_BYTE_BUFFER_OUT_OF_MEMORY;
#endif
    }
    else if (count != 0)
    {
        memmove(bb->data + bb->size, value, (size_t)count);
    }
    bb->size = required;
    return 0;
}

int bb_setUInt8(gxByteBuffer* bb, unsigned char value)
{
    return bb_set(bb, &value, 1);
}

int bb_empty(gxByteBuffer* bb)
{
    if (bb == NULL)
    {
        return GX_BYTE_BUFFER_INVALID_ARGUMENT;
    }
    bb->size = bb->position = 0;
    return 0;
}

int bb_clear(gxByteBuffer* bb)
{
    if (bb == NULL)
    {
        return GX_BYTE_BUFFER_INVALID_ARGUMENT;
    }
    if (!bb_isAttached(bb))
    {
#ifndef DLMS_IGNORE_MALLOC
        GX_COMPRESSION_FREE(bb->data);
#endif
        bb->data = NULL;
        bb->capacity = 0;
    }
    return bb_empty(bb);
}
#endif
