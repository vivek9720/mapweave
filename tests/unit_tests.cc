#include <cassert>
#include <cstdint>
#include <string>

#include "mapweave/analyzer.h"
#include "mapweave/catalog.h"
#include "mapweave/coverage.h"
#include "mapweave/geometry.h"
#include "mapweave/manifest.h"
#include "mapweave/package.h"
#include "mapweave/raster.h"
#include "mapweave/reader.h"
#include "mapweave/style.h"
#include "mapweave/update.h"

namespace {

std::string completePackage() {
  return R"(MWPK1-TEXT
version 1
meta producer=unit-test
section manifest manifest
MWM1
schema 1
meta region=test
source name=terrain codec=rle scale=1.0
layer id=base type=raster source=terrain srid=3857 priority=3
style-begin
base {
  fill: #336699;
  opacity: 0.75;
  width: (2 + 3) * 4;
}
style-end
.
section tiles catalog
CAT1
tile z=4 x=8 y=9 layer=base span=0:12 flags=1 tags=water,road
tile z=4 x=8 y=10 layer=base span=12:12 flags=0 tags=land
.
section pixels raster
RAST1
tile z=4 x=8 y=9 layer=base format=raw width=2 height=2 data="abcd"
.
section delta update
UPD1
begin 7
add tx=7 z=4 x=9 y=9 layer=base offset=24 length=12 flags=0
commit 7
.
end
)";
}

void testStyle() {
  auto style = mapweave::parseStyleSheet(R"(
base {
  fill: #112233;
  width: (1 + 2) * 3;
  label: "main";
}
)");
  assert(style);
  assert(mapweave::countStyleNodes(style.value()) >= 5);
}

void testManifest() {
  auto manifest = mapweave::parseManifest(R"(MWM1
schema 1
source name=src codec=rle scale=2.0
layer id=roads type=raster source=src srid=3857 priority=4
style-begin
roads { stroke: #ff0000; width: 2; }
style-end
)");
  assert(manifest);
  auto valid = mapweave::validateManifest(manifest.value());
  assert(valid);
  assert(manifest.value().layers.size() == 1);
  assert(mapweave::formatManifestSummary(manifest.value()).find("layers=1") !=
         std::string::npos);
}

void testCatalogAndUpdates() {
  auto catalog = mapweave::parseCatalogText(R"(CAT1
tile z=1 x=2 y=3 layer=base span=0:4 flags=1 tags=a,b
)");
  assert(catalog);
  auto updates = mapweave::parseUpdateLog(R"(UPD1
begin 1
add tx=1 z=1 x=2 y=4 layer=base offset=4 length=4 flags=0
commit 1
)");
  assert(updates);
  auto replay = mapweave::replayUpdates(catalog.value(), updates.value());
  assert(replay);
  assert(replay.value().tiles.size() == 2);
}

void testRaster() {
  auto raster = mapweave::parseRasterText(R"(RAST1
tile z=2 x=4 y=5 layer=base format=raw width=2 height=2 data="ab\x63d"
)");
  assert(raster);
  assert(raster.value().tiles.size() == 1);
  assert(mapweave::stringFromBytes(raster.value().tiles[0].pixels) == "abcd");
}

void testGeometryAndCoverage() {
  auto tile = mapweave::parseTileSpec("4/8/9");
  assert(tile);
  auto quadkey = mapweave::encodeQuadKey(tile.value());
  assert(quadkey);
  auto decoded = mapweave::decodeQuadKey(quadkey.value());
  assert(decoded);
  assert(decoded.value().z == tile.value().z);
  assert(decoded.value().x == tile.value().x);
  assert(decoded.value().y == tile.value().y);

  std::vector<mapweave::TileCoord> coords = {
      {4, 8, 9},
      {4, 9, 9},
      {4, 8, 10},
      {4, 9, 10},
  };
  auto ranges = mapweave::compactTileRanges(coords);
  assert(ranges.size() == 1);
  assert(mapweave::tileArea(ranges[0]) == 4);

  auto manifest = mapweave::parseManifest(R"(MWM1
schema 1
source name=terrain codec=raw scale=1.0
layer id=base type=raster source=terrain srid=3857 priority=1
)");
  assert(manifest);
  auto catalog = mapweave::parseCatalogText(R"(CAT1
tile z=4 x=8 y=9 layer=base span=0:4 flags=1 tags=a,b
tile z=4 x=9 y=9 layer=base span=4:4 flags=0 tags=c
)");
  assert(catalog);
  auto raster = mapweave::parseRasterText(R"(RAST1
tile z=4 x=8 y=9 layer=base format=raw width=1 height=1 data="x"
)");
  assert(raster);

  auto coverage = mapweave::buildCoverageReport(&manifest.value(), &catalog.value(),
                                                &raster.value());
  assert(coverage.layers.size() == 1);
  assert(coverage.total_catalog_records == 2);
  assert(coverage.total_raster_records == 1);
  assert(!coverage.warnings.empty());

  const auto bounds = mapweave::tileBounds(tile.value());
  auto queried = mapweave::queryCatalog(catalog.value(), "base", bounds, 4, 10);
  assert(queried);
  assert(!queried.value().empty());
}

void testPackageAnalysis() {
  const std::string package = completePackage();
  auto report = mapweave::analyzePackage(reinterpret_cast<const std::uint8_t*>(package.data()),
                                         package.size());
  assert(report);
  assert(report.value().sections == 4);
  assert(report.value().layers == 1);
  assert(report.value().sources == 1);
  assert(report.value().catalog_tiles == 2);
  assert(report.value().raster_tiles == 1);
  assert(report.value().update_records == 3);
  assert(report.value().replay_tiles == 3);
  assert(report.value().coverage_layers == 1);
  assert(report.value().coverage_ranges >= 1);
}

}  // namespace

int main() {
  testStyle();
  testManifest();
  testCatalogAndUpdates();
  testRaster();
  testGeometryAndCoverage();
  testPackageAnalysis();
  return 0;
}
