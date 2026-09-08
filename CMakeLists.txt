
cmake_minimum_required(VERSION 3.24)
project(MemoryExpansionFabric VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

# Put all executables in a single bin/ dir so tests can locate peers easily.
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/bin)
endforeach()

option(MEF_BUILD_TESTS "Build tests" ON)
option(MEF_ENABLE_ASAN "Build with AddressSanitizer" ON)

function(mef_apply_warnings tgt)
  if(MSVC)
    target_compile_options(${tgt} PRIVATE /W4 /WX)
  else()
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

add_library(mef_core STATIC
  src/fabric.cpp
  src/selection.cpp
  src/reservation.cpp
  src/snapshot.cpp
  src/persistence.cpp
  src/protocol.cpp
  src/backends.cpp
)
target_include_directories(mef_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
mef_apply_warnings(mef_core)

if(MSVC AND MEF_ENABLE_ASAN)
  target_compile_options(mef_core PRIVATE /fsanitize=address)
endif()

# CLI / inspection tool.
add_executable(mef_cli tools/cli.cpp)
target_link_libraries(mef_cli PRIVATE mef_core)
mef_apply_warnings(mef_cli)

# Real CUDA consumer-gating proof. CMake's VS generator cannot drive nvcc (no
# CUDA MSBuild toolset), so this proof is built/run by a standalone script
# (proofs/build_cuda_proof.ps1) that requires a CUDA toolchain + GPU.
option(MEF_ENABLE_CUDA "Build the real CUDA gating proof (via proofs/build_cuda_proof.ps1)" ON)

# Reference multiprocess deployment executables.
add_executable(mef_coordinator src/coordinator_main.cpp src/coordinator.cpp)
target_link_libraries(mef_coordinator PRIVATE mef_core)
mef_apply_warnings(mef_coordinator)
if(WIN32)
  target_link_libraries(mef_coordinator PRIVATE ws2_32)
endif()

add_executable(mef_worker src/worker_main.cpp src/worker.cpp)
target_link_libraries(mef_worker PRIVATE mef_core)
mef_apply_warnings(mef_worker)
if(WIN32)
  target_link_libraries(mef_worker PRIVATE ws2_32)
endif()

add_subdirectory(examples)
add_subdirectory(benchmarks)

if(MEF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
