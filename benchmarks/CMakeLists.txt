
add_executable(mef_benchmark benchmark.cpp)
target_link_libraries(mef_benchmark PRIVATE mef_core)
mef_apply_warnings(mef_benchmark)
