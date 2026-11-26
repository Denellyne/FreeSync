# This file is mostly AI Generated, it is a build file so who cares 

cmake_minimum_required(VERSION 3.25)
project(FreeSync LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# Build Options
# ==============================================================================
option(BUILD_CLIENT "Build the FreeSync Client CLI target" ON)
option(BUILD_FUSE_FS "Build the FreeSync FUSE3 target" ON)
option(BUILD_SERVER "Build the FreeSync Server target" ON)
option(BUILD_TESTS  "Build the test suite" OFF)


# ==============================================================================
# Dependencies
# ==============================================================================
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Require zlib-ng
pkg_check_modules(ZLIB_NG zlib-ng)
if(NOT ZLIB_NG_FOUND)
  message(FATAL_ERROR "This project uses zlib-ng instead of vanilla zlib, please install before proceeding.")
endif()

# FUSE3 dependency (Checked only if FUSE target is enabled)
if(BUILD_FUSE_FS)
  pkg_check_modules(FUSE3 REQUIRED fuse3)
endif()

# ==============================================================================
# Compiler Flags & Optimizations
# ==============================================================================
add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wnon-virtual-dtor
  -fno-exceptions
  -fno-plt
  -fno-math-errno
  -ffunction-sections
  -fdata-sections
  -fvisibility=hidden
  -fno-semantic-interposition
  -falign-functions=32
  -falign-loops=32
)

# Enable LTO across supported compilers
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
if(ipo_supported)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

add_compile_options(-fsanitize=address,undefined)
add_link_options(-fsanitize=address,undefined)
# Native CPU tuning for release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT CMAKE_BUILD_TYPE)
  add_compile_options(-O2 -march=native -ftree-vectorize)
endif()

# Enable Linker GC
add_link_options(-Wl,--gc-sections)

# ==============================================================================
# Shared Core Sources Setup
# ==============================================================================
# Collect generic core sources (excluding platform-specific arch files)
file(GLOB_RECURSE CORE_SOURCES 
  CONFIGURE_DEPENDS 
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
)
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/FuseFS/arch/.*")

if(BUILD_FUSE_FS)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/core/FuseFS/arch/linux.cpp")
  elseif(WIN32)
    message(FATAL_ERROR "Windows WinFSP architecture implementation is not supported yet.")
  endif()
endif()

# Create object library with fully expanded source list
add_library(FSCore OBJECT ${CORE_SOURCES})

target_include_directories(FSCore PUBLIC 
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  ${ZLIB_NG_INCLUDE_DIRS}
  $<$<BOOL:${BUILD_FUSE_FS}>:${FUSE3_INCLUDE_DIRS}>
)

target_link_libraries(FSCore PUBLIC 
  OpenSSL::SSL 
  OpenSSL::Crypto 
  ${ZLIB_NG_LIBRARIES} 
  $<$<BOOL:${BUILD_FUSE_FS}>:${FUSE3_LIBRARIES}>
  Threads::Threads
)

# Standard Client Target
if(BUILD_CLIENT)
  add_executable(FSClient src/client.cpp)
  target_link_libraries(FSClient PRIVATE FSCore)
endif()

# FUSE Virtual Filesystem Client Target
if(BUILD_FUSE_FS)
  add_executable(FSClientFuse src/fuse.cpp)
  target_link_libraries(FSClientFuse PRIVATE FSCore)
endif()

# Server Target
if(BUILD_SERVER)
  add_executable(FSServer src/server.cpp)
  target_link_libraries(FSServer PRIVATE FSCore)
endif()

# Unit Tests Target
if(BUILD_TESTS)
  enable_testing()
  file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp")

  add_executable(FreeSyncTests ${TEST_SOURCES})
  target_link_libraries(FreeSyncTests PRIVATE FSCore)
  add_test(NAME CoreTests COMMAND FreeSyncTests)
endif()
