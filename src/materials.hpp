#pragma once

#include <NCollection_Sequence.hxx>
#include <Quantity_Color.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TDF_Label.hxx>
#include <TDataStd_TreeNode.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_MaterialTool.hxx>
#include <XCAFDoc_VisMaterial.hxx>
#include <XCAFDoc_VisMaterialTool.hxx>
#include <rapidjson/document.h>

// Materials as JSON, in the shape the TM_brep_faces schema describes
// (`$defs/material`). A STEP file can define two kinds, and a part can
// have one of each:
//   physical: a name and a density
//   visual:   colors, and optionally PBR or classic shading parameters

using JsonAllocator = rapidjson::Document::AllocatorType;

/// Add `key` to `object` if the string has any text.
static void addText(rapidjson::Value &object, const char *key,
                    const Handle(TCollection_HAsciiString) & text,
                    JsonAllocator &alloc) {
  if (!text.IsNull() && text->Length() > 0) {
    object.AddMember(rapidjson::StringRef(key),
                     rapidjson::Value(text->ToCString(), alloc), alloc);
  }
}

static rapidjson::Value colorJson(const Quantity_Color &color,
                                  JsonAllocator &alloc) {
  rapidjson::Value array(rapidjson::kArrayType);
  array.PushBack(color.Red(), alloc)
      .PushBack(color.Green(), alloc)
      .PushBack(color.Blue(), alloc);
  return array;
}

static rapidjson::Value colorJson(const Quantity_ColorRGBA &color,
                                  JsonAllocator &alloc) {
  rapidjson::Value array = colorJson(color.GetRGB(), alloc);
  array.PushBack(color.Alpha(), alloc);
  return array;
}

/// The physical material stored on `label`, or a JSON null if there is none.
static rapidjson::Value physicalMaterial(const TDF_Label &label,
                                         JsonAllocator &alloc) {
  Handle(TCollection_HAsciiString) name, description, densityName, valueType;
  double density = 0.0;
  rapidjson::Value material;
  if (!XCAFDoc_MaterialTool::GetMaterial(label, name, description, density,
                                         densityName, valueType)) {
    return material;
  }
  material.SetObject();
  addText(material, "name", name, alloc);
  addText(material, "description", description, alloc);
  if (density > 0.0) {
    material.AddMember("density", density, alloc);
    addText(material, "density_name", densityName, alloc);
    addText(material, "density_value_type", valueType, alloc);
  }
  return material;
}

/// A visual material as JSON, or a JSON null if it is missing or empty.
static rapidjson::Value visualMaterial(const Handle(XCAFDoc_VisMaterial) & visual,
                                       JsonAllocator &alloc) {
  rapidjson::Value material;
  if (visual.IsNull() || visual->IsEmpty()) {
    return material;
  }
  material.SetObject();
  addText(material, "name", visual->RawName(), alloc);
  material.AddMember("base_color", colorJson(visual->BaseColor(), alloc), alloc);
  material.AddMember("alpha_cutoff", static_cast<double>(visual->AlphaCutOff()),
                     alloc);

  if (visual->HasPbrMaterial()) {
    const XCAFDoc_VisMaterialPBR &pbr = visual->PbrMaterial();
    rapidjson::Value json(rapidjson::kObjectType);
    json.AddMember("base_color", colorJson(pbr.BaseColor, alloc), alloc);
    json.AddMember("metallic", static_cast<double>(pbr.Metallic), alloc);
    json.AddMember("roughness", static_cast<double>(pbr.Roughness), alloc);
    json.AddMember("refraction_index",
                   static_cast<double>(pbr.RefractionIndex), alloc);
    rapidjson::Value emissive(rapidjson::kArrayType);
    emissive.PushBack(static_cast<double>(pbr.EmissiveFactor.x()), alloc)
        .PushBack(static_cast<double>(pbr.EmissiveFactor.y()), alloc)
        .PushBack(static_cast<double>(pbr.EmissiveFactor.z()), alloc);
    json.AddMember("emissive_factor", emissive, alloc);
    material.AddMember("pbr", json, alloc);
  }

  if (visual->HasCommonMaterial()) {
    const XCAFDoc_VisMaterialCommon &common = visual->CommonMaterial();
    rapidjson::Value json(rapidjson::kObjectType);
    json.AddMember("ambient_color", colorJson(common.AmbientColor, alloc), alloc);
    json.AddMember("diffuse_color", colorJson(common.DiffuseColor, alloc), alloc);
    json.AddMember("specular_color", colorJson(common.SpecularColor, alloc),
                   alloc);
    json.AddMember("emissive_color", colorJson(common.EmissiveColor, alloc),
                   alloc);
    json.AddMember("shininess", static_cast<double>(common.Shininess), alloc);
    json.AddMember("transparency", static_cast<double>(common.Transparency),
                   alloc);
    material.AddMember("common", json, alloc);
  }
  return material;
}

/// The materials assigned to one part, given the part's label in the
/// document. Empty if the part has none of its own.
static rapidjson::Value partMaterials(const TDF_Label &part,
                                      JsonAllocator &alloc) {
  rapidjson::Value materials(rapidjson::kArrayType);
  if (part.IsNull()) {
    return materials;
  }
  // a part points at its physical material through a tree node:
  // the node's father sits on the material's label
  Handle(TDataStd_TreeNode) reference;
  if (part.FindAttribute(XCAFDoc::MaterialRefGUID(), reference) &&
      reference->HasFather()) {
    rapidjson::Value material =
        physicalMaterial(reference->Father()->Label(), alloc);
    if (!material.IsNull()) {
      materials.PushBack(material, alloc);
    }
  }
  rapidjson::Value visual =
      visualMaterial(XCAFDoc_VisMaterialTool::GetShapeMaterial(part), alloc);
  if (!visual.IsNull()) {
    materials.PushBack(visual, alloc);
  }
  return materials;
}

/// Every material the document defines, whichever part uses it.
static rapidjson::Value allMaterials(const Handle(TDocStd_Document) & doc,
                                     JsonAllocator &alloc) {
  rapidjson::Value materials(rapidjson::kArrayType);
  const TDF_Label root = doc->Main();

  NCollection_Sequence<TDF_Label> labels;
  Handle(XCAFDoc_MaterialTool) physical =
      XCAFDoc_DocumentTool::MaterialTool(root);
  if (!physical.IsNull()) {
    physical->GetMaterialLabels(labels);
    for (int i = 1; i <= labels.Length(); i++) {
      rapidjson::Value material = physicalMaterial(labels.Value(i), alloc);
      if (!material.IsNull()) {
        materials.PushBack(material, alloc);
      }
    }
  }

  labels.Clear();
  Handle(XCAFDoc_VisMaterialTool) visual =
      XCAFDoc_DocumentTool::VisMaterialTool(root);
  if (!visual.IsNull()) {
    visual->GetMaterials(labels);
    for (int i = 1; i <= labels.Length(); i++) {
      rapidjson::Value material = visualMaterial(
          XCAFDoc_VisMaterialTool::GetMaterial(labels.Value(i)), alloc);
      if (!material.IsNull()) {
        materials.PushBack(material, alloc);
      }
    }
  }
  return materials;
}
