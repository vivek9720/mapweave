#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "mapweave/result.h"
#include "mapweave/style.h"

namespace mapweave {

struct Layer {
  std::string id;
  std::string type;
  std::string source;
  std::uint32_t srid = 0;
  std::int32_t priority = 0;
  std::map<std::string, std::string> attributes;
};

struct Source {
  std::string name;
  std::string codec;
  std::string path_hint;
  double scale = 1.0;
  std::map<std::string, std::string> attributes;
};

struct Manifest {
  std::uint32_t schema = 1;
  std::vector<Layer> layers;
  std::vector<Source> sources;
  StyleSheet inline_style;
  std::map<std::string, std::string> metadata;
};

Result<Manifest> parseManifest(const std::string& text);
Result<void> validateManifest(const Manifest& manifest);
std::string formatManifestSummary(const Manifest& manifest);

}  // namespace mapweave
