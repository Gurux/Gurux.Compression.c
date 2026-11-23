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
#ifndef GX_V44_MEMORY_H
#define GX_V44_MEMORY_H

/* The public wrapper loads the optional application configuration first. */
#include "gxbytebuffer.h"

#ifndef DLMS_IGNORE_MALLOC
#if defined(GX_COMPRESSION_MALLOC) != defined(GX_COMPRESSION_FREE)
#error Define both GX_COMPRESSION_MALLOC and GX_COMPRESSION_FREE.
#endif

#ifdef GX_COMPRESSION_EXTERNAL_BUFFER_HEADER
#include "gxmem.h"
#ifndef GX_COMPRESSION_MALLOC
#define GX_COMPRESSION_MALLOC gxmalloc
#endif
#ifndef GX_COMPRESSION_FREE
#define GX_COMPRESSION_FREE gxfree
#endif
#endif

#ifndef GX_COMPRESSION_MALLOC
#include <stdlib.h>
#define GX_COMPRESSION_MALLOC malloc
#define GX_COMPRESSION_FREE free
#endif
#endif
#endif
