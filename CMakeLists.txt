
cmake_minimum_required(VERSION 3.24)
project(MemoryExpansionFabric VERSION 1.0.1 LANGUAGES CXX)

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

# MSVC's AddressSanitizer requires a consistent, fully-instrumented build: every
# compilation unit and image that links mef_core must be compiled with
# /fsanitize=address, or the linker rejects mef_core's ASan annotate_* symbols
# with LNK2038. ASan also requires debug-information emission (/Zi) or cl emits
# C5072, which the /W4 /WX policy turns into an error, and /DEBUG at link time
# for ASan symbolization (else LNK4302). Applying the flags tree-wide keeps the
# build genuinely instrumented and warning-clean without weakening /W4 /WX or
# suppressing any diagnostic.
if(MSVC AND MEF_ENABLE_ASAN)
  add_compile_options(/fsanitize=address /Zi)
  # /DEBUG for ASan symbolization; /INCREMENTAL:NO because ASan metadata is
  # incompatible with incremental linking (LNK4300 otherwise).
  add_link_options(/DEBUG /INCREMENTAL:NO)
endif()

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
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
mef_apply_warnings(mef_core)

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

# ---------------------------------------------------------------------------
# Installable CMake package: find_package(MemoryExpansionFabric CONFIG REQUIRED)
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)
install(TARGETS mef_core EXPORT MemoryExpansionFabricTargets
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib)
install(DIRECTORY include/memory_expansion_fabric DESTINATION include)
install(EXPORT MemoryExpansionFabricTargets
  FILE MemoryExpansionFabricTargets.cmake
  NAMESPACE MemoryExpansionFabric::
  DESTINATION lib/cmake/MemoryExpansionFabric)
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MemoryExpansionFabricConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/MemoryExpansionFabricConfig.cmake
  INSTALL_DESTINATION lib/cmake/MemoryExpansionFabric)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/MemoryExpansionFabricConfigVersion.cmake
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/MemoryExpansionFabricConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/MemoryExpansionFabricConfigVersion.cmake
  DESTINATION lib/cmake/MemoryExpansionFabric)

if(MEF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
