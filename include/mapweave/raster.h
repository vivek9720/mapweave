#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "mapweave/catalog.h"
#include "mapweave/result.h"

namespace mapweave {

enum class RasterFormat {
  kRaw,
  kRle,
  kDelta,
  kUnknown,
};

struct RasterTile {
  TileCoord coord;
  std::string layer;
  RasterFormat format = RasterFormat::kUnknown;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t flags = 0;
  std::vector<std::uint8_t> pixels;
  std::uint32_t digest = 0;
};

struct RasterSet {
  std::vector<RasterTile> tiles;
  std::size_t decoded_bytes = 0;
};

const char* rasterFormatName(RasterFormat format);
RasterFormat rasterFormatFromName(const std::string& name);

Result<RasterSet> parseRasterSet(const std::uint8_t* data, std::size_t size);
Result<RasterSet> parseRasterText(const std::string& text);
Result<RasterSet> parseRasterBinary(const std::uint8_t* data, std::size_t size);
Result<std::vector<std::uint8_t>> decodeRasterPayload(RasterFormat format,
                                                      const std::string& encoded,
                                                      std::size_t line_number);
std::string formatRasterSummary(const RasterSet& set);

}  // namespace mapweave
