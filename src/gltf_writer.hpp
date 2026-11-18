#pragma once

#include <RWGltf_CafWriter.hxx>
#include <RWGltf_GltfFace.hxx>
#include <RWMesh_FaceIterator.hxx>
#include <TopoDS_Face.hxx>

#include <map>
#include <vector>

/// The run of triangles one BREP face contributed to a glTF index accessor.
struct FaceTriangles {
  int triStart;     ///< first triangle within the accessor
  int triCount;     ///< number of triangles from this face
  TopoDS_Face face; ///< the source face, kept for surface extraction
};

/// BREP faces grouped by the id of the glTF accessor holding their triangle
/// indices, in the order the writer emitted them. The id is the accessor's
/// index in the output JSON, so it identifies the primitive exactly.
using FacesByAccessor = std::map<int, std::vector<FaceTriangles>>;

/// RWGltf_CafWriter that records which triangles came from which face.
/// It only uses hooks OCCT already exposes to subclasses, so it works
/// against an unmodified OpenCASCADE.
class GltfWriter : public RWGltf_CafWriter {
public:
  /// @param path    output GLB path
  /// @param binPath where to put the temporary binary buffer, or nullptr to
  ///                let OCCT create `<path>.bin.tmp` next to the output
  /// @param faces   filled during Perform() if not null
  GltfWriter(const char *path, const char *binPath, FacesByAccessor *faces)
      : RWGltf_CafWriter(path, true), faces_(faces) {
    if (binPath != nullptr) {
      myBinFileNameFull = binPath;
    }
  }

protected:
  void saveTriangleIndices(
      RWGltf_GltfFace &gltfFace, std::ostream &binFile,
      const RWMesh_FaceIterator &faceIter,
      const std::shared_ptr<RWGltf_CafWriter::Mesh> &mesh) override {
    if (faces_ != nullptr) {
      // Indices.Count is the number of indices written so far for this
      // accessor; the base class advances it by this face's triangles.
      (*faces_)[gltfFace.Indices.Id].push_back(
          {static_cast<int>(gltfFace.Indices.Count / 3),
           faceIter.NbTriangles(), faceIter.Face()});
    }
    RWGltf_CafWriter::saveTriangleIndices(gltfFace, binFile, faceIter, mesh);
  }

private:
  FacesByAccessor *faces_;
};
