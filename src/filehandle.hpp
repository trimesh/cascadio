#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#ifdef __linux__
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
#endif

/// A scratch file for OpenCASCADE APIs that only take a path.
/// On Linux it lives in memory (memfd), so nothing touches the filesystem;
/// elsewhere it is a temporary file that is deleted when this goes away.
class FileHandle {
public:
  /// `extension` (for example ".glb") only names the temporary file.
  explicit FileHandle(const std::string &extension) {
#ifdef __linux__
    if (memfdAvailable()) {
      fd_ = static_cast<int>(syscall(SYS_memfd_create, "cascadio", MFD_CLOEXEC));
      if (fd_ >= 0) {
        path_ = "/proc/self/fd/" + std::to_string(fd_);
        return;
      }
    }
#endif
    static const char letters[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::random_device random;
    std::string name = "cascadio_";
    for (int i = 0; i < 12; ++i) {
      name += letters[random() % (sizeof(letters) - 1)];
    }
    path_ = (std::filesystem::temp_directory_path() / (name + extension))
                .string();
    if (!std::ofstream(path_, std::ios::binary)) {
      path_.clear(); // could not create it
    }
  }

  ~FileHandle() {
#ifdef __linux__
    if (fd_ >= 0) {
      close(fd_);
      return;
    }
#endif
    if (!path_.empty()) {
      std::error_code ignored;
      std::filesystem::remove(path_, ignored);
    }
  }

  FileHandle(const FileHandle &) = delete;
  FileHandle &operator=(const FileHandle &) = delete;

  bool valid() const { return !path_.empty(); }
  bool is_memfd() const { return fd_ >= 0; }
  const char *path() const { return path_.c_str(); }

  /// Replace the content. Returns false on failure.
  bool write_data(const char *data, size_t size) {
    std::ofstream file(path_, std::ios::binary | std::ios::trunc);
    file.write(data, static_cast<std::streamsize>(size));
    return file.good();
  }

  /// Everything in the file, or empty on failure.
  std::vector<char> read_all() const {
    std::ifstream file(path_, std::ios::binary | std::ios::ate);
    const std::streamoff size = file ? std::streamoff(file.tellg()) : -1;
    if (size <= 0) {
      return {};
    }
    std::vector<char> content(static_cast<size_t>(size));
    file.seekg(0);
    file.read(content.data(), size);
    if (!file) {
      return {};
    }
    return content;
  }

private:
#ifdef __linux__
  /// Can this system create a memfd and reach it through /proc?
  /// Probed once; the static makes the first call safe from any thread.
  static bool memfdAvailable() {
    static const bool available = [] {
      const int fd =
          static_cast<int>(syscall(SYS_memfd_create, "probe", MFD_CLOEXEC));
      if (fd < 0) {
        return false;
      }
      struct stat info;
      const std::string path = "/proc/self/fd/" + std::to_string(fd);
      const bool reachable = stat(path.c_str(), &info) == 0;
      close(fd);
      return reachable;
    }();
    return available;
  }
#endif

  std::string path_; ///< empty if the file could not be created
  int fd_ = -1;      ///< the memfd, or -1 for a temporary file
};
