cmake_minimum_required(VERSION 3.16)

project(mapweave VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MAPWEAVE_BUILD_FUZZERS "Build local libFuzzer targets with Clang" OFF)

add_library(mapweave STATIC
  src/analyzer.cc
  src/catalog.cc
  src/checksum.cc
  src/coverage.cc
  src/geometry.cc
  src/manifest.cc
  src/package.cc
  src/raster.cc
  src/reader.cc
  src/result.cc
  src/style.cc
  src/update.cc
)

target_include_directories(mapweave PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(mapweave PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(mapweave PRIVATE /W4 /permissive-)
else()
  target_compile_options(mapweave PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(mw_dump tools/mw_dump.cc)
target_link_libraries(mw_dump PRIVATE mapweave)

add_executable(mapweave_tests tests/unit_tests.cc)
target_link_libraries(mapweave_tests PRIVATE mapweave)

enable_testing()
add_test(NAME mapweave_tests COMMAND mapweave_tests)

if(MAPWEAVE_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "MAPWEAVE_BUILD_FUZZERS expects Clang")
  endif()

  target_compile_options(mapweave PRIVATE -fsanitize=fuzzer-no-link,address,undefined)

  add_executable(mapweave_package_fuzzer fuzz/mapweave_package_fuzzer.cc)
  target_link_libraries(mapweave_package_fuzzer PRIVATE mapweave)
  target_compile_options(mapweave_package_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(mapweave_package_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)

  add_executable(mapweave_manifest_fuzzer fuzz/mapweave_manifest_fuzzer.cc)
  target_link_libraries(mapweave_manifest_fuzzer PRIVATE mapweave)
  target_compile_options(mapweave_manifest_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(mapweave_manifest_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
