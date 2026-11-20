cmake_minimum_required(VERSION 3.20)

project(ViGEmBus LANGUAGES CXX)

# The kernel-mode driver and the legacy C client are built with MSBuild through
# ViGEmBus.sln. This CMake project builds the modern C++ client API in libvigem.

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(VIGEM_BUILD_EXAMPLES "Build the libvigem examples" ON)
option(VIGEM_BUILD_TESTS "Build the libvigem tests" ON)

add_subdirectory(libvigem)

if(VIGEM_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(VIGEM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
