# Gurux.V44.c -- GNU Make with GCC/Clang-compatible tools and a POSIX shell.
# Keep configuration outputs separate so changing malloc/buffer modes is safe.
.DEFAULT_GOAL := all

GURUX_DLMS_ROOT ?=
GX_COMPRESSION_EXTERNAL_BUFFER_HEADER ?=
GX_COMPRESSION_BUFFER_LIBRARIES ?=
GX_COMPRESSION_CONFIG_HEADER ?=
GX_V44_BUILD_BUFFER ?= 1
DLMS_IGNORE_MALLOC ?= 0
COMPRESSION_TRACE ?= 0
GX_V44_BUFFER_16 ?= 0
SIZE ?= size
ARM_CC ?= arm-none-eabi-gcc
ARM_SIZE ?= arm-none-eabi-size
ARM_CFLAGS ?= -mcpu=cortex-m0 -mthumb
CFLAGS ?= -Os
UPSTREAM_CFLAGS ?= -std=c99
UPSTREAM_CONFIG :=
LDLIBS ?= -lm

CONFIG_DEFINES :=
MEMORY_MODE := heap
BUFFER_MODE := 32
TRACE_MODE :=
ifneq ($(filter 1 ON on TRUE true,$(COMPRESSION_TRACE)),)
CONFIG_DEFINES += -DCOMPRESSION_TRACE
TRACE_MODE := -trace
endif
ifneq ($(filter 1 ON on TRUE true,$(DLMS_IGNORE_MALLOC)),)
CONFIG_DEFINES += -DDLMS_IGNORE_MALLOC
MEMORY_MODE := static
endif
ifneq ($(filter 1 ON on TRUE true,$(GX_V44_BUFFER_16)),)
CONFIG_DEFINES += -DGX_DLMS_MICROCONTROLLER
BUFFER_MODE := 16
endif
EXTERNAL_MODE :=
ifneq ($(strip $(GURUX_DLMS_ROOT)),)
ifeq ($(strip $(GX_COMPRESSION_EXTERNAL_BUFFER_HEADER)),)
GX_COMPRESSION_EXTERNAL_BUFFER_HEADER := $(abspath $(GURUX_DLMS_ROOT)/include/bytebuffer.h)
endif
endif
ifneq ($(strip $(GX_COMPRESSION_CONFIG_HEADER)),)
CONFIG_DEFINES += -DGX_COMPRESSION_CONFIG_HEADER='"$(GX_COMPRESSION_CONFIG_HEADER)"'
UPSTREAM_CONFIG := -include "$(GX_COMPRESSION_CONFIG_HEADER)"
endif
ifneq ($(strip $(GX_COMPRESSION_EXTERNAL_BUFFER_HEADER)),)
CONFIG_DEFINES += -DGX_COMPRESSION_EXTERNAL_BUFFER_HEADER='"$(GX_COMPRESSION_EXTERNAL_BUFFER_HEADER)"'
EXTERNAL_MODE := -external
endif
BUILD_DIR ?= .artifacts/make-$(MEMORY_MODE)$(BUFFER_MODE)$(TRACE_MODE)$(EXTERNAL_MODE)
INCLUDES := -Iinclude
ifneq ($(strip $(GX_COMPRESSION_EXTERNAL_BUFFER_HEADER)),)
INCLUDES += -isystem "$(dir $(GX_COMPRESSION_EXTERNAL_BUFFER_HEADER))"
endif
# -MD also tracks system-marked Gurux headers: their macros define the buffer ABI.
STRICT_CFLAGS := -std=c90 -pedantic-errors -Wall -Wextra -Werror -Wdeclaration-after-statement
CODEC := $(BUILD_DIR)/libgurux_v44.a
BUFFER := $(BUILD_DIR)/libgurux_v44_buffer.a
BUFFER_OBJECTS := $(BUILD_DIR)/gxbytebuffer.o
ifneq ($(strip $(GX_COMPRESSION_EXTERNAL_BUFFER_HEADER)),)
BUFFER_OBJECTS := $(BUILD_DIR)/bytebuffer.o $(BUILD_DIR)/helpers.o $(BUILD_DIR)/bitarray.o
ifeq ($(strip $(GURUX_DLMS_ROOT)),)
BUFFER :=
BUFFER_OBJECTS :=
endif
endif
ifneq ($(strip $(GX_COMPRESSION_BUFFER_LIBRARIES)),)
BUFFER :=
BUFFER_OBJECTS :=
endif
ifeq ($(filter 1 ON on TRUE true,$(GX_V44_BUILD_BUFFER)),)
BUFFER :=
BUFFER_OBJECTS :=
endif
PROGRAMS := $(BUILD_DIR)/v44_benchmark
OBJECTS := $(BUILD_DIR)/gxv44.o $(BUFFER_OBJECTS) $(BUILD_DIR)/benchmark.o

.PHONY: all benchmark report arm-check clean

all: $(CODEC) $(BUFFER)

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/gxv44.o: src/gxv44.c Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CONFIG_DEFINES) $(INCLUDES) $(CFLAGS) $(STRICT_CFLAGS) -MD -MP -c "$<" -o "$@"

$(BUILD_DIR)/gxbytebuffer.o: src/gxbytebuffer.c Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CONFIG_DEFINES) $(INCLUDES) $(CFLAGS) $(STRICT_CFLAGS) -MD -MP -c "$<" -o "$@"

$(BUILD_DIR)/%.o: $(GURUX_DLMS_ROOT)/src/%.c Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CONFIG_DEFINES) $(INCLUDES) -DDLMS_IGNORE_STRING_CONVERTER -include stdio.h $(UPSTREAM_CONFIG) $(CFLAGS) $(UPSTREAM_CFLAGS) -MD -MP -c "$<" -o "$@"

$(BUILD_DIR)/benchmark.o: tests/benchmark.c Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CONFIG_DEFINES) $(INCLUDES) $(CFLAGS) $(STRICT_CFLAGS) -MD -MP -c "$<" -o "$@"

$(CODEC): $(BUILD_DIR)/gxv44.o
	$(AR) rcs "$@" $^

ifneq ($(strip $(BUFFER)),)
$(BUFFER): $(BUFFER_OBJECTS)
	$(AR) rcs "$@" $^
endif

$(BUILD_DIR)/v44_benchmark: $(BUILD_DIR)/benchmark.o $(CODEC) $(BUFFER)
	$(CC) $(CFLAGS) $(LDFLAGS) -o "$@" $^ $(GX_COMPRESSION_BUFFER_LIBRARIES) $(LDLIBS)

benchmark: $(BUILD_DIR)/v44_benchmark
	"$(BUILD_DIR)/v44_benchmark"

# GCC/Clang stack reports describe individual desktop/selected-target frames,
# not interrupt nesting, libc internals or a hardware-verified stack budget.
report: benchmark
	$(CC) $(CPPFLAGS) $(CONFIG_DEFINES) $(INCLUDES) $(CFLAGS) $(STRICT_CFLAGS) -fstack-usage -c src/gxv44.c -o "$(BUILD_DIR)/gxv44_report.o"
	$(SIZE) "$(BUILD_DIR)/gxv44_report.o"
	cat "$(BUILD_DIR)/gxv44_report.su"

# Compile the actual codec for Cortex-M0; linking/board execution is separate.
arm-check: | $(BUILD_DIR)
	$(ARM_CC) $(CPPFLAGS) $(CONFIG_DEFINES) -DDLMS_IGNORE_MALLOC -DGX_DLMS_MICROCONTROLLER $(INCLUDES) $(CFLAGS) $(STRICT_CFLAGS) $(ARM_CFLAGS) -fstack-usage -c src/gxv44.c -o "$(BUILD_DIR)/gxv44_arm.o"
	$(ARM_SIZE) "$(BUILD_DIR)/gxv44_arm.o"
	cat "$(BUILD_DIR)/gxv44_arm.su"

# Remove only enumerated build artifacts; never recursively delete a supplied path.
clean:
	$(RM) $(OBJECTS) $(OBJECTS:.o=.d) $(CODEC) $(BUFFER) $(PROGRAMS) "$(BUILD_DIR)/gxv44_report.o" "$(BUILD_DIR)/gxv44_report.su" "$(BUILD_DIR)/gxv44_arm.o" "$(BUILD_DIR)/gxv44_arm.su"

-include $(OBJECTS:.o=.d)
