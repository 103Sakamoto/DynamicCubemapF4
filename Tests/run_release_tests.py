"""Run existing production regression tests sequentially; no game is started."""
from pathlib import Path
import argparse,json,os,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runtime-binaries',action='store_true',help='Also audit user-provided OG/NG and Addictol binaries; see Tests/README.md.')
args=parser.parse_args()
assert shutil.which('cl'),'Run from a Visual Studio x64 Developer Command Prompt'
clang=os.environ.get('DCF4_CLANG') or shutil.which('clang++')
if not clang:
    candidate=Path(os.environ.get('VCINSTALLDIR',''))/'Tools/Llvm/x64/bin/clang++.exe'
    if candidate.is_file():clang=str(candidate)
assert clang,'Install Visual Studio LLVM tools or set DCF4_CLANG to clang++.exe'
assert os.environ.get('DCF4_DETOURS'),'Set DCF4_DETOURS to the Detours installation root'
temp=ROOT/'Tests/temp';temp.mkdir(exist_ok=True)
env=dict(os.environ,TEMP=str(temp),TMP=str(temp))
steps=[]
def execute(name,command,compiler='cl'):
    start=time.monotonic();local=dict(env,CXX=compiler)
    result=subprocess.run(command,cwd=ROOT,env=local,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                          text=True,errors='replace')
    (temp/(name+'.log')).write_text(result.stdout,encoding='utf-8')
    steps.append({'name':name,'exit_code':result.returncode,'seconds':time.monotonic()-start})
    print(f"{'PASS' if result.returncode==0 else 'FAIL'} {name} ({steps[-1]['seconds']:.1f}s)",flush=True)
    if result.returncode:
        print(result.stdout,flush=True)
        (temp/'release-test-results.json').write_text(json.dumps(steps,indent=2))
        raise SystemExit(result.returncode)
def script(name,compiler='cl'):
    execute(name,[sys.executable,str(ROOT/'Tests'/name)],compiler)
script('release_scope.py')
for name in ['run_interop_test.py','run_performance_test.py','run_enable_test.py',
             'run_bind_cpu_tests.py','run_array_acceptance_test.py','run_stream_cache_test.py',
             'run_stream_audit_test.py','run_tint_cache_test.py']:
    script(name,clang)
for name in ['run_precompiled_tests.py','run_tint_batch_tests.py','run_optional_sky_tests.py',
             'run_sky_color_tests.py','run_dispatch_trace_tests.py']:
    script(name)
for name,libraries in [('array_guard_test',[]),('array_write_test',['d3d11.lib',str(Path(env['DCF4_DETOURS'])/'lib/detours.lib')])]:
    exe=temp/(name+'.exe')
    execute(name+'-build',['cl','/nologo','/std:c++20','/EHsc','/MD','/O2','/DNOMINMAX',
        '/I'+str(Path(env['DCF4_DETOURS'])/'include'),str(ROOT/'Tests'/(name+'.cpp')),
        '/Fo:'+str(temp/(name+'.obj')),'/Fe:'+str(exe),*libraries])
    execute(name,[str(exe)])
if args.runtime_binaries:
    for key in ['DCF4_OG_EXE','DCF4_OG_DATABASE','DCF4_NG984_EXE','DCF4_NG984_DATABASE','DCF4_ADDICTOL_DLL']:
        assert env.get(key) and Path(env[key]).is_file(),f'Missing {key}'
    script('run_runtime_compat_tests.py')
(temp/'release-test-results.json').write_text(json.dumps(steps,indent=2))
print(f'PASS: {len(steps)} release regression steps; mock/WARP/binary checks are not in-game tests.')
