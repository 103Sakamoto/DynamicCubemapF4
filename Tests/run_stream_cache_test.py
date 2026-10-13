import os
from pathlib import Path
import subprocess,re,struct,hashlib
r=Path(__file__).resolve().parents[1];s=(r/'Source/src/Main.cpp').read_text()
def extract(sig):
 a=s.index(sig);b=s.index('{',a);n=1;e=b+1
 while n:n+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
p=r/'Tests/temp';p.mkdir(exist_ok=True)
prefix=r"""
#include <atomic>
#include <cstdint>
#include <string>
#include <cassert>
#include <stdexcept>
namespace RE { struct NiTexture { int kind; void* rendererTexture; std::int8_t desiredDegradeLevel; }; }
struct CellFallbackLoadScope {int kind=0;bool view=false;};
thread_local CellFallbackLoadScope* g_cellFallbackScope=nullptr;
std::atomic_uint64_t g_fallbackNotificationEpoch{0};std::atomic_uint g_fallbackNotificationActive{0};
int submitted=0;bool hooks=true;
int NamedFallbackKind(RE::NiTexture* t){return t?t->kind:0;}
bool EnsureCellFallbackDeviceHooks(){return hooks;}
void SubmitCellFallback(const CellFallbackLoadScope&,const char*){++submitted;}
void Log(std::string){}
"""
prefix+='void AdoptLegacyNamedView(RE::NiTexture*,CellFallbackLoadScope&){}\n'
main=r"""
int main(){
 unsigned char renderer[64]{}; renderer[0x3d]=2;
 RE::NiTexture named{1,renderer,2},other{0,renderer,2};
 CellFallbackLoadScope outer;g_cellFallbackScope=&outer;
 assert(!NativeMipChangeRequested(&named));named.desiredDegradeLevel=1;assert(NativeMipChangeRequested(&named));
 assert(ObserveStreamingRoute(&named,"read",[&](){assert(g_fallbackNotificationActive==1);g_cellFallbackScope->view=true;return false;})==false);
 assert(submitted==1 && g_fallbackNotificationEpoch==2 && g_cellFallbackNotificationActive_DO_NOT_USE);
}
"""
main=main.replace('g_cellFallbackNotificationActive_DO_NOT_USE','g_fallbackNotificationActive==0 && g_cellFallbackScope==&outer')
main=main[:-2]+r"""
 auto epoch=g_fallbackNotificationEpoch.load();
 ObserveStreamingRoute(&other,"other",[&](){assert(g_cellFallbackScope==&outer);});assert(g_fallbackNotificationEpoch==epoch);
 try {ObserveStreamingRoute(&named,"throw",[]()->void{throw std::runtime_error("test");});}catch(...){}
 assert(g_fallbackNotificationActive==0 && g_cellFallbackScope==&outer);
 ObserveStreamingRoute(&named,"nested",[&](){ObserveStreamingRoute(&named,"child",[&](){assert(g_fallbackNotificationActive==2);});assert(g_fallbackNotificationActive==1);});
 assert(g_fallbackNotificationActive==0);
 hooks=false;epoch=g_fallbackNotificationEpoch;ObserveStreamingRoute(&named,"unavailable",[](){});assert(g_fallbackNotificationEpoch==epoch);
 assert(TintReferenceNeedsProbe(false,true));assert(!TintReferenceNeedsProbe(true,true));assert(TintReferenceNeedsProbe(true,false));
 assert(TintProbeEpochCurrent(true,8,8,0));assert(!TintProbeEpochCurrent(true,8,10,0));
 assert(!TintProbeEpochCurrent(true,8,8,1));assert(!TintProbeEpochCurrent(false,8,8,0));
}
"""
# Exercise the actual typed wrappers, including stack-passed arguments.
extra=r"""
 hooks=true; named.desiredDegradeLevel=1;
 g_originalCreateStreaming=+[](void* self,RE::NiTexture* t,const void* in,void* out){assert(self==(void*)1 && t->kind==1 && in==(void*)2 && out==(void*)3);};
 CreateStreamingHook((void*)1,&named,(void*)2,(void*)3);
 g_originalCreateArraySlice=+[](void* self,RE::NiTexture* t,const void* in,void* out,std::uint32_t slice){assert(self==(void*)1 && t->kind==1 && in==(void*)2 && out==(void*)3 && slice==5);return false;};
 assert(!CreateArraySliceHook((void*)1,&named,(void*)2,(void*)3,5));
 g_originalReadArraySlice=+[](void* self,RE::NiTexture* t,void* in,std::uint32_t slice){assert(self==(void*)1 && t->kind==1 && in==(void*)2 && slice==5);return true;};
 assert(ReadArraySliceHook((void*)1,&named,(void*)2,5));
 auto simple=+[](void* self,RE::NiTexture* t){assert(self==(void*)1 && t->kind==1);};
 g_originalFinishStreaming=simple;g_originalLoadUpgrade=simple;g_originalFinishUpgrade=simple;
 FinishStreamingHook((void*)1,&named);LoadUpgradeHook((void*)1,&named);FinishUpgradeHook((void*)1,&named);
 unsigned level=0;
 g_originalUpdateMip=+[](void* self,RE::NiTexture* t,std::uint32_t* level){assert(self==(void*)1 && t->kind==1);*level=8;return false;};
 assert(!UpdateMipHook((void*)1,&named,&level) && level==8);
 g_originalUpdateStreamingMip=+[](void* self,RE::NiTexture* t,std::uint32_t* level,void* in,void* out){assert(self==(void*)1 && t->kind==1 && in==(void*)2 && out==(void*)3);*level=9;return true;};
 assert(UpdateStreamingMipHook((void*)1,&named,&level,(void*)2,(void*)3) && level==9);
 named.desiredDegradeLevel=2;epoch=g_fallbackNotificationEpoch;
 UpdateMipHook((void*)1,&named,&level);UpdateStreamingMipHook((void*)1,&named,&level,(void*)2,(void*)3);
 assert(g_fallbackNotificationEpoch==epoch && g_fallbackNotificationActive==0 && g_cellFallbackScope==&outer);
"""
main=main[:main.rfind('}')]+extra+'}'
wrappers=s[s.index('using CreateStreaming_t='):s.index('#include "KnownTextureLoad.h"')]
code=prefix+extract('struct FallbackNotificationScope')+';\n'+'template<class Call> '+extract('decltype(auto) ObserveStreamingRoute(')+wrappers+extract('bool TintReferenceNeedsProbe(')+extract('bool TintProbeEpochCurrent(')+main
(p/'cache.cpp').write_text(code)
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++20',str(p/'cache.cpp'),'-o',str(p/'cache.exe')],check=True)
subprocess.run([str(p/'cache.exe')],check=True)

print("PASS: production source/cache acceptance checks")
