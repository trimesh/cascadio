#pragma once

#include "extras.hpp"

#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <gp_Pln.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Cone.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>
#include <gp_Ax3.hxx>
#include <BRepTools.hxx>

// ============================================================================
// BREP Primitive Extraction
// ============================================================================

/// Get the type name for a surface type, or nullptr if not analytical
static const char* getSurfaceTypeName(GeomAbs_SurfaceType surfType) {
    switch (surfType) {
        case GeomAbs_Plane: return "plane";
        case GeomAbs_Cylinder: return "cylinder";
        case GeomAbs_Cone: return "cone";
        case GeomAbs_Sphere: return "sphere";
        case GeomAbs_Torus: return "torus";
        default: return nullptr;
    }
}

/// Extract BREP primitive info for a face and add to JSON array
/// If allowedTypes is non-empty, only include faces with types in the set
static void extractFacePrimitive(const TopoDS_Face& face, int faceIndex,
                                  rapidjson::Value& facesArray,
                                  rapidjson::Document::AllocatorType& alloc,
                                  const std::set<std::string>& allowedTypes = {}) {
    BRepAdaptor_Surface surf(face, Standard_True);
    GeomAbs_SurfaceType surfType = surf.GetType();
    
    // Determine type name for filtering
    const char* typeName = getSurfaceTypeName(surfType);
    
    // If filtering is enabled, check if this type should be included
    if (!allowedTypes.empty()) {
        if (typeName == nullptr || allowedTypes.find(typeName) == allowedTypes.end()) {
            return; // Skip this face
        }
    }
    
    // Get UV bounds
    Standard_Real uMin, uMax, vMin, vMax;
    BRepTools::UVBounds(face, uMin, uMax, vMin, vMax);
    
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("face_index", faceIndex, alloc);
    addBounds(obj, "u_bounds", uMin, uMax, alloc);
    addBounds(obj, "v_bounds", vMin, vMax, alloc);
    
    switch (surfType) {
        case GeomAbs_Plane: {
            gp_Pln pln = surf.Plane();
            gp_Ax3 pos = pln.Position();
            obj.AddMember("type", "plane", alloc);
            addVec3(obj, "origin", pos.Location().X(), pos.Location().Y(), pos.Location().Z(), alloc);
            addVec3(obj, "normal", pos.Direction().X(), pos.Direction().Y(), pos.Direction().Z(), alloc);
            addVec3(obj, "x_dir", pos.XDirection().X(), pos.XDirection().Y(), pos.XDirection().Z(), alloc);
            break;
        }
        case GeomAbs_Cylinder: {
            gp_Cylinder cyl = surf.Cylinder();
            gp_Ax3 pos = cyl.Position();
            obj.AddMember("type", "cylinder", alloc);
            addVec3(obj, "origin", pos.Location().X(), pos.Location().Y(), pos.Location().Z(), alloc);
            addVec3(obj, "axis", pos.Direction().X(), pos.Direction().Y(), pos.Direction().Z(), alloc);
            obj.AddMember("radius", cyl.Radius(), alloc);
            break;
        }
        case GeomAbs_Cone: {
            gp_Cone cone = surf.Cone();
            gp_Ax3 pos = cone.Position();
            gp_Pnt apex = cone.Apex();
            obj.AddMember("type", "cone", alloc);
            addVec3(obj, "apex", apex.X(), apex.Y(), apex.Z(), alloc);
            addVec3(obj, "axis", pos.Direction().X(), pos.Direction().Y(), pos.Direction().Z(), alloc);
            obj.AddMember("semi_angle", cone.SemiAngle(), alloc);
            obj.AddMember("ref_radius", cone.RefRadius(), alloc);
            break;
        }
        case GeomAbs_Sphere: {
            gp_Sphere sph = surf.Sphere();
            obj.AddMember("type", "sphere", alloc);
            addVec3(obj, "center", sph.Location().X(), sph.Location().Y(), sph.Location().Z(), alloc);
            obj.AddMember("radius", sph.Radius(), alloc);
            break;
        }
        case GeomAbs_Torus: {
            gp_Torus tor = surf.Torus();
            gp_Ax3 pos = tor.Position();
            obj.AddMember("type", "torus", alloc);
            addVec3(obj, "center", pos.Location().X(), pos.Location().Y(), pos.Location().Z(), alloc);
            addVec3(obj, "axis", pos.Direction().X(), pos.Direction().Y(), pos.Direction().Z(), alloc);
            obj.AddMember("major_radius", tor.MajorRadius(), alloc);
            obj.AddMember("minor_radius", tor.MinorRadius(), alloc);
            break;
        }
        default:
            // Non-analytical surface - still record face_index but no primitive type
            obj.AddMember("type", rapidjson::Value(), alloc); // null
            break;
    }
    
    facesArray.PushBack(obj, alloc);
}

/// Extract all BREP primitives from a shape into a JSON array
static rapidjson::Value extractAllPrimitives(const TopoDS_Shape& shape,
                                              rapidjson::Document::AllocatorType& alloc,
                                              const std::set<std::string>& allowedTypes = {}) {
    rapidjson::Value facesArray(rapidjson::kArrayType);
    int faceIndex = 0;
    
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        TopoDS_Face face = TopoDS::Face(explorer.Current());
        extractFacePrimitive(face, faceIndex, facesArray, alloc, allowedTypes);
        faceIndex++;
    }
    
    return facesArray;
}
