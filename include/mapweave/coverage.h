#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "mapweave/catalog.h"
#include "mapweave/geometry.h"
#include "mapweave/manifest.h"
#include "mapweave/raster.h"
#include "mapweave/result.h"

namespace mapweave {

struct LayerCoverage {
  std::string layer;
  std::set<std::uint32_t> zooms;
  std::vector<TileRange> ranges;
  std::size_t catalog_records = 0;
  std::size_t raster_records = 0;
  std::uint64_t covered_tiles = 0;
  std::map<std::string, std::string> attributes;
};

struct CoverageReport {
  std::vector<LayerCoverage> layers;
  std::size_t manifest_layers = 0;
  std::size_t total_catalog_records = 0;
  std::size_t total_raster_records = 0;
  std::uint64_t total_covered_tiles = 0;
  std::vector<std::string> warnings;
};

CoverageReport buildCoverageReport(const Manifest* manifest,
                                   const Catalog* catalog,
                                   const RasterSet* rasters);

Result<std::vector<TileRecord>> queryCatalog(const Catalog& catalog,
                                             const std::string& layer,
                                             const GeoBounds& bounds,
                                             std::uint32_t zoom,
                                             std::size_t limit);

std::vector<TileRange> compactTileRanges(const std::vector<TileCoord>& coords);
std::string formatCoverageReport(const CoverageReport& report);

}  // namespace mapweave
