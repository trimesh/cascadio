#pragma once

#include "tm_brep_faces.hpp" // generated from the schema at build time

#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax3.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>
#include <gp_Trsf.hxx>

#include <set>
#include <string>

/// Describe one BREP face as TM_brep_faces JSON.
///
/// Returns a JSON null when the face is not a plane, cylinder, cone, sphere
/// or torus, or when `allowedTypes` is non-empty and does not list its type.
/// Callers keep the null so array positions still match face indices.
///
/// @param lengthUnit scale from the file's unit to meters
/// @param frame      moves the surface into the coordinate frame of the mesh
static rapidjson::Value describeFace(const TopoDS_Face &face, int faceIndex,
                                     brep::Allocator &alloc,
                                     const std::set<std::string> &allowedTypes,
                                     double lengthUnit, const gp_Trsf &frame) {
  rapidjson::Value described; // null
  if (face.IsNull()) {
    return described;
  }

  const BRepAdaptor_Surface surface(face, true);
  double uMin, uMax, vMin, vMax;
  BRepTools::UVBounds(face, uMin, uMax, vMin, vMax);

  // positions and lengths are scaled to meters; directions and angles are not
  const double m = lengthUnit;
  auto point = [m](const gp_Pnt &p) {
    return brep::Vec3{p.X() * m, p.Y() * m, p.Z() * m};
  };
  auto direction = [](const gp_Dir &d) {
    return brep::Vec3{d.X(), d.Y(), d.Z()};
  };
  const brep::Bounds u{uMin, uMax}, v{vMin, vMax};
  const brep::Bounds uMeters{uMin * m, uMax * m}, vMeters{vMin * m, vMax * m};

  // The schema says angles turn counter-clockwise about the axis and a
  // plane's Y is normal cross X. OpenCASCADE allows a mirrored frame, where
  // both run the other way; negate that coordinate so the output keeps
  // its promise.
  auto mirrored = [](const brep::Bounds &range) {
    return brep::Bounds{-range.max, -range.min};
  };

  switch (surface.GetType()) {
  case GeomAbs_Plane: {
    const gp_Ax3 axes = surface.Plane().Transformed(frame).Position();
    described = brep::plane(faceIndex, point(axes.Location()),
                            direction(axes.Direction()),
                            direction(axes.XDirection()), uMeters,
                            axes.Direct() ? vMeters : mirrored(vMeters), alloc);
    break;
  }
  case GeomAbs_Cylinder: {
    const gp_Cylinder cylinder = surface.Cylinder().Transformed(frame);
    const gp_Ax3 &axes = cylinder.Position();
    described = brep::cylinder(faceIndex, point(axes.Location()),
                               direction(axes.Direction()),
                               direction(axes.XDirection()),
                               cylinder.Radius() * m,
                               axes.Direct() ? u : mirrored(u), vMeters, alloc);
    break;
  }
  case GeomAbs_Cone: {
    const gp_Cone cone = surface.Cone().Transformed(frame);
    const gp_Ax3 &axes = cone.Position();
    described = brep::cone(faceIndex, point(cone.Apex()),
                           direction(axes.Direction()),
                           direction(axes.XDirection()), cone.SemiAngle(),
                           cone.RefRadius() * m,
                           axes.Direct() ? u : mirrored(u), vMeters, alloc);
    break;
  }
  case GeomAbs_Sphere: {
    const gp_Sphere sphere = surface.Sphere().Transformed(frame);
    const gp_Ax3 &axes = sphere.Position();
    described = brep::sphere(faceIndex, point(axes.Location()),
                             direction(axes.Direction()),
                             direction(axes.XDirection()), sphere.Radius() * m,
                             axes.Direct() ? u : mirrored(u), v, alloc);
    break;
  }
  case GeomAbs_Torus: {
    const gp_Torus torus = surface.Torus().Transformed(frame);
    const gp_Ax3 &axes = torus.Position();
    described = brep::torus(faceIndex, point(axes.Location()),
                            direction(axes.Direction()),
                            direction(axes.XDirection()),
                            torus.MajorRadius() * m, torus.MinorRadius() * m,
                            axes.Direct() ? u : mirrored(u), v, alloc);
    break;
  }
  default:
    return described;
  }

  if (!allowedTypes.empty() &&
      allowedTypes.count(described["type"].GetString()) == 0) {
    described.SetNull();
  }
  return described;
}
