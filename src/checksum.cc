#include "mapweave/checksum.h"

namespace mapweave {

std::uint32_t checksum32(const std::uint8_t* data, std::size_t size) {
  std::uint32_t state = 2166136261u;
  for (std::size_t i = 0; i < size; ++i) {
    state ^= data[i];
    state *= 16777619u;
    state = (state << 7) | (state >> 25);
  }
  return state;
}

std::uint32_t checksum32(const std::vector<std::uint8_t>& data) {
  return checksum32(data.data(), data.size());
}

std::uint32_t checksum32(const std::string& text) {
  return checksum32(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

}  // namespace mapweave
