# SPDX-FileCopyrightText: 2026 gookie and g6shim contributors
# SPDX-License-Identifier: LGPL-3.0-or-later
file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
g6shim_add_hle_module(hle_acphal
    DLL "AcpHal.dll"
    DEF "${CMAKE_CURRENT_SOURCE_DIR}/acphal.def"
    SOURCES ${sources})
set(G6SHIM_FFMPEG_INCLUDE_DIR "" CACHE PATH "FFmpeg headers for the run-time loaded XMA decoder")
if(G6SHIM_FFMPEG_INCLUDE_DIR)
  target_include_directories(hle_acphal SYSTEM PRIVATE "${G6SHIM_FFMPEG_INCLUDE_DIR}")
  target_compile_definitions(hle_acphal PRIVATE G6SHIM_HAVE_FFMPEG)
endif()
