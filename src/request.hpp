#pragma once

#include <cstdint>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

/// What the Python module asks the worker program to do. This header is the
/// whole contract between the two and uses nothing from OpenCASCADE or
/// Python, so both sides can include it.
///
/// The module writes to the worker's standard input:
///     the request as text, one `key=value` per line, ended by an empty line
///     the input file
/// The worker writes the GLB to standard output and exits with one of the
/// codes below. On failure the last line it writes to standard error says
/// why. A line on standard error that starts with `worker_warning` is
/// something the user should know, shown as a Python warning.

enum class FileType { UNSPECIFIED = 0, STEP = 1, IGES = 2 };

/// Which name a GLB node gets. The values match OpenCASCADE's
/// RWMesh_NameFormat, which the worker checks at compile time.
enum class NodeNameFormat {
  PRODUCT = 1,
  INSTANCE = 2,
  INSTANCE_OR_PRODUCT = 3,
  PRODUCT_OR_INSTANCE = 4,
  PRODUCT_AND_INSTANCE = 5,
};

/// How the worker ended, as its process exit code.
namespace worker_exit {
constexpr int OK = 0;
constexpr int INVALID_INPUT = 64;   // -> ValueError
constexpr int FAILED = 65;          // -> RuntimeError
constexpr int OUT_OF_MEMORY = 66;   // -> MemoryError
constexpr int NO_SANDBOX = 67;      // -> RuntimeError: could not lock down
constexpr int MISMATCH = 68;        // -> RuntimeError: another version
} // namespace worker_exit

constexpr char worker_warning[] = "cascadio warning: ";

/// A request the worker cannot read: the module that wrote it is from
/// another version.
struct RequestError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Request {
  FileType file_type = FileType::STEP;
  double tol_linear = 0.01;
  double tol_angular = 0.5;
  bool tol_relative = false;
  bool merge_primitives = true;
  bool use_parallel = true;
  bool include_brep = false;
  std::set<std::string> brep_types;
  NodeNameFormat node_name_format = NodeNameFormat::INSTANCE_OR_PRODUCT;
  /// Rotate from CAD's Z-up into glTF's Y-up: (x, y, z) -> (x, z, -y)
  bool y_up = false;

  /// Every field with its key, the one list serialize() and parse() use.
  /// `brep_types` is one `brep_type` line per type.
  template <typename Self, typename Visit>
  static void fields(Self &request, Visit &&visit) {
    visit("file_type", request.file_type);
    visit("tol_linear", request.tol_linear);
    visit("tol_angular", request.tol_angular);
    visit("tol_relative", request.tol_relative);
    visit("merge_primitives", request.merge_primitives);
    visit("use_parallel", request.use_parallel);
    visit("include_brep", request.include_brep);
    visit("brep_type", request.brep_types);
    visit("node_name_format", request.node_name_format);
    visit("y_up", request.y_up);
  }

  std::string serialize() const {
    std::ostringstream text;
    text.precision(17); // enough digits to round-trip a double
    fields(*this, [&text](const char *key, const auto &value) {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, std::set<std::string>>) {
        for (const std::string &item : value) {
          text << key << '=' << item << '\n';
        }
      } else if constexpr (std::is_enum_v<T>) {
        text << key << '=' << static_cast<int>(value) << '\n';
      } else {
        text << key << '=' << value << '\n'; // a bool is 0 or 1
      }
    });
    text << '\n';
    return text.str();
  }

  /// Read the text form from a stream, leaving it at the start of the input
  /// file. Throws RequestError on anything unexpected, which means the
  /// module and the worker come from different versions.
  static Request parse(std::istream &stream) try {
    Request request;
    std::string line;
    while (std::getline(stream, line) && !line.empty()) {
      const size_t split = line.find('=');
      if (split == std::string::npos) {
        throw RequestError("malformed request line: " + line);
      }
      const std::string key = line.substr(0, split);
      const std::string value = line.substr(split + 1);
      bool known = false;
      fields(request, [&](const char *name, auto &field) {
        if (key != name) {
          return;
        }
        known = true;
        using T = std::decay_t<decltype(field)>;
        if constexpr (std::is_same_v<T, std::set<std::string>>) {
          field.insert(value);
        } else if constexpr (std::is_same_v<T, bool>) {
          field = value == "1";
        } else if constexpr (std::is_enum_v<T>) {
          field = static_cast<T>(std::stoi(value));
        } else {
          static_assert(std::is_same_v<T, double>, "no reader for this type");
          field = std::stod(value);
        }
      });
      if (!known) {
        throw RequestError("unknown request key: " + key);
      }
    }
    return request;
  } catch (const std::logic_error &error) {
    // a number std::stoi and friends could not read
    throw RequestError(std::string("unreadable request value: ") +
                       error.what());
  }
};
