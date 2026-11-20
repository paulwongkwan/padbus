add_executable(padbus_api_smoke api_smoke.cpp)
target_link_libraries(padbus_api_smoke PRIVATE padbus::padbus)

add_test(NAME padbus_api_smoke COMMAND padbus_api_smoke)

# Host-level check that mirrors how Sunshine's input.cpp drives the client, built as C++23
# because that is the standard Sunshine compiles with.
add_executable(padbus_sunshine_host sunshine_host.cpp)
target_link_libraries(padbus_sunshine_host PRIVATE padbus::padbus)
target_compile_features(padbus_sunshine_host PRIVATE cxx_std_23)

add_test(NAME padbus_sunshine_host COMMAND padbus_sunshine_host)

# Submissions racing a close and feedback dispatch on three threads at once.
add_executable(padbus_stress stress.cpp)
target_link_libraries(padbus_stress PRIVATE padbus::padbus)

add_test(NAME padbus_stress COMMAND padbus_stress)
