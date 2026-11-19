#pragma once

#include <RWGltf_CafWriter.hxx>
#include <RWGltf_GltfFace.hxx>
#include <RWMesh_FaceIterator.hxx>
#include <RWMesh_ShapeIterator.hxx>
#include <TDF_Label.hxx>
#include <TopoDS_Face.hxx>
#include <XCAFPrs_DocumentNode.hxx>

#include <map>
#include <vector>

/// The run of triangles one BREP face contributed to a glTF index accessor.
struct FaceTriangles {
  int triStart;     ///< first triangle within the accessor
  int triCount;     ///< number of triangles from this face
  TopoDS_Face face; ///< the source face, kept for surface extraction
};

/// What the writer learned about each mesh primitive it wrote, keyed by the
/// id of the glTF accessor holding the primitive's triangle indices. The id
/// is the accessor's index in the output JSON, so it identifies the
/// primitive exactly.
struct Recorded {
  /// the BREP faces behind the triangles, in the order they were written
  std::map<int, std::vector<FaceTriangles>> faces;
  /// the document label of the part the primitive belongs to
  std::map<int, TDF_Label> parts;
};

/// RWGltf_CafWriter that records which triangles came from which face and
/// which part each primitive belongs to. It only uses hooks OCCT already
/// exposes to subclasses, so it works against an unmodified OpenCASCADE.
class GltfWriter : public RWGltf_CafWriter {
public:
  /// @param path     output GLB path
  /// @param binPath  where to put the temporary binary buffer, or nullptr to
  ///                 let OCCT create `<path>.bin.tmp` next to the output
  /// @param recorded filled during Perform() if not null
  GltfWriter(const char *path, const char *binPath, Recorded *recorded)
      : RWGltf_CafWriter(path, true), recorded_(recorded) {
    if (binPath != nullptr) {
      myBinFileNameFull = binPath;
    }
  }

protected:
  /// Called once per part, before any of its triangles are written.
  /// The base class creates the part's primitives here; remember which
  /// part they belong to.
  void dispatchShapes(
      const XCAFPrs_DocumentNode &node, const Message_ProgressScope &progress,
      NCollection_DataMap<XCAFPrs_Style, Handle(RWGltf_GltfFace)> &merged,
      RWMesh_ShapeIterator &shapes) override {
    RWGltf_CafWriter::dispatchShapes(node, progress, merged, shapes);
    if (recorded_ == nullptr) {
      return;
    }
    const RWGltf_StyledShape key(shapes.ExploredShape(), node.Style);
    if (myBinDataMap.Contains(key)) {
      for (const Handle(RWGltf_GltfFace) & primitive :
           *myBinDataMap.FindFromKey(key)) {
        // an instanced part is dispatched again; the first label stands
        partOf_.emplace(primitive.get(), node.RefLabel);
      }
    }
  }

  void saveTriangleIndices(
      RWGltf_GltfFace &gltfFace, std::ostream &binFile,
      const RWMesh_FaceIterator &faceIter,
      const std::shared_ptr<RWGltf_CafWriter::Mesh> &mesh) override {
    if (recorded_ != nullptr) {
      const int accessor = gltfFace.Indices.Id;
      // Indices.Count is the number of indices written so far for this
      // accessor; the base class advances it by this face's triangles.
      recorded_->faces[accessor].push_back(
          {static_cast<int>(gltfFace.Indices.Count / 3),
           faceIter.NbTriangles(), faceIter.Face()});
      const auto part = partOf_.find(&gltfFace);
      if (part != partOf_.end()) {
        recorded_->parts.emplace(accessor, part->second);
      }
    }
    RWGltf_CafWriter::saveTriangleIndices(gltfFace, binFile, faceIter, mesh);
  }

private:
  Recorded *recorded_;
  std::map<const RWGltf_GltfFace *, TDF_Label> partOf_;
};
