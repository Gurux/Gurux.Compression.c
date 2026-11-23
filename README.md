# Gurux.V44.c

ANSI C V.44 compression and decompression library for ANSI C.

## Build

Requires CMake 3.16+ and a C90 compiler. No external library is required:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build --config Debug
```

GNU Make can build directly with GCC/Clang, `ar` and a POSIX shell (Linux, WSL
or a suitable MinGW/MSYS environment). CMake is not required for this route:

```sh
make -j2                         # Two static libraries under .artifacts/make-heap32
make report DLMS_IGNORE_MALLOC=1 GX_V44_BUFFER_16=1
make arm-check                   # Optional Cortex-M0 compile, requires GNU Arm tools
make clean                       # Remove this configuration's enumerated artifacts
```

Override `CC`, `AR`, `CFLAGS`, `LDFLAGS` or `LDLIBS` as needed.
Memory/layout configurations have separate build directories.
Clean that configuration before changing compiler
or custom flags, switching configuration/external headers, or changing the
external library source. Alternatively select a distinct `BUILD_DIR`.
`make arm-check` always checks the no-malloc, 16-bit buffer
layout; override `ARM_CC`, `ARM_SIZE` and `ARM_CFLAGS` for another Arm target.
Its result is a cross-compiled codec object, not a linked or tested firmware.

The codec and included buffer compile as strict C89/C90 without `stdint.h`.
The included buffer provides initialization, attachment, capacity, byte append,
size/cursor queries and cleanup; numeric/protocol conversion APIs are outside
this subset. `gxbytebuffer.h` is the buffer entry point.

## External GuruxDLMS buffer

To reuse an application's existing GuruxDLMS buffer, select its actual header
and supply its library target or archive:

```sh
cmake -S . -B build-external -DBUILD_TESTING=OFF \
  -DGX_COMPRESSION_EXTERNAL_BUFFER_HEADER=/path/to/GuruxDLMS.c/development/include/bytebuffer.h \
  -DGX_COMPRESSION_BUFFER_LIBRARIES=/path/to/libgurux_dlms.a
cmake --build build-external --config Release
```

CMake propagates the external header and buffer configuration to consumers.
The local buffer implementation is excluded, avoiding duplicate `bb_*` symbols.
An existing CMake target also supplies its transitive dependencies. With raw
archives, list their dependencies explicitly in `GX_COMPRESSION_BUFFER_LIBRARIES`;
for example `"/path/to/libgurux_dlms.a;m"` if the Unix build needs libm.
Alternatively pass `-DGX_V44_BUILD_BUFFER=OFF` and link the application's buffer
implementation yourself. Supplying `GURUX_DLMS_ROOT` explicitly selects its
external header and, unless disabled, builds the upstream buffer support.
Upstream headers and sources retain their C99 requirements; they are treated
as external code. Reconfigure older build directories with
`-DGURUX_DLMS_ROOT=` to select standalone mode.

GNU Make accepts the same external-header variable and
`GX_COMPRESSION_BUFFER_LIBRARIES` as linker inputs. Set `GX_V44_BUILD_BUFFER=0`
when supplying the implementation yourself. External builds use an additional
`-external` directory suffix. Use an absolute external-header path.

For manual builds compile `src/gxv44.c` and `src/gxbytebuffer.c` with `-Iinclude`.
In external mode define `GX_COMPRESSION_EXTERNAL_BUFFER_HEADER` as a quoted
header path for every translation unit, include the Gurux header directory,
exclude `src/gxbytebuffer.c` and link the external buffer implementation.
Do not include both independent buffer definitions in one application.

## Memory configuration

Standalone allocation defaults to `malloc`/`free`. Set
`GX_COMPRESSION_CONFIG_HEADER` to a shared configuration header to override
both `GX_COMPRESSION_MALLOC(size)` and `GX_COMPRESSION_FREE(pointer)` together.
They must follow malloc/free semantics, including accepting NULL on free;
allocator declarations belong in the configuration header. These hooks apply
to both dictionary and local buffer storage. No realloc hook is required.
External mode defaults to Gurux's `gxmalloc`/`gxfree` through `gxmem.h`;
`DLMS_USE_CUSTOM_MALLOC` configurations must supply the Gurux allocator hooks.
`DLMS_IGNORE_MALLOC` removes allocation calls in either mode.

## API and ownership

Include `gxv44.h`; link the CMake target `gurux_v44`. It brings in buffer support.
All translation units must agree on the selected buffer/configuration headers,
`DLMS_IGNORE_MALLOC`, `GX_DLMS_MICROCONTROLLER`, `GX_DLMS_BYTE_BUFFER_SIZE_32`
and `COMPRESSION_TRACE`. External mode uses the actual Gurux type, without
redeclaring or casting it; matching buffer layout settings are required.

Initialize buffers with `BYTE_BUFFER_INIT`. `bb_attach` borrows caller-owned
storage and never frees it; clear any owned allocation before attaching.
`bb_clear` frees owned storage, or resets the size/cursor of attached storage
while retaining the attachment. Attached capacity is fixed. Allocation and
capacity failures preserve local buffer contents and state. Local buffer
functions return zero on success, -1 for invalid arguments or -3 when capacity
is unavailable; external functions retain their upstream error codes.

```c
#include "gxv44.h"

int example(void)
{
    gxV44 codec;
    gxByteBuffer input, compressed, restored;
    unsigned char text[] = "Hello, hello, hello!";
    int ret;
    BYTE_BUFFER_INIT(&input);
    BYTE_BUFFER_INIT(&compressed);
    BYTE_BUFFER_INIT(&restored);
    bb_attach(&input, text, sizeof(text) - 1, sizeof(text));
    ret = gxv44_init(&codec, NULL);
    if (ret == 0)
    {
        ret = gxv44_compress(&codec, &input, &compressed);
    }
    if (ret == 0)
    {
        ret = gxv44_decompress(&codec, &compressed, &restored);
    }
    /* restored.data[0:restored.size] contains the original binary bytes. */
    gxv44_clear(&codec);
    bb_clear(&compressed);
    bb_clear(&restored);
    return ret;
}
```

Each call starts an independent codec operation, consumes bytes from
`input.position` through `input.size`, and appends to `output.size`.
The output position stays unchanged. On success the input position advances
to its size. On failure input position and output size are preserved;
capacity and bytes beyond the old output size may have changed. The same
context can be reused after a failure. Input, output and dictionary storage
must be separate. A context must be cleared before reinitializing it, must
not be copied while initialized, and supports one operation at a time.

Options are copied on initialization. Defaults match .NET: codeword width 6,
ordinal width 7, 1024 codewords, maximum string length 255, history limit 3072.
Supported string limits are 46/78/142/255; codeword limits 256..65535;
history limits >=512; initial widths 6..16 and 7..8 respectively.
Both peers must use matching options. Context fields are implementation state;
do not mutate them after initialization.

Finite option choices use actual enum types and named values:

```c
gxV44Options options;
gxv44_options_init(&options);
options.default_codeword_size = GX_V44_CODEWORD_SIZE_6;
options.default_ordinal_size = GX_V44_ORDINAL_SIZE_7;
options.maximum_string_length = GX_V44_MAXIMUM_STRING_LENGTH_142;
```

The types are `GX_V44_CODEWORD_SIZE` (6..16), `GX_V44_ORDINAL_SIZE` (7/8) and
`GX_V44_MAXIMUM_STRING_LENGTH` (46/78/142/255). The runtime width fields use the
corresponding types too. Counts such as `max_codewords`, dictionary indices
and `history_limit` remain integer quantities. Internally, token kinds and
reserved wire controls also use named enums. Validation remains necessary:
C enum variables can contain unnamed integer values.

Enum type names use uppercase identifiers: `GX_V44_ERROR`,
`GX_V44_CODEWORD_SIZE`, `GX_V44_ORDINAL_SIZE`,
`GX_V44_MAXIMUM_STRING_LENGTH` and, in trace builds,
`GX_V44_TRACE_OPERATION`. Only these type names are supported; compatibility
aliases are not provided. Private token/control enum types follow the same
uppercase convention. Struct and callback typedef names are unchanged.

The width/string fields were previously `unsigned int`. Rebuild the library
and every consumer together after this change; enum sizes are implementation
defined and compiler/enum-size/packing options must match. Struct sizes stay
unchanged in the tested MSVC and native x86-64 GCC default builds, while this
Cortex-M0 compiler's compact enums reduce `sizeof(gxV44)` from 1068 to 1056
bytes. The C++ interface remains usable with named enum values; numeric input
must be validated and explicitly cast to its enum type. Function names, error
values and V.44 wire values are retained. Do not serialize the C structures.

The API processes complete binary streams in buffers. Text encoding, file I/O
and .NET diagnostic objects remain application concerns. No global mutable
codec state, recursion, or formatted diagnostic allocations are used.

Operations are synchronous and do not perform transport I/O, wait for events
or time out. Optional trace callbacks can cancel processing. Distinct contexts with disjoint storage are
reentrant. The caller must synchronize shared contexts/buffers across threads
or interrupts; allocation-enabled calls are unsuitable for interrupt handlers.

## Optional COMPRESSION_TRACE

Trace is excluded by default: `COMPRESSION_TRACE` is not defined, and
`CGXV44TraceArgs`, trace enums/callbacks, registration, event generation and
callback context fields are absent from the build. Enable it globally for
the library and all callers:

```sh
make COMPRESSION_TRACE=1
cmake -S . -B build-trace -DBUILD_TESTING=OFF -DCOMPRESSION_TRACE=ON
cmake --build build-trace --config Debug
```

For manual builds pass `-DCOMPRESSION_TRACE` to every relevant compilation, or
put `#define COMPRESSION_TRACE` in a shared build configuration included before
`gxv44.h` and the codec implementation. The test is `#ifdef`: even defining the
macro as zero enables trace. CMake/Make disabled options leave it undefined.
Make places enabled builds in separate `-trace` directories.

```c
#ifdef COMPRESSION_TRACE
static void on_trace(CGXV44TraceArgs* args, void* user_data)
{
    unsigned int* events;
    events = (unsigned int*)user_data;
    ++*events;
    /* Inspect args->operation, value, error, buffers and dictionary here.
     * Set args->stop = 1 to cancel this operation. */
}

/* After successfully initializing codec, register caller-owned state: */
/* gxv44_set_trace(&codec, on_trace, &event_count); */
#endif
```

`gxv44_set_trace(codec, callback, user_data)` registers a synchronous callback;
passing NULL disables it. Initialization/clear removes registration; reset
preserves it. Events cover start/end, ordinals, codewords, extensions, width
increases, REINIT, FLUSH and errors, using named `GX_V44_TRACE_OPERATION` values
matching .NET's operation numbers. Invalid arguments rejected before operation
preparation generate no events. A NULL callback skips constructing arguments.

`CGXV44TraceArgs` is a temporary, allocation-free C view rather than a managed
snapshot. Its compressed/uncompressed buffers and dictionary are borrowed and
read-only, including their data; they are valid only during that callback.
Copy desired diagnostics into caller-managed storage. No binary strings or
dictionary snapshots are allocated. Input/output coordinates are absolute byte
offsets; `compressed_position` plus `compressed_bit` locates the next compressed
bit without overflowing a single bit-count integer on a large buffer. Encoder
FLUSH/END can point into its final byte before implicit zero padding; decoder
FLUSH has validated/consumed padding and points to the next byte boundary.

The callback may modify only `stop`, must not reenter/mutate the active codec,
and must keep its user data valid. A stop request, including at END, returns
`GX_V44_CANCELLED` and restores logical input position/output size. ERROR is
reported before rollback; setting stop during ERROR does not replace the
original error. No cancellation/error events allocate memory, so tracing also
works with `DLMS_IGNORE_MALLOC` and attached buffers. Callbacks themselves must
respect the application's memory, interrupt and synchronization constraints.

Rebuild all consumers with the same trace setting because it changes the
context layout. On tested x86-64 the context grows from 1080 to 1096 bytes.
On Cortex-M0 it grows from 1056 to 1064 bytes; trace arguments are 56 bytes on
the stack. With `-Os`, the cross-compiled codec text grows from 2688 to 3172
bytes, while own static data/BSS and heap use remain zero. Compiler frames are
112 bytes for compression, 104 for decompression and 88 for event dispatch;
the estimated deepest path including the local buffer is 208 bytes before callback/libc/interrupt
stack. These are compiler measurements/estimates, not hardware execution or
whole-firmware budgets. Buffer storage requirements do not change with tracing.

## DLMS_IGNORE_MALLOC

```sh
cmake -S . -B build-static -DBUILD_TESTING=OFF -DDLMS_IGNORE_MALLOC=ON
cmake --build build-static --config Debug
```

Use `gxv44_attach` instead of `gxv44_init`, and attach output buffers too:

```c
static gxV44Entry dictionary[1020]; /* max_codewords - 4 */
static unsigned char encoded[256];
static unsigned char decoded[256];
gxV44 codec;
gxByteBuffer output, restored;
int ret;
ret = gxv44_attach(&codec, NULL, dictionary, 1020);
bb_attach(&output, encoded, 0, sizeof(encoded));
bb_attach(&restored, decoded, 0, sizeof(decoded));
/* Use gxv44_compress / gxv44_decompress as above, checking each result. */
```

Dictionary storage requires `(max_codewords - 4) * sizeof(gxV44Entry)` bytes;
the context also contains 256 root indices. History refers to input or output
coordinates, so no separate history allocation is needed. Static storage avoids
placing large dictionaries on a small stack. Heap-enabled builds also support
this entirely static usage. In a no-malloc build `gxv44_init` returns
`GX_V44_OUT_OF_MEMORY`; no allocation functions are referenced by the codec.

`GX_V44_BUFFER_16=ON` selects the upstream microcontroller layout. Its attached
buffer capacity is limited to 32767 bytes because the high bit is the attachment
flag; the 32-bit layout has the corresponding 2147483647-byte limit. Operations
return `GX_V44_OUT_OF_MEMORY` when output space runs out. They never grow an
attached buffer. Caller-provided workspaces must have at least the required
entry count, otherwise initialization returns `GX_V44_INVALID_ARGUMENT`.

## MCU memory and packet measurements

For `n` input bytes a conservative encoder output capacity is `2*n + 32` bytes.
Before computing it, check `n <= (((size_t)-1 - 32) / 2)` and the buffer layout's
capacity limit. Data tokens use at most ten bits per consumed byte; width/reset
controls are bounded because a history/dictionary window consumes at least 252
bytes before a reset. The bound also covers final FLUSH and byte padding, and
is deliberately larger than the typical output. Decompression instead needs
the known maximum uncompressed length. No implicit NUL byte is included.

`make report DLMS_IGNORE_MALLOC=1 GX_V44_BUFFER_16=1` runs two deterministic
2000-byte packets with 256- and 1024-codeword dictionaries, measures desktop CPU
ticks and reports storage sizes, object size and per-function compiler stack
frames. Timings are outside sample generation/validation. The benchmark reuses
the original buffer as the decoded destination. Its statically allocated
dictionary always has 1020 entries; `required_storage_bytes` is a calculated
minimum for the configured entry count, not the program's actual peak RAM.

Measured on GCC 12.2, x86-64 WSL, `-Os`, no malloc, 16-bit buffer layout:

| Dictionary | Packet | Compressed bytes | Encode / decode per packet |
| --- | --- | ---: | ---: |
| 256 codewords | Repeated text | 149 | about 13 / 19 microseconds |
| 256 codewords | Specified 32-bit LCG | 2261 | about 47 / 31 microseconds |
| 1024 codewords | Repeated text | 149 | about 17 / 21 microseconds |
| 1024 codewords | Specified 32-bit LCG | 2256 | about 55 / 38 microseconds |

These are representative desktop CPU timings, not MCU cycles or latency
guarantees. Here `sizeof(gxV44)=1080`, `sizeof(gxV44Entry)=24` and
`sizeof(gxByteBuffer)=16`. The benchmark reserves 31592 static bytes plus stack;
the codec object has no own `.data` or `.bss`. The program is a desktop test,
not an 8 KiB firmware example.

GNU Arm Embedded GCC 12.2.1 also compiled the codec for Cortex-M0/Thumb with
strict C90, `-Os`, `DLMS_IGNORE_MALLOC` and `GX_DLMS_MICROCONTROLLER`.
Compiled type sizes are 1056 bytes per context, 16 per entry and 12 per buffer
descriptor. The codec object's text is about 2.6 KiB before linking the Gurux
buffer support, libc and compiler helpers; this is not total firmware flash.
Compiler-reported individual frames are 96 bytes for compression and 88 for
decompression. Summing the deepest codec call path gives an estimated 160 bytes
of codec stack, excluding the caller, upstream/library frames and interrupts.
The included no-malloc buffer adds 400 bytes of object text and no static
data/BSS on this compiler. Its capacity-call path adds an estimated 32 bytes
of stack, giving approximately 192 bytes for the combined deepest path.

An estimated Cortex-M0 static budget with 256 codewords is:

| Component | 512-byte packet | 2000-byte packet |
| --- | ---: | ---: |
| Context and 252 dictionary entries | 5088 | 5088 |
| Original and worst-case compressed buffers | 1568 | 6032 |
| Three buffer descriptors | 36 | 36 |
| Subtotal before stack/other application state | 6692 | 11156 |

The 512-byte profile leaves 1500 bytes of an 8 KiB RAM budget for all stack,
interrupts and other state. The 2000-byte profile exceeds that budget; the
default 1024-codeword dictionary alone needs 16320 bytes on this target. Choose
packet limits and negotiated options for the actual board. There is no
automatic fragmentation or transport layer in this buffer API: independently
compressed smaller messages require appropriate application framing. No board
execution, MCU cycle measurement or full firmware stack/flash measurement has
been performed. Heap-enabled output growth can temporarily require allocator
overhead and both old/new allocations; use attached buffers for a fixed budget.

## License

GPL-2.0-only OR a separate Gurux commercial license, matching the reference
project. See `LICENSE` and `LICENSES/LicenseRef-Gurux-Commercial`.
The existing `.txt` notice is retained with identical authoritative content.
The external GuruxDLMS sources retain their own license notices.
