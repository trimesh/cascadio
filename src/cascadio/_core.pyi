from __future__ import annotations

from enum import Enum
from typing import Literal, Optional, Set

BrepType = Literal["plane", "cylinder", "cone", "sphere", "torus"]

class FileType(Enum):
    UNSPECIFIED: FileType
    STEP: FileType
    IGES: FileType

def to_glb_bytes(
    data: bytes,
    file_type: FileType = FileType.STEP,
    tol_linear: float = 0.01,
    tol_angular: float = 0.5,
    tol_relative: bool = False,
    merge_primitives: bool = True,
    use_parallel: bool = True,
    include_brep: bool = False,
    brep_types: Optional[Set[BrepType]] = None,
    include_materials: bool = False,
) -> bytes: ...
def step_to_glb(
    input_path: str,
    output_path: str,
    tol_linear: float = 0.01,
    tol_angular: float = 0.5,
    tol_relative: bool = False,
    merge_primitives: bool = True,
    use_parallel: bool = True,
    include_brep: bool = False,
    brep_types: Optional[Set[BrepType]] = None,
    include_materials: bool = False,
) -> int: ...
def step_to_obj(
    input_path: str,
    output_path: str,
    tol_linear: float = 0.01,
    tol_angular: float = 0.5,
    tol_relative: bool = False,
    use_parallel: bool = True,
    use_colors: bool = True,
) -> int: ...

__version__: str
