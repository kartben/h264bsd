# Plain Makefile equivalent of the Rakefile, for environments without ruby.
#   make            build lib/libh264bsd.a and bin/test_h264bsd
#   make test       decode test/test_1920x1080.h264
#   make hashes     print per-picture hashes (STREAM=... to choose the file)
CC      ?= cc
AR      ?= ar
CFLAGS  ?= -std=gnu99 -O3 -D_ERROR_PRINT
SRC_DIR := ../src
SRCS    := $(wildcard $(SRC_DIR)/*.c)
OBJS    := $(patsubst $(SRC_DIR)/%.c,obj/%.o,$(SRCS))
LIB     := lib/libh264bsd.a
APP     := bin/test_h264bsd
STREAM  ?= ../test/test_640x360.h264

all: $(LIB) $(APP)

obj lib bin:
	mkdir -p $@

obj/%.o: $(SRC_DIR)/%.c $(wildcard $(SRC_DIR)/*.h) | obj
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS) | lib
	$(AR) rcs $@ $(OBJS)

$(APP): $(LIB) test_h264bsd.c | bin
	$(CC) test_h264bsd.c $(CFLAGS) -Llib -lh264bsd -o $@

test: $(APP)
	$(APP) ../test/test_1920x1080.h264

hashes: $(APP)
	$(APP) -h $(STREAM)

# Build the portable (default on x86) and the packed-kernel/in-loop variant of
# the decoder and check that both produce identical pictures for every stream.
PACKED_DIR := obj-packed
PACKED_APP := bin/test_h264bsd_packed
PACKED_OBJS := $(patsubst $(SRC_DIR)/%.c,$(PACKED_DIR)/%.o,$(SRCS))

$(PACKED_DIR):
	mkdir -p $@

$(PACKED_DIR)/%.o: $(SRC_DIR)/%.c $(wildcard $(SRC_DIR)/*.h) | $(PACKED_DIR)
	$(CC) $(CFLAGS) -DH264BSD_PACKED_KERNELS=1 -DH264BSD_INLOOP_DEBLOCKING=1 -c $< -o $@

$(PACKED_APP): $(PACKED_OBJS) test_h264bsd.c | bin
	$(CC) test_h264bsd.c $(CFLAGS) $(PACKED_OBJS) -o $@

check: $(APP) $(PACKED_APP)
	@for f in ../test/*.h264; do \
	  $(APP) -h $$f | grep '^pic ' > bin/hashes_default.txt; \
	  $(PACKED_APP) -h $$f | grep '^pic ' > bin/hashes_packed.txt; \
	  cmp bin/hashes_default.txt bin/hashes_packed.txt || { echo "FAIL: $$f"; exit 1; }; \
	  echo "PASS: $$f ($$(wc -l < bin/hashes_default.txt) pictures identical)"; \
	done

clean:
	rm -rf obj obj-packed lib bin include

.PHONY: all test hashes check clean
