#pragma once

#include "primitives.hpp"

#include <Interface_InterfaceModel.hxx>
#include <STEPConstruct_UnitContext.hxx>
#include <StepBasic_DerivedUnit.hxx>
#include <StepBasic_DerivedUnitElement.hxx>
#include <StepBasic_MeasureWithUnit.hxx>
#include <StepBasic_SiUnitAndLengthUnit.hxx>
#include <StepBasic_SiUnitAndMassUnit.hxx>
#include <StepBasic_ConversionBasedUnitAndLengthUnit.hxx>
#include <StepRepr_MeasureRepresentationItem.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TDF_Label.hxx>
#include <TDataStd_TreeNode.hxx>
#include <XCAFDoc.hxx>
#include <XCAFDoc_MaterialTool.hxx>
#include <rapidjson/document.h>

#include <cmath>
#include <optional>
#include <string>

// What a part is made of: the `substance` of the TM_brep_faces schema.
// This is the CAD material (steel, ABS), not the glTF material, which is
// how the part looks and which OpenCASCADE's glTF writer already writes.

/// Grams per cubic centimeter, the unit OpenCASCADE stores densities in,
/// to kilograms per cubic meter.
constexpr double PER_CUBIC_CENTIMETER = 1000.0;

/// The factor that turns the densities OpenCASCADE stores into kilograms
/// per cubic meter, given the STEP model they were read from.
///
/// OpenCASCADE converts a density's length unit and any conversion-based
/// mass unit (pound) to grams per cubic centimeter, but it ignores the
/// prefix of an SI mass unit. A file in kilograms per cubic meter, which
/// is most of them, comes out 1000 times too small. This finds that prefix.
///
/// Returns nothing if the file's densities use SI mass units with
/// different prefixes, since a stored density does not say which it had.
static std::optional<double>
densityScale(const Handle(Interface_InterfaceModel) & model) {
  std::optional<double> prefix;
  if (model.IsNull()) {
    return PER_CUBIC_CENTIMETER;
  }
  for (int i = 1; i <= model->NbEntities(); i++) {
    Handle(StepRepr_MeasureRepresentationItem) item =
        Handle(StepRepr_MeasureRepresentationItem)::DownCast(model->Value(i));
    if (item.IsNull() || item->Measure().IsNull()) {
      continue;
    }
    Handle(StepBasic_DerivedUnit) unit =
        item->Measure()->UnitComponent().DerivedUnit();
    if (unit.IsNull()) {
      continue;
    }
    // a density is a mass and a length. Files do not agree on the
    // exponents (+3 and -3 for the length, some write 0 for both), and
    // OpenCASCADE ignores them, so this does too.
    Handle(StepBasic_SiUnitAndMassUnit) mass;
    bool perVolume = false;
    for (int j = 1; j <= unit->NbElements(); j++) {
      Handle(StepBasic_DerivedUnitElement) element = unit->ElementsValue(j);
      Handle(StepBasic_NamedUnit) named =
          element.IsNull() ? nullptr : element->Unit();
      if (named.IsNull()) {
        continue;
      }
      if (named->IsKind(STANDARD_TYPE(StepBasic_SiUnitAndMassUnit))) {
        mass = Handle(StepBasic_SiUnitAndMassUnit)::DownCast(named);
      } else if (named->IsKind(STANDARD_TYPE(StepBasic_SiUnitAndLengthUnit)) ||
                 named->IsKind(STANDARD_TYPE(
                     StepBasic_ConversionBasedUnitAndLengthUnit))) {
        perVolume = true;
      }
    }
    if (mass.IsNull() || !perVolume) {
      continue;
    }
    const double found =
        mass->HasPrefix()
            ? STEPConstruct_UnitContext::ConvertSiPrefix(mass->Prefix())
            : 1.0;
    if (prefix && *prefix != found) {
      return std::nullopt;
    }
    prefix = found;
  }
  return PER_CUBIC_CENTIMETER * prefix.value_or(1.0);
}

/// The substance of the part at `label`, or a JSON null if the source file
/// gives it none. `densityScale` turns the stored density into kilograms
/// per cubic meter; without one the density is left out.
static rapidjson::Value partSubstance(const TDF_Label &part,
                                      std::optional<double> densityScale,
                                      rapidjson::Document::AllocatorType &alloc) {
  // a part points at its material through a tree node:
  // the node's father sits on the material's label
  Handle(TDataStd_TreeNode) reference;
  if (part.IsNull() ||
      !part.FindAttribute(XCAFDoc::MaterialRefGUID(), reference) ||
      !reference->HasFather()) {
    return rapidjson::Value();
  }
  Handle(TCollection_HAsciiString) name, description, densityName, valueType;
  double density = 0.0;
  if (!XCAFDoc_MaterialTool::GetMaterial(reference->Father()->Label(), name,
                                         description, density, densityName,
                                         valueType) ||
      name.IsNull() || name->Length() == 0) {
    return rapidjson::Value();
  }
  std::optional<double> kgPerCubicMeter;
  if (density > 0.0 && densityScale) {
    // to the micrograms, dropping float noise from the unit conversions:
    // 7850, not 7849.999999999999
    kgPerCubicMeter = std::round(density * *densityScale * 1e6) / 1e6;
  }
  return brep::substance(name->ToCString(), kgPerCubicMeter, alloc);
}
