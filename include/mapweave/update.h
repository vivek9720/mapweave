#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "mapweave/catalog.h"
#include "mapweave/result.h"

namespace mapweave {

enum class UpdateOp {
  kBegin,
  kAddTile,
  kRemoveTile,
  kReplaceLayer,
  kCommit,
  kRollback,
};

struct UpdateRecord {
  UpdateOp op = UpdateOp::kAddTile;
  std::uint64_t txid = 0;
  TileRecord tile;
  std::string layer;
};

struct UpdateLog {
  std::vector<UpdateRecord> records;
};

struct CatalogState {
  std::map<std::string, TileRecord> tiles;
  std::set<std::uint64_t> open_transactions;
  std::size_t applied = 0;
  std::size_t discarded = 0;
};

Result<UpdateLog> parseUpdateLog(const std::string& text);
Result<CatalogState> replayUpdates(const Catalog& base, const UpdateLog& log);
const char* updateOpName(UpdateOp op);

}  // namespace mapweave
