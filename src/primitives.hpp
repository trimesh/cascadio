#pragma once

#include "extras.hpp"

#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>

// ============================================================================
// BREP Primitive Extraction
// ============================================================================

/// Get the type name for a surface type, or nullptr if not analytical
static const char *getSurfaceTypeName(GeomAbs_SurfaceType surfType) {
  switch (surfType) {
  case GeomAbs_Plane:
    return "plane";
  case GeomAbs_Cylinder:
    return "cylinder";
  case GeomAbs_Cone:
    return "cone";
  case GeomAbs_Sphere:
    return "sphere";
  case GeomAbs_Torus:
    return "torus";
  default:
    return nullptr;
  }
}

/// Extract BREP primitive info for a face and add to JSON array
/// If allowedTypes is non-empty, only include faces with types in the set
/// (filtered faces get a null entry to preserve index mapping)
/// lengthUnit is the scale factor to convert to meters (from
/// XCAFDoc_DocumentTool::GetLengthUnit)
static void extractFacePrimitive(const TopoDS_Face &face, int faceIndex,
                                 rapidjson::Value &facesArray,
                                 rapidjson::Document::AllocatorType &alloc,
                                 const std::set<std::string> &allowedTypes = {},
                                 Standard_Real lengthUnit = 1.0) {
  BRepAdaptor_Surface surf(face, Standard_True);
  GeomAbs_SurfaceType surfType = surf.GetType();

  // Determine type name for filtering
  const char *typeName = getSurfaceTypeName(surfType);

  // If filtering is enabled, check if this type should be included
  // Add null entry to preserve index mapping with brep_index
  if (!allowedTypes.empty()) {
    // Note: std::set::count with const char* creates a temp string - acceptable
    // overhead for filtering flexibility. For perf-critical paths, could use
    // an enum-based filter instead.
    if (typeName == nullptr || allowedTypes.count(typeName) == 0) {
      // Add null to preserve index (brep_index references array position)
      facesArray.PushBack(rapidjson::Value(), alloc);
      return;
    }
  }

  // Get UV bounds
  Standard_Real uMin, uMax, vMin, vMax;
  BRepTools::UVBounds(face, uMin, uMax, vMin, vMax);

  rapidjson::Value obj(rapidjson::kObjectType);
  obj.AddMember("face_index", faceIndex, alloc);
  addBounds(obj, "u_bounds", uMin, uMax, alloc);
  // v_bounds need scaling for surfaces where v is a length (cylinder, cone)
  // This is handled per-surface-type below

  switch (surfType) {
  case GeomAbs_Plane: {
    gp_Pln pln = surf.Plane();
    gp_Ax3 pos = pln.Position();
    obj.AddMember("type", "plane", alloc);
    // For planes, u and v are both lengths, scale them
    addBounds(obj, "v_bounds", vMin * lengthUnit, vMax * lengthUnit, alloc);
    addVec3(obj, "origin", pos.Location().X() * lengthUnit,
            pos.Location().Y() * lengthUnit, pos.Location().Z() * lengthUnit,
            alloc);
    addVec3(obj, "normal", pos.Direction().X(), pos.Direction().Y(),
            pos.Direction().Z(), alloc);
    addVec3(obj, "x_dir", pos.XDirection().X(), pos.XDirection().Y(),
            pos.XDirection().Z(), alloc);
    break;
  }
  case GeomAbs_Cylinder: {
    gp_Cylinder cyl = surf.Cylinder();
    gp_Ax3 pos = cyl.Position();
    obj.AddMember("type", "cylinder", alloc);
    // For cylinders, v is the height along axis (length), scale it
    addBounds(obj, "v_bounds", vMin * lengthUnit, vMax * lengthUnit, alloc);
    addVec3(obj, "origin", pos.Location().X() * lengthUnit,
            pos.Location().Y() * lengthUnit, pos.Location().Z() * lengthUnit,
            alloc);
    addVec3(obj, "axis", pos.Direction().X(), pos.Direction().Y(),
            pos.Direction().Z(), alloc);
    obj.AddMember("radius", cyl.Radius() * lengthUnit, alloc);
    break;
  }
  case GeomAbs_Cone: {
    gp_Cone cone = surf.Cone();
    gp_Ax3 pos = cone.Position();
    gp_Pnt apex = cone.Apex();
    obj.AddMember("type", "cone", alloc);
    // For cones, v is distance along axis (length), scale it
    addBounds(obj, "v_bounds", vMin * lengthUnit, vMax * lengthUnit, alloc);
    addVec3(obj, "apex", apex.X() * lengthUnit, apex.Y() * lengthUnit,
            apex.Z() * lengthUnit, alloc);
    addVec3(obj, "axis", pos.Direction().X(), pos.Direction().Y(),
            pos.Direction().Z(), alloc);
    obj.AddMember("semi_angle", cone.SemiAngle(), alloc);
    obj.AddMember("ref_radius", cone.RefRadius() * lengthUnit, alloc);
    break;
  }
  case GeomAbs_Sphere: {
    gp_Sphere sph = surf.Sphere();
    obj.AddMember("type", "sphere", alloc);
    // For spheres, both u and v are angles, no scaling needed for bounds
    addBounds(obj, "v_bounds", vMin, vMax, alloc);
    addVec3(obj, "center", sph.Location().X() * lengthUnit,
            sph.Location().Y() * lengthUnit, sph.Location().Z() * lengthUnit,
            alloc);
    obj.AddMember("radius", sph.Radius() * lengthUnit, alloc);
    break;
  }
  case GeomAbs_Torus: {
    gp_Torus tor = surf.Torus();
    gp_Ax3 pos = tor.Position();
    obj.AddMember("type", "torus", alloc);
    // For torus, both u and v are angles, no scaling needed for bounds
    addBounds(obj, "v_bounds", vMin, vMax, alloc);
    addVec3(obj, "center", pos.Location().X() * lengthUnit,
            pos.Location().Y() * lengthUnit, pos.Location().Z() * lengthUnit,
            alloc);
    addVec3(obj, "axis", pos.Direction().X(), pos.Direction().Y(),
            pos.Direction().Z(), alloc);
    obj.AddMember("major_radius", tor.MajorRadius() * lengthUnit, alloc);
    obj.AddMember("minor_radius", tor.MinorRadius() * lengthUnit, alloc);
    break;
  }
  default:
    // Non-analytical surface - still record face_index but no primitive type
    addBounds(obj, "v_bounds", vMin, vMax, alloc);
    obj.AddMember("type", rapidjson::Value(), alloc); // null
    break;
  }

  facesArray.PushBack(obj, alloc);
}

/// Extract all BREP primitives from a shape into a JSON array
/// lengthUnit is the scale factor to convert to meters (from
/// XCAFDoc_DocumentTool::GetLengthUnit)
static rapidjson::Value
extractAllPrimitives(const TopoDS_Shape &shape,
                     rapidjson::Document::AllocatorType &alloc,
                     const std::set<std::string> &allowedTypes = {},
                     Standard_Real lengthUnit = 1.0) {
  rapidjson::Value facesArray(rapidjson::kArrayType);
  int faceIndex = 0;

  for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More();
       explorer.Next()) {
    TopoDS_Face face = TopoDS::Face(explorer.Current());
    extractFacePrimitive(face, faceIndex, facesArray, alloc, allowedTypes,
                         lengthUnit);
    faceIndex++;
  }

  return facesArray;
}
