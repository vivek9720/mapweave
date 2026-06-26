#include "mapweave/package.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "mapweave/checksum.h"
#include "mapweave/reader.h"

namespace mapweave {
namespace {

struct BinaryHeader {
  std::string name;
  SectionKind kind = SectionKind::kUnknown;
  std::uint32_t flags = 0;
  std::uint32_t length = 0;
  std::uint32_t checksum = 0;
};

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

Result<std::uint32_t> parseU32(const std::string& text, std::size_t offset) {
  try {
    std::size_t consumed = 0;
    unsigned long value = std::stoul(text, &consumed, 0);
    if (consumed != text.size() || value > 0xffffffffUL) {
      return Result<std::uint32_t>::failure(
          makeError(ErrorCode::kInvalidSyntax, "invalid integer", offset));
    }
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(value));
  } catch (...) {
    return Result<std::uint32_t>::failure(
        makeError(ErrorCode::kInvalidSyntax, "invalid integer", offset));
  }
}

Result<void> parseMetadata(Package& package, const std::string& text,
                           std::size_t line_number) {
  const std::size_t equals = text.find('=');
  if (equals == std::string::npos || equals == 0) {
    return Result<void>::failure(
        makeError(ErrorCode::kInvalidSyntax, "metadata expects key=value", line_number));
  }
  package.metadata[trim(text.substr(0, equals))] = trim(text.substr(equals + 1));
  return Result<void>::success();
}

SectionKind binaryKind(std::uint8_t value) {
  switch (value) {
    case 1:
      return SectionKind::kManifest;
    case 2:
      return SectionKind::kCatalog;
    case 3:
      return SectionKind::kStyle;
    case 4:
      return SectionKind::kRaster;
    case 5:
      return SectionKind::kUpdate;
    case 6:
      return SectionKind::kNotes;
    default:
      return SectionKind::kUnknown;
  }
}

}  // namespace

const char* sectionKindName(SectionKind kind) {
  switch (kind) {
    case SectionKind::kManifest:
      return "manifest";
    case SectionKind::kCatalog:
      return "catalog";
    case SectionKind::kStyle:
      return "style";
    case SectionKind::kRaster:
      return "raster";
    case SectionKind::kUpdate:
      return "update";
    case SectionKind::kNotes:
      return "notes";
    case SectionKind::kUnknown:
      return "unknown";
  }
  return "unknown";
}

SectionKind sectionKindFromName(const std::string& name) {
  std::string lowered = name;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (lowered == "manifest" || lowered == "mwm1") {
    return SectionKind::kManifest;
  }
  if (lowered == "catalog" || lowered == "cat1") {
    return SectionKind::kCatalog;
  }
  if (lowered == "style" || lowered == "style1") {
    return SectionKind::kStyle;
  }
  if (lowered == "raster" || lowered == "rast1") {
    return SectionKind::kRaster;
  }
  if (lowered == "update" || lowered == "upd1") {
    return SectionKind::kUpdate;
  }
  if (lowered == "notes") {
    return SectionKind::kNotes;
  }
  return SectionKind::kUnknown;
}

Result<Package> parsePackage(const std::uint8_t* data, std::size_t size,
                             PackageLimits limits) {
  if (data != nullptr && size >= 4 &&
      std::equal(data, data + 4, reinterpret_cast<const std::uint8_t*>("MWPB"))) {
    return parseBinaryPackage(data, size, limits);
  }
  return parseTextPackage(data, size, limits);
}

Result<Package> parseBinaryPackage(const std::uint8_t* data, std::size_t size,
                                   PackageLimits limits) {
  ByteReader reader(data, size);
  if (!reader.startsWith("MWPB", 4)) {
    return Result<Package>::failure(
        makeError(ErrorCode::kInvalidMagic, "binary package missing MWPB", 0));
  }
  auto skipped = reader.skip(4);
  if (!skipped) {
    return Result<Package>::failure(skipped.error());
  }
  auto version = reader.readLe16();
  auto count = reader.readLe16();
  if (!version) {
    return Result<Package>::failure(version.error());
  }
  if (!count) {
    return Result<Package>::failure(count.error());
  }
  if (version.value() == 0 || version.value() > 2) {
    return Result<Package>::failure(
        makeError(ErrorCode::kUnsupportedVersion, "unsupported package version", 4));
  }
  if (count.value() > limits.max_sections) {
    return Result<Package>::failure(
        makeError(ErrorCode::kLimitExceeded, "too many sections", reader.offset()));
  }

  std::vector<BinaryHeader> headers;
  for (std::size_t i = 0; i < count.value(); ++i) {
    auto name_len = reader.readU8();
    if (!name_len) {
      return Result<Package>::failure(name_len.error());
    }
    auto name = reader.readString(name_len.value());
    auto kind = reader.readU8();
    auto flags = reader.readLe16();
    auto length = reader.readLe32();
    auto expected = reader.readLe32();
    if (!name) {
      return Result<Package>::failure(name.error());
    }
    if (!kind) {
      return Result<Package>::failure(kind.error());
    }
    if (!flags) {
      return Result<Package>::failure(flags.error());
    }
    if (!length) {
      return Result<Package>::failure(length.error());
    }
    if (!expected) {
      return Result<Package>::failure(expected.error());
    }
    if (length.value() > limits.max_payload_size) {
      return Result<Package>::failure(
          makeError(ErrorCode::kLimitExceeded, "section payload too large", reader.offset()));
    }
    headers.push_back(BinaryHeader{name.takeValue(), binaryKind(kind.value()), flags.value(),
                                   length.value(), expected.value()});
  }

  Package package;
  package.version = version.value();
  for (const auto& header : headers) {
    auto payload = reader.readBytes(header.length);
    if (!payload) {
      return Result<Package>::failure(payload.error());
    }
    if (header.checksum != 0 && checksum32(payload.value()) != header.checksum) {
      return Result<Package>::failure(
          makeError(ErrorCode::kChecksumMismatch, "section checksum mismatch", reader.offset()));
    }
    Section section;
    section.name = header.name;
    section.kind = header.kind;
    section.flags = header.flags;
    section.checksum = header.checksum;
    section.payload = payload.takeValue();
    package.sections.push_back(std::move(section));
  }
  return Result<Package>::success(std::move(package));
}

Result<Package> parseTextPackage(const std::uint8_t* data, std::size_t size,
                                 PackageLimits limits) {
  std::string text;
  if (data != nullptr && size != 0) {
    text.assign(reinterpret_cast<const char*>(data), size);
  }

  std::istringstream input(text);
  std::string line;
  std::size_t line_number = 0;
  if (!std::getline(input, line)) {
    return Result<Package>::failure(
        makeError(ErrorCode::kUnexpectedEof, "empty package", 0));
  }
  ++line_number;
  if (trim(line) != "MWPK1-TEXT") {
    return Result<Package>::failure(
        makeError(ErrorCode::kInvalidMagic, "text package missing MWPK1-TEXT", 0));
  }

  Package package;
  while (std::getline(input, line)) {
    ++line_number;
    if (line_number > limits.max_text_lines) {
      return Result<Package>::failure(
          makeError(ErrorCode::kLimitExceeded, "too many package lines", line_number));
    }
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (line == "end") {
      break;
    }
    if (line.rfind("version ", 0) == 0) {
      auto version = parseU32(trim(line.substr(8)), line_number);
      if (!version) {
        return Result<Package>::failure(version.error());
      }
      if (version.value() == 0 || version.value() > 2) {
        return Result<Package>::failure(
            makeError(ErrorCode::kUnsupportedVersion, "unsupported version", line_number));
      }
      package.version = static_cast<std::uint16_t>(version.value());
      continue;
    }
    if (line.rfind("meta ", 0) == 0) {
      auto meta = parseMetadata(package, trim(line.substr(5)), line_number);
      if (!meta) {
        return Result<Package>::failure(meta.error());
      }
      continue;
    }
    if (line.rfind("section ", 0) == 0) {
      auto words = splitWords(line);
      if (words.size() < 3 || words.size() > 4) {
        return Result<Package>::failure(
            makeError(ErrorCode::kInvalidSyntax, "section expects name and kind", line_number));
      }
      if (package.sections.size() >= limits.max_sections) {
        return Result<Package>::failure(
            makeError(ErrorCode::kLimitExceeded, "too many sections", line_number));
      }

      Section section;
      section.name = words[1];
      section.kind = sectionKindFromName(words[2]);
      if (words.size() == 4) {
        auto expected = parseU32(words[3], line_number);
        if (!expected) {
          return Result<Package>::failure(expected.error());
        }
        section.checksum = expected.value();
      }

      std::ostringstream payload;
      bool closed = false;
      while (std::getline(input, line)) {
        ++line_number;
        if (trim(line) == ".") {
          closed = true;
          break;
        }
        payload << line << '\n';
        if (static_cast<std::size_t>(payload.tellp()) > limits.max_payload_size) {
          return Result<Package>::failure(
              makeError(ErrorCode::kLimitExceeded, "section payload too large", line_number));
        }
      }
      if (!closed) {
        return Result<Package>::failure(
            makeError(ErrorCode::kUnexpectedEof, "unterminated section", line_number));
      }
      std::string payload_text = payload.str();
      section.payload = bytesFromString(payload_text);
      if (section.checksum != 0 && checksum32(section.payload) != section.checksum) {
        return Result<Package>::failure(
            makeError(ErrorCode::kChecksumMismatch, "section checksum mismatch", line_number));
      }
      package.sections.push_back(std::move(section));
      continue;
    }

    return Result<Package>::failure(
        makeError(ErrorCode::kInvalidSyntax, "unknown package directive", line_number));
  }

  return Result<Package>::success(std::move(package));
}

Result<void> validatePackage(const Package& package) {
  if (package.version == 0 || package.version > 2) {
    return Result<void>::failure(
        makeError(ErrorCode::kUnsupportedVersion, "unsupported version", 0));
  }
  if (package.sections.empty()) {
    return Result<void>::failure(
        makeError(ErrorCode::kInvalidState, "package has no sections", 0));
  }
  for (const auto& section : package.sections) {
    if (section.name.empty()) {
      return Result<void>::failure(
          makeError(ErrorCode::kInvalidState, "section has empty name", 0));
    }
    if (section.kind == SectionKind::kUnknown) {
      return Result<void>::failure(
          makeError(ErrorCode::kUnknownSection, "unknown section kind", 0));
    }
  }
  return Result<void>::success();
}

}  // namespace mapweave
