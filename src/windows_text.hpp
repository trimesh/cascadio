#pragma once

// The one piece of Windows code the module and the worker share. It uses
// nothing from OpenCASCADE or Python, so the rule in CMakeLists.txt holds:
// the module has no OpenCASCADE and the worker has no Python.

#include <windows.h> // NOMINMAX and WIN32_LEAN_AND_MEAN come from CMake

#include <cstring>
#include <string>

/// The text Windows has for an error code, without the final period.
inline std::string systemText(DWORD error) {
  char *text = nullptr;
  const DWORD length = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error, 0, reinterpret_cast<LPSTR>(&text), 0, nullptr);
  std::string result = length > 0 ? std::string(text, length)
                                  : "error " + std::to_string(error);
  LocalFree(text);
  while (!result.empty() && std::strchr(" .\r\n", result.back()) != nullptr) {
    result.pop_back();
  }
  return result;
}
