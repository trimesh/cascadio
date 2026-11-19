#pragma once

// Everything the worker does that depends on the operating system, which
// is mostly its lock-down. After `lockDown()` the process can compute,
// start threads, read its input and write its result: nothing else. No
// files, no network, no other processes. The worker applies it to itself,
// because only the worker knows when its set-up is over, and so it holds
// however the worker was started: by the module, the fuzzer or by hand.
//
// Nothing here can be switched off. A sanitizer build leaves out only the
// system call filter, so that a sanitizer can still print its report.
//
// Linux: a fixed seccomp allowlist; any other call kills the process.
// macOS: Apple's fixed "pure computation" sandbox profile.
// Windows: a job that allows no other process, a low-integrity token and
// process mitigations. Windows has no system call filter, so the worker
// can still read files there.
//
// Each platform's section defines the same functions in `os`, and the
// code shared by all of them is at the end.

#include "request.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
#define CASCADIO_SANITIZED 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
#define CASCADIO_SANITIZED 1
#endif

/// Proof that the lock-down is in force, and the only way to the input and
/// the result. Only lockDown() can make one, so the untrusted input cannot
/// be read before the lock-down.
class Locked {
public:
  /// Everything on standard input.
  std::vector<char> readInput() const;
  /// Write the result to the real standard output. False if it failed.
  bool writeResult(const char *data, size_t size) const;
  /// Stop the process once it has used more than `bytes`; 0: no limit.
  void limitMemory(uint64_t bytes) const;

private:
  explicit Locked(int result) : result_(result) {}
  int result_; ///< where the real standard output went
  template <typename SetUp> friend Locked lockDown(SetUp &&setUp);
};

/// Stop with a message: the lock-down failed, so nothing may run.
[[noreturn]] inline void cannotLockDown(const char *what) {
  std::fprintf(stderr, "cannot lock down the converter: %s\n", what);
  std::fflush(stderr);
  std::_Exit(worker_exit::FAILED);
}

#ifndef _WIN32
// ============================================================================
// POSIX: Linux and macOS
// ============================================================================

#include <csignal>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifdef __linux__
#include <malloc.h>
#endif

namespace os {

/// Close every descriptor but standard input, output and error, so nothing
/// the parent leaked stays reachable.
inline void closeInherited() {
#ifdef SYS_close_range
  if (syscall(SYS_close_range, 3U, ~0U, 0U) == 0) {
    return;
  }
#endif
  rlimit limit = {};
  getrlimit(RLIMIT_NOFILE, &limit);
  for (rlim_t fd = 3; fd < limit.rlim_cur && fd < (1U << 20); fd++) {
    close(static_cast<int>(fd));
  }
}

/// Keep the real standard output aside for the result and point descriptor
/// 1 at standard error, so a stray print from any library cannot corrupt
/// the result. Returns the descriptor the result goes to.
inline int keepResultStream() {
  const int result = dup(1);
  dup2(2, 1);
  return result;
}

/// OCCT reads message files relative to the working directory: make that
/// somewhere nobody can plant one.
inline void leaveWorkingDirectory() {
  if (chdir("/") != 0) {
    cannotLockDown("cannot leave the working directory");
  }
}

/// Set up what the C library would otherwise set up on first use, by
/// opening a file.
inline void prepareCLibrary() {
  // with TZ unset, every localtime() reads /etc/localtime again
  setenv("TZ", "UTC0", 1);
  tzset();
#ifdef __GLIBC__
  // glibc's malloc counts processors (from /sys) when threads need more
  // arenas, unless told how many it may have: here its own default, 8 per
  // processor. It reads /proc/sys/vm/overcommit_memory the first time a
  // thread's heap shrinks; heaps that never shrink never ask. The memory
  // limit counts the peak, which this does not change. Fixing the trim
  // threshold also stops the mmap threshold from rising by itself, which
  // would send every allocation over 128 KiB to the kernel: set it to the
  // most it would rise to (32 MiB on 64-bit).
  const unsigned processors = std::max(std::thread::hardware_concurrency(), 1U);
  mallopt(M_ARENA_MAX, static_cast<int>(8 * processors));
  mallopt(M_TRIM_THRESHOLD, INT_MAX);
  mallopt(M_MMAP_THRESHOLD, 32 << 20);
#endif
}

/// Plain reads and writes: the C library's buffered streams ask the system
/// about a descriptor on first use, which the lock-down does not allow.
inline long readSome(int fd, char *data, size_t size) {
  return read(fd, data, size);
}
inline long writeSome(int fd, const char *data, size_t size) {
  return write(fd, data, size);
}

/// The most physical memory this process has used so far, in bytes.
inline uint64_t peakMemory() {
  rusage usage = {};
  getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
  return static_cast<uint64_t>(usage.ru_maxrss); // bytes
#else
  return static_cast<uint64_t>(usage.ru_maxrss) * 1024; // kilobytes
#endif
}

// What `--probe` tries (see probe() at the end). Each is true if it worked.

inline bool readFile(const std::string &path) {
  return open(path.c_str(), O_RDONLY) >= 0;
}

inline bool writeIn(const std::string &folder) {
  return open((folder + "/cascadio-probe").c_str(),
              O_WRONLY | O_CREAT | O_EXCL, 0600) >= 0;
}

inline bool connectTo(int port) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<uint16_t>(port));
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return fd >= 0 &&
         connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
}

inline bool spawn() {
  char *const arguments[] = {const_cast<char *>("/bin/sh"),
                             const_cast<char *>("-c"),
                             const_cast<char *>("echo escaped >&2"), nullptr};
  char *const environment[] = {nullptr};
  execve("/bin/sh", arguments, environment);
  return false; // only reached if it failed
}

/// The probes only POSIX has.
inline bool probe(const std::string &name) {
  if (name == "fork") {
    // the filter allows clone for new threads only
    const pid_t child = fork();
    if (child == 0) {
      _exit(0);
    }
    return child > 0;
  }
  if (name == "inherited") {
    // any descriptor past the standard ones and the result's copy (3)
    for (int fd = 4; fd < 1024; fd++) {
      if (write(fd, "", 0) >= 0) {
        return true;
      }
    }
    return false;
  }
  std::fprintf(stderr, "unknown probe: %s\n", name.c_str());
  std::_Exit(2);
}

} // namespace os

#if defined(__linux__)
// ----------------------------------------------------------------------------
// Linux
// ----------------------------------------------------------------------------

#include <cerrno>
#include <cstddef>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <sys/prctl.h>

#if defined(__x86_64__)
#define CASCADIO_AUDIT_ARCH AUDIT_ARCH_X86_64
#elif defined(__aarch64__)
#define CASCADIO_AUDIT_ARCH AUDIT_ARCH_AARCH64
#else
#error "cascadio has no system call filter for this architecture"
#endif

namespace os {

/// Every system call the worker makes once it is locked down, and why.
static const int ALLOWED[] = {
    __NR_read,  __NR_write,                 // stdin; the GLB and messages
    __NR_brk,   __NR_mmap,  __NR_munmap,    // malloc and thread stacks
    __NR_mremap, __NR_mprotect, __NR_madvise, //
    __NR_futex, __NR_set_robust_list,       // threads
    __NR_rseq,  __NR_rt_sigprocmask,        //
    __NR_exit,  __NR_exit_group,            // a thread, the process
    __NR_rt_sigreturn, __NR_restart_syscall, // signals, resumed sleeps
    __NR_getrusage,                         // the memory watcher
    __NR_nanosleep, __NR_clock_nanosleep,   //
    __NR_clock_gettime, __NR_gettimeofday,  // when the vDSO falls back
    __NR_times,                             // OSD_Chronometer
    __NR_sched_yield,                       // OSD_ThreadPool
};

inline void installFilter() {
  std::vector<sock_filter> program = {
      // a call made through another architecture's entry point is refused
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, CASCADIO_AUDIT_ARCH, 1, 0),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
#if defined(__x86_64__)
      // and so is the x32 ABI
      BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000U, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
#endif
  };
  auto answer = [&program](long call, unsigned action) {
    program.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                               static_cast<unsigned>(call), 0, 1));
    program.push_back(BPF_STMT(BPF_RET | BPF_K, action));
  };
  for (const int call : ALLOWED) {
    answer(call, SECCOMP_RET_ALLOW);
  }
  // clone3 passes its flags in memory the filter cannot read; "not
  // implemented" makes glibc fall back to clone, whose flags it can
  answer(__NR_clone3, SECCOMP_RET_ERRNO | ENOSYS);
  // clone: new threads only, so no process can be started
  program.insert(
      program.end(),
      {
          BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clone, 0, 3),
          BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args)),
          BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, CLONE_THREAD, 0, 1),
          BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
          BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      });

  const sock_fprog filter = {static_cast<unsigned short>(program.size()),
                             program.data()};
  // TSYNC: every thread that already runs gets the filter too, and it
  // fails rather than leave one out
  if (syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER,
              SECCOMP_FILTER_FLAG_TSYNC, &filter) != 0) {
    cannotLockDown(std::strerror(errno));
  }
}

/// Apply the lock-down itself.
inline void confine() {
  const pid_t parent = getppid();
  // die with the parent, even if it is killed
  if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != parent) {
    cannotLockDown("the parent is gone");
  }
  // no debugger can attach and no other process can read our memory
  const rlimit noCore = {0, 0};
  if (prctl(PR_SET_DUMPABLE, 0) != 0 || setrlimit(RLIMIT_CORE, &noCore) != 0 ||
      prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
    cannotLockDown(std::strerror(errno));
  }
#ifndef CASCADIO_SANITIZED
  installFilter();
#endif
}

} // namespace os

#elif defined(__APPLE__)
// ----------------------------------------------------------------------------
// macOS
// ----------------------------------------------------------------------------

#include <sandbox.h>

namespace os {

/// Apply the lock-down itself.
inline void confine() {
  const rlimit noCore = {0, 0};
  setrlimit(RLIMIT_CORE, &noCore);
  // Deprecated since macOS 10.8 but still what Apple's own tools use; the
  // profile is fixed: computation and already open descriptors only.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  char *error = nullptr;
  if (sandbox_init(kSBXProfilePureComputation, SANDBOX_NAMED, &error) != 0) {
    cannotLockDown(error != nullptr ? error : "sandbox_init failed");
  }
#pragma clang diagnostic pop
}

} // namespace os

#else
#error "cascadio has no lock-down for this platform"
#endif

#else
// ============================================================================
// Windows
// ============================================================================

#include <winsock2.h> // before windows.h, which would bring the old one
#include <windows.h> // NOMINMAX and WIN32_LEAN_AND_MEAN come from CMake

#include <fcntl.h>
#include <io.h>
#include <psapi.h>

namespace os {

/// The parent lists the handles the worker inherits (process.hpp).
inline void closeInherited() {}

inline int keepResultStream() {
  // no translation of line endings in either direction
  _setmode(_fileno(stdin), _O_BINARY);
  const int result = _dup(1);
  _dup2(2, 1);
  _setmode(result, _O_BINARY);
  return result;
}

/// The parent starts the worker in its own folder (process.hpp).
inline void leaveWorkingDirectory() {}

inline void prepareCLibrary() {
  _putenv_s("TZ", "UTC0");
  _tzset();
}

inline long readSome(int fd, char *data, size_t size) {
  return _read(fd, data, static_cast<unsigned>(std::min<size_t>(size, 1 << 30)));
}
inline long writeSome(int fd, const char *data, size_t size) {
  return _write(fd, data, static_cast<unsigned>(std::min<size_t>(size, 1 << 30)));
}

inline uint64_t peakMemory() {
  PROCESS_MEMORY_COUNTERS counters = {};
  GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
  return counters.PeakWorkingSetSize;
}

/// Apply the lock-down itself.
inline void confine() {
  // A job that allows this process alone: it cannot start another, and
  // anything it holds dies with it.
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
  limits.BasicLimitInformation.LimitFlags =
      JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
      JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
  limits.BasicLimitInformation.ActiveProcessLimit = 1;
  if (job == nullptr ||
      !SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                               &limits, sizeof(limits)) ||
      !AssignProcessToJobObject(job, GetCurrentProcess())) {
    cannotLockDown("cannot create its job object");
  }

  // Low integrity: no writing to anything the user owns.
  HANDLE token = nullptr;
  SID_IDENTIFIER_AUTHORITY authority = SECURITY_MANDATORY_LABEL_AUTHORITY;
  PSID low = nullptr;
  bool lowered = false;
  if (OpenProcessToken(GetCurrentProcess(),
                       TOKEN_ADJUST_DEFAULT | TOKEN_QUERY, &token) &&
      AllocateAndInitializeSid(&authority, 1, SECURITY_MANDATORY_LOW_RID, 0,
                               0, 0, 0, 0, 0, 0, &low)) {
    TOKEN_MANDATORY_LABEL label = {};
    label.Label.Attributes = SE_GROUP_INTEGRITY;
    label.Label.Sid = low;
    lowered = SetTokenInformation(
                  token, TokenIntegrityLevel, &label,
                  static_cast<DWORD>(sizeof(label) + GetLengthSid(low))) != 0;
  }
  if (low != nullptr) {
    FreeSid(low);
  }
  if (token != nullptr) {
    CloseHandle(token);
  }
  if (!lowered) {
    cannotLockDown("cannot lower its integrity level");
  }

  // No generated code, no stray handle use, no injected DLLs.
  PROCESS_MITIGATION_DYNAMIC_CODE_POLICY code = {};
  code.ProhibitDynamicCode = 1;
  PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY handles = {};
  handles.RaiseExceptionOnInvalidHandleReference = 1;
  handles.HandleExceptionsPermanentlyEnabled = 1;
  PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY extensions = {};
  extensions.DisableExtensionPoints = 1;
  PROCESS_MITIGATION_IMAGE_LOAD_POLICY images = {};
  images.NoRemoteImages = 1;
  images.NoLowMandatoryLabelImages = 1;
  if (!SetProcessMitigationPolicy(ProcessDynamicCodePolicy, &code,
                                  sizeof(code)) ||
      !SetProcessMitigationPolicy(ProcessStrictHandleCheckPolicy, &handles,
                                  sizeof(handles)) ||
      !SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy,
                                  &extensions, sizeof(extensions)) ||
      !SetProcessMitigationPolicy(ProcessImageLoadPolicy, &images,
                                  sizeof(images))) {
    cannotLockDown("cannot set its mitigation policies");
  }
}

// What `--probe` tries (see probe() at the end). Each is true if it worked.

inline bool readFile(const std::string &path) {
  HANDLE file = CreateFileA(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, 0, nullptr);
  return file != INVALID_HANDLE_VALUE;
}

inline bool writeIn(const std::string &folder) {
  HANDLE file = CreateFileA((folder + "\\cascadio-probe").c_str(),
                            GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_FLAG_DELETE_ON_CLOSE, nullptr);
  return file != INVALID_HANDLE_VALUE;
}

inline bool connectTo(int port) {
  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    return false;
  }
  const SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<u_short>(port));
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return fd != INVALID_SOCKET &&
         connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
}

inline bool spawn() {
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION child = {};
  wchar_t command[] = L"cmd.exe /c exit 0";
  return CreateProcessW(nullptr, command, nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &child) != 0;
}

/// The probes only Windows has: none.
inline bool probe(const std::string &name) {
  std::fprintf(stderr, "unknown probe: %s\n", name.c_str());
  std::_Exit(2);
}

} // namespace os

#endif

// ============================================================================
// All platforms
// ============================================================================

/// The memory limit the watcher enforces: unknown until the request is
/// read, then a number of bytes, or 0 for none.
inline std::atomic<uint64_t> memoryLimit{UINT64_MAX};

/// Stop the whole process as soon as it has used more than the memory
/// limit. Memory has to be touched to count, which takes far longer than
/// the interval, so checking every few milliseconds cannot be outrun.
inline void startMemoryWatcher() {
  std::thread([] {
    while (true) {
      const uint64_t limit = memoryLimit.load();
      if (limit == 0) {
        return;
      }
      if (limit != UINT64_MAX && os::peakMemory() > limit) {
        std::_Exit(worker_exit::OUT_OF_MEMORY);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }).detach();
}

/// Set up, then lock down. `setUp` is where the caller does whatever needs
/// files while it still can.
template <typename SetUp> Locked lockDown(SetUp &&setUp) {
  os::closeInherited();
  const int result = os::keepResultStream();
  os::leaveWorkingDirectory();
  os::prepareCLibrary();
  setUp();
  // The watcher is the first thread, which the C library sets up for on
  // the way (glibc installs signal handlers). Starting it before the
  // lock-down puts that set-up before it too; the lock-down then holds
  // the watcher like every other thread.
  startMemoryWatcher();
  os::confine();
  return Locked(result);
}

inline std::vector<char> Locked::readInput() const {
  std::vector<char> input(1 << 16);
  size_t used = 0;
  long got;
  while ((got = os::readSome(0, input.data() + used, input.size() - used)) > 0) {
    used += static_cast<size_t>(got);
    if (used == input.size()) {
      input.resize(2 * used);
    }
  }
  input.resize(used);
  return input;
}

inline bool Locked::writeResult(const char *data, size_t size) const {
  while (size > 0) {
    const long wrote = os::writeSome(result_, data, size);
    if (wrote <= 0) {
      return false;
    }
    data += wrote;
    size -= static_cast<size_t>(wrote);
  }
  return true;
}

inline void Locked::limitMemory(uint64_t bytes) const {
  memoryLimit.store(bytes);
}

/// `cascadio-worker --probe NAME [ARGUMENT]`: try one thing the lock-down
/// forbids. If it works this prints "escaped" and exits 0, so a test can
/// tell; otherwise it exits 1. On Linux the attempt kills the process.
/// "allowed" does something the lock-down allows, so a test can see that
/// a probe reports success.
[[noreturn]] inline void probe(const Locked &, const std::string &name,
                               const std::string &argument) {
#if defined(CASCADIO_SANITIZED) && defined(__linux__)
  // nothing to show: a sanitizer build has no system call filter
  (void)name;
  (void)argument;
  std::_Exit(77);
#else
  bool escaped = false;
  if (name == "allowed") {
    escaped = os::writeSome(2, "", 0) >= 0;
  } else if (name == "read") {
    escaped = os::readFile(argument);
  } else if (name == "thread-read") {
    // a thread started after the lock-down is held by it too
    std::thread([&] { escaped = os::readFile(argument); }).join();
  } else if (name == "write") {
    escaped = os::writeIn(argument);
  } else if (name == "connect") {
    escaped = os::connectTo(std::atoi(argument.c_str()));
  } else if (name == "spawn") {
    escaped = os::spawn();
  } else {
    escaped = os::probe(name);
  }
  if (escaped) {
    os::writeSome(2, "escaped\n", 8);
    std::_Exit(0);
  }
  std::_Exit(1);
#endif
}
