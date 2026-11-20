cmake_minimum_required(VERSION 3.20)

project(ViGEmBus LANGUAGES CXX)

# The kernel-mode driver and the legacy C client are built with MSBuild through
# ViGEmBus.sln. This CMake project builds the modern C++ client API in libvigem.

# libvigem drives ViGEmBus through the Windows client library and targets Windows 11.
if(NOT WIN32)
  message(FATAL_ERROR "libvigem supports Windows 11 only")
endif()

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

# Packaging: cpack produces a portable archive of the installed headers, library and
# CMake package files, so the library can be handed to another project without a clone.
set(CPACK_PACKAGE_NAME "libvigem")
set(CPACK_PACKAGE_VENDOR "ViGEm")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modern C++ client API for the ViGEmBus driver")
set(CPACK_PACKAGE_FILE_NAME "libvigem-${CPACK_PACKAGE_VERSION}-win64")
set(CPACK_GENERATOR "ZIP")

include(CPack)
