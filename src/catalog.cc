#include "mapweave/catalog.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "mapweave/reader.h"

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

Result<std::uint64_t> parseU64(const std::string& text, std::size_t offset) {
  try {
    std::size_t consumed = 0;
    unsigned long long value = std::stoull(text, &consumed, 0);
    if (consumed != text.size()) {
      return Result<std::uint64_t>::failure(
          makeError(ErrorCode::kInvalidSyntax, "invalid integer", offset));
    }
    return Result<std::uint64_t>::success(static_cast<std::uint64_t>(value));
  } catch (...) {
    return Result<std::uint64_t>::failure(
        makeError(ErrorCode::kInvalidSyntax, "invalid integer", offset));
  }
}

Result<Span> parseSpan(const std::string& text, std::size_t line_number) {
  const std::size_t colon = text.find(':');
  if (colon == std::string::npos) {
    return Result<Span>::failure(
        makeError(ErrorCode::kInvalidSyntax, "span expects offset:length", line_number));
  }
  auto offset = parseU64(text.substr(0, colon), line_number);
  auto length = parseU64(text.substr(colon + 1), line_number);
  if (!offset) {
    return Result<Span>::failure(offset.error());
  }
  if (!length) {
    return Result<Span>::failure(length.error());
  }
  return Result<Span>::success(Span{offset.value(), length.value()});
}

Result<std::map<std::string, std::string>> attrsFromWords(
    const std::vector<std::string>& words, std::size_t start, std::size_t line_number) {
  std::map<std::string, std::string> attrs;
  for (std::size_t i = start; i < words.size(); ++i) {
    const std::size_t equals = words[i].find('=');
    if (equals == std::string::npos || equals == 0) {
      return Result<std::map<std::string, std::string>>::failure(
          makeError(ErrorCode::kInvalidSyntax, "attribute expects key=value", line_number));
    }
    attrs[words[i].substr(0, equals)] = words[i].substr(equals + 1);
  }
  return Result<std::map<std::string, std::string>>::success(std::move(attrs));
}

void parseTags(const std::string& text, std::set<std::string>& tags) {
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t comma = text.find(',', start);
    const std::string item = comma == std::string::npos
                                 ? text.substr(start)
                                 : text.substr(start, comma - start);
    if (!item.empty()) {
      tags.insert(item);
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
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

Result<TileRecord> recordFromAttributes(const std::map<std::string, std::string>& attrs,
                                        std::size_t line_number) {
  TileRecord record;
  record.coord.z = static_cast<std::uint32_t>(attrU64(attrs, "z", 0));
  record.coord.x = attrU64(attrs, "x", 0);
  record.coord.y = attrU64(attrs, "y", 0);
  auto layer = attrs.find("layer");
  if (layer != attrs.end()) {
    record.layer = layer->second;
  }
  auto span = attrs.find("span");
  if (span != attrs.end()) {
    auto parsed = parseSpan(span->second, line_number);
    if (!parsed) {
      return Result<TileRecord>::failure(parsed.error());
    }
    record.span = parsed.value();
  }
  record.flags = static_cast<std::uint32_t>(attrU64(attrs, "flags", 0));
  auto tags = attrs.find("tags");
  if (tags != attrs.end()) {
    parseTags(tags->second, record.tags);
  }
  if (record.layer.empty()) {
    return Result<TileRecord>::failure(
        makeError(ErrorCode::kInvalidSyntax, "tile missing layer", line_number));
  }
  return Result<TileRecord>::success(std::move(record));
}

}  // namespace

Result<Catalog> parseCatalog(const std::uint8_t* data, std::size_t size) {
  if (data != nullptr && size >= 4 &&
      std::equal(data, data + 4, reinterpret_cast<const std::uint8_t*>("CATB"))) {
    return parseCatalogBinary(data, size);
  }
  std::string text;
  if (data != nullptr && size != 0) {
    text.assign(reinterpret_cast<const char*>(data), size);
  }
  return parseCatalogText(text);
}

Result<Catalog> parseCatalogText(const std::string& text) {
  std::istringstream input(text);
  std::string line;
  std::size_t line_number = 0;
  bool saw_header = false;
  Catalog catalog;

  while (std::getline(input, line)) {
    ++line_number;
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (!saw_header) {
      if (line != "CAT1") {
        return Result<Catalog>::failure(
            makeError(ErrorCode::kInvalidMagic, "catalog missing CAT1", line_number));
      }
      saw_header = true;
      continue;
    }

    auto words = splitWords(line);
    if (words.empty()) {
      continue;
    }
    if (words[0] != "tile") {
      return Result<Catalog>::failure(
          makeError(ErrorCode::kInvalidSyntax, "expected tile record", line_number));
    }
    auto attrs = attrsFromWords(words, 1, line_number);
    if (!attrs) {
      return Result<Catalog>::failure(attrs.error());
    }
    auto record = recordFromAttributes(attrs.value(), line_number);
    if (!record) {
      return Result<Catalog>::failure(record.error());
    }
    ++catalog.layer_counts[record.value().layer];
    catalog.tiles.push_back(record.takeValue());
    if (catalog.tiles.size() > 16384) {
      return Result<Catalog>::failure(
          makeError(ErrorCode::kLimitExceeded, "too many catalog records", line_number));
    }
  }
  if (!saw_header) {
    return Result<Catalog>::failure(
        makeError(ErrorCode::kInvalidMagic, "empty catalog", 0));
  }
  return Result<Catalog>::success(std::move(catalog));
}

Result<Catalog> parseCatalogBinary(const std::uint8_t* data, std::size_t size) {
  ByteReader reader(data, size);
  if (!reader.startsWith("CATB", 4)) {
    return Result<Catalog>::failure(
        makeError(ErrorCode::kInvalidMagic, "binary catalog missing CATB", 0));
  }
  auto skipped = reader.skip(4);
  if (!skipped) {
    return Result<Catalog>::failure(skipped.error());
  }
  auto count = reader.readVarint(16384);
  if (!count) {
    return Result<Catalog>::failure(count.error());
  }

  Catalog catalog;
  for (std::size_t i = 0; i < count.value(); ++i) {
    TileRecord record;
    auto z = reader.readVarint(32);
    auto x = reader.readVarint(1ull << 40);
    auto y = reader.readVarint(1ull << 40);
    auto flags = reader.readVarint(0xffff);
    auto offset = reader.readVarint(1ull << 48);
    auto length = reader.readVarint(1ull << 32);
    auto layer_len = reader.readVarint(512);
    if (!z) return Result<Catalog>::failure(z.error());
    if (!x) return Result<Catalog>::failure(x.error());
    if (!y) return Result<Catalog>::failure(y.error());
    if (!flags) return Result<Catalog>::failure(flags.error());
    if (!offset) return Result<Catalog>::failure(offset.error());
    if (!length) return Result<Catalog>::failure(length.error());
    if (!layer_len) return Result<Catalog>::failure(layer_len.error());
    auto layer = reader.readString(static_cast<std::size_t>(layer_len.value()));
    if (!layer) return Result<Catalog>::failure(layer.error());
    record.coord.z = static_cast<std::uint32_t>(z.value());
    record.coord.x = x.value();
    record.coord.y = y.value();
    record.flags = static_cast<std::uint32_t>(flags.value());
    record.span = Span{offset.value(), length.value()};
    record.layer = layer.takeValue();
    catalog.layer_counts[record.layer]++;
    catalog.tiles.push_back(std::move(record));
  }
  return Result<Catalog>::success(std::move(catalog));
}

std::string tileKey(const TileRecord& record) {
  std::ostringstream out;
  out << record.layer << "/" << record.coord.z << "/" << record.coord.x << "/" << record.coord.y;
  return out.str();
}

std::string formatCatalogSummary(const Catalog& catalog) {
  std::ostringstream out;
  out << "catalog_tiles=" << catalog.tiles.size() << '\n';
  for (const auto& item : catalog.layer_counts) {
    out << "catalog_layer=" << item.first << ":" << item.second << '\n';
  }
  return out.str();
}

}  // namespace mapweave
