#include "mapweave/result.h"

namespace mapweave {

const char* errorCodeName(ErrorCode code) {
  switch (code) {
    case ErrorCode::kUnexpectedEof:
      return "unexpected_eof";
    case ErrorCode::kInvalidMagic:
      return "invalid_magic";
    case ErrorCode::kUnsupportedVersion:
      return "unsupported_version";
    case ErrorCode::kMalformedVarint:
      return "malformed_varint";
    case ErrorCode::kInvalidSyntax:
      return "invalid_syntax";
    case ErrorCode::kLimitExceeded:
      return "limit_exceeded";
    case ErrorCode::kChecksumMismatch:
      return "checksum_mismatch";
    case ErrorCode::kInvalidState:
      return "invalid_state";
    case ErrorCode::kUnknownSection:
      return "unknown_section";
  }
  return "unknown";
}

}  // namespace mapweave
