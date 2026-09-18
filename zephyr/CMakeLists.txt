# Copyright (c) 2026 Benjamin Cabé
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_H264BSD)
  set(h264bsd_src ${ZEPHYR_CURRENT_MODULE_DIR}/src)
  file(GLOB h264bsd_sources ${h264bsd_src}/*.c)

  zephyr_library_named(h264bsd)
  zephyr_library_sources(${h264bsd_sources})
  zephyr_include_directories(${h264bsd_src})

  # The decoder reaches for malloc both through its own ALLOCATE macro and
  # directly in a few places, so the redirection has to be on the names.
  if(NOT CONFIG_H264BSD_MALLOC STREQUAL "malloc")
    zephyr_library_compile_definitions(malloc=${CONFIG_H264BSD_MALLOC})
  endif()
  if(NOT CONFIG_H264BSD_FREE STREQUAL "free")
    zephyr_library_compile_definitions(free=${CONFIG_H264BSD_FREE})
  endif()

  # The pictures are named separately so they can be placed separately; left
  # alone they follow the redirection above like everything else.
  if(NOT CONFIG_H264BSD_PICTURE_MALLOC STREQUAL "malloc")
    zephyr_library_compile_definitions(
      H264BSD_PICTURE_MALLOC=${CONFIG_H264BSD_PICTURE_MALLOC})
  endif()
  if(NOT CONFIG_H264BSD_PICTURE_FREE STREQUAL "free")
    zephyr_library_compile_definitions(
      H264BSD_PICTURE_FREE=${CONFIG_H264BSD_PICTURE_FREE})
  endif()

  # The decoder predates these warnings and is not ours to reflow
  zephyr_library_compile_options(
    -Wno-sign-compare
    -Wno-unused-but-set-variable
    -Wno-unused-variable
  )
endif()
