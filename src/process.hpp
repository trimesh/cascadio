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
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct ProcessResult {
  bool timed_out = false;   ///< it was killed for running past the limit
  bool interrupted = false; ///< it was killed because `interrupted` said so
  int exit_code = -1;       ///< valid unless timed out or killed by a signal
  int signal = 0;           ///< POSIX: the signal that killed it, if any
  std::vector<char> output; ///< everything it wrote to standard output
  std::string errors;       ///< everything it wrote to standard error
};

/// Why a program could not be started.
struct StartError : std::runtime_error {
  enum Kind { MISSING, NOT_RUNNABLE, OTHER } kind;
  StartError(Kind why, const std::string &reason)
      : std::runtime_error(reason), kind(why) {}
};

/// Run `program`, writing `head` then `body` to its standard input.
/// A negative `timeout_seconds` means wait forever. `interrupted` is asked
/// at least every 100 ms; when it returns true the program is stopped.
/// Throws StartError if the program cannot be started.
static ProcessResult runProcess(const std::string &program,
                                const std::string &head, const char *body,
                                size_t body_size, double timeout_seconds,
                                const std::function<bool()> &interrupted);

/// The folder holding the shared library or executable this code is in.
static std::string thisBinaryFolder();

/// An environment variable as it is right now, or empty if it is not set.
/// On Windows std::getenv is not enough: this module links its own copy of
/// the C runtime, which does not see changes Python makes to os.environ.
static std::string environmentValue(const char *name);

/// The environment a started program gets, as NAME=value: only the few
/// variables the worker reads. None of the caller's own (credentials,
/// tokens) is in its memory, so a hijacked worker has none to send back.
static std::vector<std::string> childEnvironment() {
  static const char *const KEEP[] = {
      "CASCADIO_TRACE", "ASAN_OPTIONS", "UBSAN_OPTIONS",
#ifdef _WIN32
      "SystemRoot", // much of Windows does not work without it
      // folders, not secrets: Windows makes an AppContainer's own folders
      // from them when the worker starts itself inside one (sandbox.hpp)
      "USERPROFILE", "LOCALAPPDATA", "TEMP", "TMP",
#endif
  };
  std::vector<std::string> kept;
  for (const char *name : KEEP) {
    const std::string value = environmentValue(name);
    if (!value.empty()) {
      kept.push_back(std::string(name) + "=" + value);
    }
  }
  return kept;
}

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
                                size_t body_size, double timeout_seconds,
                                const std::function<bool()> &interrupted) {
  Pipe input, output, errors;

  // the child gets the three pipe ends as its standard streams
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, input.read_end, STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&actions, output.write_end, STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, errors.write_end, STDERR_FILENO);
  char *const arguments[] = {const_cast<char *>(program.c_str()), nullptr};
  std::vector<std::string> environment = childEnvironment();
  std::vector<char *> environmentPointers;
  for (std::string &entry : environment) {
    environmentPointers.push_back(entry.data());
  }
  environmentPointers.push_back(nullptr);
  pid_t child = -1;
  const int failed =
      posix_spawn(&child, program.c_str(), &actions, nullptr, arguments,
                  environmentPointers.data());
  posix_spawn_file_actions_destroy(&actions);
  if (failed != 0) {
    throw StartError(failed == ENOENT ? StartError::MISSING
                     : failed == EACCES || failed == ENOEXEC || failed == EPERM
                         ? StartError::NOT_RUNNABLE
                         : StartError::OTHER,
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
  // never more than 100 ms, so `interrupted` is asked often enough
  auto millisecondsLeft = [&]() -> int {
    if (timeout_seconds < 0) {
      return 100;
    }
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                          deadline - Clock::now())
                          .count();
    return left < 0 ? 0 : static_cast<int>(std::min<long long>(left, 100));
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
    if (interrupted()) {
      result.interrupted = true;
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
  while (!result.timed_out && !result.interrupted) {
    const pid_t done = waitpid(child, &status, WNOHANG);
    if (done == child || (done < 0 && errno != EINTR)) {
      break;
    }
    if (timeout_seconds >= 0 && millisecondsLeft() == 0) {
      result.timed_out = true;
      break;
    }
    result.interrupted = interrupted();
    usleep(500);
  }
  if (result.timed_out || result.interrupted) {
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

/// The text Windows has for an error code. (sandbox.hpp has the same for
/// the worker, which shares no code with this module but request.hpp.)
static std::string systemText(DWORD error) {
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

static ProcessResult runProcess(const std::string &program,
                                const std::string &head, const char *body,
                                size_t body_size, double timeout_seconds,
                                const std::function<bool()> &interrupted) {
  Pipe input(true), output(false), errors(false);

  auto widen = [](const std::string &utf8) {
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), size);
    wide.resize(static_cast<size_t>(size) - 1);
    return wide;
  };
  const std::wstring wide = widen(program);
  // the path, quoted, is the whole command line
  std::wstring command = L"\"" + wide + L"\"";
  // NAME=value, each ended by a zero, and the block by one more
  std::wstring environment;
  for (const std::string &entry : childEnvironment()) {
    environment += widen(entry) + L'\0';
  }
  environment += L'\0';

  // the three pipe ends are the only handles it inherits
  HANDLE inherited[] = {input.read_end, output.write_end, errors.write_end};
  SIZE_T attributesSize = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attributesSize);
  std::vector<char> attributes(attributesSize);
  STARTUPINFOEXW startup = {};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = input.read_end;
  startup.StartupInfo.hStdOutput = output.write_end;
  startup.StartupInfo.hStdError = errors.write_end;
  startup.lpAttributeList =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
  if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0,
                                         &attributesSize) ||
      !UpdateProcThreadAttribute(startup.lpAttributeList, 0,
                                 PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                 sizeof(inherited), nullptr, nullptr)) {
    throw StartError(StartError::OTHER, systemText(GetLastError()));
  }
  PROCESS_INFORMATION child = {};
  // DETACHED_PROCESS: no console, so no console window can flash up and
  // no console host is started for it. Suspended until it is in the job.
  const BOOL started = CreateProcessW(
      wide.c_str(), command.data(), nullptr, nullptr, TRUE,
      DETACHED_PROCESS | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
          EXTENDED_STARTUPINFO_PRESENT,
      environment.data(), nullptr, &startup.StartupInfo, &child);
  const DWORD error = GetLastError();
  DeleteProcThreadAttributeList(startup.lpAttributeList);
  if (!started) {
    throw StartError(error == ERROR_FILE_NOT_FOUND ||
                             error == ERROR_PATH_NOT_FOUND
                         ? StartError::MISSING
                     : error == ERROR_ACCESS_DENIED ||
                             error == ERROR_BAD_EXE_FORMAT
                         ? StartError::NOT_RUNNABLE
                         : StartError::OTHER,
                     systemText(error));
  }
  // The job's handle closes when this call ends, or when Windows ends this
  // process however it ends: either way the worker and anything it
  // started go with it.
  struct Job {
    HANDLE handle = CreateJobObjectW(nullptr, nullptr);
    ~Job() {
      if (handle != nullptr) {
        CloseHandle(handle);
      }
    }
  } job;
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (job.handle == nullptr ||
      !SetInformationJobObject(job.handle, JobObjectExtendedLimitInformation,
                               &limits, sizeof(limits)) ||
      !AssignProcessToJobObject(job.handle, child.hProcess)) {
    const DWORD jobError = GetLastError();
    TerminateProcess(child.hProcess, 1);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    throw StartError(StartError::OTHER, systemText(jobError));
  }
  ResumeThread(child.hThread);
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

  // wait in steps of 100 ms, so `interrupted` is asked often enough
  const auto deadline =
      std::chrono::steady_clock::now() +
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(std::max(timeout_seconds, 0.0)));
  while (WaitForSingleObject(child.hProcess, 100) == WAIT_TIMEOUT) {
    if (timeout_seconds >= 0 && std::chrono::steady_clock::now() >= deadline) {
      result.timed_out = true;
    } else if (interrupted()) {
      result.interrupted = true;
    } else {
      continue;
    }
    TerminateJobObject(job.handle, 1);
    WaitForSingleObject(child.hProcess, INFINITE);
    break;
  }
  // the child is gone, so its pipe ends are closed and the threads finish
  writer.join();
  outputReader.join();
  errorReader.join();

  DWORD code = 0;
  GetExitCodeProcess(child.hProcess, &code);
  CloseHandle(child.hProcess);
  if (!result.timed_out && !result.interrupted) {
    result.exit_code = static_cast<int>(code);
  }
  return result;
}

#endif
