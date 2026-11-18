#pragma once

#include <zstd.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

/// Does the data start with the Zstandard frame magic number, 28 B5 2F FD?
/// No STEP or IGES file can start with these bytes.
static bool isZstd(const char *data, size_t size) {
  static const unsigned char magic[4] = {0x28, 0xB5, 0x2F, 0xFD};
  return size >= 4 && std::memcmp(data, magic, 4) == 0;
}

/// Decompress Zstandard data, however many frames it holds and whether or
/// not they record their size. Throws std::invalid_argument if the data is
/// corrupt or cut short.
///
/// The output size is not limited here: a small input can expand enormously.
/// `cascadio.load` runs conversions in a child process with a memory limit,
/// which is what stops such an input.
static std::vector<char> zstdDecompress(const char *data, size_t size) {
  std::unique_ptr<ZSTD_DCtx, size_t (*)(ZSTD_DCtx *)> context(
      ZSTD_createDCtx(), ZSTD_freeDCtx);
  if (!context) {
    throw std::bad_alloc();
  }

  std::vector<char> output;
  // use the recorded size as a starting point when there is a sane one
  const unsigned long long hint = ZSTD_getFrameContentSize(data, size);
  if (hint != ZSTD_CONTENTSIZE_UNKNOWN && hint != ZSTD_CONTENTSIZE_ERROR &&
      hint < (1ull << 30)) {
    output.reserve(static_cast<size_t>(hint));
  }

  const size_t chunk = ZSTD_DStreamOutSize();
  ZSTD_inBuffer input = {data, size, 0};
  size_t used = 0;
  size_t status = 0; // 0 means the current frame is complete
  while (input.pos < input.size) {
    output.resize(used + chunk);
    ZSTD_outBuffer out = {output.data() + used, chunk, 0};
    status = ZSTD_decompressStream(context.get(), &out, &input);
    if (ZSTD_isError(status)) {
      throw std::invalid_argument(
          std::string("could not decompress the Zstandard data: ") +
          ZSTD_getErrorName(status));
    }
    used += out.pos;
  }
  if (status != 0) {
    throw std::invalid_argument("the Zstandard data is cut short");
  }
  output.resize(used);
  return output;
}
