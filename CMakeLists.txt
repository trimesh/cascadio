cmake_minimum_required(VERSION 3.29)

# Use the same compiler, linker and flags as the OpenCASCADE build
# unless the caller brought their own toolchain.
if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/toolchain.cmake")
endif()

# scikit-build-core passes the version twice: numbers only for CMake, and
# in full (for example 0.2.0rc0) for the module's __version__.
if(NOT SKBUILD_PROJECT_VERSION)
  set(SKBUILD_PROJECT_VERSION 0.0.0) # building with plain cmake
  set(SKBUILD_PROJECT_VERSION_FULL 0.0.0)
endif()
project(cascadio VERSION ${SKBUILD_PROJECT_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Python 3.12 REQUIRED
  COMPONENTS Interpreter Development.Module
  OPTIONAL_COMPONENTS Development.SABIModule)
find_package(nanobind CONFIG REQUIRED)

# OpenCASCADE: the static build from scripts/build_occt.py when it exists,
# otherwise whatever the system provides (distribution packages, sdist).
set(OCCT_MINIMUM 7.9) # first release where our glTF writer hooks exist
if(DEFINED ENV{CASCADIO_OCCT_PREFIX})
  set(vendored_occt "$ENV{CASCADIO_OCCT_PREFIX}")
else()
  set(vendored_occt "${CMAKE_CURRENT_LIST_DIR}/occt_install")
endif()
if(EXISTS "${vendored_occt}")
  find_package(OpenCASCADE CONFIG REQUIRED PATHS "${vendored_occt}" NO_DEFAULT_PATH)
else()
  find_package(OpenCASCADE CONFIG)
endif()
if(NOT OpenCASCADE_FOUND)
  message(FATAL_ERROR
    "OpenCASCADE was not found. From a git checkout run "
    "`python scripts/build_occt.py` first. Otherwise install the "
    "OpenCASCADE ${OCCT_MINIMUM}+ development files (plus RapidJSON and zstd) "
    "and point CMAKE_PREFIX_PATH at them if they are not found.")
elseif(OpenCASCADE_VERSION VERSION_LESS OCCT_MINIMUM)
  message(FATAL_ERROR
    "cascadio needs OpenCASCADE ${OCCT_MINIMUM} or newer, "
    "found ${OpenCASCADE_VERSION} in ${OpenCASCADE_INSTALL_PREFIX}")
endif()
message(STATUS "Using OpenCASCADE ${OpenCASCADE_VERSION} from ${OpenCASCADE_INSTALL_PREFIX}")

if(WIN32)
  # keep <windows.h> from defining min/max macros and pulling in the world
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Two binaries:
#   cascadio-worker  a plain program that does one conversion; all of
#                    OpenCASCADE is in here and Python is not
#   _core            the Python module, which starts the worker for every
#                    conversion and enforces the time and memory limits
# They share src/request.hpp (the request and the exit codes) and, on
# Windows, src/windows_text.hpp; neither of those has OpenCASCADE or Python.

add_executable(cascadio-worker src/worker.cpp)
target_include_directories(cascadio-worker SYSTEM PRIVATE "${OpenCASCADE_INCLUDE_DIR}")
# Only what src/ uses directly; these targets bring their own dependencies.
target_link_libraries(cascadio-worker PRIVATE
  TKDESTEP TKDEIGES TKDEGLTF TKXCAF TKMesh)

# The code that writes TM_brep_faces JSON is generated from the extension's
# schema on every build, so the two cannot disagree.
set(brep_schema "${CMAKE_CURRENT_LIST_DIR}/extensions/TM_brep_faces/schema/mesh.primitive.TM_brep_faces.schema.json")
set(brep_header "${CMAKE_CURRENT_BINARY_DIR}/generated/tm_brep_faces.hpp")
add_custom_command(
  OUTPUT "${brep_header}"
  COMMAND Python::Interpreter "${CMAKE_CURRENT_LIST_DIR}/scripts/gen_schema.py"
          header "${brep_header}"
  DEPENDS "${brep_schema}" "${CMAKE_CURRENT_LIST_DIR}/scripts/gen_schema.py"
  VERBATIM)
target_sources(cascadio-worker PRIVATE "${brep_header}")
target_include_directories(cascadio-worker PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
# the schema ships in the package for anyone who wants to validate output
install(FILES "${brep_schema}" DESTINATION cascadio RENAME schema.json)

# RapidJSON is header-only: vendored copy if checked out, else the system one.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/upstream/rapidjson/include/rapidjson")
  target_include_directories(cascadio-worker SYSTEM PRIVATE upstream/rapidjson/include)
else()
  find_package(RapidJSON REQUIRED)
  target_include_directories(cascadio-worker SYSTEM PRIVATE ${RapidJSON_INCLUDE_DIRS} ${RAPIDJSON_INCLUDE_DIRS})
endif()

# Zstandard, to accept compressed input. Only the decompressor is built from
# the vendored copy (about 100 KB); otherwise the system library is used.
set(zstd_source "${CMAKE_CURRENT_LIST_DIR}/upstream/zstd/lib")
if(EXISTS "${zstd_source}/zstd.h")
  file(GLOB zstd_files "${zstd_source}/common/*.c" "${zstd_source}/decompress/*.c")
  add_library(zstd_decompress STATIC ${zstd_files})
  target_include_directories(zstd_decompress PUBLIC "${zstd_source}")
  # no assembly (one less language to set up) and no pre-1.0 formats
  target_compile_definitions(zstd_decompress PRIVATE ZSTD_DISABLE_ASM ZSTD_LEGACY_SUPPORT=0)
  target_link_libraries(cascadio-worker PRIVATE zstd_decompress)
else()
  find_path(ZSTD_INCLUDE_DIR zstd.h REQUIRED)
  find_library(ZSTD_LIBRARY zstd REQUIRED)
  target_include_directories(cascadio-worker SYSTEM PRIVATE "${ZSTD_INCLUDE_DIR}")
  target_link_libraries(cascadio-worker PRIVATE "${ZSTD_LIBRARY}")
endif()

# Drop every function the worker never reaches. With a static
# OpenCASCADE this is what keeps the wheel small.
if(APPLE)
  target_link_options(cascadio-worker PRIVATE -Wl,-dead_strip)
elseif(MSVC)
  target_link_options(cascadio-worker PRIVATE /OPT:REF /OPT:ICF)
else()
  target_link_options(cascadio-worker PRIVATE -Wl,--gc-sections)
  if(CMAKE_LINKER_TYPE STREQUAL "LLD")
    # fold identical functions, which OCCT's templates produce in bulk
    target_link_options(cascadio-worker PRIVATE -Wl,--icf=safe)
  endif()
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(cascadio-worker PRIVATE -s) # no symbol table
  endif()
endif()
find_package(Threads REQUIRED)
target_link_libraries(cascadio-worker PRIVATE Threads::Threads)

# STABLE_ABI: one wheel serves every regular CPython from 3.12 on.
# FREE_THREADED: on free-threaded CPython (which has no stable ABI yet, so it
# gets its own wheel) declare that the module is safe without the GIL.
# It is: the module keeps no state, each call talks to its own process.
nanobind_add_module(_core STABLE_ABI FREE_THREADED src/main.cpp)
target_compile_definitions(_core PRIVATE VERSION_INFO=${SKBUILD_PROJECT_VERSION_FULL})
target_link_libraries(_core PRIVATE ${CMAKE_DL_LIBS})
if(WIN32)
  # GetProcessMemoryInfo, to check the worker's memory (process.hpp)
  target_link_libraries(_core PRIVATE psapi)
endif()

# Our own code is compiled with every reasonable warning on. OpenCASCADE,
# RapidJSON and zstd headers are included as SYSTEM, so they stay quiet.
# CASCADIO_STRICT=ON (set by the Makefile and by CI) turns warnings into
# errors; it is off by default so a packager's newer compiler cannot break
# the build over a warning we have not seen yet.
if(MSVC)
  set(strict_warnings /W4)
  set(warnings_as_errors /WX)
else()
  set(strict_warnings -Wall -Wextra -Wpedantic -Wshadow -Wconversion
      -Wsign-conversion -Wold-style-cast -Wcast-qual -Wnon-virtual-dtor
      -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion -Wformat=2
      -Wimplicit-fallthrough)
  set(warnings_as_errors -Werror)
endif()
# nanobind is built as part of this project, so its headers (and Python's,
# which it brings along) would be held to our warnings; mark it SYSTEM.
get_target_property(core_libraries _core LINK_LIBRARIES)
foreach(library ${core_libraries})
  if(TARGET ${library} AND library MATCHES "nanobind")
    set_target_properties(${library} PROPERTIES SYSTEM TRUE)
  endif()
endforeach()
foreach(target cascadio-worker _core)
  target_compile_options(${target} PRIVATE ${strict_warnings})
  if("$ENV{CASCADIO_STRICT}")
    target_compile_options(${target} PRIVATE ${warnings_as_errors})
  endif()
endforeach()

install(TARGETS cascadio-worker RUNTIME DESTINATION cascadio)
install(TARGETS _core LIBRARY DESTINATION cascadio)

# Type stubs are generated from the compiled module so they cannot go stale.
nanobind_add_stub(_core_stub
  MODULE _core
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/_core.pyi"
  PYTHON_PATH $<TARGET_FILE_DIR:_core>
  DEPENDS _core)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/_core.pyi" DESTINATION cascadio)
