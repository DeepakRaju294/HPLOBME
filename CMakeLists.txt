cmake_minimum_required(VERSION 3.20)
project(limit_order_book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(LOB_BUILD_TESTS "Build unit tests" ON)
option(LOB_BUILD_BENCHMARKS "Build benchmarks" ON)
option(LOB_BUILD_PYTHON_BINDINGS "Build pybind11 module" OFF)
option(LOB_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(LOB_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

add_library(lob_core STATIC
  cpp/src/price_level.cpp
  cpp/src/order_book.cpp
  cpp/src/dense_order_book.cpp
  cpp/src/invariants.cpp
  cpp/src/matching_engine.cpp
  cpp/src/replay.cpp
)
target_include_directories(lob_core PUBLIC ${CMAKE_SOURCE_DIR}/cpp/include)
target_compile_features(lob_core PUBLIC cxx_std_20)

if(LOB_ENABLE_ASAN)
  target_compile_options(lob_core PUBLIC -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(lob_core PUBLIC -fsanitize=address)
endif()

if(LOB_ENABLE_UBSAN)
  target_compile_options(lob_core PUBLIC -fsanitize=undefined -fno-omit-frame-pointer)
  target_link_options(lob_core PUBLIC -fsanitize=undefined)
endif()

if(LOB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(cpp/tests)
endif()

if(LOB_BUILD_BENCHMARKS)
  add_subdirectory(cpp/benchmarks)
endif()

if(LOB_BUILD_PYTHON_BINDINGS)
  add_subdirectory(bindings)
endif()
