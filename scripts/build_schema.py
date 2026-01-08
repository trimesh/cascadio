#!/usr/bin/env python3
"""
Build a resolved JSON schema from the TM_brep_faces extension schema.

This script resolves all $ref references and outputs a single JSON file
that can be packaged with cascadio.

Usage:
    python scripts/build_schema.py
"""

import json
from pathlib import Path

# Paths
SCRIPT_DIR = Path(__file__).parent
REPO_ROOT = SCRIPT_DIR.parent
SCHEMA_DIR = REPO_ROOT / "extensions" / "TM_brep_faces" / "schema"
OUTPUT_PATH = REPO_ROOT / "src" / "cascadio" / "schema.json"


def load_schemas():
    """Load all schema files from the schema directory."""
    schemas = {}
    for schema_file in SCHEMA_DIR.glob("*.json"):
        with open(schema_file) as f:
            schemas[schema_file.name] = json.load(f)
    return schemas


def resolve_ref(ref_path, schemas):
    """
    Resolve a $ref path to its actual schema content.

    Handles two forms:
    - "face.schema.json" (whole file)
    - "definitions.schema.json#/$defs/vec3" (file + JSON pointer)
    """
    if "#" in ref_path:
        file_name, pointer = ref_path.split("#", 1)
        schema = schemas[file_name]
        # Navigate JSON pointer (e.g., "/$defs/vec3")
        parts = pointer.strip("/").split("/")
        result = schema
        for part in parts:
            result = result[part]
        return result
    else:
        # Whole file reference
        return schemas[ref_path]


def resolve_refs(obj, schemas):
    """
    Recursively resolve all $ref references in a schema object.

    Returns a new object with all references inlined.
    """
    if isinstance(obj, dict):
        if "$ref" in obj:
            # Resolve the reference
            resolved = resolve_ref(obj["$ref"], schemas)
            # Recursively resolve any refs in the resolved content
            return resolve_refs(resolved, schemas)
        # Recursively process all values
        return {k: resolve_refs(v, schemas) for k, v in obj.items()}
    elif isinstance(obj, list):
        return [resolve_refs(item, schemas) for item in obj]
    return obj


def build_resolved_schema():
    """Build and return the fully resolved schema."""
    schemas = load_schemas()

    # Start with the top-level extension schema
    root_schema = schemas["mesh.primitive.TM_brep_faces.schema.json"]

    # Resolve all $ref references
    resolved = resolve_refs(root_schema, schemas)

    return resolved


def main():
    resolved = build_resolved_schema()

    # Write the resolved schema
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    with open(OUTPUT_PATH, "w") as f:
        json.dump(resolved, f, indent=2)

    print(f"Wrote resolved schema to {OUTPUT_PATH}")


if __name__ == "__main__":
    main()
