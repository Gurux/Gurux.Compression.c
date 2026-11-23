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
#ifndef GX_V44_BUFFER_LIMITS_H
#define GX_V44_BUFFER_LIMITS_H

#include "gxbytebuffer.h"

/* Restrict byte offsets to both the selected buffer layout and size_t.
 * Some small targets explicitly select 32-bit buffer fields with 16-bit size_t.
 */
static size_t gxv44_buffer_limit(void)
{
    gxV44BufferSize maximum;
    size_t limit;
    maximum = (gxV44BufferSize)(((gxV44BufferSize)-1) >> 1);
    limit = (size_t)maximum;
    return (gxV44BufferSize)limit == maximum ? limit : (size_t)-1;
}

/* Preconditions: capacity <= limit and required <= limit. Test subtraction
 * before adding the growth increment, including when limit is SIZE_MAX.
 */
static size_t gxv44_buffer_growth(size_t capacity, size_t required, size_t limit)
{
    size_t next;
    if (capacity < 32)
    {
        next = limit < 32 ? limit : 32;
    }
    else
    {
        next = capacity / 2;
        next = next > limit - capacity ? limit : capacity + next;
    }
    return next < required ? required : next;
}

#endif
