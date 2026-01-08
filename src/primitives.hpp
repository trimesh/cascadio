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
  // Null/empty face -> null entry
  if (face.IsNull()) {
    facesArray.PushBack(rapidjson::Value(), alloc);
    return;
  }

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

  // Add face_index first (required by schema)
  obj.AddMember("face_index", faceIndex, alloc);

  switch (surfType) {
  case GeomAbs_Plane: {
    gp_Pln pln = surf.Plane();
    gp_Ax3 pos = pln.Position();
    obj.AddMember("type", "plane", alloc);
    addVec3(obj, "origin", pos.Location().X() * lengthUnit,
            pos.Location().Y() * lengthUnit, pos.Location().Z() * lengthUnit,
            alloc);
    addVec3(obj, "normal", pos.Direction().X(), pos.Direction().Y(),
            pos.Direction().Z(), alloc);
    addVec3(obj, "x_dir", pos.XDirection().X(), pos.XDirection().Y(),
            pos.XDirection().Z(), alloc);
    // For planes, u and v are both lengths in the plane's local coordinates
    addBounds(obj, "extent_x", uMin * lengthUnit, uMax * lengthUnit, alloc);
    addBounds(obj, "extent_y", vMin * lengthUnit, vMax * lengthUnit, alloc);
    break;
  }
  case GeomAbs_Cylinder: {
    gp_Cylinder cyl = surf.Cylinder();
    gp_Ax3 pos = cyl.Position();
    obj.AddMember("type", "cylinder", alloc);
    addVec3(obj, "origin", pos.Location().X() * lengthUnit,
            pos.Location().Y() * lengthUnit, pos.Location().Z() * lengthUnit,
            alloc);
    addVec3(obj, "axis", pos.Direction().X(), pos.Direction().Y(),
            pos.Direction().Z(), alloc);
    obj.AddMember("radius", cyl.Radius() * lengthUnit, alloc);
    // u is angle around axis (radians), v is height along axis (length)
    addBounds(obj, "extent_angle", uMin, uMax, alloc);
    addBounds(obj, "extent_height", vMin * lengthUnit, vMax * lengthUnit, alloc);
    break;
  }
  case GeomAbs_Cone: {
    gp_Cone cone = surf.Cone();
    gp_Ax3 pos = cone.Position();
    gp_Pnt apex = cone.Apex();
    obj.AddMember("type", "cone", alloc);
    addVec3(obj, "apex", apex.X() * lengthUnit, apex.Y() * lengthUnit,
            apex.Z() * lengthUnit, alloc);
    addVec3(obj, "axis", pos.Direction().X(), pos.Direction().Y(),
            pos.Direction().Z(), alloc);
    obj.AddMember("semi_angle", cone.SemiAngle(), alloc);
    obj.AddMember("ref_radius", cone.RefRadius() * lengthUnit, alloc);
    // u is angle around axis (radians), v is distance from apex (length)
    addBounds(obj, "extent_angle", uMin, uMax, alloc);
    addBounds(obj, "extent_distance", vMin * lengthUnit, vMax * lengthUnit, alloc);
    break;
  }
  case GeomAbs_Sphere: {
    gp_Sphere sph = surf.Sphere();
    obj.AddMember("type", "sphere", alloc);
    addVec3(obj, "center", sph.Location().X() * lengthUnit,
            sph.Location().Y() * lengthUnit, sph.Location().Z() * lengthUnit,
            alloc);
    obj.AddMember("radius", sph.Radius() * lengthUnit, alloc);
    // u is longitude (radians), v is latitude (radians)
    addBounds(obj, "extent_longitude", uMin, uMax, alloc);
    addBounds(obj, "extent_latitude", vMin, vMax, alloc);
    break;
  }
  case GeomAbs_Torus: {
    gp_Torus tor = surf.Torus();
    gp_Ax3 pos = tor.Position();
    obj.AddMember("type", "torus", alloc);
    addVec3(obj, "center", pos.Location().X() * lengthUnit,
            pos.Location().Y() * lengthUnit, pos.Location().Z() * lengthUnit,
            alloc);
    addVec3(obj, "axis", pos.Direction().X(), pos.Direction().Y(),
            pos.Direction().Z(), alloc);
    obj.AddMember("major_radius", tor.MajorRadius() * lengthUnit, alloc);
    obj.AddMember("minor_radius", tor.MinorRadius() * lengthUnit, alloc);
    // u is angle around main axis (radians), v is angle around tube (radians)
    addBounds(obj, "extent_major_angle", uMin, uMax, alloc);
    addBounds(obj, "extent_minor_angle", vMin, vMax, alloc);
    break;
  }
  default:
    // Non-analytical surface - push null to preserve index mapping
    facesArray.PushBack(rapidjson::Value(), alloc);
    return;
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
