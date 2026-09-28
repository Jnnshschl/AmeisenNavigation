# libFuzzer targets (configure with -DANAV_BUILD_FUZZERS=ON and Clang), see tests/fuzz/run_fuzzers.sh
#   FuzzTile      Detour tile validation + addTile + queries
#   FuzzAnp       .anp archive loading
#   FuzzExporter  ADT/WMO/M2/WDT/DBC parsers of the exporter
#   FuzzProtocol  request handlers of the server (every request gets exactly one response)
#   FuzzCorpus    writes the seed corpora

function(anav_add_fuzzer name)
    add_executable(${name} ${name}.cpp)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    target_link_libraries(${name} PRIVATE ${ARGN})
endfunction()

anav_add_fuzzer(FuzzTile anav_pack)
anav_add_fuzzer(FuzzAnp anav_core)
anav_add_fuzzer(FuzzProtocol anav_server_lib anav_exporter_core)

if(TARGET anav_exporter_lib)
    anav_add_fuzzer(FuzzExporter anav_exporter_lib anav_core)

    add_executable(FuzzCorpus FuzzCorpus.cpp)
    target_link_libraries(FuzzCorpus PRIVATE anav_exporter_lib anav_server_lib)
endif()
