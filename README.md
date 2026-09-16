# h264bsd [![Build Status](https://github.com/oneam/h264bsd/actions/workflows/build.yml/badge.svg)](https://github.com/oneam/h264bsd/actions/workflows/build.yml)

This is a software-based library that was extracted from the Android project with the intention of being used elsewhere.

Some modifications have been made to the original project in order to remove the top-level API, add an alloc and free for encoder storage, convert to ARGB format, and add optimizations for certain platforms.

The intention is to provide a simple H.264 decoder that can be easily invoked from [ffi](http://en.wikipedia.org/wiki/Foreign_function_interface) systems.

## Implementation Notes

Currently, the process of decoding modifies the input data. This has tripped me a few times in the past so others should be aware of it.

The decoder only works nicely if it has a single consistent stream to deal with. If you want to change the width/height or restart the stream with a new access unit delimiter, it's better to shutdown and init a new decoder.

## Directories

* *src* The modified source.
* *test* Contains test data available for all platforms.
* *win* Visual Studio project files and test application.
* *posix* Simple c file that loads a test file and runs through a decode loop.
* *js* Pure JavaScript version of the library created using [emscripten](http://emscripten.org/ *Note: this version is largely deprecated and replaced with the wasm version.*
* *wasm* JavaScript WebAssembly version created using [emscripten](http://emscripten.org/).
* *ios* XCode project and objective-c wrapper classes.
* *cortex-m* Makefile for Arm Cortex-M (Cortex-M33 by default) plus a bare-metal QEMU test harness and an instruction profiler.
* *zephyr* Zephyr RTOS module glue (`module.yml`, `Kconfig`, `CMakeLists.txt`).

This project was heavily inspired by [Broadway.js](https://github.com/mbebenita/Broadway). Much love to them for pioneering the idea.

## Building

This project generally uses [rake](https://github.com/ruby/rake) as a build tool, since I find it simple, clear, and compatible with many different environments.

On Windows, you can download rake along with Ruby using [RubyInstaller](https://rubyinstaller.org)
On Mac, ruby and rake are already installed

In most cases, once you've installed the dependencies, you can build by changing to the desired directory and running:

```
rake
```

Here are very basic instructions for building each version:

### wasm and js

wasm and js use emscripten and Terser.

* Instructions for getting started with emscripten are here: https://emscripten.org/docs/getting_started/index.html
* Terser is available here: https://www.npmjs.com/package/terser (You can override Terser for another compatible uglify tool using the `UGLIFY_JS` environment variable)

### Windows

A Visual Studio project is available for the library as well as a simple test application to ensure it works.

I don't have plans to create a VSCode version of the project any time soon.

### iOS

You should only need Xcode in order to build and test the iOS version of the library. A project file for the library and a simple wrapper application is provided.

### posix

The posix build has been tested with both gcc and clang and the test application only uses POSIX.2 system calls.
A plain `Makefile` is also provided (`make -C posix`, `make -C posix hashes` prints a hash of every decoded picture, `make -C posix check` verifies the optimized code paths against the portable ones).

### Arm Cortex-M (Cortex-M33)

The decoder has a set of optimizations for Arm Cortex-M cores with the DSP
extension (Cortex-M4/M7/M33/M55/M85). They are selected automatically when the
compiler reports `__ARM_FEATURE_SIMD32` and `__ARM_FEATURE_SAT`, and the
decoded pictures are bit-exact with every other build:

* **Packed-lane pixel kernels** (`H264BSD_PACKED_KERNELS`): the luma 6-tap and
  chroma bilinear sub-pixel interpolation and the residual reconstruction work
  on four pixels at a time with the `UXTB16`/`UXTAB16`, `SADD16`/`SSUB16`,
  `SMLAD`/`SMLABB`, `USAT`/`USAT16`, `UHADD8` and `PKHBT`/`PKHTB` instructions
  (via ACLE intrinsics in `src/h264bsd_platform.h`). Clipping uses `USAT`
  instead of a 1280-byte lookup table, the bit reader uses `LDR`+`REV` and
  `CLZ`.
* **In-loop deblocking** (`H264BSD_INLOOP_DEBLOCKING`): each macroblock row is
  deblocked as soon as it (and every row above it) is complete, instead of a
  second pass over the whole picture after decoding. The row is still in
  whatever cache the SoC has and the reference pictures are streamed once,
  which matters on cores without a data cache or with the frames in external
  RAM. The bottom sample line of the previous row is kept unfiltered in a small
  line buffer (32 bytes per macroblock column) for intra prediction.
* **Less memory traffic per macroblock**: only the residual blocks that are
  actually parsed are cleared (instead of a 2 KB `memset` per macroblock),
  full-sample reference blocks are copied a word at a time, skipped
  macroblocks with integer motion are copied straight from the reference
  picture, and the per-macroblock bookkeeping structure shrank from 240 to
  156 bytes (77 KB less RAM at 640x360).

Both switches default to on for Arm DSP targets and to off elsewhere (x86,
wasm, AArch64 keep the original scalar kernels and single-pass deblocking,
which are faster on wide out-of-order CPUs). `-DH264BSD_NO_ARM_SIMD` forces
the portable code on Arm; `-DH264BSD_PACKED_KERNELS=1
-DH264BSD_INLOOP_DEBLOCKING=1` enables the new code paths on any target
(`make -C posix check` builds both variants and compares every decoded
picture).

Measured on QEMU's `mps2-an505` Cortex-M33 model (dynamic instruction count
for the 73-picture 640x360 test clip, `arm-none-eabi-gcc 13.2 -O2`; the
instruction count is a good proxy for cycles on an in-order single-issue
core, memory stalls not included):

| | instructions | per picture | library code size |
|---|---|---|---|
| original | 525 M | 7.2 M | 58.2 KB |
| optimized | 418 M | 5.7 M | 56.8 KB |

so about 20% fewer instructions (more in cycles: the removed table lookups
and byte accesses are the multi-cycle instructions), and the deblocking no
longer makes a second pass over the frame. Measured on the same run, the decoder used 1.8 MB
of heap for the 640x360 clip (five decoded picture buffers of 353 KB, the
per-macroblock state and the parsing buffers; `num_ref_frames + 1` pictures
of `width * height * 3 / 2` bytes each are needed, pass `noOutputReordering
= 1` to `h264bsdInit()` to keep the count at the minimum), 15 MB for the
1080p clip, and 2.5 KB of stack. The QEMU harness prints both high-water
marks at the end of each run.

Memory placement hints for a real SoC:

* Put the decoded picture buffers in the fastest RAM that fits them; on
  parts with a system cache or TCM, keep the per-macroblock state
  (`storage_t`, `mbStorage_t[]`, the 2 KB `macroblockLayer_t` and the line
  buffer) in internal SRAM/TCM and the pictures in external RAM.
* Define `H264BSD_FAST_CODE` (e.g. to `__ramfunc` on Zephyr) to run the
  interpolation, deblocking and residual functions from RAM instead of
  flash, and `H264BSD_FAST_DATA` for the CAVLC tables.
* Enable the instruction cache / flash accelerator of the SoC and compile the
  decoder with `-O2` (Zephyr's default `-Os` costs 15-20%).

Building and testing on the emulated Cortex-M33 needs `gcc-arm-none-eabi`,
newlib and `qemu-system-arm`:

```
make -C cortex-m           # lib/libh264bsd.a + bin/test_h264bsd_m33.elf
make -C cortex-m check     # decode test/test_640x360.h264 on QEMU and compare
                           # every picture with the host build
make -C cortex-m plugin profile   # per-function instruction profile (needs
                           # glib-2.0 dev headers and qemu-plugin.h)
```

#### Zephyr RTOS

The repository is a Zephyr module: add it to your `west.yml` (or to
`EXTRA_ZEPHYR_MODULES`), enable `CONFIG_H264BSD=y` and include
`h264bsd_decoder.h`. `CONFIG_H264BSD_FAST_CODE_IN_RAM` places the hot
functions in `.ramfunc`, `CONFIG_H264BSD_SPEED_OPTIMIZATIONS` (default y)
compiles the decoder with `-O2`. The decoder uses `malloc()`/`free()` for its
buffers, so size the libc heap accordingly (`CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE`
or a `newlib` heap large enough for the pictures). The module glue has not
been built against a Zephyr tree in this repository's CI yet.

### test

The test files are generated from a snippet of the movie ["Big Buck Bunny"](https://peach.blender.org) with uncompressed frames provided by [Xiph.org](https://media.xiph.org)

The encoding is done using [FFMPEG](https://ffmpeg.org)
