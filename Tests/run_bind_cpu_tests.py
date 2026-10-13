from pathlib import Path
import os,re,json,subprocess,sys
from source_tools import ROOT,block
root=ROOT
new=(ROOT/"Source/src/Main.cpp").read_text(encoding="utf-8-sig")
import runpy
runpy.run_path(str(ROOT / 'Tests/run_interop_test.py'), run_name='__main__')
temp = ROOT / 'Tests/temp'
temp.mkdir(exist_ok=True)
diagnostics = (ROOT / 'Source/src/PerformanceDiagnostics.h').read_text()
test = '''#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
using LONGLONG=long long;
bool g_diagnosticsEnabled=false;
std::uint64_t g_perfHookCalls=0;
LONGLONG ticks=100;
unsigned clockCalls=0;
LONGLONG PerfClock(){++clockCalls;return ticks;}
double PerfMilliseconds(LONGLONG value){return double(value)/10;}
''' + block(diagnostics, 'struct PerfStats') + ';\nPerfStats g_perfHook,g_perfHookProcessing,g_perfHookDriver;\n' + block(diagnostics, 'struct PerfHookScope') + ''';
int main(){
 for(unsigned i=0;i<10000;++i){PerfHookScope s;s.BeginDriver();}
 assert(clockCalls==0 && g_perfHookCalls==0 && g_perfHook.n==0);
 g_diagnosticsEnabled=true;
 for(unsigned i=0;i<4095;++i){PerfHookScope s;s.BeginDriver();}
 assert(clockCalls==0 && g_perfHook.n==0);
 {PerfHookScope s;ticks=145;s.BeginDriver();ticks=200;}
 assert(clockCalls==3 && g_perfHook.n==1 && g_perfHookProcessing.n==1 && g_perfHookDriver.n==1);
 assert(g_perfHook.sum==10 && g_perfHookProcessing.sum==4.5 && g_perfHookDriver.sum==5.5);
 assert(g_perfHook.sum==g_perfHookProcessing.sum+g_perfHookDriver.sum);
 g_diagnosticsEnabled=false;unsigned before=clockCalls;auto calls=g_perfHookCalls;
 for(unsigned i=0;i<10000;++i){PerfHookScope s;s.BeginDriver();}
 assert(clockCalls==before && g_perfHookCalls==calls && g_perfHook.n==1);
 std::cout<<"PASS: fixed 1/4096 sampling, exact processing/forwarded-call split and zero timing/counter activity while disabled\\n";
}
'''
(temp / 'bind_cpu_timer_test.cpp').write_text(test)
exe = temp / 'bind_cpu_timer_test.exe'
subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', str(temp / 'bind_cpu_timer_test.cpp'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
