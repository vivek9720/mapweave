#include "mapweave/raster.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "mapweave/checksum.h"
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

int hexValue(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
  if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
  return -1;
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

Result<std::map<std::string, std::string>> parseAttributes(const std::string& line,
                                                           std::size_t line_number) {
  std::map<std::string, std::string> attrs;
  std::size_t offset = 0;
  while (offset < line.size()) {
    while (offset < line.size() && std::isspace(static_cast<unsigned char>(line[offset]))) {
      ++offset;
    }
    if (offset >= line.size()) {
      break;
    }
    const std::size_t key_start = offset;
    while (offset < line.size() &&
           (std::isalnum(static_cast<unsigned char>(line[offset])) ||
            line[offset] == '_' || line[offset] == '-')) {
      ++offset;
    }
    if (key_start == offset || offset >= line.size() || line[offset] != '=') {
      return Result<std::map<std::string, std::string>>::failure(
          makeError(ErrorCode::kInvalidSyntax, "raster attribute expects key=value",
                    line_number));
    }
    std::string key = line.substr(key_start, offset - key_start);
    ++offset;
    std::string value;
    if (offset < line.size() && line[offset] == '"') {
      ++offset;
      bool closed = false;
      while (offset < line.size()) {
        const char ch = line[offset++];
        if (ch == '"') {
          closed = true;
          break;
        }
        if (ch == '\\' && offset < line.size()) {
          value.push_back(ch);
          value.push_back(line[offset++]);
        } else {
          value.push_back(ch);
        }
      }
      if (!closed) {
        return Result<std::map<std::string, std::string>>::failure(
            makeError(ErrorCode::kUnexpectedEof, "unterminated raster string", line_number));
      }
    } else {
      const std::size_t value_start = offset;
      while (offset < line.size() && !std::isspace(static_cast<unsigned char>(line[offset]))) {
        ++offset;
      }
      value = line.substr(value_start, offset - value_start);
    }
    attrs[std::move(key)] = std::move(value);
  }
  return Result<std::map<std::string, std::string>>::success(std::move(attrs));
}

std::size_t estimateRleSize(const std::string& encoded) {
  std::size_t count = 0;
  for (std::size_t i = 0; i < encoded.size(); ++i) {
    if (encoded[i] != '\\') {
      ++count;
      continue;
    }
    if (i + 1 >= encoded.size()) {
      ++count;
      continue;
    }
    const char kind = encoded[++i];
    if (kind == 'x' && i + 2 < encoded.size()) {
      i += 2;
      ++count;
    } else if (kind == 'z') {
      while (i + 1 < encoded.size() &&
             std::isdigit(static_cast<unsigned char>(encoded[i + 1]))) {
        ++i;
      }
      ++count;
    } else {
      ++count;
    }
  }
  return count;
}

Result<std::vector<std::uint8_t>> decodeRawPayload(const std::string& encoded,
                                                   std::size_t line_number) {
  std::vector<std::uint8_t> out;
  out.reserve(encoded.size());
  for (std::size_t i = 0; i < encoded.size(); ++i) {
    const char ch = encoded[i];
    if (ch != '\\') {
      out.push_back(static_cast<std::uint8_t>(ch));
      continue;
    }
    if (i + 1 >= encoded.size()) {
      out.push_back('\\');
      continue;
    }
    const char kind = encoded[++i];
    if (kind == 'n') {
      out.push_back('\n');
    } else if (kind == 't') {
      out.push_back('\t');
    } else if (kind == 'x') {
      if (i + 2 >= encoded.size()) {
        return Result<std::vector<std::uint8_t>>::failure(
            makeError(ErrorCode::kInvalidSyntax, "short hex escape", line_number));
      }
      const int hi = hexValue(encoded[i + 1]);
      const int lo = hexValue(encoded[i + 2]);
      if (hi < 0 || lo < 0) {
        return Result<std::vector<std::uint8_t>>::failure(
            makeError(ErrorCode::kInvalidSyntax, "invalid hex escape", line_number));
      }
      out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
      i += 2;
    } else {
      out.push_back(static_cast<std::uint8_t>(kind));
    }
  }
  return Result<std::vector<std::uint8_t>>::success(std::move(out));
}

Result<std::vector<std::uint8_t>> decodeRlePayload(const std::string& encoded,
                                                   std::size_t line_number) {
  const std::size_t estimated = estimateRleSize(encoded);
  std::vector<std::uint8_t> out(estimated + 1);
  std::size_t cursor = 0;

  for (std::size_t i = 0; i < encoded.size(); ++i) {
    const char ch = encoded[i];
    if (ch != '\\') {
      out[cursor++] = static_cast<std::uint8_t>(ch);
      continue;
    }
    if (i + 1 >= encoded.size()) {
      out[cursor++] = '\\';
      continue;
    }
    const char kind = encoded[++i];
    if (kind == 'x') {
      if (i + 2 >= encoded.size()) {
        return Result<std::vector<std::uint8_t>>::failure(
            makeError(ErrorCode::kInvalidSyntax, "short RLE hex escape", line_number));
      }
      const int hi = hexValue(encoded[i + 1]);
      const int lo = hexValue(encoded[i + 2]);
      if (hi < 0 || lo < 0) {
        return Result<std::vector<std::uint8_t>>::failure(
            makeError(ErrorCode::kInvalidSyntax, "invalid RLE hex escape", line_number));
      }
      out[cursor++] = static_cast<std::uint8_t>((hi << 4) | lo);
      i += 2;
    } else if (kind == 'z') {
      const std::size_t start = i + 1;
      while (i + 1 < encoded.size() &&
             std::isdigit(static_cast<unsigned char>(encoded[i + 1]))) {
        ++i;
      }
      auto repeat = parseU64(encoded.substr(start, i - start + 1), line_number);
      if (!repeat) {
        return Result<std::vector<std::uint8_t>>::failure(repeat.error());
      }
      if (repeat.value() > 65536) {
        return Result<std::vector<std::uint8_t>>::failure(
            makeError(ErrorCode::kLimitExceeded, "RLE repeat too large", line_number));
      }
      for (std::uint64_t j = 0; j < repeat.value(); ++j) {
        out[cursor++] = 0;
      }
    } else {
      out[cursor++] = static_cast<std::uint8_t>(kind);
    }
  }
  out.resize(cursor);
  return Result<std::vector<std::uint8_t>>::success(std::move(out));
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

Result<RasterTile> parseTileLine(const std::string& line, std::size_t line_number) {
  const std::string prefix = "tile ";
  if (line.rfind(prefix, 0) != 0) {
    return Result<RasterTile>::failure(
        makeError(ErrorCode::kInvalidSyntax, "expected raster tile record", line_number));
  }
  auto attrs = parseAttributes(line.substr(prefix.size()), line_number);
  if (!attrs) {
    return Result<RasterTile>::failure(attrs.error());
  }
  RasterTile tile;
  tile.coord.z = static_cast<std::uint32_t>(attrU64(attrs.value(), "z", 0));
  tile.coord.x = attrU64(attrs.value(), "x", 0);
  tile.coord.y = attrU64(attrs.value(), "y", 0);
  tile.width = static_cast<std::uint32_t>(attrU64(attrs.value(), "width", 0));
  tile.height = static_cast<std::uint32_t>(attrU64(attrs.value(), "height", 0));
  tile.flags = static_cast<std::uint32_t>(attrU64(attrs.value(), "flags", 0));
  tile.layer = attrs.value()["layer"];
  tile.format = rasterFormatFromName(attrs.value()["format"]);
  if (tile.layer.empty() || tile.format == RasterFormat::kUnknown) {
    return Result<RasterTile>::failure(
        makeError(ErrorCode::kInvalidSyntax, "raster tile missing layer or format", line_number));
  }
  auto decoded = decodeRasterPayload(tile.format, attrs.value()["data"], line_number);
  if (!decoded) {
    return Result<RasterTile>::failure(decoded.error());
  }
  tile.pixels = decoded.takeValue();
  tile.digest = checksum32(tile.pixels);
  return Result<RasterTile>::success(std::move(tile));
}

}  // namespace

const char* rasterFormatName(RasterFormat format) {
  switch (format) {
    case RasterFormat::kRaw:
      return "raw";
    case RasterFormat::kRle:
      return "rle";
    case RasterFormat::kDelta:
      return "delta";
    case RasterFormat::kUnknown:
      return "unknown";
  }
  return "unknown";
}

RasterFormat rasterFormatFromName(const std::string& name) {
  if (name == "raw") return RasterFormat::kRaw;
  if (name == "rle") return RasterFormat::kRle;
  if (name == "delta") return RasterFormat::kDelta;
  return RasterFormat::kUnknown;
}

Result<RasterSet> parseRasterSet(const std::uint8_t* data, std::size_t size) {
  if (data != nullptr && size >= 4 &&
      std::equal(data, data + 4, reinterpret_cast<const std::uint8_t*>("RAB1"))) {
    return parseRasterBinary(data, size);
  }
  std::string text;
  if (data != nullptr && size != 0) {
    text.assign(reinterpret_cast<const char*>(data), size);
  }
  return parseRasterText(text);
}

Result<RasterSet> parseRasterText(const std::string& text) {
  std::istringstream input(text);
  std::string line;
  std::size_t line_number = 0;
  bool saw_header = false;
  RasterSet set;

  while (std::getline(input, line)) {
    ++line_number;
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (!saw_header) {
      if (line != "RAST1") {
        return Result<RasterSet>::failure(
            makeError(ErrorCode::kInvalidMagic, "raster section missing RAST1", line_number));
      }
      saw_header = true;
      continue;
    }
    auto tile = parseTileLine(line, line_number);
    if (!tile) {
      return Result<RasterSet>::failure(tile.error());
    }
    set.decoded_bytes += tile.value().pixels.size();
    set.tiles.push_back(tile.takeValue());
    if (set.tiles.size() > 8192) {
      return Result<RasterSet>::failure(
          makeError(ErrorCode::kLimitExceeded, "too many raster tiles", line_number));
    }
  }
  if (!saw_header) {
    return Result<RasterSet>::failure(
        makeError(ErrorCode::kInvalidMagic, "empty raster section", 0));
  }
  return Result<RasterSet>::success(std::move(set));
}

Result<RasterSet> parseRasterBinary(const std::uint8_t* data, std::size_t size) {
  ByteReader reader(data, size);
  if (!reader.startsWith("RAB1", 4)) {
    return Result<RasterSet>::failure(
        makeError(ErrorCode::kInvalidMagic, "binary raster missing RAB1", 0));
  }
  auto skipped = reader.skip(4);
  if (!skipped) {
    return Result<RasterSet>::failure(skipped.error());
  }
  auto count = reader.readVarint(8192);
  if (!count) {
    return Result<RasterSet>::failure(count.error());
  }
  RasterSet set;
  for (std::size_t i = 0; i < count.value(); ++i) {
    RasterTile tile;
    auto z = reader.readVarint(32);
    auto x = reader.readVarint(1ull << 40);
    auto y = reader.readVarint(1ull << 40);
    auto width = reader.readVarint(65535);
    auto height = reader.readVarint(65535);
    auto format = reader.readU8();
    auto payload_size = reader.readVarint(1 << 20);
    if (!z) return Result<RasterSet>::failure(z.error());
    if (!x) return Result<RasterSet>::failure(x.error());
    if (!y) return Result<RasterSet>::failure(y.error());
    if (!width) return Result<RasterSet>::failure(width.error());
    if (!height) return Result<RasterSet>::failure(height.error());
    if (!format) return Result<RasterSet>::failure(format.error());
    if (!payload_size) return Result<RasterSet>::failure(payload_size.error());
    auto payload = reader.readString(static_cast<std::size_t>(payload_size.value()));
    if (!payload) return Result<RasterSet>::failure(payload.error());
    tile.coord.z = static_cast<std::uint32_t>(z.value());
    tile.coord.x = x.value();
    tile.coord.y = y.value();
    tile.width = static_cast<std::uint32_t>(width.value());
    tile.height = static_cast<std::uint32_t>(height.value());
    tile.format = format.value() == 1 ? RasterFormat::kRaw : RasterFormat::kRle;
    auto decoded = decodeRasterPayload(tile.format, payload.value(), reader.offset());
    if (!decoded) return Result<RasterSet>::failure(decoded.error());
    tile.pixels = decoded.takeValue();
    tile.digest = checksum32(tile.pixels);
    set.decoded_bytes += tile.pixels.size();
    set.tiles.push_back(std::move(tile));
  }
  return Result<RasterSet>::success(std::move(set));
}

Result<std::vector<std::uint8_t>> decodeRasterPayload(RasterFormat format,
                                                      const std::string& encoded,
                                                      std::size_t line_number) {
  if (format == RasterFormat::kRaw || format == RasterFormat::kDelta) {
    return decodeRawPayload(encoded, line_number);
  }
  if (format == RasterFormat::kRle) {
    return decodeRlePayload(encoded, line_number);
  }
  return Result<std::vector<std::uint8_t>>::failure(
      makeError(ErrorCode::kInvalidSyntax, "unknown raster format", line_number));
}

std::string formatRasterSummary(const RasterSet& set) {
  std::ostringstream out;
  out << "raster_tiles=" << set.tiles.size() << '\n';
  out << "raster_bytes=" << set.decoded_bytes << '\n';
  for (const auto& tile : set.tiles) {
    out << "raster_tile=" << tile.layer << ":" << tile.coord.z << ":" << tile.coord.x
        << ":" << tile.coord.y << ":" << rasterFormatName(tile.format) << ":"
        << tile.digest << '\n';
  }
  return out.str();
}

}  // namespace mapweave
