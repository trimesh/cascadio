#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>

/// Times one stage of a conversion. When the environment variable
/// CASCADIO_TRACE is set, each stage prints a line to standard error as it
/// ends, for example
///
///     cascadio: mesh            412.7 ms
///
/// Otherwise it costs two clock reads and prints nothing.
class Span {
public:
  explicit Span(const char *name)
      : name_(name), start_(std::chrono::steady_clock::now()) {}
  ~Span() {
    static const bool enabled = std::getenv("CASCADIO_TRACE") != nullptr;
    if (enabled) {
      const std::chrono::duration<double, std::milli> elapsed =
          std::chrono::steady_clock::now() - start_;
      std::fprintf(stderr, "cascadio: %-12s %9.1f ms\n", name_,
                   elapsed.count());
    }
  }
  Span(const Span &) = delete;
  Span &operator=(const Span &) = delete;

private:
  const char *name_;
  std::chrono::steady_clock::time_point start_;
};
