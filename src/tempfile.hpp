#pragma once

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

/// RAII wrapper for temporary files. Creates a unique temp file that is
/// automatically deleted when the object goes out of scope.
/// Required because OCCT APIs need file paths, not streams.
class TempFile {
public:
  /// Create a temporary file with the given extension (e.g., ".glb", ".igs")
  explicit TempFile(const std::string &extension) {
    // Generate a unique filename using random characters
    static const char charset[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);

    std::string random_part;
    for (int i = 0; i < 8; ++i) {
      random_part += charset[dist(gen)];
    }

    path_ = (std::filesystem::temp_directory_path() /
             ("cascadio_" + random_part + extension))
                .string();

    // Create the file to ensure it exists and we have exclusive access
    std::ofstream ofs(path_, std::ios::binary);
    if (!ofs) {
      path_.clear();
    }
  }

  ~TempFile() {
    if (!path_.empty()) {
      std::filesystem::remove(path_);
    }
  }

  // Non-copyable, non-movable (simple RAII)
  TempFile(const TempFile &) = delete;
  TempFile &operator=(const TempFile &) = delete;
  TempFile(TempFile &&) = delete;
  TempFile &operator=(TempFile &&) = delete;

  /// Check if the temp file was created successfully
  bool valid() const { return !path_.empty(); }

  /// Get the file path
  const char *path() const { return path_.c_str(); }

  /// Write data to the file (overwrites any existing content)
  bool write_and_close(const void *data, size_t size) {
    if (path_.empty())
      return false;

    std::ofstream ofs(path_, std::ios::binary | std::ios::trunc);
    if (!ofs)
      return false;

    ofs.write(static_cast<const char *>(data),
              static_cast<std::streamsize>(size));
    return ofs.good();
  }

  /// No-op for API compatibility (file is already closed after construction)
  void close_fd() {}

private:
  std::string path_;
};
