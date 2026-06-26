#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "mapweave/result.h"

namespace mapweave {

struct TileCoord {
  std::uint32_t z = 0;
  std::uint64_t x = 0;
  std::uint64_t y = 0;
};

struct Span {
  std::uint64_t offset = 0;
  std::uint64_t length = 0;
};

struct TileRecord {
  TileCoord coord;
  std::string layer;
  Span span;
  std::uint32_t flags = 0;
  std::set<std::string> tags;
};

struct Catalog {
  std::vector<TileRecord> tiles;
  std::map<std::string, std::size_t> layer_counts;
};

Result<Catalog> parseCatalog(const std::uint8_t* data, std::size_t size);
Result<Catalog> parseCatalogText(const std::string& text);
Result<Catalog> parseCatalogBinary(const std::uint8_t* data, std::size_t size);
std::string tileKey(const TileRecord& record);
std::string formatCatalogSummary(const Catalog& catalog);

}  // namespace mapweave
