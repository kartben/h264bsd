# h264bsd as a Zephyr module: add this repository to your west manifest (or
# to EXTRA_ZEPHYR_MODULES) and enable CONFIG_H264BSD.
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_H264BSD)

zephyr_library_named(h264bsd)

set(H264BSD_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)
file(GLOB H264BSD_SOURCES ${H264BSD_SRC_DIR}/*.c)

zephyr_library_sources(${H264BSD_SOURCES})

# public headers for applications (h264bsd_decoder.h and friends)
zephyr_include_directories(${H264BSD_SRC_DIR})

if(CONFIG_H264BSD_SPEED_OPTIMIZATIONS)
  zephyr_library_compile_options(-O2)
endif()

if(CONFIG_H264BSD_FAST_CODE_IN_RAM)
  zephyr_library_compile_definitions(
    "H264BSD_FAST_CODE=__attribute__((section(\".ramfunc\"),noinline))")
endif()

if(CONFIG_H264BSD_PORTABLE_KERNELS)
  zephyr_library_compile_definitions(H264BSD_NO_ARM_SIMD)
endif()

# the decoder is C99 with GNU extensions (the rest of the tree uses -std=gnu99)
zephyr_library_compile_options(-std=gnu99)

endif()
