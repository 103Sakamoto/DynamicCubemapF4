from pathlib import Path
import os,re,json,subprocess,sys
from source_tools import ROOT,block
root=ROOT
new=(ROOT/"Source/src/Main.cpp").read_text(encoding="utf-8-sig")
temp=ROOT/'Tests/temp';temp.mkdir(exist_ok=True)

def reader(path):
    data=path.read_bytes();pe=struct.unpack_from('<I',data,60)[0];start=pe+24+struct.unpack_from('<H',data,pe+20)[0];count=struct.unpack_from('<H',data,pe+6)[0]
    sections=[struct.unpack_from('<IIII',data,start+i*40+8) for i in range(count)]
    def at(rva,size):
        for vs,va,rs,rp in sections:
            if va<=rva<va+rs:return data[rp+rva-va:rp+rva-va+size]
        raise ValueError(hex(rva))
    # Whole-body pins must not include loader-relocated absolute addresses.
    rva,size=struct.unpack_from('<II',data,pe+24+112+5*8)
    relocations=set()
    if rva and size:
        table=at(rva,size);cursor=0
        while cursor+8<=len(table):
            page,length=struct.unpack_from('<II',table,cursor)
            if not length:break
            assert length>=8 and cursor+length<=len(table)
            for (entry,) in struct.iter_unpack('<H',table[cursor+8:cursor+length]):
                if entry>>12:relocations.add(page+(entry&0xfff))
            cursor+=length
    return data,at,relocations

fixtures=['struct Chunk { std::uintptr_t rva; const unsigned char* bytes; unsigned length; };']
for name in ['OG','NG984']:
    profile=json.loads((ROOT/'Tests/runtime-profiles'/(name+'.json')).read_text())
    data,at,relocations=reader(Path(os.environ[profile['executable_env']]));assert hashlib.sha256(data).hexdigest()==profile['exe_sha256']
    raw=Path(os.environ[profile['database_env']]).read_bytes();assert hashlib.sha256(raw).hexdigest()==profile['database_sha256']
    ids=dict(struct.iter_unpack('<QQ',raw[8:]));assert ids[187043]==int(profile['vtable_rva'],16)
    chunks=[]
    for route in profile['routes']:
        rva=int(route['rva'],16);code=at(rva,route['length']);assert hashlib.sha256(code).hexdigest()==route['sha256']
        assert not any(rva<=item<rva+len(code) for item in relocations),'ASLR changes pinned route bytes'
        chunks.append((str(route['slot']),rva,code))
    for label,key,size in [('deferred','deferred_rva',32),('anchor','anchor_rva',32),('call','call_rva',5),('target','draw_target_rva',32)]:
        rva=int(profile[key],16);chunks.append((label,rva,at(rva,size)))
        assert not any(rva<=item<rva+size for item in relocations),'ASLR changes pinned renderer bytes'
    for label,rva,code in chunks:
        fixtures.append(f'const unsigned char {name}_{label}[]={{'+','.join(hex(v) for v in code)+'};')
    fixtures.append(f'const Chunk {name}_chunks[]={{'+','.join(f'{{{hex(rva)},{name}_{label},sizeof({name}_{label})}}' for label,rva,code in chunks)+'};')
    fixtures.append(f'constexpr std::uintptr_t {name}_deferred_rva={profile["deferred_rva"]}, {name}_anchor_rva={profile["anchor_rva"]}, {name}_call_rva={profile["call_rva"]};')
(temp/'runtime_fixtures.inc').write_text('\n'.join(fixtures))
signatures=['bool VerifyOG163Mappings(', 'bool VerifyNG984Mappings(', 'bool IsLegacyRuntime()',
    'bool LegacyRouteHash(', 'bool VerifyLegacyStreamingRoutes(', 'bool UniversalFloatSampleFormat(',
    'void AdoptLegacyNamedView(', 'void CellFallbackLoadHook(', 'bool CellFallbackReadHook(',
    'template<class Call> decltype(auto) ObserveStreamingRoute(']
(temp/'compat_production.inc').write_text('\n'.join(block(new,sig) for sig in signatures))
wrappers=new[new.index('using CreateStreaming_t='):new.index('#include "KnownTextureLoad.h"')]
(temp/'compat_wrappers.inc').write_text(wrappers+'\n'+block(new,'bool InstallStreamingCacheRoutes('))
exe=temp/'runtime_compat_test.exe'
subprocess.run(['cl','/nologo','/std:c++20','/EHsc','/MD','/O2','/DNOMINMAX','/I'+str(temp),
    str(ROOT/'Tests/runtime_compat_test.cpp'),'/Fo:'+str(temp/'runtime_compat_test.obj'),'/Fe:'+str(exe),
    '/I'+str(Path(os.environ['DCF4_DETOURS'])/'include'),'/link',
    '/LIBPATH:'+str(Path(os.environ['DCF4_DETOURS'])/'lib'),'detours.lib',
    'd3d11.lib','d3dcompiler.lib','bcrypt.lib'],check=True,cwd=ROOT)
subprocess.run([str(exe)],check=True,cwd=ROOT)
print('PASS: verified native OG/NG bodies and typed compatibility adapters pass; rendering identity is checked by release_scope.py')
