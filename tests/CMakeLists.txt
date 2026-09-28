# Unit tests (fast, pure logic)
add_executable(anav_unit_tests
    TestMain.cpp
    UnitSmoothingTests.cpp
    UnitCoreTests.cpp
    UnitConfigTests.cpp
    UnitExporterTests.cpp)
target_link_libraries(anav_unit_tests PRIVATE anav_server_lib anav_exporter_core anav_options)
add_test(NAME unit COMMAND anav_unit_tests)

# End-to-end: synthetic geometry -> exporter pipeline -> .anp -> navigation queries -> TCP server
add_executable(anav_e2e_tests
    TestMain.cpp
    E2EPipelineTests.cpp
    E2EServerTests.cpp)
target_link_libraries(anav_e2e_tests PRIVATE anav_server_lib anav_exporter_core anav_options)
add_test(NAME e2e COMMAND anav_e2e_tests)
set_tests_properties(e2e PROPERTIES TIMEOUT 600)

# Benchmark (manual): anav_benchmark [adtsPerAxis] [pathQueries]
add_executable(anav_benchmark Benchmark.cpp)
target_link_libraries(anav_benchmark PRIVATE anav_core anav_exporter_core anav_options)
