#!/bin/bash -eu

ROOT="${SRC:-$(cd "$(dirname "$0")/.." && pwd)}"
OUT_DIR="${OUT:?OUT must be set}"
CXX_BIN="${CXX:-clang++}"
FUZZ_ENGINE="${LIB_FUZZING_ENGINE:-}"

COMMON_FLAGS=(
  -std=c++17
  -I"${ROOT}/include"
  -Wall
  -Wextra
  -Wpedantic
)

SOURCES=(
  "${ROOT}/src/analyzer.cc"
  "${ROOT}/src/catalog.cc"
  "${ROOT}/src/checksum.cc"
  "${ROOT}/src/coverage.cc"
  "${ROOT}/src/geometry.cc"
  "${ROOT}/src/manifest.cc"
  "${ROOT}/src/package.cc"
  "${ROOT}/src/raster.cc"
  "${ROOT}/src/reader.cc"
  "${ROOT}/src/result.cc"
  "${ROOT}/src/style.cc"
  "${ROOT}/src/update.cc"
)

mkdir -p "${OUT_DIR}"

"${CXX_BIN}" ${CXXFLAGS:-} "${COMMON_FLAGS[@]}" "${SOURCES[@]}" \
  "${ROOT}/fuzz/mapweave_package_fuzzer.cc" ${FUZZ_ENGINE} \
  -o "${OUT_DIR}/mapweave_package_fuzzer"

"${CXX_BIN}" ${CXXFLAGS:-} "${COMMON_FLAGS[@]}" "${SOURCES[@]}" \
  "${ROOT}/fuzz/mapweave_manifest_fuzzer.cc" ${FUZZ_ENGINE} \
  -o "${OUT_DIR}/mapweave_manifest_fuzzer"
