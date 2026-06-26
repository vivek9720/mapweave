#include <cstddef>
#include <cstdint>
#include <string>

#include "mapweave/analyzer.h"
#include "mapweave/catalog.h"
#include "mapweave/manifest.h"
#include "mapweave/package.h"
#include "mapweave/raster.h"
#include "mapweave/update.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  auto report = mapweave::analyzePackage(data, size);
  if (report) {
    volatile std::size_t sink = report.value().sections + report.value().layers +
                                report.value().catalog_tiles + report.value().raster_bytes;
    (void)sink;
  }

  if (size <= 65536) {
    std::string text;
    if (data != nullptr && size != 0) {
      text.assign(reinterpret_cast<const char*>(data), size);
    }
    auto manifest = mapweave::parseManifest(text);
    if (manifest) {
      volatile std::size_t count = manifest.value().layers.size() +
                                   mapweave::countStyleNodes(manifest.value().inline_style);
      (void)count;
    }
    auto catalog = mapweave::parseCatalog(data, size);
    if (catalog) {
      volatile std::size_t count = catalog.value().tiles.size();
      (void)count;
    }
    auto raster = mapweave::parseRasterSet(data, size);
    if (raster) {
      volatile std::size_t count = raster.value().decoded_bytes;
      (void)count;
    }
    auto updates = mapweave::parseUpdateLog(text);
    if (updates && catalog) {
      auto state = mapweave::replayUpdates(catalog.value(), updates.value());
      if (state) {
        volatile std::size_t count = state.value().tiles.size();
        (void)count;
      }
    }
  }
  return 0;
}
