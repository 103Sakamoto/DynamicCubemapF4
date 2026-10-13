#include <cstdlib>
// SPDX-License-Identifier: GPL-3.0-only
#define main ExistingSceneTests
#include "scene_fallback_gpu_test.cpp"
#undef main
#include <bcrypt.h>
#include <atomic>
#include <detours/detours.h>
#include <fstream>
#include <limits>
#include <string>
#include "../Source/src/ReflectionInterop.h"
#include "../Source/src/RuntimeStreamingProfiles.h"
#include "runtime_fixtures.inc"

enum class RuntimeFamily {unsupported,og,ng,ae};
struct {RuntimeFamily family=RuntimeFamily::og;}g_runtimeProfile;
std::string g_runtimeVersion="1.10.163.0";
bool g_streamingRoutesVerified=true;
std::atomic_bool g_diagnosticsEnabled{true};
void* testBase=nullptr;
HMODULE InspectionBase(const wchar_t*){return static_cast<HMODULE>(testBase);}
#define GetModuleHandleW InspectionBase
bool IsReadableSpan(std::uintptr_t address,std::size_t size){
    MEMORY_BASIC_INFORMATION i{};
    if(!address||!size||address>UINTPTR_MAX-size||!VirtualQuery(reinterpret_cast<void*>(address),&i,sizeof(i))||
        i.State!=MEM_COMMIT||(i.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
    return address+size<=reinterpret_cast<std::uintptr_t>(i.BaseAddress)+i.RegionSize;
}
bool IsExecutableAddress(std::uintptr_t address){MEMORY_BASIC_INFORMATION i{};
    return address&&VirtualQuery(reinterpret_cast<void*>(address),&i,sizeof(i))&&i.State==MEM_COMMIT&&
        !(i.Protect&(PAGE_NOACCESS|PAGE_GUARD))&&(i.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));}
template<std::size_t N>bool MatchAE240Bytes(std::uintptr_t address,const std::uint8_t(&bytes)[N]){
    return IsReadableSpan(address,N)&&!memcmp(reinterpret_cast<void*>(address),bytes,N);}
template<std::size_t N>bool MatchNG984Bytes(std::uintptr_t address,const std::uint8_t(&bytes)[N]){return MatchAE240Bytes(address,bytes);}
void Log(const std::string&){}
template<class F>void LogDiagnostic(F f){f();}
std::string Ptr(const void*){return "pointer";}
std::string ProbeBytes(std::uintptr_t,std::size_t){return "fixture bytes";}
namespace RE {
struct NiTexture {void* rendererTexture=nullptr;std::int8_t desiredDegradeLevel=0;int kind=1;};
namespace BSGraphics {struct RendererData{ID3D11Device* device;};RendererData* renderer=nullptr;RendererData* GetRendererData(){return renderer;}}
namespace ID {
struct Field {std::uintptr_t og,ng;std::uintptr_t address()const{return reinterpret_cast<std::uintptr_t>(testBase)+(g_runtimeVersion=="1.10.163.0"?og:ng);}};
namespace BSGraphics {Field GetRendererData{0x609bf80,0x3624ca0};}
namespace Main {Field WorldRootNode{0x5aa4358,0x303ac68};}
namespace PlayerCharacter {Field Singleton{0x59d6fd0,0x2f6b8f0};}
}}
struct CellFallbackLoadScope {int kind=1;ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;};
thread_local CellFallbackLoadScope* g_cellFallbackScope=nullptr;
int NamedFallbackKind(RE::NiTexture* t){return t?t->kind:0;}
bool EnsureCellFallbackDeviceHooks(){return true;}
unsigned notifications=0,active=0,loads=0,reads=0,submits=0;bool readSuccess=true;
struct FallbackNotificationScope{explicit FallbackNotificationScope(const char*){++notifications;++active;}~FallbackNotificationScope(){--active;}};
void SubmitCellFallback(const CellFallbackLoadScope& scope,const char*){Require(bool(scope.view),"Submitted absent view");++submits;}
void g_originalCellFallbackLoad(void*,RE::NiTexture*){++loads;Require(active!=0,"Load not bracketed");}
bool g_originalCellFallbackRead(void*,RE::NiTexture*,void*){++reads;Require(active!=0,"Read not bracketed");return readSuccess;}
#include "../Source/src/KnownTextureLoad.h"
#include "compat_production.inc"
#include "compat_wrappers.inc"

void DetachRoutes(){
    Require(DetourTransactionBegin()==NO_ERROR,"Detach transaction failed");
    Require(DetourUpdateThread(GetCurrentThread())==NO_ERROR,"Detach thread failed");
#define DETACH_ROUTE(name) Require(DetourDetach(reinterpret_cast<PVOID*>(&g_original##name),reinterpret_cast<PVOID>(name##Hook))==NO_ERROR,"Detach " #name " failed")
    DETACH_ROUTE(CreateStreaming);DETACH_ROUTE(CreateArraySlice);DETACH_ROUTE(ReadArraySlice);DETACH_ROUTE(FinishStreaming);
    DETACH_ROUTE(UpdateMip);DETACH_ROUTE(LoadUpgrade);DETACH_ROUTE(UpdateStreamingMip);DETACH_ROUTE(FinishUpgrade);
#undef DETACH_ROUTE
    Require(DetourTransactionCommit()==NO_ERROR,"Detach commit failed");
}

template<std::size_t N>void Profile(const RuntimeStreaming::Profile& p,const Chunk(&chunks)[N],RuntimeFamily family,
    std::uintptr_t deferred,std::uintptr_t anchor,std::uintptr_t call) {
    testBase=VirtualAlloc(nullptr,0x6400000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);Require(testBase!=nullptr,"Fixture allocation failed");
    const auto base=reinterpret_cast<std::uintptr_t>(testBase);auto* table=reinterpret_cast<std::uintptr_t*>(base+p.vtable);
    for(const auto& c:chunks)memcpy(reinterpret_cast<void*>(base+c.rva),c.bytes,c.length);
    for(const auto& r:p.routes)table[r.slot]=base+r.rva;
    g_runtimeProfile.family=family;g_runtimeVersion=std::string(p.version);
    Require(VerifyLegacyStreamingRoutes(table),"Inspected native routes rejected");
    for(const auto& r:p.routes){auto* bytes=reinterpret_cast<unsigned char*>(base+r.rva);bytes[r.length-1]^=1;
        Require(!VerifyLegacyStreamingRoutes(table),"Changed native function body accepted");bytes[r.length-1]^=1;
        const auto saved=table[r.slot];table[r.slot]=saved+1;Require(!VerifyLegacyStreamingRoutes(table),"Different route address accepted");table[r.slot]=saved;}
    Require(!VerifyLegacyStreamingRoutes(nullptr),"Null vtable accepted");
    Require(!VerifyLegacyStreamingRoutes(table+1),"Wrong vtable accepted");
    const auto verify=[&]{return family==RuntimeFamily::og?VerifyOG163Mappings(base+deferred,base+anchor,base+call,base+p.vtable):
        VerifyNG984Mappings(base+deferred,base+anchor,base+call,base+p.vtable);};
    Require(verify(),"Exact renderer profile rejected");
    auto* opcode=reinterpret_cast<unsigned char*>(base+call);const auto saved=*opcode;*opcode=0x90;Require(!verify(),"Wrong draw call accepted");*opcode=saved;
    g_runtimeVersion="1.10.985.0";Require(!VerifyLegacyStreamingRoutes(table)&&!verify(),"Unsupported version accepted");
    Require(!InstallStreamingCacheRoutes(table),"Unsupported runtime installed observers");
    g_runtimeVersion=std::string(p.version);
    Require(DetourTransactionBegin()==NO_ERROR,"Fixture transaction failed");
    Require(!InstallStreamingCacheRoutes(table),"Installer entered existing transaction");
    Require(DetourTransactionCommit()==NO_ERROR,"Installer aborted caller transaction");
    Require(InstallStreamingCacheRoutes(table),"Native inspected observer install failed");
    DetachRoutes();
    Require(VerifyLegacyStreamingRoutes(table),"Detach did not restore inspected native methods");
    VirtualFree(testBase,0,MEM_RELEASE);testBase=nullptr;
}

void AddictolHelpers(){
    // Reuse the existing standalone PE inspection fixture; never load or execute it.
    std::ifstream file(std::getenv("DCF4_ADDICTOL_DLL"),std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});Require(bytes.size()>4096,"Missing inspected Addictol image");
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data()+dos->e_lfanew);
    void* memory=VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Require(memory!=nullptr,"Addictol fixture allocation failed");
    memcpy(memory,bytes.data(),nt->OptionalHeader.SizeOfHeaders);
    const auto* section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        memcpy(static_cast<char*>(memory)+section[i].VirtualAddress,bytes.data()+section[i].PointerToRawData,section[i].SizeOfRawData);
    const auto module=reinterpret_cast<std::uintptr_t>(memory),function=module+0x110900,game=std::uintptr_t{0x140000000};
    auto* helpers=reinterpret_cast<std::uintptr_t*>(module+0x5c57c0);
    struct Case{const char* version;std::uintptr_t helpers[5];};
    const Case cases[]={
        {"1.10.163.0",{0x1b93840,0x1d35ac0,0x1d0ec70,0x1d363a0,0x61e0900}},
        {"1.10.984.0",{0x159ec90,0x1721bf0,0x1700850,0x1722790,0x3769600}},
        {"1.11.240.0",{0x16b9d40,0x183cf10,0x181bb30,0x183dab0,0x3a25d00}}};
    for(const auto& item:cases){
        g_runtimeVersion=item.version;
        for(unsigned i=0;i<5;++i)helpers[i]=game+item.helpers[i];
        Require(ValidateAddictolLoadImage(module,function,game),"Reviewed Addictol helper profile rejected");
        for(unsigned i=0;i<5;++i){++helpers[i];Require(!ValidateAddictolLoadImage(module,function,game),"Wrong Addictol helper accepted");--helpers[i];}
        auto* changed=reinterpret_cast<unsigned char*>(function+100);*changed^=1;
        Require(!ValidateAddictolLoadImage(module,function,game),"Changed Addictol code accepted");*changed^=1;
        Require(!ValidateAddictolLoadImage(module,function+1,game),"Wrong Addictol RVA accepted");
        Require(!IsKnownAddictolLoad(function,game),"Non-image addon allocation accepted by module identity guard");
    }
    g_runtimeVersion="unsupported";Require(!ValidateAddictolLoadImage(module,function,game),"Unsupported Addictol runtime accepted");
    VirtualFree(memory,0,MEM_RELEASE);
}
struct Backend{ID3D11ShaderResourceView* srv=nullptr;unsigned char pad[0x3d-sizeof(void*)]{};unsigned char degrade=0;};
static_assert(offsetof(Backend,degrade)==0x3d);
Cube ArrayCube(ID3D11Device* d,UINT cubes,UINT bind=D3D11_BIND_SHADER_RESOURCE,bool one=false,bool standalone=false){
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=4;desc.MipLevels=1;desc.ArraySize=6*cubes;
    desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.SampleDesc.Count=1;desc.BindFlags=bind;desc.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
    Cube result;Check(d->CreateTexture2D(&desc,nullptr,&result.texture));
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=desc.Format;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
    view.TextureCubeArray.MipLevels=1;view.TextureCubeArray.NumCubes=one?1:cubes;
    if(one)view.TextureCubeArray.First2DArrayFace=6;
    if(standalone){view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE;view.TextureCube.MostDetailedMip=0;view.TextureCube.MipLevels=1;}
    Check(d->CreateShaderResourceView(result.texture.Get(),&view,&result.view));return result;
}
void ExistingView(ID3D11Device* d,ID3D11DeviceContext* c){
    RE::BSGraphics::RendererData renderer{d};RE::BSGraphics::renderer=&renderer;
    const auto cube=ArrayCube(d,1,D3D11_BIND_SHADER_RESOURCE,false,true);Backend backend{cube.view.Get()};RE::NiTexture named{&backend,0,1};
    for(auto family:{RuntimeFamily::og,RuntimeFamily::ng}){
        g_runtimeProfile.family=family;g_runtimeVersion=family==RuntimeFamily::og?"1.10.163.0":"1.10.984.0";g_streamingRoutesVerified=true;
        CellFallbackLoadScope scope;AdoptLegacyNamedView(&named,scope);Require(scope.view.Get()==cube.view.Get()&&scope.texture.Get()==cube.texture.Get(),"Existing named cube rejected");
        // Acquisition itself never sets reference-readiness or replaces a material binding.
        CellFallbackLoadScope parent;g_cellFallbackScope=&parent;const auto before=submits;
        CellFallbackLoadHook(nullptr,&named);Require(g_cellFallbackScope==&parent&&submits==before+1,"Load state/submit mismatch");
        Require(CellFallbackReadHook(nullptr,&named,nullptr)&&g_cellFallbackScope==&parent&&submits==before+2,"Read state/submit mismatch");
        readSuccess=false;Require(!CellFallbackReadHook(nullptr,&named,nullptr)&&submits==before+2,"Failed read submitted source");readSuccess=true;
        Require(ObserveStreamingRoute(&named,"test",[]{return 7;})==7&&g_cellFallbackScope==&parent&&submits==before+3,"Generic typed result/scope mismatch");
        RE::NiTexture other{&backend,0,0};unsigned calls=0;Require(ObserveStreamingRoute(&other,"other",[&]{++calls;return false;})==false&&calls==1,"Unrelated call not forwarded");
        named.desiredDegradeLevel=0;backend.degrade=0;Require(!NativeMipChangeRequested(&named),"Equal native mip level not skipped");
        backend.degrade=1;Require(NativeMipChangeRequested(&named),"Changed native mip level skipped");backend.degrade=0;
        Require(active==0,"Notification scope leaked");g_cellFallbackScope=nullptr;
    }
    auto reject=[&](RE::NiTexture* texture){CellFallbackLoadScope scope;AdoptLegacyNamedView(texture,scope);Require(!scope.view,"Unsafe named view accepted");};
    reject(nullptr);named.kind=0;reject(&named);named.kind=1;g_streamingRoutesVerified=false;reject(&named);g_streamingRoutesVerified=true;
    g_runtimeProfile.family=RuntimeFamily::ae;g_runtimeVersion="1.11.240.0";reject(&named);g_runtimeProfile.family=RuntimeFamily::og;g_runtimeVersion="1.10.163.0";
    backend.srv=nullptr;reject(&named);backend.srv=cube.view.Get();
    const auto array=ArrayCube(d,2);backend.srv=array.view.Get();reject(&named);
    const auto single=ArrayCube(d,2,D3D11_BIND_SHADER_RESOURCE,true);backend.srv=single.view.Get();CellFallbackLoadScope singleScope;
    AdoptLegacyNamedView(&named,singleScope);Require(singleScope.view.Get()==single.view.Get(),"Exact single-cube array slice rejected");
    const auto writable=ArrayCube(d,1,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);backend.srv=writable.view.Get();reject(&named);
    backend.srv=cube.view.Get();CellFallbackLoadScope wrong;wrong.texture=array.texture;AdoptLegacyNamedView(&named,wrong);Require(!wrong.view,"Different captured resource accepted");
    for(const auto& tag:{RealisticReflections::Interop::RR_OWNED,RealisticReflections::Interop::RR_PEER_REPLACEMENT}){
        const UINT yes=1;Check(cube.texture->SetPrivateData(tag,sizeof(yes),&yes));reject(&named);Check(cube.texture->SetPrivateData(tag,0,nullptr));}
    ComPtr<ID3D11Device> other;ComPtr<ID3D11DeviceContext> otherContext;Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&other,nullptr,&otherContext));
    renderer.device=other.Get();reject(&named);renderer.device=d;
    RE::BSGraphics::renderer=nullptr;reject(&named);
}
int main()try{
    Profile(RuntimeStreaming::OG,OG_chunks,RuntimeFamily::og,OG_deferred_rva,OG_anchor_rva,OG_call_rva);
    Profile(RuntimeStreaming::NG984,NG984_chunks,RuntimeFamily::ng,NG984_deferred_rva,NG984_anchor_rva,NG984_call_rva);
    AddictolHelpers();
    ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c));
    ExistingView(d.Get(),c.Get());
    std::cout<<"PASS exact OG/NG renderer profiles and ten full native hashes; real Detours attach/detach and transaction failure; changed byte/address/version rejected; pinned Addictol OG/NG/AE helpers and module guards; named existing SRV/one-cube slice, owned references, failed read, typed forwarding, nested scopes, no-op mip, peer/atlas/writable/resource/device guards; AE acquisition unchanged\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
