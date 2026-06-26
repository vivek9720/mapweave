#include <cstddef>
#include <cstdint>
#include <string>

#include "mapweave/manifest.h"
#include "mapweave/style.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > 131072) {
    return 0;
  }
  std::string text;
  if (data != nullptr && size != 0) {
    text.assign(reinterpret_cast<const char*>(data), size);
  }
  auto manifest = mapweave::parseManifest(text);
  if (manifest) {
    auto valid = mapweave::validateManifest(manifest.value());
    volatile std::size_t sink = manifest.value().layers.size() +
                                manifest.value().sources.size() +
                                mapweave::countStyleNodes(manifest.value().inline_style) +
                                (valid ? 1u : 0u);
    (void)sink;
  }
  auto style = mapweave::parseStyleSheet(text);
  if (style) {
    volatile std::size_t sink = mapweave::countStyleNodes(style.value());
    (void)sink;
  }
  return 0;
}
