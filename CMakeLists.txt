cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0042 NEW)

project (llama-node)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED true)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED true)

if(NOT DEFINED napi_build_version)
  set(napi_build_version 6)
endif()
add_definitions(-DNAPI_VERSION=${napi_build_version})
message(STATUS "NAPI_VERSION: ${napi_build_version}")

set(CMAKE_SYSTEM_PROCESSOR ${NODE_ARCH})
string(TOLOWER ${CMAKE_SYSTEM_NAME} PLATFORM)
string(TOLOWER ${CMAKE_SYSTEM_PROCESSOR} ARCH)

# normalize platform to nodejs
string(REPLACE "windows" "win32" PLATFORM ${PLATFORM})

# normalize arch to nodejs: 'arm', 'arm64', 'ia32', 'loong64', 'mips', 'mipsel', 'ppc', 'ppc64', 'riscv64', 's390', 's390x', and 'x64'.
string(REPLACE "amd64" "x64" ARCH ${ARCH})
string(REPLACE "x86_64" "x64" ARCH ${ARCH})
string(REPLACE "i686" "ia32" ARCH ${ARCH})
string(REPLACE "i386" "ia32" ARCH ${ARCH})
string(REPLACE "armv7l" "arm" ARCH ${ARCH})
string(REPLACE "arm" "arm" ARCH ${ARCH})
string(REPLACE "arm64x" "arm64" ARCH ${ARCH})
string(REPLACE "aarch64" "arm64" ARCH ${ARCH})

if(DEFINED VARIANT)
  set(VARIANT -${VARIANT})
else()
  set(VARIANT "")
endif()

set(PLATFORM_BINARY_DIR ${CMAKE_SOURCE_DIR}/bin/${PLATFORM}${VARIANT}/${ARCH})

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Platform: ${PLATFORM}")
message(STATUS "Architecture: ${ARCH}")
message(STATUS "PLATFORM_BINARY_DIR: ${PLATFORM_BINARY_DIR}")

# set strip flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  if(UNIX OR MINGW)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s")
  elseif(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /s")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /s")
  endif()
endif()

include_directories(${CMAKE_JS_INC})

# flags: -fPIC
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# VULKAN_SDK
if (VULKAN_SDK)
  set(ENV{VULKAN_SDK} ${VULKAN_SDK})
  find_package(Vulkan REQUIRED)
endif()

find_program(PATCH patch REQUIRED)

add_custom_target(
  patch ALL
  COMMAND ${PATCH} -p1 -N -i ${CMAKE_SOURCE_DIR}/patches/llama.patch
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/llama.cpp
  COMMENT "Applying patches"
)

set(LLAMA_STATIC ON CACHE BOOL "Build llama as static library")
add_subdirectory("src/llama.cpp")

file(
  GLOB SOURCE_FILES
    "src/addons.cc"
    "src/common.hpp"
    "src/DisposeWorker.cpp"
    "src/DisposeWorker.h"
    "src/LlamaCompletionWorker.cpp"
    "src/LlamaCompletionWorker.h"
    "src/LlamaContext.cpp"
    "src/LlamaContext.h"
    "src/LoadSessionWorker.cpp"
    "src/LoadSessionWorker.h"
    "src/SaveSessionWorker.cpp"
    "src/SaveSessionWorker.h"
)

if (LLAMA_QNN)
  if (PLATFORM STREQUAL "linux" AND ARCH STREQUAL "x64")
    set(QNN_PLATFORM "x86_64-linux-clang")
  elseif (PLATFORM STREQUAL "linux" AND ARCH STREQUAL "arm64")
    set(QNN_PLATFORM "aarch64-ubuntu-gcc7.5")
  elseif (PLATFORM STREQUAL "win32" AND ARCH STREQUAL "x64")
    set(QNN_PLATFORM "x86_64-windows-msvc")
  elseif (PLATFORM STREQUAL "win32" AND ARCH STREQUAL "arm64")
    set(QNN_PLATFORM "aarch64-windows-msvc")
  endif()

  if (NOT QNN_PLATFORM)
    message(FATAL_ERROR "QNN is not supported on this platform")
  endif()
  set(QNN_LIB_PATH ${QNN_ROOT}/lib/${QNN_PLATFORM})
  message(STATUS "QNN_LIB_PATH: ${QNN_LIB_PATH}")

  file(
    GLOB QNN_SO_FILES
      "${QNN_LIB_PATH}/libc++*"
      "${QNN_LIB_PATH}/libQnn*.so"
      "${QNN_LIB_PATH}/Htp*.dll"
      "${QNN_LIB_PATH}/Qnn*"
  )

  file(COPY ${QNN_SO_FILES} DESTINATION ${PLATFORM_BINARY_DIR})

  file(GLOB QNN_EXTRA_FILES "${QNN_ROOT}/lib/hexagon-v*/unsigned/libQnn*Skel.so")

  if (PLATFORM STREQUAL "win32")
    list(APPEND QNN_EXTRA_FILES "${QNN_ROOT}/lib/hexagon-v73/unsigned/libqnnhtpv73.cat")
  endif()

  file(COPY ${QNN_EXTRA_FILES} DESTINATION ${PLATFORM_BINARY_DIR})

  list(APPEND LINKS ${QNN_SO_FILES})

  file(
    GLOB QNN_HEADER_FILES
      "src/ggml-qnn/ggml-qnn.h"
  )

  file(
    GLOB QNN_SOURCE_FILES
      "src/ggml-qnn/pthread-shim.h"
      "src/ggml-qnn/ggml-qnn.cpp"
  )

  target_compile_definitions(ggml PUBLIC GGML_USE_QNN)
  target_include_directories(ggml PUBLIC ${QNN_ROOT}/include ${QNN_ROOT}/include/QNN)
  target_sources(ggml PRIVATE ${QNN_SOURCE_FILES} ${QNN_HEADER_FILES})
  target_include_directories(llama PRIVATE "src/ggml-qnn")
  set_target_properties(ggml PROPERTIES CXX_STANDARD 17)
  set_target_properties(ggml PROPERTIES CXX_STANDARD_REQUIRED ON)
  set_target_properties(ggml PROPERTIES C_STANDARD 11)
endif()

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")
target_link_libraries(${PROJECT_NAME} ${CMAKE_JS_LIB} llama ggml common)

add_custom_target(copy_assets ALL DEPENDS ${PROJECT_NAME})

add_custom_command(
  TARGET copy_assets
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${PLATFORM_BINARY_DIR}
  COMMENT "Cleaning bin folder"
)

if(MSVC AND CMAKE_JS_NODELIB_DEF AND CMAKE_JS_NODELIB_TARGET)
  # Generate node.lib
  execute_process(COMMAND ${CMAKE_AR} /def:${CMAKE_JS_NODELIB_DEF} /out:${CMAKE_JS_NODELIB_TARGET} ${CMAKE_STATIC_LINKER_FLAGS})
  # copy target to bin folder
  get_filename_component(CMAKE_JS_NODELIB_TARGET_NAME ${CMAKE_JS_NODELIB_TARGET} NAME)
  add_custom_command(TARGET copy_assets
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_JS_NODELIB_TARGET} ${PLATFORM_BINARY_DIR}/${CMAKE_JS_NODELIB_TARGET_NAME}
    COMMENT "Copying to bin folder"
  )
endif()

# copy target to bin folder
add_custom_command(TARGET copy_assets
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${PLATFORM_BINARY_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>
  COMMENT "Copying to bin folder"
)

if (LLAMA_METAL)
  # copy ${CMAKE_BINARY_DIR}/bin/default.metallib
  add_custom_command(
    TARGET copy_assets
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/bin/default.metallib ${PLATFORM_BINARY_DIR}/default.metallib
    COMMENT "Copying default.metallib to bin folder"
  )
  add_dependencies(copy_assets ggml-metal)
endif()

if (LLAMA_CLBLAST)
  find_package(CLBlast)
  if (CLBlast_FOUND)
    message(STATUS "CLBlast found: ${CLBlast_DIR}")
    file(
      GLOB CLBlast_SO_FILES
      ${CLBlast_DIR}/../../../bin/clblast.dll
      ${CLBlast_DIR}/../../../lib/libclblast.so
    )
    add_custom_command(
      TARGET copy_assets
      COMMAND ${CMAKE_COMMAND} -E copy ${CLBlast_SO_FILES} ${PLATFORM_BINARY_DIR}
      COMMENT "Copying CLBlast SO files to bin folder"
    )
  endif()
endif()
