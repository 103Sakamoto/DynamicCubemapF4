"""Check release cleanup against the accepted runtime without sibling checkouts."""
from pathlib import Path
import hashlib,json,re,configparser
ROOT=Path(__file__).resolve().parents[1]
accepted=json.loads((ROOT/'Tests/accepted_runtime_identity.json').read_text())
dead='''void UniversalReplicatePool(UniversalPool& pool)
{
    UniversalReplicationScope state;
    UniversalDispatchPool(pool);
}

'''
for name, expected in accepted['files'].items():
    path=ROOT/name
    if name=='Source/src/Main.cpp':
        source=path.read_text(encoding='utf-8-sig')
        assert 'UniversalReplicatePool' not in source
        source=source.replace('void UniversalPublish()\n{',dead+'void UniversalPublish()\n{',1)
        source=source.replace('DynamicCubemapF4 1.5','DynamicCubemapF4 1.4 RuntimeCompat01')
        source=source.replace('v.PluginVersion({1, 5, 0, 0});','v.PluginVersion({1, 4, 0, 0});')
        source=source.replace('REL::Version{1,5,0,0}.pack()','REL::Version{1,4,0,0}.pack()')
        data=source.rstrip().encode('utf-8')
    elif name=='Source/src/DiagnosticsOverlay.cpp':
        data=path.read_text(encoding='utf-8-sig').replace('Dynamic Cubemap F4 - 1.5','Dynamic Cubemap F4 - RuntimeCompat01').rstrip().encode('utf-8')
    else:
        data=path.read_bytes()
    if name in accepted.get('text_lf_hashes',{}):
        assert hashlib.sha256(data.replace(b'\r\n',b'\n')).hexdigest()==accepted['text_lf_hashes'][name],name
    else:
        assert hashlib.sha256(data).hexdigest()==expected,name
manifest=json.loads((ROOT/'Source/generated/ShaderManifest.json').read_text())
assert manifest['shaders']==accepted['shaders'] and len(manifest['shaders'])==13
settings=configparser.ConfigParser()
settings.read(ROOT/'Data/MCM/Config/DynamicCubemapF4/settings.ini')
assert settings.getint('General','bDiagnostics')==0
assert settings.getint('General','bReprojection')==0
main=(ROOT/'Source/src/Main.cpp').read_text()
assert 'ReadMCMInt("bDiagnostics",0)' in main and 'ReadMCMInt("bReprojection",0)' in main
assert 'v.PluginVersion({1, 5, 0, 0});' in main and 'REL::Version{1,5,0,0}.pack()' in main
assert all(name not in main for name in ['iSharpnessMode','g_materialMip','g_interiorDarkCube','g_nativeSky'])
overlay=(ROOT/'Source/src/DiagnosticsOverlay.cpp').read_text()
assert 'visible=false;showPreviews=false;' in overlay
assert 'if(showPreviews)UpdatePreviews();' in overlay
assert 'D3DCompile' not in main and 'D3DCompile' not in overlay
print('PASS: runtime cleanup confined to unused wrapper and release labels/version; all 13 shaders, dependencies, licenses, caches, tint, safety, lighting and scheduling preserved; diagnostics/reprojection/previews default OFF')
