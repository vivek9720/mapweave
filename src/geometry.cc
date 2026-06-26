#include "mapweave/geometry.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace mapweave {
namespace {

constexpr double kPi = 3.14159265358979323846264338327950288;
constexpr double kWorldWest = -180.0;
constexpr double kWorldEast = 180.0;

std::string trim(std::string_view input) {
  std::size_t first = 0;
  while (first < input.size() &&
         std::isspace(static_cast<unsigned char>(input[first])) != 0) {
    ++first;
  }
  std::size_t last = input.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(input[last - 1])) != 0) {
    --last;
  }
  return std::string(input.substr(first, last - first));
}

bool parseUnsigned(std::string_view text, std::uint64_t* out) {
  if (text.empty() || out == nullptr) {
    return false;
  }
  std::uint64_t value = 0;
  for (char c : text) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
    const auto digit = static_cast<std::uint64_t>(c - '0');
    if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
      return false;
    }
    value = value * 10 + digit;
  }
  *out = value;
  return true;
}

double lonToTileX(double lon, std::uint32_t z) {
  const double n = static_cast<double>(tileCountAtZoom(z));
  const double normalized = (normalizeLongitude(lon) + 180.0) / 360.0;
  double x = std::floor(normalized * n);
  if (x < 0.0) {
    return 0.0;
  }
  if (x >= n) {
    return n - 1.0;
  }
  return x;
}

double latToTileY(double lat, std::uint32_t z) {
  const double clamped = clampLatitude(lat) * kPi / 180.0;
  const double n = static_cast<double>(tileCountAtZoom(z));
  const double y =
      (1.0 - std::log(std::tan(clamped) + 1.0 / std::cos(clamped)) / kPi) /
      2.0 * n;
  if (y < 0.0) {
    return 0.0;
  }
  if (y >= n) {
    return n - 1.0;
  }
  return std::floor(y);
}

double tileXToLon(std::uint64_t x, std::uint32_t z) {
  const double n = static_cast<double>(tileCountAtZoom(z));
  return static_cast<double>(x) / n * 360.0 - 180.0;
}

double tileYToLat(std::uint64_t y, std::uint32_t z) {
  const double n = static_cast<double>(tileCountAtZoom(z));
  const double mercator = kPi * (1.0 - 2.0 * static_cast<double>(y) / n);
  return std::atan(std::sinh(mercator)) * 180.0 / kPi;
}

std::vector<std::string_view> splitTileSpec(std::string_view text) {
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '/' || text[i] == ',' || text[i] == ':') {
      parts.push_back(text.substr(start, i - start));
      start = i + 1;
    }
  }
  return parts;
}

}  // namespace

double clampLatitude(double latitude) {
  if (!std::isfinite(latitude)) {
    return 0.0;
  }
  return std::max(-kWebMercatorMaxLatitude,
                  std::min(kWebMercatorMaxLatitude, latitude));
}

double normalizeLongitude(double longitude) {
  if (!std::isfinite(longitude)) {
    return 0.0;
  }
  double normalized = std::fmod(longitude + 180.0, 360.0);
  if (normalized < 0.0) {
    normalized += 360.0;
  }
  return normalized - 180.0;
}

std::uint64_t tileCountAtZoom(std::uint32_t z) {
  if (z > kMaxSupportedZoom) {
    return 0;
  }
  return std::uint64_t{1} << z;
}

bool isValidTileCoord(const TileCoord& coord) {
  const std::uint64_t count = tileCountAtZoom(coord.z);
  return count != 0 && coord.x < count && coord.y < count;
}

std::string formatTileCoord(const TileCoord& coord) {
  std::ostringstream out;
  out << coord.z << '/' << coord.x << '/' << coord.y;
  return out.str();
}

Result<TileCoord> parseTileSpec(std::string_view text) {
  const std::string cleaned = trim(text);
  const auto parts = splitTileSpec(cleaned);
  if (parts.size() != 3) {
    return Result<TileCoord>::failure(
        makeError(ErrorCode::kInvalidSyntax, "tile spec must be z/x/y", 0));
  }

  std::uint64_t z = 0;
  std::uint64_t x = 0;
  std::uint64_t y = 0;
  if (!parseUnsigned(trim(parts[0]), &z) || !parseUnsigned(trim(parts[1]), &x) ||
      !parseUnsigned(trim(parts[2]), &y) || z > kMaxSupportedZoom) {
    return Result<TileCoord>::failure(
        makeError(ErrorCode::kInvalidSyntax, "invalid tile spec numeric field", 0));
  }

  TileCoord coord{static_cast<std::uint32_t>(z), x, y};
  if (!isValidTileCoord(coord)) {
    return Result<TileCoord>::failure(
        makeError(ErrorCode::kLimitExceeded, "tile coordinate out of range", 0));
  }
  return Result<TileCoord>::success(coord);
}

Result<std::string> encodeQuadKey(const TileCoord& coord) {
  if (!isValidTileCoord(coord)) {
    return Result<std::string>::failure(
        makeError(ErrorCode::kLimitExceeded, "cannot encode invalid tile coordinate", 0));
  }
  std::string out;
  out.reserve(coord.z);
  for (std::int32_t bit = static_cast<std::int32_t>(coord.z) - 1; bit >= 0; --bit) {
    const std::uint64_t mask = std::uint64_t{1} << bit;
    char digit = '0';
    if ((coord.x & mask) != 0) {
      digit = static_cast<char>(digit + 1);
    }
    if ((coord.y & mask) != 0) {
      digit = static_cast<char>(digit + 2);
    }
    out.push_back(digit);
  }
  return Result<std::string>::success(std::move(out));
}

Result<TileCoord> decodeQuadKey(std::string_view quadkey) {
  if (quadkey.size() > kMaxSupportedZoom) {
    return Result<TileCoord>::failure(
        makeError(ErrorCode::kLimitExceeded, "quadkey zoom too deep", 0));
  }
  TileCoord coord{static_cast<std::uint32_t>(quadkey.size()), 0, 0};
  for (std::size_t i = 0; i < quadkey.size(); ++i) {
    const std::uint32_t bit =
        static_cast<std::uint32_t>(quadkey.size() - i - 1);
    switch (quadkey[i]) {
      case '0':
        break;
      case '1':
        coord.x |= std::uint64_t{1} << bit;
        break;
      case '2':
        coord.y |= std::uint64_t{1} << bit;
        break;
      case '3':
        coord.x |= std::uint64_t{1} << bit;
        coord.y |= std::uint64_t{1} << bit;
        break;
      default:
        return Result<TileCoord>::failure(
            makeError(ErrorCode::kInvalidSyntax, "quadkey contains non-quadtree digit",
                      i + 1));
    }
  }
  return Result<TileCoord>::success(coord);
}

GeoBounds tileBounds(const TileCoord& coord) {
  if (!isValidTileCoord(coord)) {
    return GeoBounds{kWorldWest, -kWebMercatorMaxLatitude, kWorldEast,
                     kWebMercatorMaxLatitude};
  }
  return GeoBounds{tileXToLon(coord.x, coord.z),
                   tileYToLat(coord.y + 1, coord.z),
                   tileXToLon(coord.x + 1, coord.z),
                   tileYToLat(coord.y, coord.z)};
}

GeoPoint tileCenter(const TileCoord& coord) {
  const GeoBounds bounds = tileBounds(coord);
  return GeoPoint{(bounds.west + bounds.east) / 2.0,
                  (bounds.south + bounds.north) / 2.0};
}

bool isValidBounds(const GeoBounds& bounds) {
  return std::isfinite(bounds.west) && std::isfinite(bounds.east) &&
         std::isfinite(bounds.south) && std::isfinite(bounds.north) &&
         bounds.south <= bounds.north && bounds.west <= bounds.east;
}

GeoBounds normalizeBounds(const GeoBounds& bounds) {
  GeoBounds normalized;
  normalized.west = normalizeLongitude(bounds.west);
  normalized.east = normalizeLongitude(bounds.east);
  normalized.south = clampLatitude(bounds.south);
  normalized.north = clampLatitude(bounds.north);
  if (normalized.west > normalized.east) {
    std::swap(normalized.west, normalized.east);
  }
  if (normalized.south > normalized.north) {
    std::swap(normalized.south, normalized.north);
  }
  return normalized;
}

bool boundsIntersect(const GeoBounds& a, const GeoBounds& b) {
  const GeoBounds lhs = normalizeBounds(a);
  const GeoBounds rhs = normalizeBounds(b);
  if (!isValidBounds(lhs) || !isValidBounds(rhs)) {
    return false;
  }
  return lhs.west <= rhs.east && lhs.east >= rhs.west &&
         lhs.south <= rhs.north && lhs.north >= rhs.south;
}

Result<TileRange> tileRangeForBounds(std::uint32_t z, const GeoBounds& bounds) {
  if (z > kMaxSupportedZoom) {
    return Result<TileRange>::failure(
        makeError(ErrorCode::kLimitExceeded, "zoom level is too deep", 0));
  }
  const GeoBounds normalized = normalizeBounds(bounds);
  if (!isValidBounds(normalized)) {
    return Result<TileRange>::failure(
        makeError(ErrorCode::kInvalidSyntax, "invalid geographic bounds", 0));
  }

  const auto min_x = static_cast<std::uint64_t>(lonToTileX(normalized.west, z));
  const auto max_x = static_cast<std::uint64_t>(lonToTileX(normalized.east, z));
  const auto min_y = static_cast<std::uint64_t>(latToTileY(normalized.north, z));
  const auto max_y = static_cast<std::uint64_t>(latToTileY(normalized.south, z));
  TileRange range{z, std::min(min_x, max_x), std::max(min_x, max_x),
                  std::min(min_y, max_y), std::max(min_y, max_y)};
  return Result<TileRange>::success(range);
}

bool isValidTileRange(const TileRange& range) {
  if (range.z > kMaxSupportedZoom || range.min_x > range.max_x ||
      range.min_y > range.max_y) {
    return false;
  }
  const std::uint64_t count = tileCountAtZoom(range.z);
  return count != 0 && range.max_x < count && range.max_y < count;
}

bool containsTile(const TileRange& range, const TileCoord& coord) {
  return isValidTileRange(range) && coord.z == range.z && coord.x >= range.min_x &&
         coord.x <= range.max_x && coord.y >= range.min_y &&
         coord.y <= range.max_y;
}

bool rangesIntersect(const TileRange& a, const TileRange& b) {
  if (!isValidTileRange(a) || !isValidTileRange(b) || a.z != b.z) {
    return false;
  }
  return a.min_x <= b.max_x && a.max_x >= b.min_x && a.min_y <= b.max_y &&
         a.max_y >= b.min_y;
}

Result<TileRange> intersectRanges(const TileRange& a, const TileRange& b) {
  if (!rangesIntersect(a, b)) {
    return Result<TileRange>::failure(
        makeError(ErrorCode::kInvalidSyntax, "tile ranges do not intersect", 0));
  }
  return Result<TileRange>::success(TileRange{
      a.z,
      std::max(a.min_x, b.min_x),
      std::min(a.max_x, b.max_x),
      std::max(a.min_y, b.min_y),
      std::min(a.max_y, b.max_y),
  });
}

std::vector<TileCoord> enumerateTiles(const TileRange& range, std::size_t cap) {
  std::vector<TileCoord> out;
  if (!isValidTileRange(range) || cap == 0) {
    return out;
  }
  for (std::uint64_t y = range.min_y; y <= range.max_y; ++y) {
    for (std::uint64_t x = range.min_x; x <= range.max_x; ++x) {
      out.push_back(TileCoord{range.z, x, y});
      if (out.size() >= cap) {
        return out;
      }
      if (x == std::numeric_limits<std::uint64_t>::max()) {
        break;
      }
    }
    if (y == std::numeric_limits<std::uint64_t>::max()) {
      break;
    }
  }
  return out;
}

std::uint64_t tileArea(const TileRange& range) {
  if (!isValidTileRange(range)) {
    return 0;
  }
  const std::uint64_t width = range.max_x - range.min_x + 1;
  const std::uint64_t height = range.max_y - range.min_y + 1;
  if (height != 0 && width > std::numeric_limits<std::uint64_t>::max() / height) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return width * height;
}

std::string formatTileRange(const TileRange& range) {
  std::ostringstream out;
  out << "z=" << range.z << " x=" << range.min_x << ".." << range.max_x
      << " y=" << range.min_y << ".." << range.max_y
      << " tiles=" << tileArea(range);
  return out.str();
}

}  // namespace mapweave
