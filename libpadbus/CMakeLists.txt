add_library(padbus
  src/backend_padbus.cpp
  src/mapping.cpp
  src/profiles.cpp
  src/runtime.cpp
  src/types.cpp
)

add_library(padbus::padbus ALIAS padbus)

target_include_directories(padbus PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_compile_features(padbus PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(padbus PRIVATE /W4 /permissive-)
else()
  target_compile_options(padbus PRIVATE -Wall -Wextra)
endif()

include(GNUInstallDirs)

# Consumers can either add_subdirectory() or install and use find_package(padbus).
install(TARGETS padbus EXPORT padbusTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT padbusTargets NAMESPACE padbus:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/padbus)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/cmake/padbusConfig.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/padbus)
