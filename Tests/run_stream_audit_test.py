import os
from pathlib import Path
import subprocess,re
r=Path(__file__).resolve().parents[1]
s=(r/'Source/src/Main.cpp').read_text(encoding='utf-8')
def extract(sig):
 a=s.index(sig);b=s.index('{',a);n=1;e=b+1
 while n:n+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
p=r/'Tests/temp';p.mkdir(exist_ok=True)
prefix=r"""
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <cassert>
using ULONGLONG=unsigned long long;using UINT=unsigned;
struct ID3D11ShaderResourceView{};
struct TintProbeResult{float a=0;};
template<class T> struct Ptr {T* p=nullptr;T* Get(){return p;}void Reset(){p=nullptr;}Ptr& operator=(T* v){p=v;return *this;}};
std::atomic_uint64_t g_fallbackNotificationEpoch{0};std::atomic_uint g_fallbackNotificationActive{0};
uint64_t g_probeNotificationEpoch=0,g_auditReferenceEpoch=0;
bool g_probeNotificationQuiet=true;
Ptr<ID3D11ShaderResourceView> g_auditReferenceView;
TintProbeResult g_auditReferenceValue{};
ULONGLONG g_auditNextReport=0;UINT g_auditStable=0,g_auditMissed=0,g_auditNotified=0,g_auditInconclusive=0;
void Log(std::string){} ULONGLONG GetTickCount64(){return 10;}
"""
prefix+='template<class Build> void LogDiagnostic(Build&& build){Log(build());}\n'
main=r"""
int main(){
 ID3D11ShaderResourceView v,w;
 AuditReferenceNotification({1},&v);AuditReferenceNotification({1},&v);assert(g_auditStable==1);
 AuditReferenceNotification({2},&v);assert(g_auditMissed==1);
 {FallbackNotificationScope outer("load");assert(g_fallbackNotificationActive==1);
  {FallbackNotificationScope inner("read");assert(g_fallbackNotificationActive==2);}
  assert(g_fallbackNotificationActive==1);}
 assert(g_fallbackNotificationActive==0 && g_fallbackNotificationEpoch==4);
 g_probeNotificationEpoch=4;AuditReferenceNotification({3},&v);assert(g_auditNotified==1);
 AuditReferenceNotification({9},&w);assert(g_auditMissed==1);
 g_probeNotificationEpoch=2;AuditReferenceNotification({10},&w);assert(g_auditInconclusive==1);
 g_probeNotificationEpoch=4;g_fallbackNotificationActive=1;AuditReferenceNotification({11},&w);assert(g_auditInconclusive==2);
}
"""
(p/'audit.cpp').write_text(prefix+extract('struct FallbackNotificationScope')+';'+extract('void AuditReferenceNotification(')+main)
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++20',str(p/'audit.cpp'),'-o',str(p/'audit.exe')],check=True)
subprocess.run([str(p/'audit.exe')],check=True)

print("PASS: production source/cache acceptance checks")
