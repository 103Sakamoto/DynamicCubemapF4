import os
from pathlib import Path
import subprocess, re
root=Path(__file__).resolve().parents[1]
s=(root/'Source/src/Main.cpp').read_text(encoding='utf-8-sig')
p=Path(__file__).parent/'temp';p.mkdir(exist_ok=True)
f=re.search(r'bool TintReferenceNeedsProbe\(.*?\n}',s,re.S).group()
(p/'tint_cache.cpp').write_text('#include <cassert>\n'+f+'\nint main(){\n // Invalid immutable and mutable sources must retry.\n assert(TintReferenceNeedsProbe(false,true));\n assert(TintReferenceNeedsProbe(false,false));\n // Valid immutable data remains cached indefinitely.\n for(int i=0;i<10000;++i) assert(!TintReferenceNeedsProbe(true,true));\n // Valid mutable sources still require freshness checks.\n assert(TintReferenceNeedsProbe(true,false));\n // Replacement/reset invalidates even an immutable result.\n bool valid=true; valid=false; assert(TintReferenceNeedsProbe(valid,true));\n}\n')
subprocess.run([os.environ.get('CXX','clang++'),str(p/'tint_cache.cpp'),'-o',str(p/'tint_cache.exe')],check=True)
subprocess.run([str(p/'tint_cache.exe')],check=True)

print("PASS: production source/cache acceptance checks")
