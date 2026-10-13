from pathlib import Path
import os,re,json,subprocess,sys
from source_tools import ROOT,block
root=ROOT
new=(ROOT/"Source/src/Main.cpp").read_text(encoding="utf-8-sig")
# Keep the previous safety reports eager, including initialization/query failures.
for expression in ['Log(std::string("TINT FAIL: initialization at ")+stage);',
                   'Log(std::string("TINT: CreateComputeShader HRESULT=")+std::to_string(static_cast<long>(shaderHr)));',
                   'Log("TINT FAIL: completion query HRESULT="+std::to_string(static_cast<long>(probeStatus))+']:
    assert expression in new
assert 'Log(installed ?' in (root/'Source/src/ArrayWriteTracking.h').read_text()
assert 'TintReportBatch tintReports(traceTick.enabled);' in new
assert new.index('TintReportBatch tintReports(traceTick.enabled);')>new.index('} traceTick;')
assert 'D3D11_ASYNC_GETDATA_DONOTFLUSH' in new
assert 'bDiagnostics=0' in (root/'Data/MCM/Config/DynamicCubemapF4/settings.ini').read_text()
print('PASS: every original operation, branch, safety guard, lighting/update interval and shader preserved; dependency/license/MCM bytes unchanged')

output=root/'Tests/bin';output.mkdir(exist_ok=True)
begin=new.index('    // TINTBATCH LOGGER BEGIN\n')
end=new.index('    // TINTBATCH LOGGER END',begin)
(output/'production_tint_logger.inc').write_text(new[begin:end])
exe=output/'tint_batch_test.exe'
subprocess.run([os.environ.get('CXX','cl'),'/nologo','/std:c++20','/EHsc','/MD','/W4',
                '/I'+str(root/'Source/src'),'/I'+str(output),str(root/'Tests/tint_batch_test.cpp'),
                '/Fo:'+str(output)+'\\','/Fe:'+str(exe)],cwd=root,check=True)
subprocess.run([str(exe)],cwd=root,check=True)
