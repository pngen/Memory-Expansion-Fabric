
cmake_minimum_required(VERSION 3.24)
project(MemoryExpansionFabric VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

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
)
target_include_directories(mef_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
mef_apply_warnings(mef_core)

if(MSVC AND MEF_ENABLE_ASAN)
  target_compile_options(mef_core PRIVATE /fsanitize=address)
  target_link_options(mef_core PRIVATE /fsanitize=address)
endif()

if(MEF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
