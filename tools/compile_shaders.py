"""Embed production HLSL using the existing shader entries, macros and compiler flags.

Windows, 64-bit Python, and the system D3DCompiler 47 are required only to build.
There is deliberately no shader cache or runtime compilation fallback.
"""
from pathlib import Path
import argparse
import ast
import ctypes as C
import hashlib
import json
import os
import re
import sys

STRICT = 1 << 11
LEVEL3 = 1 << 15


def extract(source, name):
    match = re.search(r"\b" + re.escape(name) + r'\s*=\s*R"(\w*)\((.*?)\)\1"', source, re.S)
    if not match:
        raise RuntimeError(f"Missing shader source: {name}")
    return match.group(2).encode("utf-8")


def extract_string(source, name):
    # C++ phase-two line splicing precedes decoding adjacent string literals.
    source = source.replace("\\\n", "")
    match = re.search(r"\b" + name + r'\s*=\s*((?:"(?:\\.|[^"\\])*"\s*)+);', source)
    if not match:
        raise RuntimeError(f"Missing backend shader source: {name}")
    return "".join(ast.literal_eval(value) for value in re.findall(r'"(?:\\.|[^"\\])*"', match.group(1))).encode("utf-8")


def specifications(root):
    main = (root / "Source/src/Main.cpp").read_text(encoding="utf-8")
    overlay = (root / "Source/src/DiagnosticsOverlay.cpp").read_text(encoding="utf-8")
    backend = (root / "Source/lib/imgui/backends/imgui_impl_dx11.cpp").read_text(encoding="utf-8")
    shaders = []

    def add(name, symbol, label, entry="main", macros=(), flags=STRICT, source=main,
            target="cs_5_0", literal=False):
        shaders.append(dict(name=name, source=(extract_string if literal else extract)(source, symbol),
                            source_name=label, entry=entry, macros=macros, flags=flags, target=target))

    add("Capture", "kCaptureShader", "StandaloneCSUpdateCubemapCS", macros=(("SEPARATE_SKY_HISTORY", "1"),))
    add("InferLive", "kInferShader", "StandaloneCSInferCubemapLiveLinearCS", macros=(("SEPARATE_SKY_HISTORY", "1"),))
    add("CaptureDirect", "kCaptureShader", "StandaloneCSUpdateCubemapDirectCS")
    add("InferDirect", "kInferShader", "StandaloneCSInferCubemapDirectCS")
    for entry in ("DepthPass", "OwnerPass", "ResolvePass"):
        add("Reproject" + entry, "kReprojectShader", "Reprojection", entry=entry,
            flags=STRICT | LEVEL3)
    add("SkyColor", "kSkyColorShader", "DCF4SkyColorCS")
    add("UniversalReplicate", "kUniversalReplicateShader", "DCF4UniversalReplicateCS")
    add("TintProbe", "kTintProbeShader", "DCF4TintProbe")
    add("Preview", "previewHLSL", "DCF4 previews", source=overlay)
    add("ImGuiVertex", "vertexShader", None, flags=0, source=backend, target="vs_4_0", literal=True)
    add("ImGuiPixel", "pixelShader", None, flags=0, source=backend, target="ps_4_0", literal=True)
    return shaders


class Macro(C.Structure):
    _fields_ = [("Name", C.c_char_p), ("Definition", C.c_char_p)]


def blob_bytes(blob):
    if not blob.value:
        return b""
    vtable = C.cast(blob, C.POINTER(C.POINTER(C.c_void_p))).contents
    pointer = C.WINFUNCTYPE(C.c_void_p, C.c_void_p)(vtable[3])(blob)
    size = C.WINFUNCTYPE(C.c_size_t, C.c_void_p)(vtable[4])(blob)
    return C.string_at(pointer, size)


def release(blob):
    if blob.value:
        vtable = C.cast(blob, C.POINTER(C.POINTER(C.c_void_p))).contents
        C.WINFUNCTYPE(C.c_ulong, C.c_void_p)(vtable[2])(blob)


def compiler():
    if sys.platform != "win32" or C.sizeof(C.c_void_p) != 8:
        raise RuntimeError("Shader build requires 64-bit Windows Python")
    path = Path(os.environ["SystemRoot"]) / "System32/d3dcompiler_47.dll"
    dll = C.WinDLL(str(path))
    compile_shader = dll.D3DCompile
    compile_shader.argtypes = [C.c_void_p, C.c_size_t, C.c_char_p, C.POINTER(Macro),
                               C.c_void_p, C.c_char_p, C.c_char_p, C.c_uint, C.c_uint,
                               C.POINTER(C.c_void_p), C.POINTER(C.c_void_p)]
    compile_shader.restype = C.c_int32
    return compile_shader, path


def compile_one(compile_shader, shader):
    macros = (Macro * (len(shader["macros"]) + 1))()
    for i, (name, value) in enumerate(shader["macros"]):
        macros[i] = Macro(name.encode("ascii"), value.encode("ascii"))
    code, errors = C.c_void_p(), C.c_void_p()
    source = C.create_string_buffer(shader["source"])
    try:
        hr = compile_shader(source, len(shader["source"]),
                            shader["source_name"].encode("ascii") if shader["source_name"] else None,
                            macros if shader["macros"] else None, None,
                            shader["entry"].encode("ascii"), shader["target"].encode("ascii"), shader["flags"], 0,
                            C.byref(code), C.byref(errors))
        message = blob_bytes(errors).rstrip(b"\0").decode("utf-8", errors="replace")
        if hr < 0 or not code.value:
            raise RuntimeError(f"{shader['name']}: D3DCompile failed 0x{hr & 0xffffffff:08x}\n{message}")
        if message:
            print(message, file=sys.stderr)
        bytecode = blob_bytes(code)
        if not bytecode.startswith(b"DXBC"):
            raise RuntimeError(f"{shader['name']}: missing DXBC bytecode")
        return bytecode
    finally:
        release(errors)
        release(code)


def write_if_changed(path, text):
    data = text.encode("utf-8")
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(path)


def generate(root):
    compile_shader, compiler_path = compiler()
    shaders = specifications(root)
    # Compile everything before replacing outputs; a failure stops the DLL build.
    compiled = [(shader, compile_one(compile_shader, shader)) for shader in shaders]
    header = ["// SPDX-License-Identifier: GPL-3.0-only",
              "// Generated by tools/compile_shaders.py. Edit the original HLSL, not this file.",
              "#pragma once", "#include <cstddef>", "namespace DCF4Shaders {",
              "struct Bytecode { const unsigned char* data; std::size_t size; };",
              "struct NamedBytecode { const char* name; Bytecode code; };"]
    manifest = dict(flags2=0, compiler=str(compiler_path),
                    compiler_sha256=hashlib.sha256(compiler_path.read_bytes()).hexdigest(), shaders=[])
    for shader, bytecode in compiled:
        name = shader["name"]
        header.append(f"inline constexpr unsigned char {name}Bytes[] = {{")
        for start in range(0, len(bytecode), 24):
            header.append("    " + ",".join(f"0x{v:02x}" for v in bytecode[start:start+24]) + ",")
        header.extend(["};", f"inline constexpr Bytecode {name}{{{name}Bytes, sizeof({name}Bytes)}};"])
        if name.startswith("ImGui"):
            header.append(f"inline constexpr const char {name}Source[]=" + json.dumps(shader["source"].decode("utf-8")) + ";")
        manifest["shaders"].append({k: v for k, v in shader.items() if k != "source"} |
            dict(source_sha256=hashlib.sha256(shader["source"]).hexdigest(), bytes=len(bytecode),
                 bytecode_sha256=hashlib.sha256(bytecode).hexdigest()))
    header.append("inline constexpr Bytecode Reproject[] = {ReprojectDepthPass,ReprojectOwnerPass,ReprojectResolvePass};")
    header.append("inline constexpr NamedBytecode All[] = {")
    header.extend(f'    {{"{shader["name"]}", {shader["name"]}}},' for shader, _ in compiled)
    header.extend(["};", "}", ""])
    write_if_changed(root / "Source/generated/EmbeddedShaders.h", "\n".join(header))
    write_if_changed(root / "Source/generated/ShaderManifest.json", json.dumps(manifest, indent=2) + "\n")
    print(f"Embedded {len(compiled)} shaders ({sum(len(code) for _, code in compiled)} DXBC bytes); production entries/compiler flags retained")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        generate(args.root.resolve())
    except Exception as error:
        raise SystemExit(f"Shader build failed: {error}") from error
