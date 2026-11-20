cmake_minimum_required(VERSION 3.20)

project(PadBus LANGUAGES CXX)

# The kernel-mode driver and the legacy C client are built with MSBuild through
# PadBus.sln. This CMake project builds the modern C++ client API in libpadbus.

# libpadbus drives ViGEmBus through the Windows client library and targets Windows 11.
if(NOT WIN32)
  message(FATAL_ERROR "libpadbus supports Windows 11 only")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PADBUS_BUILD_EXAMPLES "Build the libpadbus examples" ON)
option(PADBUS_BUILD_TESTS "Build the libpadbus tests" ON)

add_subdirectory(libpadbus)

if(PADBUS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(PADBUS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Packaging: cpack produces a portable archive of the installed headers, library and
# CMake package files, so the library can be handed to another project without a clone.
set(CPACK_PACKAGE_NAME "libpadbus")
set(CPACK_PACKAGE_VENDOR "PadBus")
set(CPACK_PACKAGE_VERSION "2.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modern C++ client API for the PadBus driver")
set(CPACK_PACKAGE_FILE_NAME "libpadbus-${CPACK_PACKAGE_VERSION}-win64")
set(CPACK_GENERATOR "ZIP")

include(CPack)
