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

pybind11_add_module(cascadio src/main.cpp)
target_compile_definitions(cascadio
                           PRIVATE VERSION_INFO=${PROJECT_VERSION})

# Set the C++ standard to C++11
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

# Set path to header files directories
target_include_directories(cascadio PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/include/opencascade>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/inc>)

# Set path to executable directories
target_link_directories(
  cascadio PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/win64/gcc/lib"
  "${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/win64/vc14/lib"
  "${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/lin64/gcc/lib"
  "${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/lin32/gcc/lib"
  "${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/mac64/clang/lib"
)

# Add all possible libraries we might need
target_link_libraries(cascadio PRIVATE
TKGeomBase TKBinL TKFeat TKBO TKDESTL TKDE TKV3d TKOffset TKVCAF TKStdL TKG3d TKernel TKCDF TKRWMesh TKXmlXCAF TKHLR TKTopAlgo TKDECascade TKBinXCAF TKXml TKMath TKBool TKBRep TKBin TKFillet TKG2d TKXMesh TKXCAF TKDEIGES TKGeomAlgo TKDEVRML TKMesh TKStd TKXSBase TKShHealing TKPrim TKService TKXmlL TKExpress TKDESTEP TKLCAF TKCAF TKDEGLTF
)


install(TARGETS cascadio LIBRARY DESTINATION .)
