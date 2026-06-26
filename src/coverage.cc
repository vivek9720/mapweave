#include "mapweave/coverage.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace mapweave {
namespace {

struct LayerAccumulator {
  LayerCoverage coverage;
  std::vector<TileCoord> coords;
};

LayerAccumulator& layerFor(std::map<std::string, LayerAccumulator>* layers,
                           const std::string& name) {
  auto inserted = layers->emplace(name, LayerAccumulator{});
  inserted.first->second.coverage.layer = name;
  return inserted.first->second;
}

bool coordLess(const TileCoord& a, const TileCoord& b) {
  if (a.z != b.z) {
    return a.z < b.z;
  }
  if (a.y != b.y) {
    return a.y < b.y;
  }
  return a.x < b.x;
}

void appendManifestLayer(std::map<std::string, LayerAccumulator>* layers,
                         const Layer& layer) {
  LayerAccumulator& accum = layerFor(layers, layer.id);
  accum.coverage.attributes = layer.attributes;
  accum.coverage.attributes.emplace("type", layer.type);
  accum.coverage.attributes.emplace("source", layer.source);
  accum.coverage.attributes.emplace("srid", std::to_string(layer.srid));
  accum.coverage.attributes.emplace("priority", std::to_string(layer.priority));
}

void appendCatalogTile(std::map<std::string, LayerAccumulator>* layers,
                       const TileRecord& record) {
  LayerAccumulator& accum = layerFor(layers, record.layer);
  accum.coverage.catalog_records += 1;
  accum.coverage.zooms.insert(record.coord.z);
  if (isValidTileCoord(record.coord)) {
    accum.coords.push_back(record.coord);
  }
}

void appendRasterTile(std::map<std::string, LayerAccumulator>* layers,
                      const RasterTile& tile) {
  LayerAccumulator& accum = layerFor(layers, tile.layer);
  accum.coverage.raster_records += 1;
  accum.coverage.zooms.insert(tile.coord.z);
  if (isValidTileCoord(tile.coord)) {
    accum.coords.push_back(tile.coord);
  }
}

void addWarningForLayer(CoverageReport* report, const LayerCoverage& layer) {
  if (layer.catalog_records == 0 && layer.raster_records == 0) {
    report->warnings.push_back("layer '" + layer.layer +
                               "' is declared but has no tile records");
  }
  if (layer.catalog_records != 0 && layer.raster_records != 0 &&
      layer.catalog_records != layer.raster_records) {
    report->warnings.push_back("layer '" + layer.layer +
                               "' has different catalog and raster record counts");
  }
}

}  // namespace

std::vector<TileRange> compactTileRanges(const std::vector<TileCoord>& coords) {
  if (coords.empty()) {
    return {};
  }

  std::vector<TileCoord> sorted;
  sorted.reserve(coords.size());
  for (const TileCoord& coord : coords) {
    if (isValidTileCoord(coord)) {
      sorted.push_back(coord);
    }
  }
  std::sort(sorted.begin(), sorted.end(), coordLess);
  sorted.erase(std::unique(sorted.begin(), sorted.end(),
                           [](const TileCoord& a, const TileCoord& b) {
                             return a.z == b.z && a.x == b.x && a.y == b.y;
                           }),
               sorted.end());

  std::vector<TileRange> row_runs;
  for (std::size_t i = 0; i < sorted.size();) {
    TileRange run{sorted[i].z, sorted[i].x, sorted[i].x, sorted[i].y,
                  sorted[i].y};
    ++i;
    while (i < sorted.size() && sorted[i].z == run.z && sorted[i].y == run.min_y &&
           sorted[i].x == run.max_x + 1) {
      run.max_x = sorted[i].x;
      ++i;
    }
    row_runs.push_back(run);
  }

  std::vector<TileRange> compacted;
  for (const TileRange& run : row_runs) {
    bool merged = false;
    for (TileRange& existing : compacted) {
      if (existing.z == run.z && existing.min_x == run.min_x &&
          existing.max_x == run.max_x && existing.max_y + 1 == run.min_y) {
        existing.max_y = run.max_y;
        merged = true;
        break;
      }
    }
    if (!merged) {
      compacted.push_back(run);
    }
  }
  std::sort(compacted.begin(), compacted.end(),
            [](const TileRange& a, const TileRange& b) {
              if (a.z != b.z) {
                return a.z < b.z;
              }
              if (a.min_y != b.min_y) {
                return a.min_y < b.min_y;
              }
              return a.min_x < b.min_x;
            });
  return compacted;
}

CoverageReport buildCoverageReport(const Manifest* manifest,
                                   const Catalog* catalog,
                                   const RasterSet* rasters) {
  std::map<std::string, LayerAccumulator> layers;
  CoverageReport report;

  if (manifest != nullptr) {
    report.manifest_layers = manifest->layers.size();
    for (const Layer& layer : manifest->layers) {
      appendManifestLayer(&layers, layer);
    }
  }
  if (catalog != nullptr) {
    report.total_catalog_records = catalog->tiles.size();
    for (const TileRecord& tile : catalog->tiles) {
      appendCatalogTile(&layers, tile);
    }
  }
  if (rasters != nullptr) {
    report.total_raster_records = rasters->tiles.size();
    for (const RasterTile& tile : rasters->tiles) {
      appendRasterTile(&layers, tile);
    }
  }

  for (auto& entry : layers) {
    LayerCoverage coverage = std::move(entry.second.coverage);
    coverage.ranges = compactTileRanges(entry.second.coords);
    coverage.covered_tiles = 0;
    for (const TileRange& range : coverage.ranges) {
      coverage.covered_tiles += tileArea(range);
    }
    report.total_covered_tiles += coverage.covered_tiles;
    addWarningForLayer(&report, coverage);
    report.layers.push_back(std::move(coverage));
  }
  return report;
}

Result<std::vector<TileRecord>> queryCatalog(const Catalog& catalog,
                                             const std::string& layer,
                                             const GeoBounds& bounds,
                                             std::uint32_t zoom,
                                             std::size_t limit) {
  auto range = tileRangeForBounds(zoom, bounds);
  if (!range) {
    return Result<std::vector<TileRecord>>::failure(range.error());
  }

  std::vector<TileRecord> out;
  for (const TileRecord& record : catalog.tiles) {
    if (record.coord.z != zoom) {
      continue;
    }
    if (!layer.empty() && record.layer != layer) {
      continue;
    }
    if (containsTile(range.value(), record.coord)) {
      out.push_back(record);
      if (limit != 0 && out.size() >= limit) {
        break;
      }
    }
  }
  return Result<std::vector<TileRecord>>::success(std::move(out));
}

std::string formatCoverageReport(const CoverageReport& report) {
  std::ostringstream out;
  out << "manifest_layers=" << report.manifest_layers << '\n';
  out << "coverage_layers=" << report.layers.size() << '\n';
  out << "catalog_records=" << report.total_catalog_records << '\n';
  out << "raster_records=" << report.total_raster_records << '\n';
  out << "covered_tiles=" << report.total_covered_tiles << '\n';
  for (const LayerCoverage& layer : report.layers) {
    out << "layer " << layer.layer << " catalog=" << layer.catalog_records
        << " raster=" << layer.raster_records
        << " ranges=" << layer.ranges.size() << " zooms=";
    bool first = true;
    for (std::uint32_t z : layer.zooms) {
      if (!first) {
        out << ',';
      }
      first = false;
      out << z;
    }
    out << '\n';
    for (const TileRange& range : layer.ranges) {
      out << "  " << formatTileRange(range) << '\n';
    }
  }
  for (const std::string& warning : report.warnings) {
    out << "warning=" << warning << '\n';
  }
  return out.str();
}

}  // namespace mapweave
