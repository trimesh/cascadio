#!/usr/bin/env python3
"""
Generate everything that repeats the TM_brep_faces schema, from the schema.

    python scripts/gen_schema.py header OUTPUT.hpp   the C++ that writes it
    python scripts/gen_schema.py readme OUTPUT.md    the extension's README
    python scripts/gen_schema.py python OUTPUT.py    the Python dataclasses
    python scripts/gen_schema.py                     rewrite the two files
                                                     kept in the repository

The header is written by CMake on every build. It holds the extension's
name, one function for the extension object, one for a substance, and
one per face type, each taking the schema's fields as typed arguments:

    brep::EXTENSION                       "TM_brep_faces"
    brep::extension(faceIndices, faces, substance, alloc)
    brep::substance(name, density, alloc)
    brep::plane(face_index, origin, normal, x_dir, extent_x, extent_y, alloc)

The JSON keys exist only in the schema. If the schema gains, loses or
changes a field the C++ that calls these functions stops compiling, so
the code and the schema cannot drift apart.

The README and the dataclasses are kept in the repository so they can be
read without building. A test fails if either is out of date; run this
script with no arguments (or `make schema`) to refresh them.
"""

import json
import sys
import textwrap
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCHEMA = (
    ROOT / "extensions/TM_brep_faces/schema/mesh.primitive.TM_brep_faces.schema.json"
)
# the generated files that are kept in the repository
TRACKED = {
    "readme": ROOT / "extensions/TM_brep_faces/README.md",
    "python": ROOT / "src/cascadio/primitives.py",
}


def face_cases(schema: dict) -> dict:
    """Face type name -> its fields, in schema order."""
    face = schema["$defs"]["face"]
    names = face["properties"]["type"]["enum"]
    cases = {
        case["if"]["properties"]["type"]["const"]: case["then"]
        for case in face["allOf"]
    }
    if sorted(cases) != sorted(names):
        raise SystemExit(f"face types {names} do not match the cases {list(cases)}")
    for name, case in cases.items():
        if list(case["properties"]) != case["required"]:
            raise SystemExit(f"{name}: every property must be listed in `required`")
    return {name: cases[name]["properties"] for name in names}


# Fields the schema gained after cascadio 0.1. The schema requires them and
# the writer always writes them, but GLB files made by 0.1 are still out
# there. So the Python dataclasses give exactly these fields a default of
# None, and keep them last, which lets those older files load. This is a
# record of history, so it lives here and not in the schema; add to it
# whenever a field is added to an existing face type.
ADDED_SINCE_0_1 = {
    "plane": [],
    "cylinder": ["x_dir"],
    "cone": ["x_dir"],
    "sphere": ["axis", "x_dir"],
    "torus": ["x_dir"],
}


def extension_name(schema: dict) -> str:
    # "mesh.primitive.TM_brep_faces.schema.json" -> "TM_brep_faces"
    return schema["$id"].split(".")[2]


# ---------------------------------------------------------------------------
# C++ header
# ---------------------------------------------------------------------------

# schema type of a field -> C++ argument type
CPP_TYPES = {
    "#/$defs/point": "const Point &",
    "#/$defs/direction": "const Direction &",
    "#/$defs/range": "const Range &",
}

HEADER = """\
// Generated from {schema} by scripts/gen_schema.py.
// Do not edit: change the schema instead.
#pragma once

#include <optional>
#include <string>

#include <rapidjson/document.h>

namespace brep {{

// One C++ type per kind of value in the schema, so that passing a
// direction where a position belongs does not compile.

/// A position [x, y, z] in meters
struct Point {{
  double x, y, z;
}};
/// A direction [x, y, z] of length 1
struct Direction {{
  double x, y, z;
}};
/// [min, max] along one of a surface's two directions
struct Range {{
  double min, max;
}};
using Allocator = rapidjson::Document::AllocatorType;
using Value = rapidjson::Value;

inline void add(Value &object, const char *key, const Point &p, Allocator &a) {{
  Value array(rapidjson::kArrayType);
  array.PushBack(p.x, a).PushBack(p.y, a).PushBack(p.z, a);
  object.AddMember(rapidjson::StringRef(key), array, a);
}}

inline void add(Value &object, const char *key, const Direction &d, Allocator &a) {{
  Value array(rapidjson::kArrayType);
  array.PushBack(d.x, a).PushBack(d.y, a).PushBack(d.z, a);
  object.AddMember(rapidjson::StringRef(key), array, a);
}}

inline void add(Value &object, const char *key, const Range &r, Allocator &a) {{
  Value array(rapidjson::kArrayType);
  array.PushBack(r.min, a).PushBack(r.max, a);
  object.AddMember(rapidjson::StringRef(key), array, a);
}}

inline void add(Value &object, const char *key, double number, Allocator &a) {{
  object.AddMember(rapidjson::StringRef(key), number, a);
}}

inline void add(Value &object, const char *key, const std::string &text,
                Allocator &a) {{
  object.AddMember(rapidjson::StringRef(key), Value(text.c_str(), a), a);
}}

/// An optional field is left out when it has no value.
template <typename T>
inline void add(Value &object, const char *key, const std::optional<T> &value,
                Allocator &a) {{
  if (value) {{
    add(object, key, *value, a);
  }}
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

/// {substance_description}
inline Value substance({substance_arguments}, Allocator &a) {{
  Value object(rapidjson::kObjectType);
{substance_body}
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


def cpp_type(name: str, spec: dict, required: bool = True) -> str:
    if spec.get("$ref") in CPP_TYPES:
        kind = CPP_TYPES[spec["$ref"]]
    elif spec.get("type") == "number":
        kind = "double "
    elif spec.get("type") == "string":
        kind = "const std::string &"
    else:
        raise SystemExit(f"field {name!r}: no C++ type for schema {spec}")
    if required:
        return kind
    return f"const std::optional<{kind.strip(' &').removeprefix('const ')}> &"


def generate_header(schema: dict, source: str) -> str:
    cases = face_cases(schema)
    top = schema["properties"]
    kinds = {"integer": "int ", "array": "Value &"}

    def top_type(field: str, spec: dict) -> str:
        if "oneOf" in spec:  # null or an object: a JSON value either way
            return "Value &"
        if spec.get("type") in kinds:
            return kinds[spec["type"]]
        raise SystemExit(f"extension field {field!r}: no C++ type for {spec}")

    substance = schema["$defs"]["substance"]
    parts = [
        HEADER.format(
            schema=source,
            extension=extension_name(schema),
            types=", ".join(f'"{n}"' for n in cases),
            top_fields=", ".join(top),
            top_arguments=", ".join(top_type(f, spec) + f for f, spec in top.items()),
            top_body="\n".join(f'  object.AddMember("{f}", {f}, a);' for f in top),
            substance_description=substance["description"],
            substance_arguments=", ".join(
                cpp_type(f, spec, f in substance["required"]) + f
                for f, spec in substance["properties"].items()
            ),
            substance_body="\n".join(
                f'  add(object, "{f}", {f}, a);' for f in substance["properties"]
            ),
        )
    ]
    for name, properties in cases.items():
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


# ---------------------------------------------------------------------------
# README
# ---------------------------------------------------------------------------

README = """\
<!-- Generated from schema/{schema} by scripts/gen_schema.py. Do not edit. -->
# {extension}

{description}

![A machined part with faces tinted by surface type and edges colored by the pair of surfaces they join](example.png)

The picture is a mesh, not CAD geometry. Faces are tinted by their `type`. The outlines are the edges between triangles with different `faceIndices`, colored by the two kinds of surface they join: green where two planes meet, blue for a plane and a cylinder, red for two cylinders, orange for a plane and a free-form surface. It takes about thirty lines of Python: [examples/show_brep.py](../../examples/show_brep.py).

## Example

```json
{example}
```

## Properties

All of these are required, and no other properties are allowed, here or inside a face or a substance.

{top}

## Faces

{face_description}

The value types used below:

{types}

Every face has:

{face_common}

and, depending on `type`:

{faces}
## Substance

{substance_description}

{substance}

## Schema

[`schema/{schema}`](schema/{schema}) is the single source of truth. This page, the code in cascadio that writes the extension, and its Python dataclasses are all generated from it.
"""

# Shown in the README, and validated against the schema by a test.
EXAMPLE = {
    "faceIndices": 3,
    "faces": [
        {
            "type": "cylinder",
            "face_index": 0,
            "origin": [0, 0, 0],
            "axis": [0, 0, 1],
            "x_dir": [1, 0, 0],
            "radius": 0.005,
            "extent_angle": [0, 6.2832],
            "extent_height": [0, 0.01],
        },
        None,
    ],
    "substance": {"name": "Steel", "density": 7850},
}


def type_name(schema: dict, spec: dict) -> str:
    """A short human name for a field's type."""
    if "$ref" in spec:
        target = schema["$defs"][spec["$ref"].rsplit("/", 1)[1]]
        return target.get("title", target.get("type", ""))
    if "oneOf" in spec:
        return " or ".join(type_name(schema, option) for option in spec["oneOf"])
    if spec.get("type") == "array" and "$ref" in spec.get("items", {}):
        return "list of " + type_name(schema, spec["items"]) + "s"
    return spec.get("type", "")


def table(schema: dict, properties: dict) -> str:
    rows = ["| Field | Type | Meaning |", "|---|---|---|"]
    for name, spec in properties.items():
        rows.append(f"| `{name}` | {type_name(schema, spec)} | {spec['description']} |")
    return "\n".join(rows)


def example_json() -> str:
    """The example, with each face on one line so it stays readable."""
    faces = [json.dumps(face) for face in EXAMPLE["faces"]]
    lines = ["{", f'  "faceIndices": {EXAMPLE["faceIndices"]},', '  "faces": [']
    lines += [f"    {face}," for face in faces[:-1]] + [f"    {faces[-1]}"]
    lines += ["  ],", f'  "substance": {json.dumps(EXAMPLE["substance"])}', "}"]
    return "\n".join(lines)


def generate_readme(schema: dict, source: str) -> str:
    face = schema["$defs"]["face"]
    substance = schema["$defs"]["substance"]
    faces = "".join(
        f"### {name}\n\n{table(schema, properties)}\n\n"
        for name, properties in face_cases(schema).items()
    )
    return README.format(
        schema=source,
        extension=extension_name(schema),
        description=schema["description"].replace(". ", ".\n\n", 1),
        example=example_json(),
        top=table(schema, schema["properties"]),
        face_description=face["description"],
        types="\n".join(
            f"- {schema['$defs'][name]['title']}: {schema['$defs'][name]['description']}"
            for name in ("point", "direction", "range")
        ),
        face_common=table(schema, face["properties"]),
        faces=faces,
        substance_description=substance["description"] + " Only `name` is required.",
        substance=table(schema, substance["properties"]),
    )


# ---------------------------------------------------------------------------
# Python dataclasses
# ---------------------------------------------------------------------------

PYTHON = '''\
"""
The surfaces of the TM_brep_faces extension as dataclasses.

Generated from extensions/TM_brep_faces/schema/{schema}
by scripts/gen_schema.py. Do not edit: change the schema and run `make schema`.

{units}
"""

from dataclasses import dataclass, fields

# a position [x, y, z] in meters
Point = tuple[float, float, float]
# a direction [x, y, z] of length 1
Direction = tuple[float, float, float]
# [min, max] along one of a surface's two directions
Range = tuple[float, float]
{classes}


BrepPrimitive = {names}

# the `type` of a face in the extension -> its dataclass
PRIMITIVES = {{
{mapping}
}}

__all__ = [
{exports}
]


def parse_primitive(data: dict | None, face_index: int = 0) -> BrepPrimitive | None:
    """
    Parse one face of the TM_brep_faces extension into its dataclass.

    Parameters
    ----------
    data
      A face dict from the extension, or None for a face that is not an
      analytic surface or was filtered out.
    face_index
      Used when the dict carries no `face_index` of its own.

    Returns
    -------
    primitive
      The parsed primitive, or None if `data` is None or of an unknown type.
    """
    kind = PRIMITIVES.get((data or {{}}).get("type"))
    if kind is None:
        return None
    values = {{"face_index": face_index, **data}}
    return kind(
        **{{
            # JSON arrays become tuples so the frozen dataclass is hashable
            f.name: tuple(v) if isinstance(v := values[f.name], list) else v
            for f in fields(kind)
            # fields added in later versions are absent from older files
            if f.name in values
        }}
    )


def parse_brep_faces(brep_faces: list[dict | None]) -> list[BrepPrimitive | None]:
    """
    Parse the `faces` list of the TM_brep_faces extension.

    Returns one entry per face, in the same order, with None for faces
    that are not analytic surfaces.
    """
    return [parse_primitive(face, face_index=i) for i, face in enumerate(brep_faces)]
'''

CLASS = '''

@dataclass(frozen=True)
class {name}:
    """
    A face on a {kind}.

    Attributes
    ----------
{attributes}
    """

{members}
'''

PYTHON_TYPES = {
    "#/$defs/point": "Point",
    "#/$defs/direction": "Direction",
    "#/$defs/range": "Range",
}


def wrap(text: str, indent: str) -> str:
    return textwrap.fill(
        text, width=79, initial_indent=indent, subsequent_indent=indent
    )


def generate_python(schema: dict, source: str) -> str:
    face = schema["$defs"]["face"]
    classes = []
    names = []
    for kind, properties in face_cases(schema).items():
        name = kind.capitalize()
        names.append(name)
        optional = [f for f in ADDED_SINCE_0_1[kind] if f in properties]
        assert optional == ADDED_SINCE_0_1[kind], f"{kind}: {ADDED_SINCE_0_1[kind]}"
        # The order of the dataclass fields is part of the Python API, so it
        # stays what it was in 0.1: the index, the extents, then the rest.
        # Fields added since go last, with a default, so old files parse.
        required = [f for f in properties if f not in optional]
        ordered = sorted(required, key=lambda f: not f.startswith("extent_")) + optional
        members = {
            "face_index": face["properties"]["face_index"],
            **{f: properties[f] for f in ordered},
        }
        classes.append(
            CLASS.format(
                name=name,
                kind=kind,
                attributes="\n".join(
                    f"    {field}\n{wrap(spec['description'], '        ')}"
                    for field, spec in members.items()
                ),
                members="\n".join(
                    f"    {field}: "
                    + (
                        "int"
                        if field == "face_index"
                        else PYTHON_TYPES.get(spec.get("$ref"), "float")
                    )
                    + (" | None = None" if field in optional else "")
                    for field, spec in members.items()
                ),
            )
        )
    # in the order the linter wants `__all__`: constants, classes, functions
    exports = sorted(
        [*names, "BrepPrimitive", "PRIMITIVES", "parse_primitive", "parse_brep_faces"],
        key=lambda name: (not name.isupper(), not name[0].isupper(), name),
    )
    # everything after the first sentence of the schema's description
    # is about units and conventions
    units = schema["description"].split(". ", 1)[1]
    return PYTHON.format(
        schema=source,
        units=wrap(units, ""),
        classes="".join(classes).rstrip("\n"),
        names=" | ".join(names),
        mapping="\n".join(f'    "{n.lower()}": {n},' for n in names),
        exports="\n".join(f'    "{e}",' for e in exports),
    )


GENERATORS = {
    "header": generate_header,
    "readme": generate_readme,
    "python": generate_python,
}


def render(what: str) -> str:
    return GENERATORS[what](json.loads(SCHEMA.read_text()), SCHEMA.name)


if __name__ == "__main__":
    if len(sys.argv) == 1:
        for what, path in TRACKED.items():
            path.write_text(render(what))
            print(f"wrote {path.relative_to(ROOT)}")
    else:
        output = Path(sys.argv[2])
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(render(sys.argv[1]))
