#include "mapweave/analyzer.h"

#include <sstream>

#include "mapweave/catalog.h"
#include "mapweave/checksum.h"
#include "mapweave/coverage.h"
#include "mapweave/manifest.h"
#include "mapweave/package.h"
#include "mapweave/raster.h"
#include "mapweave/reader.h"
#include "mapweave/update.h"

namespace mapweave {

Result<AnalysisReport> analyzePackage(const std::uint8_t* data, std::size_t size) {
  auto package = parsePackage(data, size);
  if (!package) {
    return Result<AnalysisReport>::failure(package.error());
  }
  auto valid = validatePackage(package.value());
  if (!valid) {
    return Result<AnalysisReport>::failure(valid.error());
  }

  AnalysisReport report;
  report.sections = package.value().sections.size();
  report.package_checksum = checksum32(data, size);
  Manifest manifest_model;
  bool saw_manifest = false;
  Catalog catalog;
  bool saw_catalog = false;
  RasterSet raster_model;
  bool saw_raster = false;
  UpdateLog updates;
  bool saw_updates = false;

  for (const auto& section : package.value().sections) {
    switch (section.kind) {
      case SectionKind::kManifest: {
        auto manifest = parseManifest(stringFromBytes(section.payload));
        if (!manifest) {
          return Result<AnalysisReport>::failure(manifest.error());
        }
        manifest_model = manifest.value();
        saw_manifest = true;
        report.layers += manifest_model.layers.size();
        report.sources += manifest_model.sources.size();
        report.style_nodes += countStyleNodes(manifest_model.inline_style);
        report.package_checksum ^= checksum32(formatManifestSummary(manifest_model));
        break;
      }
      case SectionKind::kCatalog: {
        auto parsed = parseCatalog(section.payload.data(), section.payload.size());
        if (!parsed) {
          return Result<AnalysisReport>::failure(parsed.error());
        }
        catalog = parsed.value();
        saw_catalog = true;
        report.catalog_tiles += catalog.tiles.size();
        report.package_checksum ^= checksum32(formatCatalogSummary(catalog));
        break;
      }
      case SectionKind::kStyle: {
        auto style = parseStyleSheet(stringFromBytes(section.payload));
        if (!style) {
          return Result<AnalysisReport>::failure(style.error());
        }
        report.style_nodes += countStyleNodes(style.value());
        break;
      }
      case SectionKind::kRaster: {
        auto raster = parseRasterSet(section.payload.data(), section.payload.size());
        if (!raster) {
          return Result<AnalysisReport>::failure(raster.error());
        }
        raster_model = raster.value();
        saw_raster = true;
        report.raster_tiles += raster_model.tiles.size();
        report.raster_bytes += raster_model.decoded_bytes;
        report.package_checksum ^= checksum32(formatRasterSummary(raster_model));
        break;
      }
      case SectionKind::kUpdate: {
        auto log = parseUpdateLog(stringFromBytes(section.payload));
        if (!log) {
          return Result<AnalysisReport>::failure(log.error());
        }
        updates = log.value();
        saw_updates = true;
        report.update_records += updates.records.size();
        break;
      }
      case SectionKind::kNotes:
        report.notes += stringFromBytes(section.payload);
        break;
      case SectionKind::kUnknown:
        return Result<AnalysisReport>::failure(
            makeError(ErrorCode::kUnknownSection, "unknown section during analysis", 0));
    }
  }

  if (saw_catalog && saw_updates) {
    auto state = replayUpdates(catalog, updates);
    if (!state) {
      return Result<AnalysisReport>::failure(state.error());
    }
    report.replay_tiles = state.value().tiles.size();
  } else if (saw_catalog) {
    report.replay_tiles = catalog.tiles.size();
  }

  if (saw_manifest || saw_catalog || saw_raster) {
    const auto coverage = buildCoverageReport(saw_manifest ? &manifest_model : nullptr,
                                             saw_catalog ? &catalog : nullptr,
                                             saw_raster ? &raster_model : nullptr);
    report.coverage_layers = coverage.layers.size();
    report.coverage_warnings = coverage.warnings.size();
    for (const auto& layer : coverage.layers) {
      report.coverage_ranges += layer.ranges.size();
    }
    report.package_checksum ^= checksum32(formatCoverageReport(coverage));
  }

  return Result<AnalysisReport>::success(std::move(report));
}

std::string formatReport(const AnalysisReport& report) {
  std::ostringstream out;
  out << "sections=" << report.sections << '\n';
  out << "layers=" << report.layers << '\n';
  out << "sources=" << report.sources << '\n';
  out << "style_nodes=" << report.style_nodes << '\n';
  out << "catalog_tiles=" << report.catalog_tiles << '\n';
  out << "raster_tiles=" << report.raster_tiles << '\n';
  out << "raster_bytes=" << report.raster_bytes << '\n';
  out << "update_records=" << report.update_records << '\n';
  out << "replay_tiles=" << report.replay_tiles << '\n';
  out << "coverage_layers=" << report.coverage_layers << '\n';
  out << "coverage_ranges=" << report.coverage_ranges << '\n';
  out << "coverage_warnings=" << report.coverage_warnings << '\n';
  out << "package_checksum=" << report.package_checksum << '\n';
  if (!report.notes.empty()) {
    out << "notes=" << report.notes << '\n';
  }
  return out.str();
}

}  // namespace mapweave
