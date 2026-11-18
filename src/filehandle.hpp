#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
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

/// RAII file handle that automatically uses memfd (Linux) or tempfile.
/// Provides a file path suitable for OCCT APIs that require paths.
/// On Linux 3.17+: uses memfd_create (no filesystem writes)
/// Elsewhere: falls back to temp file in system temp directory
///
class FileHandle {
public:
  /// Create a file handle with the given extension hint (e.g., ".glb", ".igs")
  explicit FileHandle(const std::string &extension) {
    if (memfd_available()) {
#ifdef __linux__
      // Use memfd - no filesystem writes
      fd_ = syscall(SYS_memfd_create, "cascadio", MFD_CLOEXEC);
      if (fd_ >= 0) {
        path_ = "/proc/self/fd/" + std::to_string(fd_);
        is_memfd_ = true;
        valid_ = true;
        return;
      }
#endif
    }

    // Fallback to temp file
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

    std::ofstream ofs(path_, std::ios::binary);
    valid_ = ofs.good();
    is_memfd_ = false;
  }

  ~FileHandle() {
    if (is_memfd_) {
#ifdef __linux__
      if (fd_ >= 0) {
        close(fd_);
      }
#endif
    } else if (!path_.empty()) {
      std::filesystem::remove(path_);
    }
  }

  // Non-copyable, non-movable
  FileHandle(const FileHandle &) = delete;
  FileHandle &operator=(const FileHandle &) = delete;
  FileHandle(FileHandle &&) = delete;
  FileHandle &operator=(FileHandle &&) = delete;

  /// Check if handle was created successfully
  bool valid() const { return valid_; }

  /// True if backed by memfd rather than a file on disk
  bool is_memfd() const { return is_memfd_; }

  /// Get file path for OCCT APIs
  const char *path() const { return path_.c_str(); }

  /// Write data to the handle
  bool write_data(const void *data, size_t size) {
    if (!valid_)
      return false;

    if (is_memfd_) {
#ifdef __linux__
      if (ftruncate(fd_, 0) != 0 || lseek(fd_, 0, SEEK_SET) != 0) {
        return false;
      }
      // one write() call is capped at about 2 GB, so loop until done
      const char *cursor = static_cast<const char *>(data);
      while (size > 0) {
        const ssize_t written = write(fd_, cursor, size);
        if (written <= 0) {
          return false;
        }
        cursor += written;
        size -= static_cast<size_t>(written);
      }
      return true;
#else
      return false;
#endif
    } else {
      std::ofstream ofs(path_, std::ios::binary | std::ios::trunc);
      ofs.write(static_cast<const char *>(data),
                static_cast<std::streamsize>(size));
      return ofs.good();
    }
  }

  /// Prepare for reading (seeks to start for memfd, no-op for tempfile)
  void prepare_for_read() {
#ifdef __linux__
    if (is_memfd_ && fd_ >= 0) {
      lseek(fd_, 0, SEEK_SET);
    }
#endif
  }

  /// Read all data from the handle
  std::vector<char> read_all() {
    if (!valid_)
      return {};

    if (is_memfd_) {
#ifdef __linux__
      off_t size = lseek(fd_, 0, SEEK_END);
      if (size <= 0)
        return {};
      lseek(fd_, 0, SEEK_SET);
      std::vector<char> result(static_cast<size_t>(size));
      // one read() call is capped at about 2 GB, so loop until done
      size_t done = 0;
      while (done < result.size()) {
        const ssize_t got = read(fd_, result.data() + done, result.size() - done);
        if (got <= 0) {
          return {};
        }
        done += static_cast<size_t>(got);
      }
      return result;
#else
      return {};
#endif
    } else {
      std::ifstream file(path_, std::ios::binary | std::ios::ate);
      if (!file)
        return {};
      std::streampos pos = file.tellg();
      if (pos == std::streampos(-1) || pos < 0)
        return {};
      auto size = static_cast<size_t>(pos);
      file.seekg(0);
      std::vector<char> result(size);
      file.read(result.data(), static_cast<std::streamsize>(size));
      if (!file) {
        return {};
      }
      return result;
    }
  }

  /// Check if memfd is available on this system (cached)
  static bool memfd_available() {
    static int cached = -1;
    if (cached < 0) {
#ifdef __linux__
      int test_fd = syscall(SYS_memfd_create, "probe", MFD_CLOEXEC);
      if (test_fd >= 0) {
        std::string test_path = "/proc/self/fd/" + std::to_string(test_fd);
        struct stat st;
        cached = (stat(test_path.c_str(), &st) == 0) ? 1 : 0;
        close(test_fd);
      } else {
        cached = 0;
      }
#else
      cached = 0;
#endif
    }
    return cached == 1;
  }

private:
  bool valid_ = false;
  bool is_memfd_ = false;
  std::string path_;
  int fd_ = -1;
};
