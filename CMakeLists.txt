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
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/include/opencascade>)

# Set path to executable directories
target_link_directories(cascadio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/upstream/OCCT/lin64/gcc/lib")

# Add source files to compile to the project
# set(SOURCE_FILES src/main.cpp)
# add_executable(${project_name}  ${SOURCE_FILES})

# Add OpenCascade libraries
target_link_libraries(cascadio PRIVATE -Wl,--no-as-needed
  -lTKXSDRAWSTL -lTKGeomBase -lTKXCAF -lTKTopTest -lTKXSDRAWGLTF -lTKDECascade -lTKGeomAlgo -lTKDEIGES -lTKTObjDRAW -lTKXmlTObj -lTKDE -lTKDEOBJ -lTKLCAF -lTKXSDRAW -lTKernel -lTKShHealing -lTKOpenGlTest -lTKXSDRAWPLY -lTKService -lTKOpenGl -lTKV3d -lTKXSDRAWVRML -lTKXSDRAWDE -lTKBinL -lTKXmlL -lTKQADraw -lTKXSDRAWOBJ -lTKOffset -lTKG2d -lTKStdL -lTKXMesh -lTKExpress -lTKXDEDRAW -lTKCDF -lTKDEGLTF -lTKDCAF -lTKRWMesh -lTKFillet -lTKDEVRML -lTKBool -lTKG3d -lTKBRep -lTKBO -lTKXSBase -lTKDraw -lTKXmlXCAF -lTKPrim -lTKMeshVS -lTKHLR -lTKMesh -lTKStd -lTKViewerTest -lTKTopAlgo -lTKBinTObj -lTKVCAF -lTKDESTL -lTKXSDRAWSTEP -lTKMath -lTKBinXCAF -lTKFeat -lTKDESTEP -lTKTObj -lTKBin -lTKDEPLY -lTKXml -lTKCAF -lTKXSDRAWIGES
 )

install(TARGETS cascadio LIBRARY DESTINATION .)


