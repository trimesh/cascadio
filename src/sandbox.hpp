#pragma once

// Everything the worker does that depends on the operating system, which
// is mostly its lock-down. After `lockDown()` the process can compute,
// start threads, read its input and write its result: nothing else. No
// files, no network, no other processes. The worker applies it to itself,
// because only the worker knows when its set-up is over, and so it holds
// however the worker was started: by the module, the fuzzer or by hand.
//
// Linux: a fixed seccomp allowlist; any other call kills the process.
// macOS: Apple's fixed "pure computation" sandbox profile.
// Windows: an AppContainer with no capabilities, so no network and none
// of the user's files; a job that allows no other process; and process
// mitigations.
//
// Nothing here can be switched off. If the lock-down cannot be applied the
// worker refuses to convert and says why (exit code NO_SANDBOX), with one
// exception: a Linux kernel that cannot filter system calls at all. There
// the worker converts without the filter and says so on standard error,
// which the module turns into a Python warning. A sanitizer build leaves
// out the filter too, so that a sanitizer can still print its report.
//
// Each platform's section defines the same functions in `os`:
//     int prepare()            set up while files can still be opened;
//                              returns where the result goes
//     void confine()           the lock-down itself
//     long readSome(...), long writeSome(...)
//     uint64_t peakMemory()
//     bool attempt(name, argument)   for `--probe`; true if it worked
// The code all platforms share is at the end.

#include "request.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
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

/// Refuse to convert: the lock-down could not be applied. The module puts
/// "cascadio could not sandbox its converter, so it did not run it: "
/// before `why`, which should say what to do about it.
[[noreturn]] inline void cannotLockDown(const std::string &why) {
  std::fprintf(stderr, "%s\n", why.c_str());
  std::fflush(stderr);
  std::_Exit(worker_exit::NO_SANDBOX);
}

/// A line the module shows as a Python warning.
inline void warn(const char *text) {
  std::fprintf(stderr, "%s%s\n", worker_warning, text);
  std::fflush(stderr);
}

#ifndef _WIN32
// ============================================================================
// POSIX: Linux and macOS
// ============================================================================

#include <arpa/inet.h>
#include <csignal>
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

inline int prepare() {
  // Close every descriptor but standard input, output and error, so
  // nothing the parent leaked stays reachable.
#ifdef SYS_close_range
  if (syscall(SYS_close_range, 3U, ~0U, 0U) != 0)
#endif
  {
    rlimit limit = {};
    getrlimit(RLIMIT_NOFILE, &limit);
    for (rlim_t fd = 3; fd < limit.rlim_cur && fd < (1U << 20); fd++) {
      close(static_cast<int>(fd));
    }
  }
  // Keep the real standard output aside for the result and point
  // descriptor 1 at standard error, so a stray print from any library
  // cannot corrupt the result.
  const int result = dup(1);
  dup2(2, 1);
  // OCCT reads message files relative to the working directory: make that
  // somewhere nobody can plant one.
  if (chdir("/") != 0) {
    cannotLockDown(std::string("cannot leave the working directory (") +
                   std::strerror(errno) + ")");
  }
  // What the C library would otherwise set up on first use, by opening a
  // file. With TZ unset, every localtime() reads /etc/localtime again.
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
  return result;
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

/// What `--probe` tries (see probe() at the end); true if it worked.
inline bool attempt(const std::string &name, const std::string &argument) {
  if (name == "read") {
    return open(argument.c_str(), O_RDONLY) >= 0;
  }
  if (name == "write") {
    return open((argument + "/cascadio-probe").c_str(),
                O_WRONLY | O_CREAT | O_EXCL, 0600) >= 0;
  }
  if (name == "connect") {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(std::atoi(argument.c_str())));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return fd >= 0 && connect(fd, reinterpret_cast<sockaddr *>(&address),
                              sizeof(address)) == 0;
  }
  if (name == "spawn") {
    char *const arguments[] = {const_cast<char *>("/bin/sh"),
                               const_cast<char *>("-c"),
                               const_cast<char *>("echo escaped >&2"), nullptr};
    char *const environment[] = {nullptr};
    execve("/bin/sh", arguments, environment);
    return false; // only reached if it failed
  }
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

#include <cstddef>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <sys/prctl.h>

// the kernel headers of manylinux_2_28 predate clone3
#ifndef __NR_clone3
#define __NR_clone3 435 // the same on x86-64 and ARM64
#endif

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
    __NR_getpid, __NR_gettid, __NR_rt_sigaction, // abort(), with tgkill
};

/// A kernel that cannot filter system calls: convert anyway, and say so.
inline void unfiltered() {
  warn("cascadio is converting without its sandbox: this Linux kernel "
       "cannot filter system calls (seccomp; WSL 1, kernels before 4.14 "
       "and some emulators lack it). A damaged or hostile file could "
       "reach files and the network.");
}

inline void installFilter() {
  // Killing the whole process needs Linux 4.14. Before that a forbidden
  // call would kill one thread and leave the process hanging.
  unsigned killProcess = SECCOMP_RET_KILL_PROCESS;
  if (syscall(__NR_seccomp, SECCOMP_GET_ACTION_AVAIL, 0, &killProcess) != 0) {
    if (errno == EINVAL || errno == ENOSYS) {
      return unfiltered();
    }
    cannotLockDown(std::string("the system call filter is unavailable (") +
                   std::strerror(errno) + ")");
  }
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
  // Two calls are allowed only for some first arguments:
  //   clone, for new threads only, so no process can be started;
  //   tgkill, for this process only, which is how abort() ends it with
  //   SIGABRT. The process id cannot change: there is no fork.
  auto allowIf = [&program](long call, unsigned test, unsigned value) {
    program.insert(
        program.end(),
        {
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<unsigned>(call),
                     0, 4),
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args)),
            BPF_JUMP(BPF_JMP | test | BPF_K, value, 0, 1),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        });
  };
  allowIf(__NR_clone, BPF_JSET, CLONE_THREAD);
  allowIf(__NR_tgkill, BPF_JEQ, static_cast<unsigned>(getpid()));
  // and anything else
  program.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));

  const sock_fprog filter = {static_cast<unsigned short>(program.size()),
                             program.data()};
  // TSYNC: every thread that already runs gets the filter too, and it
  // fails rather than leave one out
  if (syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER,
              SECCOMP_FILTER_FLAG_TSYNC, &filter) != 0) {
    if (errno == EINVAL || errno == ENOSYS) {
      return unfiltered();
    }
    cannotLockDown(std::string("the system call filter could not be "
                               "installed (") +
                   std::strerror(errno) + ")");
  }
}

inline void confine() {
  const pid_t parent = getppid();
  // die with the parent, even if it is killed
  if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != parent) {
    cannotLockDown("the program that started it is gone");
  }
  // no debugger can attach and no other process can read our memory
  const rlimit noCore = {0, 0};
  if (prctl(PR_SET_DUMPABLE, 0) != 0 || setrlimit(RLIMIT_CORE, &noCore) != 0 ||
      prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
    cannotLockDown(std::string("could not restrict the process (") +
                   std::strerror(errno) + ")");
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

inline void confine() {
  const rlimit noCore = {0, 0};
  setrlimit(RLIMIT_CORE, &noCore);
  // Deprecated since macOS 10.8 but still what Apple's own tools use; the
  // profile is fixed: computation and already open descriptors only.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  char *error = nullptr;
  if (sandbox_init(kSBXProfilePureComputation, SANDBOX_NAMED, &error) != 0) {
    cannotLockDown(std::string("macOS would not apply its sandbox (") +
                   (error != nullptr ? error : "sandbox_init failed") +
                   "); a program that is itself sandboxed cannot run it");
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

// The few functions used from userenv.h and aclapi.h, declared here: both
// headers bring all of OLE along, which does not compile after OCCT has
// trimmed <windows.h> (NOMSG, NOUSER, ...).
extern "C" __declspec(dllimport) HRESULT WINAPI
DeriveAppContainerSidFromAppContainerName(PCWSTR name, PSID *sid);
extern "C" __declspec(dllimport) HRESULT WINAPI CreateAppContainerProfile(
    PCWSTR name, PCWSTR displayName, PCWSTR description,
    PSID_AND_ATTRIBUTES capabilities, DWORD capabilityCount, PSID *sid);
extern "C" __declspec(dllimport) DWORD WINAPI SetNamedSecurityInfoW(
    LPWSTR name, int objectType /* SE_OBJECT_TYPE: 1 is a file */,
    SECURITY_INFORMATION what, PSID owner, PSID group, PACL dacl, PACL sacl);

namespace os {

static const wchar_t CONTAINER[] = L"cascadio.worker";

/// The text Windows has for an error code.
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

/// Add one entry for `sid` to a file's or folder's permissions: a denial
/// goes first, an allowance after the explicit denials. Inheritable
/// entries reach what the folder already holds. (aclapi.h would do this in
/// fewer lines, but see above.)
inline bool addPermission(const wchar_t *path, PSID sid, bool allow,
                          DWORD rights, BYTE inheritance) {
  DWORD needed = 0;
  GetFileSecurityW(path, DACL_SECURITY_INFORMATION, nullptr, 0, &needed);
  std::vector<char> current(needed);
  BOOL present = FALSE;
  BOOL defaulted = FALSE;
  PACL old = nullptr;
  ACL_SIZE_INFORMATION oldSize = {};
  if (!GetFileSecurityW(path, DACL_SECURITY_INFORMATION, current.data(),
                        needed, &needed) ||
      !GetSecurityDescriptorDacl(current.data(), &present, &old,
                                 &defaulted) ||
      !present || old == nullptr ||
      !GetAclInformation(old, &oldSize, sizeof(oldSize), AclSizeInformation)) {
    return false;
  }
  const DWORD size = oldSize.AclBytesInUse + sizeof(ACCESS_ALLOWED_ACE) +
                     GetLengthSid(sid);
  std::vector<char> buffer(size);
  PACL updated = reinterpret_cast<PACL>(buffer.data());
  if (!InitializeAcl(updated, size, ACL_REVISION)) {
    return false;
  }
  bool added = false;
  auto add = [&] {
    added = allow ? AddAccessAllowedAceEx(updated, ACL_REVISION, inheritance,
                                          rights, sid) != 0
                  : AddAccessDeniedAceEx(updated, ACL_REVISION, inheritance,
                                         rights, sid) != 0;
    return added;
  };
  for (DWORD i = 0; i < oldSize.AceCount; i++) {
    void *entry = nullptr;
    if (!GetAce(old, i, &entry)) {
      continue;
    }
    const ACE_HEADER *header = static_cast<ACE_HEADER *>(entry);
    const bool explicitDenial = header->AceType == ACCESS_DENIED_ACE_TYPE &&
                                (header->AceFlags & INHERITED_ACE) == 0;
    if (!added && !(allow && explicitDenial) && !add()) {
      return false;
    }
    AddAce(updated, ACL_REVISION, MAXDWORD, entry, header->AceSize);
  }
  if (!added && !add()) {
    return false;
  }
  return SetNamedSecurityInfoW(const_cast<wchar_t *>(path), 1,
                               DACL_SECURITY_INFORMATION, nullptr, nullptr,
                               updated, nullptr) == ERROR_SUCCESS;
}

/// Register the AppContainer for this user. Windows gives every container
/// a folder its processes may write to, which every call would share:
/// deny that too.
inline void registerContainer(PSID container) {
  PSID registered = nullptr;
  const HRESULT made = CreateAppContainerProfile(
      CONTAINER, L"cascadio worker",
      L"Converts one CAD file with nothing else allowed", nullptr, 0,
      &registered);
  if (FAILED(made) && made != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
    cannotLockDown(
        "Windows would not create the sandbox profile 'cascadio.worker' "
        "for this account (" +
        systemText(static_cast<DWORD>(made)) +
        "); accounts without a loaded user profile, such as some services, "
        "cannot");
  }
  if (registered != nullptr) {
    FreeSid(registered);
  }
  wchar_t local[MAX_PATH];
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
  if (length > 0 && length < MAX_PATH) {
    const std::wstring folder =
        std::wstring(local) + L"\\Packages\\cascadio.worker\\AC";
    addPermission(folder.c_str(), container, false,
                  FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA |
                      FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD | DELETE |
                      WRITE_DAC | WRITE_OWNER,
                  OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE);
  }
}

/// Whether this process runs in our AppContainer and only that: the
/// container must be cascadio's and allow nothing (no capabilities).
/// Inside another app's container the worker refuses.
inline bool contained(PSID ours) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    cannotLockDown("could not inspect its own process (" +
                   systemText(GetLastError()) + ")");
  }
  DWORD isContainer = 0;
  DWORD size = 0;
  GetTokenInformation(token, TokenIsAppContainer, &isContainer,
                      sizeof(isContainer), &size);
  bool ours_ = false;
  if (isContainer != 0) {
    std::vector<char> sid(256);
    std::vector<char> capabilities(sizeof(TOKEN_GROUPS) + 256);
    GetTokenInformation(token, TokenAppContainerSid, sid.data(),
                        static_cast<DWORD>(sid.size()), &size);
    const bool gotCapabilities =
        GetTokenInformation(token, TokenCapabilities, capabilities.data(),
                            static_cast<DWORD>(capabilities.size()), &size) != 0;
    const auto *container =
        reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION *>(sid.data());
    const auto *groups = reinterpret_cast<TOKEN_GROUPS *>(capabilities.data());
    ours_ = container->TokenAppContainer != nullptr &&
            EqualSid(container->TokenAppContainer, ours) && gotCapabilities &&
            groups->GroupCount == 0;
  }
  CloseHandle(token);
  if (isContainer != 0 && !ours_) {
    cannotLockDown("it is already inside another app's sandbox, which "
                   "allows more than cascadio's");
  }
  return ours_;
}

/// Windows can put a process in an AppContainer only when it starts it.
/// So unless this already is that process, start this program again in
/// our AppContainer, wait for it and leave with its exit code. It gets the
/// same standard streams and arguments, and dies with this one.
inline void enterContainer() {
  PSID container = nullptr;
  if (FAILED(DeriveAppContainerSidFromAppContainerName(CONTAINER,
                                                       &container))) {
    cannotLockDown("Windows could not name its sandbox (AppContainer)");
  }
  if (contained(container)) {
    FreeSid(container);
    return;
  }
  SECURITY_CAPABILITIES capabilities = {}; // and no capabilities
  capabilities.AppContainerSid = container;

  // the standard streams are the only handles it inherits
  const HANDLE streams[] = {GetStdHandle(STD_INPUT_HANDLE),
                            GetStdHandle(STD_OUTPUT_HANDLE),
                            GetStdHandle(STD_ERROR_HANDLE)};
  std::vector<HANDLE> inherited;
  for (HANDLE stream : streams) {
    if (stream != nullptr && stream != INVALID_HANDLE_VALUE &&
        std::find(inherited.begin(), inherited.end(), stream) ==
            inherited.end()) {
      SetHandleInformation(stream, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
      inherited.push_back(stream);
    }
  }
  SIZE_T size = 0;
  InitializeProcThreadAttributeList(nullptr, 2, 0, &size);
  std::vector<char> attributes(size);
  STARTUPINFOEXW startup = {};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = streams[0];
  startup.StartupInfo.hStdOutput = streams[1];
  startup.StartupInfo.hStdError = streams[2];
  startup.lpAttributeList =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
  if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 2, 0,
                                         &size) ||
      !UpdateProcThreadAttribute(
          startup.lpAttributeList, 0,
          PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &capabilities,
          sizeof(capabilities), nullptr, nullptr) ||
      !UpdateProcThreadAttribute(startup.lpAttributeList, 0,
                                 PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                 inherited.data(),
                                 inherited.size() * sizeof(HANDLE), nullptr,
                                 nullptr)) {
    cannotLockDown("Windows could not prepare its sandbox (" +
                   systemText(GetLastError()) + ")");
  }

  wchar_t program[32768];
  GetModuleFileNameW(nullptr, program, 32768);
  // OCCT reads message files from the working directory: start it in one
  // it may read and nobody can plant a file in
  wchar_t system[MAX_PATH];
  GetSystemDirectoryW(system, MAX_PATH);
  std::wstring command = GetCommandLineW();
  // without a console of its own (the module starts it detached) the
  // copy gets none either, so no console window is made for it
  const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED |
                      (GetConsoleWindow() == nullptr ? DETACHED_PROCESS : 0);
  PROCESS_INFORMATION child = {};
  // The first time for this user Windows does not know the container yet
  // (error 2), and the program may not be the container's to read (error
  // 5). Each is fixed once, when it is in the way, in whichever order.
  bool registered = false;
  bool shared = false;
  DWORD error = 0;
  for (int start = 0; start < 3; start++) {
    if (CreateProcessW(program, command.data(), nullptr, nullptr, TRUE, flags,
                       nullptr, system, &startup.StartupInfo, &child)) {
      error = 0;
      break;
    }
    error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND && !registered) {
      registered = true;
      registerContainer(container);
    } else if (error == ERROR_ACCESS_DENIED && !shared) {
      shared = true;
      addPermission(program, container, true,
                    FILE_GENERIC_READ | FILE_GENERIC_EXECUTE, 0);
    } else {
      break;
    }
  }
  DeleteProcThreadAttributeList(startup.lpAttributeList);
  FreeSid(container);
  if (error == ERROR_ACCESS_DENIED) {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    cannotLockDown(std::string("Windows would not let the sandbox read ") +
                   path + "; an administrator can run: icacls \"" + path +
                   "\" /grant *S-1-15-2-1:RX");
  }
  if (error != 0) {
    cannotLockDown("Windows would not start the sandboxed converter (" +
                   systemText(error) + ")");
  }

  // When this process ends, the job's handle closes and kills it: this is
  // the process the module stops on a timeout.
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (job == nullptr ||
      !SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                               &limits, sizeof(limits)) ||
      !AssignProcessToJobObject(job, child.hProcess)) {
    error = GetLastError();
    TerminateProcess(child.hProcess, worker_exit::NO_SANDBOX);
    cannotLockDown("Windows could not tie the sandboxed converter to it (" +
                   systemText(error) + ")");
  }
  ResumeThread(child.hThread);
  WaitForSingleObject(child.hProcess, INFINITE);
  DWORD code = worker_exit::FAILED;
  GetExitCodeProcess(child.hProcess, &code);
  ExitProcess(code);
}

inline int prepare() {
  // a crash shows no dialog; the module reports it
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
               SEM_NOOPENFILEERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  enterContainer();
  // no translation of line endings in either direction
  _setmode(_fileno(stdin), _O_BINARY);
  const int result = _dup(1);
  _dup2(2, 1);
  _setmode(result, _O_BINARY);
  _putenv_s("TZ", "UTC0");
  _tzset();
  return result;
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
    cannotLockDown("Windows could not limit it to one process (" +
                   systemText(GetLastError()) + ")");
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
    cannotLockDown("Windows would not apply its process protections (" +
                   systemText(GetLastError()) + ")");
  }
}

/// What `--probe` tries (see probe() at the end); true if it worked.
inline bool attempt(const std::string &name, const std::string &argument) {
  if (name == "read") {
    return CreateFileA(argument.c_str(), GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                       OPEN_EXISTING, 0, nullptr) != INVALID_HANDLE_VALUE;
  }
  if (name == "write") {
    return CreateFileA((argument + "\\cascadio-probe").c_str(), GENERIC_WRITE,
                       0, nullptr, CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE,
                       nullptr) != INVALID_HANDLE_VALUE;
  }
  if (name == "connect") {
    // ws2_32.dll is delay-loaded (CMakeLists.txt): only this loads it
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      return false;
    }
    const SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) {
      return false;
    }
    // A sandboxed connect is dropped, not refused, and Windows would retry
    // for 21 seconds: wait 2.
    u_long nonBlocking = 1;
    ioctlsocket(fd, static_cast<long>(FIONBIO), &nonBlocking);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port =
        htons(static_cast<u_short>(std::atoi(argument.c_str())));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(fd, &writable);
    timeval wait = {2, 0};
    if (select(0, nullptr, &writable, nullptr, &wait) != 1) {
      return false;
    }
    int failure = 0;
    int length = sizeof(failure);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&failure),
               &length);
    return failure == 0;
  }
  if (name == "spawn") {
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child = {};
    wchar_t command[] = L"cmd.exe /c exit 0";
    return CreateProcessW(nullptr, command, nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                          &child) != 0;
  }
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
#ifdef __APPLE__
  // macOS cannot have a process die with its parent: notice instead
  const pid_t parent = getppid();
#endif
  std::thread([=] {
    while (true) {
      const uint64_t limit = memoryLimit.load();
      if (limit != UINT64_MAX && limit != 0 && os::peakMemory() > limit) {
        std::_Exit(worker_exit::OUT_OF_MEMORY);
      }
#ifdef __APPLE__
      if (getppid() != parent) {
        std::_Exit(worker_exit::FAILED);
      }
#else
      if (limit == 0) {
        return; // nothing left to watch
      }
#endif
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }).detach();
}

/// Set up, then lock down. `setUp` is where the caller does whatever needs
/// files while it still can.
template <typename SetUp> Locked lockDown(SetUp &&setUp) {
  const int result = os::prepare();
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
/// a probe reports success; "abort" calls abort(), which must end as a
/// crash (SIGABRT, or exit code 3 on Windows).
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
  } else if (name == "abort") {
    std::abort();
  } else if (name == "thread-read") {
    // a thread started after the lock-down is held by it too
    std::thread([&] { escaped = os::attempt("read", argument); }).join();
  } else {
    escaped = os::attempt(name, argument);
  }
  if (escaped) {
    os::writeSome(2, "escaped\n", 8);
    std::_Exit(0);
  }
  std::_Exit(1);
#endif
}
