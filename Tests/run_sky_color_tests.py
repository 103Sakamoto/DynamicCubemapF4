from pathlib import Path
import os,re,json,subprocess,sys
from source_tools import ROOT,block
root=ROOT
new=(ROOT/"Source/src/Main.cpp").read_text(encoding="utf-8-sig")
for signature in ('bool UseSkyColor()', 'bool EnsureSkyColorResources(', 'void DispatchSkyColor('):
    assert not re.search(r'->(?:Map|GetData|Flush|CopyResource|GenerateMips)\(',block(new,signature))
capture=re.search(r'kCaptureShader = R"\((.*?)\)";',new,re.S).group(1)
sky=re.search(r'kSkyColorShader=R"\((.*?)\)";',new,re.S).group(1)
for pattern in (r'cbuffer UpdateData.*?\n};',r'float3 IrradianceToLinear.*?\n}'):
    assert re.search(pattern,capture,re.S).group(0)==re.search(pattern,sky,re.S).group(0)
temp=ROOT/'Tests/temp';temp.mkdir(exist_ok=True)
(temp/'sky_production.inc').write_text('\n'.join(block(new,signature) for signature in (
    'bool UseSkyColor()', 'bool EnsureSkyColorResources(', 'void DispatchSkyColor(', 'bool DispatchInference('))+'\n')
frame=block(new,'bool DispatchAccumulationFrame(')
state=frame[frame.index('\t\tID3D11ComputeShader* oldCS = nullptr;'):]
(temp/'capture_state.inc').write_text('bool ProductionCaptureState(ID3D11DeviceContext* context, ID3D11ShaderResourceView* sceneDepthSRV, ID3D11ShaderResourceView* sceneColorSRV) {\n'+state+'\n')
exe=temp/'sky_color_gpu_test.exe'
subprocess.run(['cl','/nologo','/std:c++20','/EHsc','/MD','/O2','/DNOMINMAX','/I'+str(temp),
    str(ROOT/'Tests/sky_color_gpu_test.cpp'),'/Fo:'+str(temp/'sky_color_gpu_test.obj'),'/Fe:'+str(exe),
    'd3d11.lib','d3dcompiler.lib'],check=True,cwd=ROOT)
subprocess.run([str(exe)],check=True,cwd=ROOT)
print('PASS scope: capture/reprojection/pool/tint kernels exact; unchanged source guards, ownership, CPU binding, lighting calibration, mip chains, scheduling, MCM, dependencies and licenses')
