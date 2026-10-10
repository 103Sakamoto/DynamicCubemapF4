"""Build runtime-only and complete project-source release archives."""
from pathlib import Path
import hashlib,zipfile
root=Path(__file__).resolve().parents[1]
def source_files():
    excluded={'build','.xmake','.git','dist','__pycache__','.vs'}
    generated={'.exe','.dll','.pdb','.obj','.cso','.pyc','.zip','.lib','.exp','.ilk'}
    for p in sorted(root.rglob('*')):
        rel=p.relative_to(root)
        if not p.is_file() or any(x in excluded for x in rel.parts):continue
        if p.suffix.lower() in generated or p.name in {'SHA256SUMS.txt','DynamicCubemapF4-GlassInterop01.patch'}:continue
        yield p,rel.as_posix()
def main():
    dll=root/'Source/build/windows/x64/releasedbg/DynamicCubemapF4.dll'
    if not dll.is_file():raise SystemExit('Build DLL first')
    dist=root/'dist';dist.mkdir(exist_ok=True)
    runtime=dist/'DynamicCubemapF4-1.4.zip';source=dist/'DynamicCubemapF4-1.4-source.zip'
    with zipfile.ZipFile(runtime,'w',zipfile.ZIP_DEFLATED) as z:
        z.write(dll,'F4SE/Plugins/DynamicCubemapF4.dll')
        for p in sorted((root/'Data/MCM').rglob('*')):
            if p.is_file():z.write(p,p.relative_to(root/'Data').as_posix())
    with zipfile.ZipFile(source,'w',zipfile.ZIP_DEFLATED) as z:
        for p,name in source_files():z.write(p,'DynamicCubemapF4-1.4/'+name)
    checks=''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in [runtime,source])
    (dist/'SHA256SUMS-1.4.txt').write_text(checks)
    for p in [runtime,source]:print(str(p),p.stat().st_size)
if __name__=='__main__':main()
