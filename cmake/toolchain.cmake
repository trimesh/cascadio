# Shared by the OCCT build (scripts/build_occt.py) and the cascadio build so
# both use the same compiler, linker and flags.
#
# On Linux, uses clang with lld whenever they are installed. macOS already
# is clang. Windows uses MSVC: clang-cl built OpenCASCADE but the result
# did not link against Microsoft's standard library (missing std::variant
# symbols), and MSVC is what the Windows wheels have always used.
#
# Build frontends export generic CC=cc / CXX=c++, so those variables are not
# treated as a choice. To opt out on Linux:
#   CASCADIO_CLANG=OFF  use the default compiler (or CC/CXX)
#
# Link-time optimization is off by default: measured on the test models it
# made wheels 1.4 MB larger for a speedup within noise (about 2%).
#   CASCADIO_LTO=ON     turn it on (needs clang, and lld except on macOS)

set(cascadio_clang OFF)
if(APPLE)
  set(cascadio_clang ON) # the system compiler already is clang
elseif(NOT WIN32 AND (NOT DEFINED ENV{CASCADIO_CLANG} OR "$ENV{CASCADIO_CLANG}"))
  find_program(CASCADIO_CC clang)
  find_program(CASCADIO_CXX clang++)
  find_program(CASCADIO_LLD ld.lld)
  if(CASCADIO_CC AND CASCADIO_CXX)
    set(CMAKE_C_COMPILER "${CASCADIO_CC}")
    set(CMAKE_CXX_COMPILER "${CASCADIO_CXX}")
    set(cascadio_clang ON)
  endif()
endif()

# lld links faster than the GNU linker and can fold identical functions.
if(cascadio_clang AND CASCADIO_LLD)
  set(CMAKE_LINKER_TYPE LLD)
endif()
if("$ENV{CASCADIO_LTO}" AND cascadio_clang AND (APPLE OR CASCADIO_LLD))
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# Hide symbols and give every function its own section so the final link can
# drop everything the worker never reaches.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if(NOT WIN32)
  set(CMAKE_C_FLAGS_INIT "-ffunction-sections -fdata-sections")
  set(CMAKE_CXX_FLAGS_INIT "-ffunction-sections -fdata-sections")
  # -O2 measured smaller and no slower than CMake's default -O3
  set(CMAKE_C_FLAGS_RELEASE_INIT "-O2 -DNDEBUG")
  set(CMAKE_CXX_FLAGS_RELEASE_INIT "-O2 -DNDEBUG")
else()
  # MSVC's version of one section per function
  set(CMAKE_C_FLAGS_INIT "/Gy")
  set(CMAKE_CXX_FLAGS_INIT "/Gy")
  # Link the C and C++ runtime statically. The worker is a program started
  # on its own, so it cannot rely on runtime DLLs being installed on the
  # user's machine or bundled next to the Python module. build_occt.py
  # passes the policy that makes OpenCASCADE's older CMake honor this.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
