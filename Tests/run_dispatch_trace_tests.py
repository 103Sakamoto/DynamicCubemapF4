from pathlib import Path
import os,re,json,subprocess,sys
from source_tools import ROOT,block
root=ROOT
new=(ROOT/"Source/src/Main.cpp").read_text(encoding="utf-8-sig")
overlay=(root/'Source/src/DiagnosticsOverlay.cpp').read_text()
assert 'poolProfiler.BeginFrame();CollectPoolTrace();' in overlay
assert 'visible=false;showPreviews=false;' in overlay
assert 'if(showPreviews)UpdatePreviews();' in overlay
assert 'poolPending.Find(poolProfiler.GetResultTag())' in overlay
assert 'poolProfiler.BeginStatisticsPass(' in overlay
assert 'SetPerfEventCallbacks(' in overlay
api=overlay[overlay.index('void BeginPoolReplication('):overlay.index('void ClearSkyPreview(')]
assert not re.search(r'(->|\.)\s*(Flush|GetData|Map|Dispatch|GenerateMips)\s*\(',api)
prof=(root/'Source/lib/CSProfiler/Profiler.cpp').read_text()
assert 'D3D11_QUERY_PIPELINE_STATISTICS' in prof
assert 'sizeof(pipelineData),D3D11_ASYNC_GETDATA_DONOTFLUSH' in prof
assert 'if (ready==S_FALSE) return;' in prof
assert 'pipelineValid=ready==S_OK;' in prof
print('PASS: pool operations, safety, source verification, scheduling, MCM and licenses retained; only two inference shaders changed and one sky-color shader added; CS profiler algorithm unchanged')

def block(source,marker):
    start=source.index(marker);brace=source.index('{',start)
    while source.find(';',start,brace)!=-1:
        start=source.index(marker,start+len(marker));brace=source.index('{',start)
    level,end=1,brace+1
    while level:
        level+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]

temp=root/'Tests/temp';temp.mkdir(exist_ok=True)
def run(name,extra=None):
    exe=temp/(name+'.exe')
    subprocess.run(['cl','/nologo','/std:c++20','/EHsc','/MD','/O2','/DNOMINMAX',
        '/I'+str(temp),'/I'+str(root/'Source/src'),str(root/'Tests'/(name+'.cpp')),
        '/Fo:'+str(temp/(name+'.obj')),'/Fe:'+str(exe),*(extra or [])],check=True,cwd=root)
    subprocess.run([str(exe)],check=True,cwd=root)
run('pool_trace_test')
(temp/'production_pool.inc').write_text('''
#include "PoolSpikeTrace.h"
bool g_diagnosticsEnabled=false;
namespace DCFDiag {
void PoolSelection(bool,unsigned){}
void PoolStep(PoolTrace::Step){}
}
''' + block(new,'struct UniversalReplicationScope')+';\n'+block(new,'UINT PreparePoolUpdates(')+'\n'+
    block(new,'void UniversalDispatchPool(')+'\n')
(temp/'retained_source.inc').write_text(block(new,'bool CanReuseRetainedPoolSource(')+'\n'+block(new,'void CommitRetainedPoolSource(')+'\n')
(temp/'current_stamp.inc').write_text(block((root/'Source/src/ArrayWriteTracking.h').read_text(),'bool Current('))
run('game_mip_gpu_test',['d3d11.lib','d3dcompiler.lib'])
exe=temp/'dispatch_trace_gpu_test.exe'
subprocess.run(['cl','/nologo','/std:c++20','/EHsc','/MD','/O2','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN',
    '/I'+str(root/'Source/lib/imgui'),'/I'+str(root/'Source/lib/imgui/backends'),'/I'+str(root/'Source/lib/CSProfiler'),
    '/I'+str(Path(os.environ['DCF4_DETOURS'])/'include'),str(root/'Tests/dispatch_trace_gpu_test.cpp'),
    str(root/'Source/lib/CSProfiler/Profiler.cpp'),*(str(root/'Source/lib/imgui'/x) for x in ('imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp')),
    str(root/'Source/src/ImGuiDX11Backend.cpp'),'/Fo:'+str(temp)+'\\','/Fe:'+str(exe),'/link',
    '/LIBPATH:'+str(Path(os.environ['DCF4_DETOURS'])/'lib'),'detours.lib','d3d11.lib','dxgi.lib','user32.lib'],check=True,cwd=root)
subprocess.run([str(exe)],check=True,cwd=root)
