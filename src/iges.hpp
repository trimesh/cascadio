#pragma once

#include "tempfile.hpp"

#include <IGESControl_Reader.hxx>
#include <Message_ProgressRange.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
// Meshing
#include <BRepMesh_IncrementalMesh.hxx>
#include <TopoDS_Shape.hxx>
// Sewing/stitching for IGES
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRep_Builder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

#include <iostream>
#include <sstream>
#include <vector>

// ============================================================================
// IGES Loading
// ============================================================================

/// Result of loading an IGES file
struct IgesLoadResult {
  Handle(TDocStd_Document) doc;
  std::vector<TopoDS_Shape> shapes;
  bool success;

  IgesLoadResult() : success(false) {}
};

/// Stitch/sew faces together to create a unified shape
static TopoDS_Shape stitchShapes(const std::vector<TopoDS_Shape> &shapes,
                                 Standard_Real tolerance = 1e-6) {
  if (shapes.empty()) {
    return TopoDS_Shape();
  }

  if (shapes.size() == 1) {
    return shapes[0];
  }

  // Use BRepBuilderAPI_Sewing to stitch faces together
  BRepBuilderAPI_Sewing sewing(tolerance);
  sewing.SetTolerance(tolerance);
  sewing.SetMaxTolerance(tolerance * 10);
  sewing.SetMinTolerance(tolerance * 0.1);

  for (const auto &shape : shapes) {
    sewing.Add(shape);
  }

  sewing.Perform();

  // Get the sewed result - even if sewing didn't improve connectivity,
  // SewedShape() returns a valid compound of the input shapes
  TopoDS_Shape sewedShape = sewing.SewedShape();
  if (!sewedShape.IsNull()) {
    return sewedShape;
  }

  // If SewedShape() returned null (shouldn't happen), create a compound
  BRep_Builder builder;
  TopoDS_Compound compound;
  builder.MakeCompound(compound);
  for (const auto &shape : shapes) {
    builder.Add(compound, shape);
  }
  return compound;
}

/// Load an IGES file from disk and mesh the shapes
/// Note: use_colors parameter is accepted for API consistency but IGES
/// color handling is limited compared to STEP
static IgesLoadResult
loadIgesFile(const char *input_path, Standard_Real tol_linear,
             Standard_Real tol_angle, Standard_Boolean tol_relative,
             Standard_Boolean use_parallel,
             Standard_Boolean /*use_colors*/ = Standard_True,
             Standard_Boolean stitch_shapes = Standard_True) {
  IgesLoadResult result;

  IGESControl_Reader igesReader;

  if (IFSelect_RetDone != igesReader.ReadFile((Standard_CString)input_path)) {
    std::cerr << "Error: Failed to read IGES file \"" << input_path << "\""
              << std::endl;
    return result;
  }

  // Transfer all roots
  igesReader.TransferRoots();

  std::vector<TopoDS_Shape> rawShapes;

  for (int shape_id = 1; shape_id <= igesReader.NbShapes(); shape_id++) {
    TopoDS_Shape shape = igesReader.Shape(shape_id);
    if (shape.IsNull()) {
      continue;
    }
    rawShapes.push_back(shape);
  }

  if (rawShapes.empty()) {
    std::cerr << "Error: No shapes were transferred from IGES file"
              << std::endl;
    return result;
  }

  // Create document after we have shapes
  Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
  app->NewDocument("BinXCAF", result.doc);

  // Get the shape tool to add shapes to the document
  Handle(XCAFDoc_ShapeTool) shapeTool =
      XCAFDoc_DocumentTool::ShapeTool(result.doc->Main());

  // Stitch shapes together if requested
  // Use a small sewing tolerance (1e-6) suitable for joining adjacent surfaces.
  // This is independent of the meshing tolerance (tol_linear).
  if (stitch_shapes && !rawShapes.empty()) {
    constexpr Standard_Real sewingTolerance = 1e-6;
    TopoDS_Shape stitched = stitchShapes(rawShapes, sewingTolerance);
    if (stitched.IsNull()) {
      std::cerr << "Error: Failed to stitch IGES shapes" << std::endl;
      result.doc->Close();
      return result;
    }
    result.shapes.push_back(stitched);
    BRepMesh_IncrementalMesh mesh(stitched, tol_linear, tol_relative, tol_angle,
                                  use_parallel);
    mesh.Perform();
    // Add stitched shape to the document
    shapeTool->AddShape(stitched, Standard_False);
  } else {
    // No stitching, mesh each shape individually
    for (const auto &shape : rawShapes) {
      result.shapes.push_back(shape);
      BRepMesh_IncrementalMesh mesh(shape, tol_linear, tol_relative, tol_angle,
                                    use_parallel);
      mesh.Perform();
      // Add each shape to the document
      shapeTool->AddShape(shape, Standard_False);
    }
  }

  result.success = true;
  return result;
}

/// Load an IGES file from memory (bytes) and mesh the shapes
/// NOTE: IGES does not support stream reading, so this function
/// writes to a temp file first
static IgesLoadResult
loadIgesBytes(const std::string &igesData, Standard_Real tol_linear,
              Standard_Real tol_angle, Standard_Boolean tol_relative,
              Standard_Boolean use_parallel,
              Standard_Boolean use_colors = Standard_True,
              Standard_Boolean stitch_shapes = Standard_True) {
  IgesLoadResult result;

  // IGES doesn't support ReadStream, so we need to use a temp file
  TempFile tempFile(".igs");
  if (!tempFile.valid()) {
    std::cerr << "Error: Failed to create temp file for IGES loading"
              << std::endl;
    return result;
  }

  // Write data to temp file and close fd
  if (!tempFile.write_and_close(igesData.data(), igesData.size())) {
    std::cerr << "Error: Failed to write IGES data to temp file" << std::endl;
    return result;
  }

  // Load from temp file (TempFile destructor will clean up after)
  result = loadIgesFile(tempFile.path(), tol_linear, tol_angle, tol_relative,
                        use_parallel, use_colors, stitch_shapes);

  return result;
}
