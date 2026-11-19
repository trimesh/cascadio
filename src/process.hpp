#pragma once

// Run a program, feed it bytes on standard input, collect what it writes,
// and stop it if it runs too long. This is all the process handling
// cascadio has; nothing here knows about Python or OpenCASCADE.
//
// There are two implementations of one function, `runProcess`:
// POSIX (Linux, macOS, BSD) and Windows.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct ProcessResult {
  bool timed_out = false;   ///< it was killed for running past the limit
  int exit_code = -1;       ///< valid unless timed out or killed by a signal
  int signal = 0;           ///< POSIX: the signal that killed it, if any
  std::vector<char> output; ///< everything it wrote to standard output
  std::string errors;       ///< everything it wrote to standard error
};

/// Run `program`, writing `head` then `body` to its standard input.
/// A negative `timeout_seconds` means wait forever.
/// Throws std::runtime_error if the program cannot be started.
static ProcessResult runProcess(const std::string &program,
                                const std::string &head, const char *body,
                                size_t body_size, double timeout_seconds);

/// The folder holding the shared library or executable this code is in.
static std::string thisBinaryFolder();

/// An environment variable as it is right now, or empty if it is not set.
/// On Windows std::getenv is not enough: this module links its own copy of
/// the C runtime, which does not see changes Python makes to os.environ.
static std::string environmentValue(const char *name);

#ifndef _WIN32
// ============================================================================
// POSIX
// ============================================================================

#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
// on macOS a shared library cannot see `environ` directly
#include <crt_externs.h>
#define CASCADIO_ENVIRON (*_NSGetEnviron())
#else
extern char **environ;
#define CASCADIO_ENVIRON environ
#endif

static std::string thisBinaryFolder() {
  Dl_info info;
  // any address inside this binary will do
  if (dladdr(reinterpret_cast<void *>(&thisBinaryFolder), &info) == 0 ||
      info.dli_fname == nullptr) {
    throw std::runtime_error("cannot find where cascadio is installed");
  }
  const std::string path = info.dli_fname;
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? "." : path.substr(0, slash);
}

static std::string environmentValue(const char *name) {
  const char *value = std::getenv(name);
  return value == nullptr ? "" : value;
}

/// A pipe whose ends close themselves, so no error path leaks a descriptor.
struct Pipe {
  int read_end = -1;
  int write_end = -1;

  Pipe() {
    int ends[2];
    if (pipe(ends) != 0) {
      throw std::runtime_error(std::string("pipe failed: ") + strerror(errno));
    }
    read_end = ends[0];
    write_end = ends[1];
    // do not leak into other children started from other threads
    fcntl(read_end, F_SETFD, FD_CLOEXEC);
    fcntl(write_end, F_SETFD, FD_CLOEXEC);
  }
  ~Pipe() {
    closeEnd(read_end);
    closeEnd(write_end);
  }
  Pipe(const Pipe &) = delete;
  Pipe &operator=(const Pipe &) = delete;

  static void closeEnd(int &end) {
    if (end >= 0) {
      close(end);
      end = -1;
    }
  }
};

static ProcessResult runProcess(const std::string &program,
                                const std::string &head, const char *body,
                                size_t body_size, double timeout_seconds) {
  Pipe input, output, errors;

  // the child gets the three pipe ends as its standard streams
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, input.read_end, STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&actions, output.write_end, STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, errors.write_end, STDERR_FILENO);
  char *const arguments[] = {const_cast<char *>(program.c_str()), nullptr};
  pid_t child = -1;
  const int failed =
      posix_spawn(&child, program.c_str(), &actions, nullptr, arguments,
                  CASCADIO_ENVIRON);
  posix_spawn_file_actions_destroy(&actions);
  if (failed != 0) {
    throw std::runtime_error("cannot start " + program + ": " +
                             strerror(failed));
  }

  // our copies of the child's ends must go, or we never see end-of-file
  Pipe::closeEnd(input.read_end);
  Pipe::closeEnd(output.write_end);
  Pipe::closeEnd(errors.write_end);
  for (int end : {input.write_end, output.read_end, errors.read_end}) {
    fcntl(end, F_SETFL, fcntl(end, F_GETFL) | O_NONBLOCK);
  }

  using Clock = std::chrono::steady_clock;
  const auto deadline =
      Clock::now() + std::chrono::duration_cast<Clock::duration>(
                         std::chrono::duration<double>(timeout_seconds));
  auto millisecondsLeft = [&]() -> int {
    if (timeout_seconds < 0) {
      return -1; // poll and wait forever
    }
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                          deadline - Clock::now())
                          .count();
    return left < 0 ? 0 : static_cast<int>(std::min<long long>(left, 60000));
  };

  ProcessResult result;
  // what is still to be written: first the head, then the body
  const char *chunks[2] = {head.data(), body};
  size_t remaining[2] = {head.size(), body_size};
  int chunk = 0;
  char buffer[65536];

  // Write and read at the same time. Doing one and then the other would
  // deadlock as soon as either side fills a pipe.
  while (output.read_end >= 0 || errors.read_end >= 0) {
    while (chunk < 2 && remaining[chunk] == 0) {
      chunk++;
    }
    if (chunk == 2) {
      Pipe::closeEnd(input.write_end); // tells the child the input is done
    }
    pollfd watched[3] = {{input.write_end, POLLOUT, 0},
                         {output.read_end, POLLIN, 0},
                         {errors.read_end, POLLIN, 0}};
    const int wait = millisecondsLeft();
    const int ready = poll(watched, 3, wait);
    if (ready < 0 && errno == EINTR) {
      continue;
    }
    if (ready == 0 && timeout_seconds >= 0 && millisecondsLeft() == 0) {
      result.timed_out = true;
      break;
    }

    if (watched[0].revents != 0 && input.write_end >= 0) {
      const ssize_t wrote =
          write(input.write_end, chunks[chunk], remaining[chunk]);
      if (wrote > 0) {
        chunks[chunk] += wrote;
        remaining[chunk] -= static_cast<size_t>(wrote);
      } else if (wrote < 0 && errno != EAGAIN && errno != EINTR) {
        // the child stopped reading, most likely because it exited
        Pipe::closeEnd(input.write_end);
        chunk = 2;
        remaining[0] = remaining[1] = 0;
      }
    }
    auto drain = [&buffer](int &end, auto &into) {
      const ssize_t got = read(end, buffer, sizeof(buffer));
      if (got > 0) {
        into.insert(into.end(), buffer, buffer + got);
      } else if (got == 0 || (errno != EAGAIN && errno != EINTR)) {
        Pipe::closeEnd(end); // end-of-file
      }
    };
    if (watched[1].revents != 0 && output.read_end >= 0) {
      drain(output.read_end, result.output);
    }
    if (watched[2].revents != 0 && errors.read_end >= 0) {
      drain(errors.read_end, result.errors);
    }
  }

  // Both streams are closed, so the child is exiting, or the time is up.
  // Give it what is left of the limit, then kill it. Always reap it.
  int status = 0;
  while (!result.timed_out) {
    const pid_t done = waitpid(child, &status, WNOHANG);
    if (done == child || (done < 0 && errno != EINTR)) {
      break;
    }
    if (timeout_seconds >= 0 && millisecondsLeft() == 0) {
      result.timed_out = true;
      break;
    }
    usleep(500);
  }
  if (result.timed_out) {
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
  } else if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.signal = WTERMSIG(status);
  }
  return result;
}

#else
// ============================================================================
// Windows
// ============================================================================

#include <windows.h> // NOMINMAX and WIN32_LEAN_AND_MEAN come from CMake

#include <thread>

static std::string thisBinaryFolder() {
  HMODULE module = nullptr;
  wchar_t wide[32768];
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&thisBinaryFolder),
                          &module) ||
      GetModuleFileNameW(module, wide, 32768) == 0) {
    throw std::runtime_error("cannot find where cascadio is installed");
  }
  std::wstring path = wide;
  path = path.substr(0, path.find_last_of(L"\\/"));
  const int size = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr,
                                       0, nullptr, nullptr);
  std::string utf8(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8.data(), size, nullptr,
                      nullptr);
  utf8.resize(static_cast<size_t>(size) - 1); // drop the terminator
  return utf8;
}

static std::string environmentValue(const char *name) {
  char value[32768];
  const DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
  return length > 0 && length < sizeof(value) ? std::string(value, length) : "";
}

/// A pipe whose ends close themselves. `child_reads` says which end the
/// child process inherits; the other end stays private to us.
struct Pipe {
  HANDLE read_end = nullptr;
  HANDLE write_end = nullptr;

  explicit Pipe(bool child_reads) {
    SECURITY_ATTRIBUTES inheritable = {sizeof(SECURITY_ATTRIBUTES), nullptr,
                                       TRUE};
    if (!CreatePipe(&read_end, &write_end, &inheritable, 0)) {
      throw std::runtime_error("CreatePipe failed");
    }
    SetHandleInformation(child_reads ? write_end : read_end,
                         HANDLE_FLAG_INHERIT, 0);
  }
  ~Pipe() {
    closeEnd(read_end);
    closeEnd(write_end);
  }
  Pipe(const Pipe &) = delete;
  Pipe &operator=(const Pipe &) = delete;

  static void closeEnd(HANDLE &end) {
    if (end != nullptr) {
      CloseHandle(end);
      end = nullptr;
    }
  }
};

static ProcessResult runProcess(const std::string &program,
                                const std::string &head, const char *body,
                                size_t body_size, double timeout_seconds) {
  Pipe input(true), output(false), errors(false);

  // the path, quoted, is the whole command line
  const int size =
      MultiByteToWideChar(CP_UTF8, 0, program.c_str(), -1, nullptr, 0);
  std::wstring wide(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, program.c_str(), -1, wide.data(), size);
  wide.resize(static_cast<size_t>(size) - 1);
  std::wstring command = L"\"" + wide + L"\"";

  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = input.read_end;
  startup.hStdOutput = output.write_end;
  startup.hStdError = errors.write_end;
  PROCESS_INFORMATION child = {};
  // CREATE_NO_WINDOW: no console window flashing up in GUI applications
  if (!CreateProcessW(wide.c_str(), command.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) {
    throw std::runtime_error("cannot start " + program + " (error " +
                             std::to_string(GetLastError()) + ")");
  }
  CloseHandle(child.hThread);

  // our copies of the child's ends must go, or we never see end-of-file
  Pipe::closeEnd(input.read_end);
  Pipe::closeEnd(output.write_end);
  Pipe::closeEnd(errors.write_end);

  // Windows pipes block, so each stream gets a thread. Writing and reading
  // one after the other would deadlock as soon as either side fills a pipe.
  ProcessResult result;
  std::thread writer([&] {
    for (const auto &[data, length] :
         {std::pair<const char *, size_t>{head.data(), head.size()},
          std::pair<const char *, size_t>{body, body_size}}) {
      size_t done = 0;
      while (done < length) {
        const DWORD part =
            static_cast<DWORD>(std::min<size_t>(length - done, 1 << 20));
        DWORD wrote = 0;
        if (!WriteFile(input.write_end, data + done, part, &wrote, nullptr)) {
          break; // the child stopped reading
        }
        done += wrote;
      }
    }
    Pipe::closeEnd(input.write_end); // tells the child the input is done
  });
  auto drain = [](HANDLE end, auto &into) {
    char buffer[65536];
    DWORD got = 0;
    while (ReadFile(end, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
      into.insert(into.end(), buffer, buffer + got);
    }
  };
  std::thread outputReader([&] { drain(output.read_end, result.output); });
  std::thread errorReader([&] { drain(errors.read_end, result.errors); });

  const DWORD wait = timeout_seconds < 0
                         ? INFINITE
                         : static_cast<DWORD>(timeout_seconds * 1000.0);
  if (WaitForSingleObject(child.hProcess, wait) == WAIT_TIMEOUT) {
    result.timed_out = true;
    TerminateProcess(child.hProcess, 1);
    WaitForSingleObject(child.hProcess, INFINITE);
  }
  // the child is gone, so its pipe ends are closed and the threads finish
  writer.join();
  outputReader.join();
  errorReader.join();

  DWORD code = 0;
  GetExitCodeProcess(child.hProcess, &code);
  CloseHandle(child.hProcess);
  if (!result.timed_out) {
    result.exit_code = static_cast<int>(code);
  }
  return result;
}

#endif
