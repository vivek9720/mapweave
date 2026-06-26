#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "mapweave/result.h"

namespace mapweave {

enum class SectionKind {
  kManifest,
  kCatalog,
  kStyle,
  kRaster,
  kUpdate,
  kNotes,
  kUnknown,
};

struct Section {
  std::string name;
  SectionKind kind = SectionKind::kUnknown;
  std::uint32_t flags = 0;
  std::uint32_t checksum = 0;
  std::vector<std::uint8_t> payload;
};

struct Package {
  std::uint16_t version = 1;
  std::map<std::string, std::string> metadata;
  std::vector<Section> sections;
};

struct PackageLimits {
  std::size_t max_sections = 64;
  std::size_t max_payload_size = 1 << 20;
  std::size_t max_text_lines = 8192;
};

const char* sectionKindName(SectionKind kind);
SectionKind sectionKindFromName(const std::string& name);

Result<Package> parsePackage(const std::uint8_t* data, std::size_t size,
                             PackageLimits limits = PackageLimits{});
Result<Package> parseTextPackage(const std::uint8_t* data, std::size_t size,
                                 PackageLimits limits = PackageLimits{});
Result<Package> parseBinaryPackage(const std::uint8_t* data, std::size_t size,
                                   PackageLimits limits = PackageLimits{});
Result<void> validatePackage(const Package& package);

}  // namespace mapweave
