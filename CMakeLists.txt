cmake_minimum_required(VERSION 3.15...3.26)

# Set project name
# Set project name
set(project_name "${SKBUILD_PROJECT_NAME}")

project(
  ${project_name}
  LANGUAGES CXX
  VERSION "${SKBUILD_PROJECT_VERSION}")

find_package(
  Python
  COMPONENTS Interpreter Development.Module
  REQUIRED)

find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(_core src/main.cpp)
target_compile_definitions(_core
                           PRIVATE VERSION_INFO=${PROJECT_VERSION})

option(SYSTEM_OPENCASCADE "Use an external/system copy of OpenCASCADE" OFF)

# Set the C++ standard to C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(SYSTEM_OPENCASCADE)
  # this is used for system packagers like fedora
  find_package(OpenCASCADE CONFIG REQUIRED)
  # Set path to header files directories
  target_include_directories(_core PUBLIC "${OpenCASCADE_INCLUDE_DIR}")
else()
  # Set path to header files directories
  target_include_directories(_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/include/opencascade>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/inc>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/upstream/rapidjson/include>)

  # Set path to executable directories
  target_link_directories(
    _core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/win64/gcc/lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/win64/vc14/lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/lin64/gcc/lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/lin32/gcc/lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/mac64/clang/lib"
  )
endif()

# Add all possible libraries we might need
target_link_libraries(_core PRIVATE
TKCAF TKPrim TKShHealing TKDEPLY TKRWMesh TKBinL TKBRep TKVCAF TKDESTL TKCDF TKTObj TKMath TKHLR TKBinTObj TKFillet TKOffset TKStd TKV3d TKFeat TKBO TKGeomBase TKernel TKStdL TKG3d TKBin TKService TKDE TKExpress TKGeomAlgo TKDEIGES TKDEGLTF TKLCAF TKDEVRML TKXmlL TKXCAF TKDECascade TKXSBase TKDESTEP TKG2d TKBool TKTopAlgo TKXml TKXmlXCAF TKDEOBJ TKXMesh TKXmlTObj TKBinXCAF TKMesh
)

install(TARGETS _core LIBRARY DESTINATION cascadio)
