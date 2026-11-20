add_library(vigem
  src/profiles.cpp
  src/runtime.cpp
  src/types.cpp
)

add_library(vigem::vigem ALIAS vigem)

# The driver backend is Windows-only; other hosts get an unavailable backend.
if(WIN32)
  target_sources(vigem PRIVATE src/backend_vigem.cpp)
endif()

target_include_directories(vigem PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(vigem PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(vigem PRIVATE /W4 /permissive-)
else()
  target_compile_options(vigem PRIVATE -Wall -Wextra)
endif()
