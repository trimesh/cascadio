#pragma once

#include <Message_ProgressRange.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XSControl_Reader.hxx>
// Meshing
#include <BRepMesh_IncrementalMesh.hxx>
#include <TopoDS_Shape.hxx>

#include <iostream>
#include <sstream>
#include <vector>

// ============================================================================
// STEP Loading
// ============================================================================

/// Result of loading a STEP file
struct StepLoadResult {
  Handle(TDocStd_Document) doc;
  std::vector<TopoDS_Shape> shapes;
  bool success;

  StepLoadResult() : success(false) {}
};

/// Load a STEP file from disk and mesh the shapes
static StepLoadResult
loadStepFile(const char *input_path, Standard_Real tol_linear,
             Standard_Real tol_angle, Standard_Boolean tol_relative,
             Standard_Boolean use_parallel,
             Standard_Boolean use_colors = Standard_True) {
  StepLoadResult result;

  Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
  app->NewDocument("BinXCAF", result.doc);

  STEPCAFControl_Reader stepReader;

  if (IFSelect_RetDone != stepReader.ReadFile((Standard_CString)input_path)) {
    std::cerr << "Error: Failed to read STEP file \"" << input_path << "\""
              << std::endl;
    result.doc->Close();
    return result;
  }

  stepReader.SetColorMode(use_colors);
  stepReader.SetNameMode(true);
  stepReader.SetLayerMode(true);

  if (!stepReader.Transfer(result.doc)) {
    std::cerr << "Error: Failed to transfer STEP file \"" << input_path << "\""
              << std::endl;
    result.doc->Close();
    return result;
  }

  XSControl_Reader reader = stepReader.Reader();
  for (int shape_id = 1; shape_id <= reader.NbShapes(); shape_id++) {
    TopoDS_Shape shape = reader.Shape(shape_id);
    if (shape.IsNull()) {
      continue;
    }
    result.shapes.push_back(shape);
    BRepMesh_IncrementalMesh mesh(shape, tol_linear, tol_relative, tol_angle,
                                  use_parallel);
    mesh.Perform();
  }

  result.success = true;
  return result;
}

/// Load a STEP file from memory (bytes) and mesh the shapes
static StepLoadResult
loadStepBytes(const std::string &stepData, Standard_Real tol_linear,
              Standard_Real tol_angle, Standard_Boolean tol_relative,
              Standard_Boolean use_parallel,
              Standard_Boolean use_colors = Standard_True) {
  StepLoadResult result;

  Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
  app->NewDocument("BinXCAF", result.doc);

  STEPCAFControl_Reader stepReader;

  // Create an input stream from the string data
  std::istringstream stepStream(stepData);

  if (IFSelect_RetDone != stepReader.ReadStream("step_data.step", stepStream)) {
    std::cerr << "Error: Failed to read STEP data from memory" << std::endl;
    result.doc->Close();
    return result;
  }

  stepReader.SetColorMode(use_colors);
  stepReader.SetNameMode(true);
  stepReader.SetLayerMode(true);

  if (!stepReader.Transfer(result.doc)) {
    std::cerr << "Error: Failed to transfer STEP data" << std::endl;
    result.doc->Close();
    return result;
  }

  XSControl_Reader reader = stepReader.Reader();
  for (int shape_id = 1; shape_id <= reader.NbShapes(); shape_id++) {
    TopoDS_Shape shape = reader.Shape(shape_id);
    if (shape.IsNull()) {
      continue;
    }
    result.shapes.push_back(shape);
    BRepMesh_IncrementalMesh mesh(shape, tol_linear, tol_relative, tol_angle,
                                  use_parallel);
    mesh.Perform();
  }

  result.success = true;
  return result;
}
