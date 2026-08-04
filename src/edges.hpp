#pragma once

#include <Precision.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <GeomAbs_CurveType.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <cstdint>
#include <vector>

struct EdgeData {
  std::vector<float> vertices;
  std::vector<uint32_t> indices;
  uint32_t lineCount = 0;
  uint32_t edgeCount = 0;
  float minX = 1e38f, minY = 1e38f, minZ = 1e38f;
  float maxX = -1e38f, maxY = -1e38f, maxZ = -1e38f;
};

static EdgeData extractEdges(const TopoDS_Shape &shape,
                              double tol_linear,
                              double tol_angular,
                              bool tol_relative,
                              double bboxDiag,
                              double lengthUnit) {
  EdgeData result;

  if (tol_relative && bboxDiag > 0) {
    tol_linear *= bboxDiag;
  }

  uint32_t vertexOffset = 0;

  for (TopExp_Explorer exp(shape, TopAbs_EDGE); exp.More(); exp.Next()) {
    TopoDS_Edge edge = TopoDS::Edge(exp.Current());

    BRepAdaptor_Curve adaptor(edge);
    GeomAbs_CurveType ct = adaptor.GetType();

    double first = adaptor.FirstParameter();
    double last = adaptor.LastParameter();

    std::vector<gp_Pnt> pts;

    if (ct == GeomAbs_Line) {
      gp_Pnt p;
      adaptor.D0(first, p);
      pts.push_back(p);
      adaptor.D0(last, p);
      pts.push_back(p);
    } else {
      // Match mesh's BRepMesh_CurveTessellator: halve both tolerances (line 73-74)
      // and require min 4 points for conic curves (line 103-107)
      double halfAng = std::max(tol_angular * 0.5, Precision::Angular());
      double halfLin = std::max(tol_linear * 0.5, Precision::Confusion());
      int minPts = 2;
      if (ct == GeomAbs_Circle || ct == GeomAbs_Ellipse ||
          ct == GeomAbs_Parabola || ct == GeomAbs_Hyperbola) {
        minPts = 4;
      }
      GCPnts_TangentialDeflection td;
      td.Initialize(adaptor, first, last, halfAng, halfLin, minPts);
      if (td.NbPoints() >= 2) {
        int n = td.NbPoints();
        for (int i = 1; i <= n; i++) {
          pts.push_back(td.Value(i));
        }
      } else {
        gp_Pnt p;
        adaptor.D0(first, p);
        pts.push_back(p);
        adaptor.D0(last, p);
        pts.push_back(p);
      }
    }

    if (pts.size() >= 2) {
      for (const auto &p : pts) {
        float vx = static_cast<float>(p.X() * lengthUnit);
        float vy = static_cast<float>(p.Y() * lengthUnit);
        float vz = static_cast<float>(p.Z() * lengthUnit);
        result.vertices.push_back(vx);
        result.vertices.push_back(vy);
        result.vertices.push_back(vz);
        if (vx < result.minX) result.minX = vx;
        if (vy < result.minY) result.minY = vy;
        if (vz < result.minZ) result.minZ = vz;
        if (vx > result.maxX) result.maxX = vx;
        if (vy > result.maxY) result.maxY = vy;
        if (vz > result.maxZ) result.maxZ = vz;
      }

      uint32_t n = static_cast<uint32_t>(pts.size());
      for (uint32_t j = 0; j < n - 1; j++) {
        result.indices.push_back(vertexOffset + j);
        result.indices.push_back(vertexOffset + j + 1);
        result.lineCount++;
      }

      vertexOffset += n;
      result.edgeCount++;
    }
  }

  return result;
}
