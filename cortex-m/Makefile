# Build h264bsd for Arm Cortex-M33 (Armv8-M Mainline with DSP extension) and
# run the decoder test on QEMU's mps2-an505 board.
#
#   make            build lib/libh264bsd.a and bin/test_h264bsd_m33.elf
#   make run        decode test/test_640x360.h264 on QEMU and print frame hashes
#   make check      same, and compare against posix/test_h264bsd -h output
#   make profile    per-function instruction profile (needs `make plugin`)
#   make memprofile memory traffic / cache model / frame-rate estimate
#   make size       code/data size of the library objects
#
# Toolchain: GNU Arm Embedded (arm-none-eabi-gcc) 10 or newer.

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
AR      := $(CROSS)ar
SIZE    := $(CROSS)size
QEMU    ?= qemu-system-arm
QEMU_MACHINE ?= mps2-an505

CPU_FLAGS ?= -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16
OPT       ?= -O2
CFLAGS    ?= $(CPU_FLAGS) $(OPT) -std=gnu99 -Wall -Wextra -Wno-unused-parameter \
             -ffunction-sections -fdata-sections -fno-common -g
LDFLAGS   ?= $(CPU_FLAGS) --specs=nano.specs --specs=nosys.specs -nostartfiles \
             -Wl,--gc-sections -Wl,-T,qemu/mps2_an505.ld -Wl,-Map,bin/test_h264bsd_m33.map

SRC_DIR := ../src
SRCS    := $(wildcard $(SRC_DIR)/*.c)
OBJS    := $(patsubst $(SRC_DIR)/%.c,obj/%.o,$(SRCS))
LIB     := lib/libh264bsd.a
ELF     := bin/test_h264bsd_m33.elf
STREAM  ?= ../test/test_640x360.h264
PLUGIN  := qemu/tools/libinsn_profile.so
QEMU_PLUGIN_INC ?= /usr/include

QEMU_ARGS = -M $(QEMU_MACHINE) -cpu cortex-m33 -nographic -monitor none \
            -semihosting-config enable=on,target=native,arg=$(STREAM) \
            -kernel $(ELF)

all: $(LIB) $(ELF)

obj lib bin:
	mkdir -p $@

obj/%.o: $(SRC_DIR)/%.c $(wildcard $(SRC_DIR)/*.h) | obj
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS) | lib
	$(AR) rcs $@ $(OBJS)

HARNESS := qemu/startup.c qemu/semihost.c qemu/test_h264bsd_m33.c

$(ELF): $(LIB) $(HARNESS) qemu/mps2_an505.ld | bin
	$(CC) $(CFLAGS) -I$(SRC_DIR) -I../test $(HARNESS) $(LDFLAGS) -Llib -lh264bsd -o $@
	$(SIZE) $@

run: $(ELF)
	$(QEMU) $(QEMU_ARGS)

# (the semihosting console output of QEMU goes to stderr)
check: $(ELF)
	$(QEMU) $(QEMU_ARGS) 2>&1 | grep '^pic ' > bin/m33_hashes.txt
	$(MAKE) -C ../posix hashes STREAM=$(STREAM) > /dev/null
	../posix/bin/test_h264bsd -h $(STREAM) | grep '^pic ' > bin/host_hashes.txt
	@cmp bin/m33_hashes.txt bin/host_hashes.txt && \
	  echo "PASS: Cortex-M33 output is bit-exact ($$(wc -l < bin/m33_hashes.txt) pictures)"

profile: $(ELF) $(PLUGIN)
	$(QEMU) $(QEMU_ARGS) -plugin $(PLUGIN),top=45 -d plugin

# Memory traffic per buffer (picture buffers, macroblock state, ...) with a
# cache model, plus the instruction mix; the buffer address ranges are taken
# from the "alloc" lines the test application prints. Output goes to
# $(PROFILE_DIR)/prof_<clip>_mem{32,16}.txt for cortex-m/tools/fps_model.py.
PROFILE_DIR ?= bin
MEMPLUGIN := qemu/tools/libmem_profile.so
memprofile: $(ELF) $(PLUGIN) $(MEMPLUGIN)
	$(QEMU) $(QEMU_ARGS) -plugin $(PLUGIN),top=45 -d plugin \
	  > $(PROFILE_DIR)/prof_$(notdir $(basename $(STREAM)))_insn.txt 2>&1
	@ranges=$$(grep '^alloc' $(PROFILE_DIR)/prof_$(notdir $(basename $(STREAM)))_insn.txt | \
	  awk '{print "range="$$2":"$$3}' | paste -sd,); \
	for line in 32 16; do \
	  $(QEMU) -M $(QEMU_MACHINE) -cpu cortex-m33 -nographic -monitor none \
	    -semihosting-config enable=on,target=native,arg=nohash,arg=$(STREAM) -kernel $(ELF) \
	    -plugin $(MEMPLUGIN),$$ranges,line=$$line,ways=2 -d plugin \
	    > $(PROFILE_DIR)/prof_$(notdir $(basename $(STREAM)))_mem$$line.txt 2>&1; \
	done
	python3 tools/fps_model.py $(PROFILE_DIR) $(notdir $(basename $(STREAM)))

plugin: $(PLUGIN) $(MEMPLUGIN)

qemu/tools/lib%.so: qemu/tools/%.c
	$(HOSTCC_PREFIX)gcc -shared -fPIC -O2 $$(pkg-config --cflags glib-2.0) \
	  -I$(QEMU_PLUGIN_INC) $< -o $@ $$(pkg-config --libs glib-2.0)

size: $(LIB)
	$(SIZE) -t $(OBJS) | tail -1
	$(SIZE) $(OBJS) | sort -n -k1 | tail -12

clean:
	rm -rf obj lib bin

.PHONY: all run check profile memprofile plugin size clean
