// SPDX-License-Identifier: GPL-3.0-only
// Included inside the plugin namespace after the existing tint source guards.
namespace ArrayWrites {
struct Stamp { uint64_t writes=0, commands=0; bool quiet=false; };
std::atomic_uint64_t writes{0}, commands{0}, notifications{0}, reuses{0};
std::atomic_uint64_t uploads{0}, copies{0}, resolves{0}, discards{0};
std::atomic_uint active{0}, commandActive{0}, trackedCount{0};
std::mutex mutex;
std::unordered_map<ID3D11Resource*,Microsoft::WRL::ComPtr<ID3D11Resource>> resources;
std::unordered_map<ID3D11ShaderResourceView*,Stamp> cached;
bool attempted=false, installed=false;
ID3D11DeviceContext* installedContext=nullptr;
ULONGLONG nextReport=0;
Stamp Snapshot() { return {writes.load(),commands.load(),active.load()==0 && commandActive.load()==0}; }
bool Current(const Stamp& s) {
    const auto now=Snapshot();
    return s.quiet && now.quiet && s.writes==now.writes && s.commands==now.commands;
}
struct WriteScope {
    bool tracked=false;
    explicit WriteScope(ID3D11Resource* resource,std::atomic_uint64_t* category=nullptr) {
        if(!resource || !trackedCount.load()) return;
        std::lock_guard lock(mutex);
        tracked=resources.contains(resource);
        if(tracked) { ++active; ++writes; ++notifications; if(category) ++*category; }
    }
    ~WriteScope() { if(tracked) { ++writes; --active; } }
};
struct CommandScope {
    CommandScope() { ++commandActive; ++commands; }
    ~CommandScope() { ++commands; --commandActive; }
};
using Copy_t=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*);
using CopyResource_t=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,ID3D11Resource*);
using Update_t=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT);
using Resolve_t=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,ID3D11Resource*,UINT,DXGI_FORMAT);
using Execute_t=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11CommandList*,BOOL);
using Copy1_t=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*,UINT);
using Update1_t=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT,UINT);
using Discard_t=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3D11Resource*);
using DiscardView_t=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3D11View*);
using DiscardView1_t=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3D11View*,const D3D11_RECT*,UINT);
Copy_t copy=nullptr; CopyResource_t copyResource=nullptr; Update_t update=nullptr; Resolve_t resolve=nullptr; Execute_t execute=nullptr;
Copy1_t copy1=nullptr; Update1_t update1=nullptr; Discard_t discard=nullptr; DiscardView_t discardView=nullptr; DiscardView1_t discardView1=nullptr;
void STDMETHODCALLTYPE Copy(ID3D11DeviceContext* c,ID3D11Resource* d,UINT sub,UINT x,UINT y,UINT z,ID3D11Resource* s,UINT ss,const D3D11_BOX* b) {
    WriteScope scope(d,&copies); copy(c,d,sub,x,y,z,s,ss,b);
}
void STDMETHODCALLTYPE CopyResource(ID3D11DeviceContext* c,ID3D11Resource* d,ID3D11Resource* s) { WriteScope scope(d,&copies); copyResource(c,d,s); }
void STDMETHODCALLTYPE Update(ID3D11DeviceContext* c,ID3D11Resource* d,UINT sub,const D3D11_BOX* b,const void* data,UINT row,UINT depth) {
    WriteScope scope(d,&uploads); update(c,d,sub,b,data,row,depth);
}
void STDMETHODCALLTYPE Resolve(ID3D11DeviceContext* c,ID3D11Resource* d,UINT sub,ID3D11Resource* s,UINT ss,DXGI_FORMAT f) { WriteScope scope(d,&resolves); resolve(c,d,sub,s,ss,f); }
void STDMETHODCALLTYPE Execute(ID3D11DeviceContext* c,ID3D11CommandList* list,BOOL restore) {
    // Replay may contain writes recorded before tracking began, or on another context.
    CommandScope scope; execute(c,list,restore);
}
void STDMETHODCALLTYPE Copy1(ID3D11DeviceContext1* c,ID3D11Resource* d,UINT sub,UINT x,UINT y,UINT z,ID3D11Resource* s,UINT ss,const D3D11_BOX* b,UINT flags) {
    WriteScope scope(d,&copies); copy1(c,d,sub,x,y,z,s,ss,b,flags);
}
void STDMETHODCALLTYPE Update1(ID3D11DeviceContext1* c,ID3D11Resource* d,UINT sub,const D3D11_BOX* b,const void* data,UINT row,UINT depth,UINT flags) {
    WriteScope scope(d,&uploads); update1(c,d,sub,b,data,row,depth,flags);
}
void STDMETHODCALLTYPE Discard(ID3D11DeviceContext1* c,ID3D11Resource* d) { WriteScope scope(d,&discards); discard(c,d); }
Microsoft::WRL::ComPtr<ID3D11Resource> ViewResource(ID3D11View* v) { Microsoft::WRL::ComPtr<ID3D11Resource> r; if(v) v->GetResource(&r); return r; }
void STDMETHODCALLTYPE DiscardView(ID3D11DeviceContext1* c,ID3D11View* v) { auto r=ViewResource(v); WriteScope scope(r.Get(),&discards); discardView(c,v); }
void STDMETHODCALLTYPE DiscardView1(ID3D11DeviceContext1* c,ID3D11View* v,const D3D11_RECT* rects,UINT n) { auto r=ViewResource(v); WriteScope scope(r.Get(),&discards); discardView1(c,v,rects,n); }
bool Install(ID3D11DeviceContext* context) {
    if(attempted) return installed && context==installedContext;
    attempted=true;
    if(!g_streamingRoutesVerified || !context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> extended;
    if(FAILED(context->QueryInterface(IID_PPV_ARGS(&extended)))) return false;
    auto** base=*reinterpret_cast<void***>(context);
    auto** ext=*reinterpret_cast<void***>(extended.Get());
    const unsigned baseSlots[]={46,47,48,57,58};
    for(auto slot:baseSlots) if(base[slot]!=ext[slot]) return false;
    copy=reinterpret_cast<Copy_t>(base[46]); copyResource=reinterpret_cast<CopyResource_t>(base[47]);
    update=reinterpret_cast<Update_t>(base[48]); resolve=reinterpret_cast<Resolve_t>(base[57]); execute=reinterpret_cast<Execute_t>(base[58]);
    copy1=reinterpret_cast<Copy1_t>(ext[115]); update1=reinterpret_cast<Update1_t>(ext[116]); discard=reinterpret_cast<Discard_t>(ext[117]);
    discardView=reinterpret_cast<DiscardView_t>(ext[118]); discardView1=reinterpret_cast<DiscardView1_t>(ext[133]);
    struct Hook { PVOID* original; PVOID replacement; };
    const Hook hooks[]={
        {reinterpret_cast<PVOID*>(&copy),reinterpret_cast<PVOID>(Copy)}, {reinterpret_cast<PVOID*>(&copyResource),reinterpret_cast<PVOID>(CopyResource)},
        {reinterpret_cast<PVOID*>(&update),reinterpret_cast<PVOID>(Update)}, {reinterpret_cast<PVOID*>(&resolve),reinterpret_cast<PVOID>(Resolve)},
        {reinterpret_cast<PVOID*>(&execute),reinterpret_cast<PVOID>(Execute)}, {reinterpret_cast<PVOID*>(&copy1),reinterpret_cast<PVOID>(Copy1)},
        {reinterpret_cast<PVOID*>(&update1),reinterpret_cast<PVOID>(Update1)}, {reinterpret_cast<PVOID*>(&discard),reinterpret_cast<PVOID>(Discard)},
        {reinterpret_cast<PVOID*>(&discardView),reinterpret_cast<PVOID>(DiscardView)}, {reinterpret_cast<PVOID*>(&discardView1),reinterpret_cast<PVOID>(DiscardView1)}};
    for(size_t i=0;i<std::size(hooks);++i) {
        if(!*hooks[i].original) return false;
        for(size_t j=0;j<i;++j) if(*hooks[i].original==*hooks[j].original) return false;
    }
    LONG error=DetourTransactionBegin(); if(error!=NO_ERROR) return false;
    error=DetourUpdateThread(GetCurrentThread());
    for(const auto& h:hooks) if(error==NO_ERROR) error=DetourAttach(h.original,h.replacement);
    if(error!=NO_ERROR) { DetourTransactionAbort(); LogDiagnostic([&] { return "ARRAY CACHE: write hook installation rejected; periodic probes retained."; }); return false; }
    error=DetourTransactionCommit();
    installed=error==NO_ERROR; if(installed) installedContext=context;
    Log(installed ? "ARRAY CACHE: D3D11/11.1 upload, copy, resolve, discard and command-list observers installed." : "ARRAY CACHE: write hook commit failed; periodic probes retained.");
    return installed;
}
bool Track(ID3D11ShaderResourceView* view) {
    if(!installed || g_liveContext!=installedContext || !view) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC v{}; view->GetDesc(&v);
    if(v.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURECUBEARRAY) return false;
    Microsoft::WRL::ComPtr<ID3D11Resource> r; view->GetResource(&r);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> t; if(!r || FAILED(r.As(&t))) return false;
    D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d);
    // Excludes GPU render/UAV writes, CPU mapping, shared/external and tiled memory.
    if(d.Usage!=D3D11_USAGE_DEFAULT || d.BindFlags!=D3D11_BIND_SHADER_RESOURCE || d.CPUAccessFlags ||
       d.MiscFlags!=D3D11_RESOURCE_MISC_TEXTURECUBE || d.SampleDesc.Count!=1) return false;
    std::lock_guard lock(mutex);
    if(!resources.contains(r.Get())) {
        if(resources.size()>=128) return false;
        resources.emplace(r.Get(),r); trackedCount=static_cast<unsigned>(resources.size());
        LogDiagnostic([&] { return "ARRAY CACHE: tracking array resource="+Ptr(r.Get())+" faces="+std::to_string(d.ArraySize)+" mips="+std::to_string(d.MipLevels); });
    }
    return true;
}
bool Reuse(ID3D11ShaderResourceView* view) {
    auto it=cached.find(view);
    if(it==cached.end() || !Current(it->second)) return false;
    ++reuses; return true;
}
void Reset() {
    cached.clear(); std::lock_guard lock(mutex); resources.clear(); trackedCount=0;
}
void Report() {
    auto now=GetTickCount64(); if(now<nextReport) return; nextReport=now+30000;
    LogDiagnostic([&] { return "ARRAY CACHE: tracked="+std::to_string(trackedCount.load())+" cached="+std::to_string(cached.size())+
        " writes="+std::to_string(notifications.load())+" uploads="+std::to_string(uploads.load())+" copies="+std::to_string(copies.load())+" resolves="+std::to_string(resolves.load())+" discards="+std::to_string(discards.load())+" commandEpoch="+std::to_string(commands.load())+" reuseChecks="+std::to_string(reuses.load()); });
}
}
