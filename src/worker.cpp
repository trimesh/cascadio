// cascadio-worker: does one conversion and exits.
//
// The Python module starts this program for every conversion, so that a
// file which makes OpenCASCADE hang, run out of memory or crash can be
// stopped without harming the caller. See request.hpp for what goes in
// on standard input and what comes out.
//
// Before it reads a byte of input it locks itself down (sandbox.hpp): from
// then on it can compute and use its standard streams, nothing else. All
// that differs between operating systems is in sandbox.hpp.
//
// It can also be run by hand, for example to debug a file:
//     (printf 'file_type=1\n\n'; cat model.step) | cascadio-worker > model.glb
// Set CASCADIO_TRACE=1 to print how long each stage takes (see trace.hpp).
// To see which system call a locked-down worker was stopped for, start it
// under `strace -f`.

#include "convert.hpp"
#include "request.hpp"
#include "sandbox.hpp"

#include <IGESControl_Controller.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <OSD_Parallel.hxx>
#include <OSD_ThreadPool.hxx>
#include <STEPCAFControl_Controller.hxx>
#include <ShapeExtend.hxx>
#include <Standard_Failure.hxx>

#include <algorithm>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

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

/// The text of an OpenCASCADE error. It became a std::exception in OCCT 8;
/// the template keeps the branch for the other version from being compiled.
template <typename Failure> static std::string failureText(const Failure &f) {
  if constexpr (std::is_base_of_v<std::exception, Failure>) {
    return f.what();
  } else {
    return f.GetMessageString();
  }
}

/// Whatever OpenCASCADE would set up on first use by opening a file, done
/// while it still can. First the in-memory files, so OCCT opens no other.
static void setUpOpenCascade() {
  Message::DefaultMessenger()->RemovePrinters(STANDARD_TYPE(Message_Printer));
  memoryFiles();
  STEPCAFControl_Controller::Init(); // message and resource files
  IGESControl_Controller::Init();    // also looks up the user's name
  ShapeExtend::Init();
  // counting processors reads /sys; the pool's threads start when used
  OSD_Parallel::NbLogicalProcessors();
  OSD_ThreadPool::DefaultPool();
}

static Glb run(const Locked &locked, const Request &request, const char *data,
               size_t size) {
  ConvertOptions options;
  options.mesh = {request.tol_linear, request.tol_angular,
                  request.tol_relative, request.use_parallel};
  options.merge_primitives = request.merge_primitives;
  options.include_brep = request.include_brep;
  options.brep_types = request.brep_types;
  options.include_materials = request.include_materials;
  options.node_name_format =
      static_cast<RWMesh_NameFormat>(request.node_name_format);
  options.y_up = request.y_up;
  return toGlb(locked, data, size, request.file_type, options);
}

/// Read the request and the file, convert, write the result, exit.
[[noreturn]] static void serve(const Locked &locked) {
  int code = worker_exit::FAILED;
  try {
    // everything on standard input: the request, an empty line, the file
    Span total("total");
    std::vector<char> input;
    {
      Span span("receive");
      input = locked.readInput();
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
    locked.limitMemory(request.memory_limit);

    const Glb glb =
        run(locked, request, input.data() + start, input.size() - start);
    {
      Span span("send");
      if (!glb.writeTo([&locked](const char *data, size_t size) {
            return locked.writeResult(data, size);
          })) {
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

int main(int argc, char **argv) {
  const Locked locked = lockDown(setUpOpenCascade);
  if (argc >= 3 && std::string(argv[1]) == "--probe") {
    probe(locked, argv[2], argc > 3 ? argv[3] : ""); // for the tests
  }
  serve(locked);
}
