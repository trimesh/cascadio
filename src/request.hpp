#pragma once

#include <cstdint>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

/// What the Python module asks the worker program to do. This header is the
/// whole contract between the two and uses nothing from OpenCASCADE or
/// Python, so both sides can include it.
///
/// The module writes to the worker's standard input:
///     the request as text, one `key=value` per line, ended by an empty line
///     the input file
/// The worker writes the GLB to standard output and exits with one of the
/// codes below. On failure it writes a one-line message to standard error
/// instead.

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
} // namespace worker_exit

struct Request {
  FileType file_type = FileType::STEP;
  double tol_linear = 0.01;
  double tol_angular = 0.5;
  bool tol_relative = false;
  bool merge_primitives = true;
  bool use_parallel = true;
  bool include_brep = false;
  std::set<std::string> brep_types;
  bool include_materials = false;
  NodeNameFormat node_name_format = NodeNameFormat::INSTANCE_OR_PRODUCT;
  bool y_up = false;
  /// Bytes of memory the worker may use before it stops itself; 0 = no limit
  uint64_t memory_limit = 0;

  std::string serialize() const {
    std::ostringstream text;
    text.precision(17); // enough digits to round-trip a double
    text << "file_type=" << static_cast<int>(file_type) << '\n'
         << "tol_linear=" << tol_linear << '\n'
         << "tol_angular=" << tol_angular << '\n'
         << "tol_relative=" << tol_relative << '\n'
         << "merge_primitives=" << merge_primitives << '\n'
         << "use_parallel=" << use_parallel << '\n'
         << "include_brep=" << include_brep << '\n';
    for (const std::string &type : brep_types) {
      text << "brep_type=" << type << '\n';
    }
    text << "include_materials=" << include_materials << '\n'
         << "node_name_format=" << static_cast<int>(node_name_format) << '\n'
         << "y_up=" << y_up << '\n'
         << "memory_limit=" << memory_limit << '\n'
         << '\n';
    return text.str();
  }

  /// Read the text form from a stream, leaving it at the start of the input
  /// file. Throws std::invalid_argument on anything unexpected, which means
  /// the module and the worker come from different versions.
  static Request parse(std::istream &stream) {
    Request request;
    std::string line;
    while (std::getline(stream, line) && !line.empty()) {
      const size_t split = line.find('=');
      if (split == std::string::npos) {
        throw std::invalid_argument("malformed request line: " + line);
      }
      const std::string key = line.substr(0, split);
      const std::string value = line.substr(split + 1);
      auto flag = [&value] { return value == "1"; };
      if (key == "file_type") {
        request.file_type = static_cast<FileType>(std::stoi(value));
      } else if (key == "tol_linear") {
        request.tol_linear = std::stod(value);
      } else if (key == "tol_angular") {
        request.tol_angular = std::stod(value);
      } else if (key == "tol_relative") {
        request.tol_relative = flag();
      } else if (key == "merge_primitives") {
        request.merge_primitives = flag();
      } else if (key == "use_parallel") {
        request.use_parallel = flag();
      } else if (key == "include_brep") {
        request.include_brep = flag();
      } else if (key == "brep_type") {
        request.brep_types.insert(value);
      } else if (key == "include_materials") {
        request.include_materials = flag();
      } else if (key == "node_name_format") {
        request.node_name_format =
            static_cast<NodeNameFormat>(std::stoi(value));
      } else if (key == "y_up") {
        request.y_up = flag();
      } else if (key == "memory_limit") {
        request.memory_limit = std::stoull(value);
      } else {
        throw std::invalid_argument("unknown request key: " + key);
      }
    }
    return request;
  }
};
