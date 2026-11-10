# libedenvk: Eden's video_core (Vulkan renderer only), shader_recompiler and the parts of common they use,
# built as one static library for the SwitchRecomp runtime. GPL-3.0-or-later (see README.txt).
cmake_minimum_required(VERSION 3.22)
project(edenvk CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(D ${CMAKE_CURRENT_SOURCE_DIR}/deps)
# host shaders (GLSL -> SPIR-V headers); their CMakeLists looks for FidelityFX under CMAKE_SOURCE_DIR/externals
add_subdirectory(src/video_core/host_shaders)
# sirit (SPIR-V emitter) with the Khronos SPIR-V headers
set(SIRIT_USE_SYSTEM_SPIRV_HEADERS OFF CACHE BOOL "" FORCE)
add_subdirectory(${D}/sirit sirit EXCLUDE_FROM_ALL)

file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/video_core_files.txt VC_FILES)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/shader_files.txt SR_FILES)
set(EVK_SRC)
foreach(f ${VC_FILES})
  list(APPEND EVK_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/video_core/${f})
endforeach()
foreach(f ${SR_FILES})
  list(APPEND EVK_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/shader_recompiler/${f})
endforeach()
file(GLOB SHIM_SRC ${CMAKE_CURRENT_SOURCE_DIR}/glue/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/shim/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/shim/core/frontend/*.cpp)
list(APPEND EVK_SRC ${SHIM_SRC}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/video_core/gpu_logging/gpu_logging.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/video_core/gpu_logging/gpu_state_capture.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/externals/bc_decoder/bc_decoder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/externals/stb/stb_dxt.cpp)
set(COMMON_FILES address_space assert cityhash demangle dynamic_library error heap_tracker hex_util host_memory
    logging memory_detect multi_level_page_table page_table param_package settings settings_common settings_input
    sparse_large_vector steady_clock string_util thread random uuid cpu_features x64/rdtsc
    fs/file fs/fs fs/fs_util fs/path_util lz4_compression zstd_compression)
if(WIN32)
  list(APPEND COMMON_FILES windows/timer_resolution)
endif()
foreach(f ${COMMON_FILES})
  list(APPEND EVK_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/common/${f}.cpp)
endforeach()
file(GLOB LZ4_SRC ${D}/lz4/lib/lz4.c ${D}/lz4/lib/lz4hc.c)
file(GLOB_RECURSE ZSTD_SRC ${D}/zstd/lib/common/*.c ${D}/zstd/lib/compress/*.c ${D}/zstd/lib/decompress/*.c)

add_library(edenvk STATIC ${EVK_SRC} ${LZ4_SRC} ${ZSTD_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/fmt_impl.cpp)
add_dependencies(edenvk host_shaders)
target_include_directories(edenvk PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/shim ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/externals
  ${CMAKE_CURRENT_SOURCE_DIR}/externals/bc_decoder ${CMAKE_CURRENT_SOURCE_DIR}/externals/stb
  ${HOST_SHADERS_INCLUDE} ${D}/vulkan-headers/include ${D}/vul/include ${D}/vma/include ${D}/xbyak
  ${D}/fmt/include ${D}/frozen/include ${D}/lz4/lib ${D}/zstd/lib ${D}/sirit/include ${D}/spirv-headers/include
  ${EVK_BOOST_INCLUDE})
target_compile_definitions(edenvk PRIVATE ARCHITECTURE_x86_64=1 FMT_HEADER_ONLY=0 VK_NO_PROTOTYPES
  BUILD_ID="swr" ZSTD_DISABLE_ASM ZSTD_MULTITHREAD=0 SWITCHRECOMP_EDENVK=1)
if(WIN32)
  target_compile_definitions(edenvk PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX VK_USE_PLATFORM_WIN32_KHR)
else()
  target_compile_definitions(edenvk PRIVATE VK_USE_PLATFORM_XLIB_KHR)
endif()
target_compile_options(edenvk PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-w -fno-strict-aliasing> $<$<COMPILE_LANGUAGE:C>:-w>)
target_link_libraries(edenvk PRIVATE sirit)
