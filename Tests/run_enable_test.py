from pathlib import Path
import subprocess,os,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'Source/src/Main.cpp').read_text()
def extract(sig):
 a=s.index(sig);b=s.index('{',a);n=1;e=b+1
 while n:n+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
prefix='''#include <cassert>
struct FallbackNotificationScope { explicit FallbackNotificationScope(const char*){} };
namespace RE { struct NiTexture { int kind; }; }
struct CellFallbackLoadScope { int kind=0; };
thread_local CellFallbackLoadScope* g_cellFallbackScope=nullptr;
bool g_requestedEnabled=false, hooks=true, readResult=true;
int loads=0,reads=0,submits=0,ensures=0;
int NamedFallbackKind(RE::NiTexture* t){return t->kind;}
bool EnsureCellFallbackDeviceHooks(){++ensures;return hooks;}
void SubmitCellFallback(CellFallbackLoadScope& s,const char*){assert(s.kind==1);++submits;}
void g_originalCellFallbackLoad(void*,RE::NiTexture*){++loads;}
bool g_originalCellFallbackRead(void*,RE::NiTexture*,void*){++reads;return readResult;}
'''
prefix+='void AdoptLegacyNamedView(RE::NiTexture*,CellFallbackLoadScope&){}\n'
main='''int main(){
 RE::NiTexture named{1},other{0}; CellFallbackLoadScope outer;g_cellFallbackScope=&outer;
 for(bool enabled:{false,true}) {g_requestedEnabled=enabled;
 int prior=submits;CellFallbackLoadHook(nullptr,&named);assert(submits==prior+1 && g_cellFallbackScope==&outer);
 assert(CellFallbackReadHook(nullptr,&named,nullptr));assert(submits==prior+2 && g_cellFallbackScope==&outer);
 prior=ensures;CellFallbackLoadHook(nullptr,&other);assert(ensures==prior);
 readResult=false;prior=submits;assert(!CellFallbackReadHook(nullptr,&named,nullptr));assert(submits==prior && g_cellFallbackScope==&outer);readResult=true;
 }
 hooks=false;int prior=submits;CellFallbackLoadHook(nullptr,&named);assert(submits==prior && g_cellFallbackScope==&outer);
 assert(loads==5 && reads==4);
}'''
with tempfile.TemporaryDirectory(prefix='dcf4-enable-') as d:
 p=Path(d);src=p/'test.cpp';src.write_text('#include <initializer_list>\n'+prefix+extract('void CellFallbackLoadHook(')+extract('bool CellFallbackReadHook(')+main)
 exe=p/'test.exe';subprocess.run([os.environ['CXX'],'-std=c++20',str(src),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
print('PASS: fallback observation while disabled/enabled, streaming failure, unrelated loads and scope restoration')
