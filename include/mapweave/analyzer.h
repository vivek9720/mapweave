#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "mapweave/result.h"

namespace mapweave {

struct AnalysisReport {
  std::size_t sections = 0;
  std::size_t layers = 0;
  std::size_t sources = 0;
  std::size_t style_nodes = 0;
  std::size_t catalog_tiles = 0;
  std::size_t raster_tiles = 0;
  std::size_t raster_bytes = 0;
  std::size_t update_records = 0;
  std::size_t replay_tiles = 0;
  std::size_t coverage_layers = 0;
  std::size_t coverage_ranges = 0;
  std::size_t coverage_warnings = 0;
  std::uint32_t package_checksum = 0;
  std::string notes;
};

Result<AnalysisReport> analyzePackage(const std::uint8_t* data, std::size_t size);
std::string formatReport(const AnalysisReport& report);

}  // namespace mapweave
