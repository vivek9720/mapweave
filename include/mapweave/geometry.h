#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "mapweave/catalog.h"
#include "mapweave/result.h"

namespace mapweave {

struct GeoPoint {
  double lon = 0.0;
  double lat = 0.0;
};

struct GeoBounds {
  double west = 0.0;
  double south = 0.0;
  double east = 0.0;
  double north = 0.0;
};

struct TileRange {
  std::uint32_t z = 0;
  std::uint64_t min_x = 0;
  std::uint64_t max_x = 0;
  std::uint64_t min_y = 0;
  std::uint64_t max_y = 0;
};

constexpr std::uint32_t kMaxSupportedZoom = 30;
constexpr double kWebMercatorMaxLatitude = 85.05112878;

double clampLatitude(double latitude);
double normalizeLongitude(double longitude);
std::uint64_t tileCountAtZoom(std::uint32_t z);
bool isValidTileCoord(const TileCoord& coord);
std::string formatTileCoord(const TileCoord& coord);
Result<TileCoord> parseTileSpec(std::string_view text);

Result<std::string> encodeQuadKey(const TileCoord& coord);
Result<TileCoord> decodeQuadKey(std::string_view quadkey);

GeoBounds tileBounds(const TileCoord& coord);
GeoPoint tileCenter(const TileCoord& coord);
bool isValidBounds(const GeoBounds& bounds);
GeoBounds normalizeBounds(const GeoBounds& bounds);
bool boundsIntersect(const GeoBounds& a, const GeoBounds& b);

Result<TileRange> tileRangeForBounds(std::uint32_t z, const GeoBounds& bounds);
bool isValidTileRange(const TileRange& range);
bool containsTile(const TileRange& range, const TileCoord& coord);
bool rangesIntersect(const TileRange& a, const TileRange& b);
Result<TileRange> intersectRanges(const TileRange& a, const TileRange& b);
std::vector<TileCoord> enumerateTiles(const TileRange& range, std::size_t cap);
std::uint64_t tileArea(const TileRange& range);
std::string formatTileRange(const TileRange& range);

}  // namespace mapweave
