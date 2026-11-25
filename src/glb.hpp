#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

/// A GLB file as its JSON text and binary buffer, before they are packed.
/// Layout: 12-byte header, then chunks of [length][type][data].
struct Glb {
  std::string json;
  std::string bin;

  static constexpr uint32_t MAGIC = 0x46546C67;      // "glTF"
  static constexpr uint32_t CHUNK_JSON = 0x4E4F534A; // "JSON"
  static constexpr uint32_t CHUNK_BIN = 0x004E4942;  // "BIN\0"
  /// The error for output past the 32-bit sizes of the format.
  static constexpr const char *TOO_BIG =
      "the GLB would be over 4 GB, the format's limit; use a larger tol_linear";

  /// Write the GLB a piece at a time through `write(data, size)`, which
  /// returns false on failure, rather than pack a copy of it first. Both
  /// chunks are padded to 4 bytes, as the glTF spec requires.
  template <typename Write> bool writeTo(Write &&write) const {
    const size_t jsonLen = (json.size() + 3) & ~size_t(3);
    const size_t binLen = (bin.size() + 3) & ~size_t(3);
    const size_t total = 12 + 8 + jsonLen + (bin.empty() ? 0 : 8 + binLen);
    if (total > UINT32_MAX) {
      throw std::runtime_error(TOO_BIG);
    }
    const uint32_t head[] = {MAGIC, 2, static_cast<uint32_t>(total),
                             static_cast<uint32_t>(jsonLen), CHUNK_JSON};
    const uint32_t binHead[] = {static_cast<uint32_t>(binLen), CHUNK_BIN};
    auto words = [](const uint32_t *values) {
      return reinterpret_cast<const char *>(values);
    };
    if (!write(words(head), sizeof(head)) || !write(json.data(), json.size()) ||
        !write("   ", jsonLen - json.size())) {
      return false;
    }
    return bin.empty() ||
           (write(words(binHead), sizeof(binHead)) &&
            write(bin.data(), bin.size()) &&
            write("\0\0\0", binLen - bin.size()));
  }
};
