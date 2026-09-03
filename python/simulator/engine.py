"""Locates and loads the compiled ``lob_engine`` pybind11 module.

The C++ build (CMake with ``-DLOB_BUILD_PYTHON_BINDINGS=ON``) produces a
platform-specific extension module under a ``build*/bindings/`` directory
rather than an installed Python package, since this project has no
separate packaging/install step. This module finds whichever build
directory has it and loads it directly, so the rest of ``simulator`` can
just do ``from simulator.engine import lob_engine``.
"""

from __future__ import annotations

import glob
import importlib.util
import os
import sys
from types import ModuleType

_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def _find_module_file() -> str:
    patterns = [
        os.path.join(_REPO_ROOT, "build*", "bindings", "**", "lob_engine*.pyd"),
        os.path.join(_REPO_ROOT, "build*", "bindings", "**", "lob_engine*.so"),
    ]
    candidates: list[str] = []
    for pattern in patterns:
        candidates.extend(glob.glob(pattern, recursive=True))

    if not candidates:
        raise ImportError(
            "Could not find the compiled lob_engine module under build*/bindings/. "
            "Build it first:\n"
            "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLOB_BUILD_PYTHON_BINDINGS=ON\n"
            "  cmake --build build --target lob_engine --config Release"
        )

    # If multiple build directories have one (e.g. build/ and build-linux/),
    # prefer whichever was built most recently.
    candidates.sort(key=os.path.getmtime, reverse=True)
    return candidates[0]


def _load() -> ModuleType:
    path = _find_module_file()
    spec = importlib.util.spec_from_file_location("lob_engine", path)
    if spec is None or spec.loader is None:
        raise ImportError(f"Could not load lob_engine module from {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules["lob_engine"] = module
    spec.loader.exec_module(module)
    return module


lob_engine = _load()
