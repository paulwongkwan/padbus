add_executable(vigem_api_smoke api_smoke.cpp)
target_link_libraries(vigem_api_smoke PRIVATE vigem::vigem)

add_test(NAME vigem_api_smoke COMMAND vigem_api_smoke)
