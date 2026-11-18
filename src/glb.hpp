#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

/// A GLB file split into its JSON text and binary buffer.
/// Layout: 12-byte header, then chunks of [length][type][data].
struct Glb {
  std::string json;
  std::vector<char> bin;

  static constexpr uint32_t MAGIC = 0x46546C67;      // "glTF"
  static constexpr uint32_t CHUNK_JSON = 0x4E4F534A; // "JSON"
  static constexpr uint32_t CHUNK_BIN = 0x004E4942;  // "BIN\0"

  static Glb parse(const std::vector<char> &data) {
    auto u32 = [&data](size_t offset) {
      if (offset + 4 > data.size()) {
        throw std::runtime_error("GLB is truncated");
      }
      uint32_t value;
      std::memcpy(&value, data.data() + offset, 4);
      return value;
    };
    if (u32(0) != MAGIC || u32(16) != CHUNK_JSON) {
      throw std::runtime_error("not a GLB file");
    }
    Glb glb;
    const size_t jsonLen = u32(12);
    const size_t binHeader = 20 + jsonLen;
    if (binHeader > data.size()) {
      throw std::runtime_error("GLB is truncated");
    }
    glb.json.assign(data.data() + 20, jsonLen);
    if (binHeader < data.size()) {
      const size_t binLen = u32(binHeader);
      if (u32(binHeader + 4) != CHUNK_BIN ||
          binHeader + 8 + binLen > data.size()) {
        throw std::runtime_error("GLB binary chunk is malformed");
      }
      glb.bin.assign(data.data() + binHeader + 8,
                     data.data() + binHeader + 8 + binLen);
    }
    return glb;
  }

  /// Serialize, padding both chunks to 4 bytes as the glTF spec requires.
  std::vector<char> serialize() const {
    const size_t jsonLen = (json.size() + 3) & ~size_t(3);
    const size_t binLen = (bin.size() + 3) & ~size_t(3);
    const size_t total = 12 + 8 + jsonLen + (bin.empty() ? 0 : 8 + binLen);
    if (total > UINT32_MAX) {
      throw std::runtime_error("GLB would exceed the 4 GB format limit");
    }

    std::vector<char> out;
    out.reserve(total);
    auto put = [&out](uint32_t value) {
      const char *p = reinterpret_cast<const char *>(&value);
      out.insert(out.end(), p, p + 4);
    };
    put(MAGIC);
    put(2);
    put(static_cast<uint32_t>(total));
    put(static_cast<uint32_t>(jsonLen));
    put(CHUNK_JSON);
    out.insert(out.end(), json.begin(), json.end());
    out.resize(20 + jsonLen, ' ');
    if (!bin.empty()) {
      put(static_cast<uint32_t>(binLen));
      put(CHUNK_BIN);
      out.insert(out.end(), bin.begin(), bin.end());
      out.resize(total, '\0');
    }
    return out;
  }
};
