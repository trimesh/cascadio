#pragma once

#include "extras.hpp"

#include <TDocStd_Document.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_MaterialTool.hxx>
#include <XCAFDoc_VisMaterialTool.hxx>
#include <XCAFDoc_VisMaterial.hxx>
#include <TDF_LabelSequence.hxx>
#include <TCollection_HAsciiString.hxx>

// ============================================================================
// Material Extraction
// ============================================================================

/// Extract all materials from the document into a JSON array
static rapidjson::Value extractMaterials(Handle(TDocStd_Document) doc,
                                          rapidjson::Document::AllocatorType& alloc) {
    rapidjson::Value materialsArray(rapidjson::kArrayType);
    
    TDF_Label mainLabel = doc->Main();
    
    // Extract physical materials (name, description, density)
    Handle(XCAFDoc_MaterialTool) matTool = XCAFDoc_DocumentTool::MaterialTool(mainLabel);
    if (!matTool.IsNull()) {
        TDF_LabelSequence matLabels;
        matTool->GetMaterialLabels(matLabels);
        
        for (Standard_Integer i = 1; i <= matLabels.Length(); i++) {
            Handle(TCollection_HAsciiString) name, description, densName, densValType;
            Standard_Real density = 0.0;
            
            if (XCAFDoc_MaterialTool::GetMaterial(matLabels.Value(i), name, description, 
                                                   density, densName, densValType)) {
                rapidjson::Value matObj(rapidjson::kObjectType);
                
                if (!name.IsNull() && name->Length() > 0) {
                    rapidjson::Value nameVal(name->ToCString(), alloc);
                    matObj.AddMember("name", nameVal, alloc);
                }
                if (!description.IsNull() && description->Length() > 0) {
                    rapidjson::Value descVal(description->ToCString(), alloc);
                    matObj.AddMember("description", descVal, alloc);
                }
                if (density > 0.0) {
                    matObj.AddMember("density", density, alloc);
                    if (!densName.IsNull() && densName->Length() > 0) {
                        rapidjson::Value densNameVal(densName->ToCString(), alloc);
                        matObj.AddMember("density_name", densNameVal, alloc);
                    }
                    if (!densValType.IsNull() && densValType->Length() > 0) {
                        rapidjson::Value densTypeVal(densValType->ToCString(), alloc);
                        matObj.AddMember("density_value_type", densTypeVal, alloc);
                    }
                }
                
                materialsArray.PushBack(matObj, alloc);
            }
        }
    }
    
    // Extract visual materials (colors, PBR properties)
    Handle(XCAFDoc_VisMaterialTool) visMatTool = XCAFDoc_DocumentTool::VisMaterialTool(mainLabel);
    if (!visMatTool.IsNull()) {
        TDF_LabelSequence visMatLabels;
        visMatTool->GetMaterials(visMatLabels);
        
        for (Standard_Integer i = 1; i <= visMatLabels.Length(); i++) {
            Handle(XCAFDoc_VisMaterial) visMat = XCAFDoc_VisMaterialTool::GetMaterial(visMatLabels.Value(i));
            if (visMat.IsNull() || visMat->IsEmpty()) {
                continue;
            }
            
            rapidjson::Value matObj(rapidjson::kObjectType);
            
            // Get material name if available
            Handle(TCollection_HAsciiString) rawName = visMat->RawName();
            if (!rawName.IsNull() && rawName->Length() > 0) {
                rapidjson::Value nameVal(rawName->ToCString(), alloc);
                matObj.AddMember("name", nameVal, alloc);
            }
            
            // Get base color (works for both common and PBR materials)
            Quantity_ColorRGBA baseColor = visMat->BaseColor();
            addColorRGBA(matObj, "base_color", baseColor, alloc);
            
            // Alpha mode
            matObj.AddMember("alpha_cutoff", static_cast<double>(visMat->AlphaCutOff()), alloc);
            
            // PBR material properties
            if (visMat->HasPbrMaterial()) {
                const XCAFDoc_VisMaterialPBR& pbr = visMat->PbrMaterial();
                rapidjson::Value pbrObj(rapidjson::kObjectType);
                
                addColorRGBA(pbrObj, "base_color", pbr.BaseColor, alloc);
                pbrObj.AddMember("metallic", static_cast<double>(pbr.Metallic), alloc);
                pbrObj.AddMember("roughness", static_cast<double>(pbr.Roughness), alloc);
                pbrObj.AddMember("refraction_index", static_cast<double>(pbr.RefractionIndex), alloc);
                
                // Emissive factor as RGB array
                rapidjson::Value emissiveArr(rapidjson::kArrayType);
                emissiveArr.PushBack(static_cast<double>(pbr.EmissiveFactor.x()), alloc)
                           .PushBack(static_cast<double>(pbr.EmissiveFactor.y()), alloc)
                           .PushBack(static_cast<double>(pbr.EmissiveFactor.z()), alloc);
                pbrObj.AddMember("emissive_factor", emissiveArr, alloc);
                
                matObj.AddMember("pbr", pbrObj, alloc);
            }
            
            // Common (legacy) material properties
            if (visMat->HasCommonMaterial()) {
                const XCAFDoc_VisMaterialCommon& common = visMat->CommonMaterial();
                rapidjson::Value commonObj(rapidjson::kObjectType);
                
                addColorRGB(commonObj, "ambient_color", common.AmbientColor, alloc);
                addColorRGB(commonObj, "diffuse_color", common.DiffuseColor, alloc);
                addColorRGB(commonObj, "specular_color", common.SpecularColor, alloc);
                addColorRGB(commonObj, "emissive_color", common.EmissiveColor, alloc);
                commonObj.AddMember("shininess", static_cast<double>(common.Shininess), alloc);
                commonObj.AddMember("transparency", static_cast<double>(common.Transparency), alloc);
                
                matObj.AddMember("common", commonObj, alloc);
            }
            
            materialsArray.PushBack(matObj, alloc);
        }
    }
    
    return materialsArray;
}
