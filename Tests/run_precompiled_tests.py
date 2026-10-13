"""Validate embedded bytecode, retained Spike02 shaders and safe shader creation."""
from pathlib import Path
import json
import ast
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
temporary = root / "Tests/temp"
temporary.mkdir(exist_ok=True)
reference = json.loads((root / "Tests/shader_compile_baseline.json").read_text())
main = (root / "Source/src/Main.cpp").read_text()
overlay = (root / "Source/src/DiagnosticsOverlay.cpp").read_text()
backend = (root / "Source/lib/imgui/backends/imgui_impl_dx11.cpp").read_text().replace("\\\n", "")

for source in (main, overlay):
    assert "D3DCompile" not in source and "ID3DBlob" not in source

manifest = json.loads((root / "Source/generated/ShaderManifest.json").read_text())
assert len(manifest["shaders"]) == len(reference["shaders"]) == 13
assert manifest["flags2"] == 0
for actual, expected in zip(manifest["shaders"], reference["shaders"]):
    for key in ("name", "source_name", "entry", "macros", "flags", "target"):
        assert actual[key] == expected[key], (expected["name"], key)

header = ["// Frozen accepted release inputs for test-only runtime compilation.",
          "namespace Reference {", "struct Shader { const char* name; const char* source;",
          "const char* sourceName; const char* entry; const char* target; const D3D_SHADER_MACRO* macros; UINT flags; };"]
for name, source in reference["sources"].items():
    assert ')DCF4REF"' not in source
    header.append(f'inline constexpr const char* {name}=R"DCF4REF({source})DCF4REF";')
for i, shader in enumerate(reference["shaders"]):
    if shader["macros"]:
        definitions = ",".join("{" + json.dumps(n) + "," + json.dumps(v) + "}" for n, v in shader["macros"])
        header.append(f"inline const D3D_SHADER_MACRO Macros{i}[]={{" + definitions + ",{nullptr,nullptr}};")
header.append("inline const Shader Shaders[]={")
for i, shader in enumerate(reference["shaders"]):
    values = [json.dumps(shader["name"]), shader["source"], json.dumps(shader["source_name"]) if shader["source_name"] else "nullptr",
              json.dumps(shader["entry"]),json.dumps(shader["target"]), f"Macros{i}" if shader["macros"] else "nullptr", str(shader["flags"])]
    header.append("{" + ",".join(values) + "},")
header.extend(["};", "}"])
(temporary / "ShaderReference.h").write_text("\n".join(header))


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    level, end = 1, brace + 1
    while level:
        level += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


# Exercise the actual transaction, including every partial shader-creation failure.
mock = r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iterator>
#include <string>
#include "../../Source/generated/EmbeddedShaders.h"
using HRESULT=int; bool FAILED(HRESULT hr){return hr<0;}
int alive=0;
struct ID3D11ComputeShader { ID3D11ComputeShader(){++alive;} void Release(){--alive;delete this;} };
struct ID3D11Device {
 int fail=-1,calls=0;
 HRESULT CreateComputeShader(const void* data,std::size_t size,void*,ID3D11ComputeShader** out){
  assert(size>4 && memcmp(data,"DXBC",4)==0);
  if(calls++==fail)return -1;
  *out=new ID3D11ComputeShader;return 0;
 }
};
ID3D11ComputeShader* g_captureCS=nullptr,*g_inferLiveCS=nullptr;
bool g_reprojectEnabled=false,g_captureShaderSeparateSky=false;
int g_activeCellKind=0;
void Log(const std::string&){}
'''
mock += "template<class T> " + function(main, "void ReleaseOwned(")
mock += function(main, "void ClearCaptureShaders()")
mock += function(main, "bool UseSeparateSkyHistory()")
mock += function(main, "bool CreateCaptureShaders(")
mock += r'''
int main(){
 for(int cell:{0,1,2})for(bool mode:{false,true})for(int failure=0;failure<2;++failure){
  g_activeCellKind=cell;g_reprojectEnabled=mode;
  const bool separate=UseSeparateSkyHistory();assert(separate==(mode && cell==1));
  ID3D11Device d;d.fail=failure;
  assert(!CreateCaptureShaders(&d));assert(d.calls==failure+1 && alive==0);
  assert(!g_captureCS && !g_inferLiveCS);
  d.fail=-1;d.calls=0;assert(CreateCaptureShaders(&d));assert(d.calls==2 && alive==2);
  assert(g_captureShaderSeparateSky==separate);
  assert(CreateCaptureShaders(&d));assert(d.calls==2);
  if(cell!=1){
   g_reprojectEnabled=!mode;
   assert(CreateCaptureShaders(&d) && d.calls==2 && !g_captureShaderSeparateSky);
  }
  // Changing the required sky path must recreate both shaders, with rollback.
  g_activeCellKind=1;g_reprojectEnabled=!separate;
  d.fail=d.calls+failure;
  assert(!CreateCaptureShaders(&d));assert(alive==0 && !g_captureCS && !g_inferLiveCS);
  d.fail=-1;d.calls=0;
  assert(CreateCaptureShaders(&d));assert(alive==2 && d.calls==2);
  assert(g_captureShaderSeparateSky==!separate);
  ClearCaptureShaders();assert(alive==0);
 }
}
'''
(temporary / "shader_failure_test.cpp").write_text(mock)
compiler = os.environ.get("CXX", "cl")
assert Path(compiler).name.lower() in ("cl", "cl.exe"), "Run from a Visual Studio x64 Developer Command Prompt"
common = [compiler, "/nologo", "/std:c++20", "/EHsc", "/MD", "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN"]
for name, source, libraries in [
    ("precompiled_gpu_test", root / "Tests/precompiled_gpu_test.cpp", ["d3d11.lib", "d3dcompiler.lib"]),
    ("shader_failure_test", temporary / "shader_failure_test.cpp", []),
]:
    output = temporary / (name + ".exe")
    subprocess.run(common + [str(source), "/Fo:" + str(temporary / (name + ".obj")),
                            "/Fe:" + str(output)] + libraries, check=True, cwd=root)
    subprocess.run([str(output)], check=True, cwd=root)
print("PASS: 13 production shaders validated; both creation failures clean up and retry in either mode, including exterior/interior/unknown-cell and mode changes")

# A compiler error must stop BUILD.cmd before xmake can use a stale header/DLL.
fixture=temporary/'compile_failure'
(fixture/'Source/src').mkdir(parents=True,exist_ok=True)
(fixture/'Source/generated').mkdir(exist_ok=True)
(fixture/'Source/lib/imgui/backends').mkdir(parents=True,exist_ok=True)
(fixture/'tools').mkdir(exist_ok=True)
broken=main.replace('kCaptureShader = R"(', 'kCaptureShader = R"(\n#error DCF4_TEST_EXPECTED_COMPILER_FAILURE\n',1)
if broken==main:
    match=re.search(r'\bkCaptureShader\s*=\s*R"\w*\(',main)
    assert match
    broken=main[:match.end()]+'\n#error DCF4_TEST_EXPECTED_COMPILER_FAILURE\n'+main[match.end():]
(fixture/'Source/src/Main.cpp').write_text(broken)
(fixture/'Source/src/DiagnosticsOverlay.cpp').write_text(overlay)
(fixture/'Source/lib/imgui/backends/imgui_impl_dx11.cpp').write_bytes((root/'Source/lib/imgui/backends/imgui_impl_dx11.cpp').read_bytes())
(fixture/'Source/BUILD.cmd').write_bytes((root/'Source/BUILD.cmd').read_bytes())
(fixture/'Source/xmake.lua').write_text('-- Compiler failure must stop before configuring this fixture.\n')
(fixture/'tools/compile_shaders.py').write_bytes((root/'tools/compile_shaders.py').read_bytes())
sentinel=fixture/'Source/generated/EmbeddedShaders.h'
sentinel.write_text('STALE HEADER MUST NOT BE USED')
environment=dict(os.environ,DCF4_PYTHON=sys.executable)
failure=subprocess.run(['cmd','/c',str(fixture/'Source/BUILD.cmd')],cwd=fixture,
                       env=environment,capture_output=True,text=True,errors='replace')
assert failure.returncode!=0 and 'DCF4_TEST_EXPECTED_COMPILER_FAILURE' in failure.stdout+failure.stderr,failure.stdout+failure.stderr
assert sentinel.read_text()=='STALE HEADER MUST NOT BE USED'
assert not (fixture/'Source/build').exists()
print('PASS: a shader compiler error stops BUILD.cmd before xmake; stale generated output is never built')
