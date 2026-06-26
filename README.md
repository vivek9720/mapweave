# MapWeave

MapWeave is a C++17 library and command-line tool for inspecting offline map tile packages. It parses a deterministic package envelope containing a manifest, tile catalog, style rules, raster payload records, and update records. The project is dependency-free and intended for private Fenrir submission after human review.

The code does not perform network access, does not load external include files, does not prompt interactively, and does not require credentials or local absolute paths.

## Project Overview

MapWeave models an offline data bundle used by mapping tools:

- Package parser: reads a `MWPK1-TEXT` envelope with metadata and typed sections.
- Manifest parser: reads layers, sources, projection metadata, and style references.
- Style parser: handles selectors, nested rule blocks, quoted strings, numbers, colors, and expressions.
- Catalog decoder: reads tile records, spans, flags, tags, and layer references.
- Raster decoder: reads raster payload records and metadata-only compression flags.
- Update replay: applies transaction-like add/remove/replace records to the catalog state.
- Tile geometry and coverage index: computes Web Mercator tile ranges, quadkeys, and layer coverage summaries.
- Analyzer: connects all components and emits a deterministic report.

## Layout

```text
mapweave/
  CMakeLists.txt
  README.md
  include/mapweave/
  src/
  tests/
  tools/
  fuzz/
    mapweave_package_fuzzer.cc
    mapweave_manifest_fuzzer.cc
    corpus/
    dictionary.txt
  .clusterfuzzlite/
    build.sh
    project.yaml
```

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

## CLI

```bash
./build/mw_dump fuzz/corpus/mapweave_package_fuzzer/complete_package.mwp
```

## Fuzzing

ClusterFuzzLite uses `.clusterfuzzlite/build.sh` and writes these binaries into `$OUT`:

- `mapweave_package_fuzzer`
- `mapweave_manifest_fuzzer`

Local ClusterFuzzLite-style build:

```bash
mkdir -p out
SRC="$PWD" OUT="$PWD/out" CXX=clang++ CXXFLAGS="-O1 -g -fsanitize=fuzzer-no-link,address,undefined" LIB_FUZZING_ENGINE="-fsanitize=fuzzer" ./.clusterfuzzlite/build.sh
```

Run locally:

```bash
./out/mapweave_package_fuzzer fuzz/corpus/mapweave_package_fuzzer -dict=fuzz/dictionary.txt -runs=1000
./out/mapweave_manifest_fuzzer fuzz/corpus/mapweave_manifest_fuzzer -dict=fuzz/dictionary.txt -runs=1000
```

## Seed Corpus

The recognized seed corpus lives under `fuzz/corpus/`.

- `fuzz/corpus/mapweave_package_fuzzer/` contains complete package envelopes.
- `fuzz/corpus/mapweave_manifest_fuzzer/` contains standalone manifest/style inputs.

The dictionary contains package magic strings, section names, tile and raster keywords, style delimiters, update tokens, and common attributes.

## Fenrir Readiness

- [x] `.clusterfuzzlite/build.sh` exists at the repository root.
- [x] Build writes every harness executable to `$OUT`.
- [x] Build uses `$SRC` or repository-relative paths.
- [x] Build is deterministic and non-interactive.
- [x] No network access, credentials, prompts, or local absolute paths are required.
- [x] Connected fuzzing harnesses exercise real project code.
- [x] Recognized seed corpus and dictionary are included.
- [x] Unit tests and a CLI tool are included.
- [x] C/C++ source footprint is intentionally above the prior minimum-substrate floor.

## Manual Review Before Submission

- Confirm the final GitHub repository is private.
- Confirm it is not copied from or forked from a public repository.
- Build and test from a clean checkout.
- Run both fuzz targets locally with sanitizers.
- Review corpus files and dictionary entries.
- Confirm `.clusterfuzzlite/build.sh` is executable after commit.
- Verify no accidental credentials, generated logs, absolute local paths, or private notes were committed.
- Review the project manually and keep the final repository primarily human-written.
