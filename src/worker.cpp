// cascadio-worker: does one conversion and exits.
//
// The Python module starts this program for every conversion, so that a
// file which makes OpenCASCADE hang, run out of memory or crash can be
// stopped without harming the caller. See request.hpp for what goes in
// on standard input and what comes out.
//
// It can also be run by hand, for example to debug a file:
//     (printf 'file_type=1\n\n'; cat model.step) | cascadio-worker > model.glb
// Set CASCADIO_TRACE=1 to print how long each stage takes (see trace.hpp).

#include "convert.hpp"
#include "request.hpp"

#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <Standard_Failure.hxx>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <new>
#include <sstream>
#include <thread>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#include <windows.h> // NOMINMAX and WIN32_LEAN_AND_MEAN come from CMake

#include <fcntl.h>
#include <io.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

// the numbers in request.hpp are passed straight to OpenCASCADE
static_assert(static_cast<int>(NodeNameFormat::PRODUCT) ==
                  RWMesh_NameFormat_Product &&
              static_cast<int>(NodeNameFormat::INSTANCE) ==
                  RWMesh_NameFormat_Instance &&
              static_cast<int>(NodeNameFormat::INSTANCE_OR_PRODUCT) ==
                  RWMesh_NameFormat_InstanceOrProduct &&
              static_cast<int>(NodeNameFormat::PRODUCT_OR_INSTANCE) ==
                  RWMesh_NameFormat_ProductOrInstance &&
              static_cast<int>(NodeNameFormat::PRODUCT_AND_INSTANCE) ==
                  RWMesh_NameFormat_ProductAndInstance,
              "NodeNameFormat must match RWMesh_NameFormat");

/// The most physical memory this process has used so far, in bytes.
static uint64_t peakMemory() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS counters = {};
  GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
  return counters.PeakWorkingSetSize;
#else
  rusage usage = {};
  getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
  return static_cast<uint64_t>(usage.ru_maxrss); // bytes
#else
  return static_cast<uint64_t>(usage.ru_maxrss) * 1024; // kilobytes
#endif
#endif
}

/// Stop the whole process as soon as it has used more than `limit` bytes.
/// Memory has to be touched to count, which takes far longer than the
/// interval, so checking every few milliseconds cannot be outrun.
static void watchMemory(uint64_t limit) {
  std::thread([limit] {
    while (true) {
      if (peakMemory() > limit) {
        std::_Exit(worker_exit::OUT_OF_MEMORY);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }).detach();
}

/// The text of an OpenCASCADE error. It became a std::exception in OCCT 8;
/// the template keeps the branch for the other version from being compiled.
template <typename Failure> static std::string failureText(const Failure &f) {
  if constexpr (std::is_base_of_v<std::exception, Failure>) {
    return f.what();
  } else {
    return f.GetMessageString();
  }
}

static std::vector<char> run(const Request &request, const char *data,
                             size_t size) {
  const MeshOptions mesh = {request.tol_linear, request.tol_angular,
                            request.tol_relative, request.use_parallel};
  if (request.output == "obj") {
    stepToObj(data, size, request.output_path, mesh, request.use_colors);
    return {};
  }
  if (request.output != "glb") {
    throw std::invalid_argument("unknown output: " + request.output);
  }
  ConvertOptions options;
  options.mesh = mesh;
  options.merge_primitives = request.merge_primitives;
  options.include_brep = request.include_brep;
  options.brep_types = request.brep_types;
  options.include_materials = request.include_materials;
  options.node_name_format =
      static_cast<RWMesh_NameFormat>(request.node_name_format);
  options.y_up = request.y_up;
  return toGlb(data, size, request.file_type, options);
}

int main() {
#ifdef _WIN32
  // no translation of line endings in either direction
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  // Nothing but the result may reach standard output. Keep the real one
  // aside and point descriptor 1 at standard error, so a stray print from
  // any library cannot corrupt the result.
#ifdef _WIN32
  const int result_fd = _dup(1);
  _dup2(2, 1);
  _setmode(result_fd, _O_BINARY);
  FILE *result_stream = _fdopen(result_fd, "wb");
#else
  const int result_fd = dup(1);
  dup2(2, 1);
  FILE *result_stream = fdopen(result_fd, "wb");
#endif
  Message::DefaultMessenger()->RemovePrinters(STANDARD_TYPE(Message_Printer));

  int code = worker_exit::FAILED;
  try {
    // everything on standard input: the request, an empty line, the file
    Span total("total");
    std::vector<char> input;
    {
      Span span("receive");
      char buffer[65536];
      size_t got;
      while ((got = std::fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
        input.insert(input.end(), buffer, buffer + got);
      }
    }
    // the request ends at the first empty line; parse only that part
    static const char marker[] = "\n\n";
    const auto end =
        std::search(input.begin(), input.end(), marker, marker + 2);
    if (end == input.end()) {
      throw std::invalid_argument("the request has no end marker");
    }
    std::istringstream head(std::string(input.begin(), end + 1));
    const Request request = Request::parse(head);
    const size_t start = static_cast<size_t>(end - input.begin()) + 2;
    if (request.memory_limit > 0) {
      watchMemory(request.memory_limit);
    }

    const std::vector<char> output =
        run(request, input.data() + start, input.size() - start);
    {
      Span span("send");
      if (std::fwrite(output.data(), 1, output.size(), result_stream) !=
              output.size() ||
          std::fflush(result_stream) != 0) {
        throw std::runtime_error("could not write the result");
      }
    }
    code = worker_exit::OK;
  } catch (const std::invalid_argument &error) {
    std::cerr << error.what() << std::endl;
    code = worker_exit::INVALID_INPUT;
  } catch (const std::bad_alloc &) {
    code = worker_exit::OUT_OF_MEMORY;
  } catch (const Standard_Failure &failure) {
    std::cerr << "OpenCASCADE error: " << failureText(failure) << std::endl;
  } catch (const std::exception &error) {
    std::cerr << error.what() << std::endl;
  }
  // skip destructors of OpenCASCADE's globals: the work is done
  std::_Exit(code);
}
