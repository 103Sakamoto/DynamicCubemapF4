from pathlib import Path
import os,re,json,subprocess,sys
from source_tools import ROOT,block
root=ROOT
new=(ROOT/"Source/src/Main.cpp").read_text(encoding="utf-8-sig")
assert 'g_retainedPoolSources.clear();' in block(new, 'void ResetTintAnalysis(')
assert 'if (reuse && pool.style.tint[cube].w<=-1.5f) continue;' in block(new, 'UINT PreparePoolUpdates(')
print('PASS: reset clears retained publication epochs; only retain-original entries eligible for reuse')

assert all(x not in new for x in ('iSharpnessMode', 'g_requestedRoughness', 'g_materialMip', 'ApplyMaterialMip', 'g_liveMaterialViews'))
config = json.loads((root / 'Data/MCM/Config/DynamicCubemapF4/config.json').read_text())
assert all(x.get('id') != 'iSharpnessMode:General' for x in config['content'])
settings = (root / 'Data/MCM/Config/DynamicCubemapF4/settings.ini').read_text()
assert 'bDiagnostics=0' in settings and 'bReprojection=0' in settings and 'iSharpnessMode' not in settings
assert 'v.TextureCube.MipLevels=kCubeMipCount;' in new
assert 'v.TextureCubeArray.MipLevels = kCubeMipCount;' in new
publish = block(new, 'void UniversalPublish()')
assert publish.index('UniversalDispatchPool(pool);') < publish.index('pool.ready =')
assert 'for (auto& pool : g_universalPools) pool.ready=false;' in publish
assert 'D3D11_ASYNC_GETDATA_DONOTFLUSH' in new

temp = root / 'Tests/temp'
temp.mkdir(exist_ok=True)
(temp / 'production_pool.inc').write_text(block(new, 'struct UniversalReplicationScope') + ';\n' +
    block(new, 'UINT PreparePoolUpdates(') + '\n' +
    block(new, 'void UniversalDispatchPool(') + '\n')
(temp / 'retained_source.inc').write_text(block(new,'bool CanReuseRetainedPoolSource(') + '\n' +
    block(new,'void CommitRetainedPoolSource(') + '\n')
(temp / 'current_stamp.inc').write_text(block((root / 'Source/src/ArrayWriteTracking.h').read_text(),'bool Current('))
exe = temp / 'game_mip_gpu_test.exe'
subprocess.run(['cl', '/nologo', '/std:c++20', '/EHsc', '/MD', '/O2', '/DNOMINMAX',
    '/I' + str(temp), str(root / 'Tests/game_mip_gpu_test.cpp'),
    '/Fo:' + str(temp / 'game_mip_gpu_test.obj'), '/Fe:' + str(exe),
    'd3d11.lib', 'd3dcompiler.lib'], check=True, cwd=root)
subprocess.run([str(exe)], check=True, cwd=root)
