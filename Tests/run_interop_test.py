#!/usr/bin/env python3
"""Compile actual binding routines against portable mocks; not a Windows build."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "Source/src/Main.cpp").read_text()

def function(signature):
    start = source.index(signature)
    begin = source.index("{", start)
    level = 1
    end = begin + 1
    while level:
        level += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"

declarations = source[source.index("struct UniversalStyleConstants"):source.index(
    "Microsoft::WRL::ComPtr<ID3D11ComputeShader> g_universalReplicateCS;")]
declarations += "\nbool g_universalComputeFailed=false; UINT g_universalTotalCubes=0;\n"
declarations += "bool g_namedReferenceReady=false;\n"
routines = "\n".join(function(signature) for signature in [
    "void ResetNonCubeCache(", "void CacheNonCubeView(", "void RebuildMaterialViewLookup(", "ID3D11ShaderResourceView* CurrentMaterialView(", "bool UniversalIsOurs(", "void TagReflectionReplacement(",
    "BindingResult UniversalBind(", "void UniversalRepair(",
    "void SetLiveCubemapSlot(UINT slot, bool bound)\n", "void STDMETHODCALLTYPE Hook_PSResources(",
])
# Count extracted production lookups to catch a duplicate ownership lookup in the hook.
routines = routines.replace("const auto found=g_materialViewLookup.find(view);",
                            "++lookupCalls; const auto found=g_materialViewLookup.find(view);")
with tempfile.TemporaryDirectory(prefix="dcf4-interop-") as directory:
    temp = Path(directory)
    (temp / "production_interop.inc").write_text(declarations + routines)
    executable = temp / ("interop_test.exe" if os.name == "nt" else "interop_test")
    command = [os.environ.get("CXX", "g++"), "-std=c++20", "-Wall", "-Wextra",
               "-pedantic", "-I" + str(root / "Tests/interop_stubs"),
               "-I" + str(root / "Source/src"), "-I" + str(temp),
               str(root / "Tests/interop_test.cpp"), "-o", str(executable)]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
