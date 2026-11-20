add_library(vigem
  src/backend_vigem.cpp
  src/mapping.cpp
  src/profiles.cpp
  src/runtime.cpp
  src/types.cpp
)

add_library(vigem::vigem ALIAS vigem)

target_include_directories(vigem PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_compile_features(vigem PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(vigem PRIVATE /W4 /permissive-)
else()
  target_compile_options(vigem PRIVATE -Wall -Wextra)
endif()

include(GNUInstallDirs)

# Consumers can either add_subdirectory() or install and use find_package(vigem).
install(TARGETS vigem EXPORT vigemTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT vigemTargets NAMESPACE vigem:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vigem)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vigemConfig.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vigem)
