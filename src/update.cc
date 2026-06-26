#include "mapweave/update.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace mapweave {
namespace {

std::string trim(std::string text) {
  auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(),
                                        [&](char ch) { return !is_space(ch); }));
  text.erase(std::find_if(text.rbegin(), text.rend(),
                          [&](char ch) { return !is_space(ch); })
                 .base(),
             text.end());
  return text;
}

std::vector<std::string> splitWords(const std::string& line) {
  std::istringstream stream(line);
  std::vector<std::string> words;
  std::string word;
  while (stream >> word) {
    words.push_back(word);
  }
  return words;
}

Result<std::uint64_t> parseU64(const std::string& text, std::size_t line_number) {
  try {
    std::size_t consumed = 0;
    unsigned long long value = std::stoull(text, &consumed, 0);
    if (consumed != text.size()) {
      return Result<std::uint64_t>::failure(
          makeError(ErrorCode::kInvalidSyntax, "invalid integer", line_number));
    }
    return Result<std::uint64_t>::success(value);
  } catch (...) {
    return Result<std::uint64_t>::failure(
        makeError(ErrorCode::kInvalidSyntax, "invalid integer", line_number));
  }
}

Result<std::map<std::string, std::string>> attrsFromWords(
    const std::vector<std::string>& words, std::size_t start, std::size_t line_number) {
  std::map<std::string, std::string> attrs;
  for (std::size_t i = start; i < words.size(); ++i) {
    const std::size_t equals = words[i].find('=');
    if (equals == std::string::npos || equals == 0) {
      return Result<std::map<std::string, std::string>>::failure(
          makeError(ErrorCode::kInvalidSyntax, "update attribute expects key=value",
                    line_number));
    }
    attrs[words[i].substr(0, equals)] = words[i].substr(equals + 1);
  }
  return Result<std::map<std::string, std::string>>::success(std::move(attrs));
}

std::uint64_t attrU64(const std::map<std::string, std::string>& attrs,
                      const std::string& key, std::uint64_t fallback) {
  auto found = attrs.find(key);
  if (found == attrs.end()) {
    return fallback;
  }
  auto parsed = parseU64(found->second, 0);
  return parsed ? parsed.value() : fallback;
}

TileRecord tileFromAttrs(const std::map<std::string, std::string>& attrs) {
  TileRecord tile;
  tile.coord.z = static_cast<std::uint32_t>(attrU64(attrs, "z", 0));
  tile.coord.x = attrU64(attrs, "x", 0);
  tile.coord.y = attrU64(attrs, "y", 0);
  tile.layer = attrs.count("layer") ? attrs.at("layer") : "";
  tile.flags = static_cast<std::uint32_t>(attrU64(attrs, "flags", 0));
  tile.span.offset = attrU64(attrs, "offset", 0);
  tile.span.length = attrU64(attrs, "length", 0);
  return tile;
}

}  // namespace

const char* updateOpName(UpdateOp op) {
  switch (op) {
    case UpdateOp::kBegin:
      return "begin";
    case UpdateOp::kAddTile:
      return "add";
    case UpdateOp::kRemoveTile:
      return "remove";
    case UpdateOp::kReplaceLayer:
      return "replace-layer";
    case UpdateOp::kCommit:
      return "commit";
    case UpdateOp::kRollback:
      return "rollback";
  }
  return "unknown";
}

Result<UpdateLog> parseUpdateLog(const std::string& text) {
  std::istringstream input(text);
  std::string line;
  std::size_t line_number = 0;
  bool saw_header = false;
  UpdateLog log;

  while (std::getline(input, line)) {
    ++line_number;
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (!saw_header) {
      if (line != "UPD1") {
        return Result<UpdateLog>::failure(
            makeError(ErrorCode::kInvalidMagic, "update log missing UPD1", line_number));
      }
      saw_header = true;
      continue;
    }
    auto words = splitWords(line);
    if (words.empty()) {
      continue;
    }
    UpdateRecord record;
    if (words[0] == "begin") {
      record.op = UpdateOp::kBegin;
      if (words.size() != 2) {
        return Result<UpdateLog>::failure(
            makeError(ErrorCode::kInvalidSyntax, "begin expects txid", line_number));
      }
      auto txid = parseU64(words[1], line_number);
      if (!txid) return Result<UpdateLog>::failure(txid.error());
      record.txid = txid.value();
    } else if (words[0] == "commit") {
      record.op = UpdateOp::kCommit;
      if (words.size() != 2) {
        return Result<UpdateLog>::failure(
            makeError(ErrorCode::kInvalidSyntax, "commit expects txid", line_number));
      }
      auto txid = parseU64(words[1], line_number);
      if (!txid) return Result<UpdateLog>::failure(txid.error());
      record.txid = txid.value();
    } else if (words[0] == "rollback") {
      record.op = UpdateOp::kRollback;
      if (words.size() != 2) {
        return Result<UpdateLog>::failure(
            makeError(ErrorCode::kInvalidSyntax, "rollback expects txid", line_number));
      }
      auto txid = parseU64(words[1], line_number);
      if (!txid) return Result<UpdateLog>::failure(txid.error());
      record.txid = txid.value();
    } else if (words[0] == "add" || words[0] == "remove" || words[0] == "replace-layer") {
      auto attrs = attrsFromWords(words, 1, line_number);
      if (!attrs) return Result<UpdateLog>::failure(attrs.error());
      record.txid = attrU64(attrs.value(), "tx", 0);
      record.tile = tileFromAttrs(attrs.value());
      record.layer = attrs.value().count("layer") ? attrs.value().at("layer") : "";
      if (words[0] == "add") {
        record.op = UpdateOp::kAddTile;
      } else if (words[0] == "remove") {
        record.op = UpdateOp::kRemoveTile;
      } else {
        record.op = UpdateOp::kReplaceLayer;
      }
    } else {
      return Result<UpdateLog>::failure(
          makeError(ErrorCode::kInvalidSyntax, "unknown update record", line_number));
    }
    log.records.push_back(std::move(record));
    if (log.records.size() > 8192) {
      return Result<UpdateLog>::failure(
          makeError(ErrorCode::kLimitExceeded, "too many update records", line_number));
    }
  }

  if (!saw_header) {
    return Result<UpdateLog>::failure(
        makeError(ErrorCode::kInvalidMagic, "empty update log", 0));
  }
  return Result<UpdateLog>::success(std::move(log));
}

Result<CatalogState> replayUpdates(const Catalog& base, const UpdateLog& log) {
  CatalogState state;
  for (const auto& tile : base.tiles) {
    state.tiles[tileKey(tile)] = tile;
  }
  std::map<std::uint64_t, std::vector<UpdateRecord>> pending;

  for (const auto& record : log.records) {
    switch (record.op) {
      case UpdateOp::kBegin:
        state.open_transactions.insert(record.txid);
        break;
      case UpdateOp::kAddTile:
      case UpdateOp::kRemoveTile:
      case UpdateOp::kReplaceLayer:
        if (state.open_transactions.count(record.txid) != 0) {
          pending[record.txid].push_back(record);
        } else {
          if (record.op == UpdateOp::kAddTile) {
            state.tiles[tileKey(record.tile)] = record.tile;
          } else if (record.op == UpdateOp::kRemoveTile) {
            state.tiles.erase(tileKey(record.tile));
          } else {
            for (auto it = state.tiles.begin(); it != state.tiles.end();) {
              if (it->second.layer == record.layer) {
                it = state.tiles.erase(it);
              } else {
                ++it;
              }
            }
          }
          ++state.applied;
        }
        break;
      case UpdateOp::kCommit:
        if (state.open_transactions.erase(record.txid) != 0) {
          for (const auto& item : pending[record.txid]) {
            if (item.op == UpdateOp::kAddTile) {
              state.tiles[tileKey(item.tile)] = item.tile;
            } else if (item.op == UpdateOp::kRemoveTile) {
              state.tiles.erase(tileKey(item.tile));
            } else {
              for (auto it = state.tiles.begin(); it != state.tiles.end();) {
                if (it->second.layer == item.layer) {
                  it = state.tiles.erase(it);
                } else {
                  ++it;
                }
              }
            }
            ++state.applied;
          }
          pending.erase(record.txid);
        }
        break;
      case UpdateOp::kRollback:
        if (state.open_transactions.erase(record.txid) != 0) {
          state.discarded += pending[record.txid].size();
          pending.erase(record.txid);
        }
        break;
    }
  }

  return Result<CatalogState>::success(std::move(state));
}

}  // namespace mapweave
