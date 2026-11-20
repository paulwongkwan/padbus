add_executable(vigem_api_smoke api_smoke.cpp)
target_link_libraries(vigem_api_smoke PRIVATE vigem::vigem)

add_test(NAME vigem_api_smoke COMMAND vigem_api_smoke)

# Host-level check that mirrors how Sunshine's input.cpp drives the client, built as C++23
# because that is the standard Sunshine compiles with.
add_executable(vigem_sunshine_host sunshine_host.cpp)
target_link_libraries(vigem_sunshine_host PRIVATE vigem::vigem)
target_compile_features(vigem_sunshine_host PRIVATE cxx_std_23)

add_test(NAME vigem_sunshine_host COMMAND vigem_sunshine_host)

# Submissions racing a close and feedback dispatch on three threads at once.
add_executable(vigem_stress stress.cpp)
target_link_libraries(vigem_stress PRIVATE vigem::vigem)

add_test(NAME vigem_stress COMMAND vigem_stress)
