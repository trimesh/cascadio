#!/usr/bin/env python3
"""
Generate the C++ header that writes TM_brep_faces JSON from the schema.

    python scripts/gen_schema_header.py SCHEMA.json OUTPUT.hpp

CMake runs this on every build. It emits the extension's name, one function
for the extension object itself, and one per face type, each taking the
schema's fields as typed arguments:

    brep::EXTENSION                       "TM_brep_faces"
    brep::extension(faceIndices, faces, materials, alloc)
    brep::plane(face_index, origin, normal, x_dir, extent_x, extent_y, alloc)

The JSON keys exist only in the schema. If the schema gains, loses or
changes a field the C++ that calls these functions stops compiling, so
the code and the schema cannot drift apart.
"""

import json
import sys
from pathlib import Path

# schema type of a field -> C++ argument type
CPP_TYPES = {"#/$defs/vec3": "const Vec3 &", "#/$defs/bounds": "const Bounds &"}

HEADER = """\
// Generated from {schema} by scripts/gen_schema_header.py.
// Do not edit: change the schema instead.
#pragma once

#include <rapidjson/document.h>

#include <array>

namespace brep {{

using Vec3 = std::array<double, 3>;
/// [min, max]; its own type so it cannot be passed where a Vec3 belongs
struct Bounds {{
  double min, max;
}};
using Allocator = rapidjson::Document::AllocatorType;
using Value = rapidjson::Value;

inline void add(Value &object, const char *key, const Vec3 &v, Allocator &a) {{
  Value array(rapidjson::kArrayType);
  array.PushBack(v[0], a).PushBack(v[1], a).PushBack(v[2], a);
  object.AddMember(rapidjson::StringRef(key), array, a);
}}

inline void add(Value &object, const char *key, const Bounds &b, Allocator &a) {{
  Value array(rapidjson::kArrayType);
  array.PushBack(b.min, a).PushBack(b.max, a);
  object.AddMember(rapidjson::StringRef(key), array, a);
}}

inline void add(Value &object, const char *key, double number, Allocator &a) {{
  object.AddMember(rapidjson::StringRef(key), number, a);
}}

/// The name of the extension, as it appears in `extensionsUsed`.
inline constexpr const char *EXTENSION = "{extension}";

/// Every face type the schema defines.
inline constexpr const char *TYPES[] = {{{types}}};

/// The extension object attached to a mesh primitive: {top_fields}.
inline Value extension({top_arguments}, Allocator &a) {{
  Value object(rapidjson::kObjectType);
{top_body}
  return object;
}}
"""

FUNCTION = """
/// A `{name}` face: {fields}.
inline Value {name}(int face_index, {arguments}, Allocator &a) {{
  Value face(rapidjson::kObjectType);
  face.AddMember("face_index", face_index, a);
  face.AddMember("type", "{name}", a);
{body}
  return face;
}}
"""


def cpp_type(name: str, spec: dict) -> str:
    if spec.get("$ref") in CPP_TYPES:
        return CPP_TYPES[spec["$ref"]]
    if spec.get("type") == "number":
        return "double "
    raise SystemExit(f"field {name!r}: no C++ type for schema {spec}")


def generate(schema: dict, source: str) -> str:
    face = schema["$defs"]["face"]
    names = face["properties"]["type"]["enum"]
    cases = {
        case["if"]["properties"]["type"]["const"]: case["then"]
        for case in face["allOf"]
    }
    if sorted(cases) != sorted(names):
        raise SystemExit(f"face types {names} do not match the cases {list(cases)}")

    # "mesh.primitive.TM_brep_faces.schema.json" -> "TM_brep_faces"
    extension = schema["$id"].split(".")[2]
    top = schema["properties"]
    kinds = {"integer": "int ", "array": "Value &"}
    for field, spec in top.items():
        if spec.get("type") not in kinds:
            raise SystemExit(f"extension field {field!r}: no C++ type for {spec}")
    parts = [
        HEADER.format(
            schema=source,
            extension=extension,
            types=", ".join(f'"{n}"' for n in names),
            top_fields=", ".join(top),
            top_arguments=", ".join(kinds[spec["type"]] + f for f, spec in top.items()),
            top_body="\n".join(f'  object.AddMember("{f}", {f}, a);' for f in top),
        )
    ]
    for name in names:
        properties = cases[name]["properties"]
        if list(properties) != cases[name]["required"]:
            raise SystemExit(f"{name}: every property must be listed in `required`")
        parts.append(
            FUNCTION.format(
                name=name,
                fields=", ".join(properties),
                arguments=", ".join(
                    cpp_type(field, spec) + field for field, spec in properties.items()
                ),
                body="\n".join(
                    f'  add(face, "{field}", {field}, a);' for field in properties
                ),
            )
        )
    parts.append("\n} // namespace brep\n")
    return "".join(parts)


if __name__ == "__main__":
    schema_path, output_path = map(Path, sys.argv[1:3])
    text = generate(json.loads(schema_path.read_text()), schema_path.name)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(text)
