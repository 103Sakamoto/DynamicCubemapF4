#include <algorithm>
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <cstring>
#include <cassert>
using UINT=unsigned; using HRESULT=int;
#define FAILED(x) ((x)<0)
enum DXGI_FORMAT {DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8X8_UNORM,DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,DXGI_FORMAT_BC1_UNORM,DXGI_FORMAT_BC1_UNORM_SRGB,DXGI_FORMAT_BC2_UNORM,DXGI_FORMAT_BC2_UNORM_SRGB,DXGI_FORMAT_BC3_UNORM,DXGI_FORMAT_BC3_UNORM_SRGB,DXGI_FORMAT_BC4_SNORM,DXGI_FORMAT_BC4_UNORM,DXGI_FORMAT_BC5_SNORM,DXGI_FORMAT_BC5_UNORM,DXGI_FORMAT_BC6H_SF16,DXGI_FORMAT_BC6H_UF16,DXGI_FORMAT_BC7_UNORM,DXGI_FORMAT_BC7_UNORM_SRGB,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R11G11B10_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_UNORM,DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R9G9B9E5_SHAREDEXP,DXGI_FORMAT_UNKNOWN};
const int D3D11_USAGE_IMMUTABLE=1,D3D11_BIND_SHADER_RESOURCE=1,D3D11_RESOURCE_MISC_TEXTURECUBE=4,D3D11_SRV_DIMENSION_TEXTURECUBE=9;
struct D3D11_SUBRESOURCE_DATA {const void* pSysMem;UINT SysMemPitch,SysMemSlicePitch;};
struct D3D11_TEXTURE2D_DESC {UINT Width{},Height{},MipLevels{},ArraySize{};DXGI_FORMAT Format{};struct{UINT Count{};}SampleDesc;int Usage{},BindFlags{},MiscFlags{};};
struct D3D11_SHADER_RESOURCE_VIEW_DESC {DXGI_FORMAT Format{};int ViewDimension{};struct{UINT MipLevels{};}TextureCube;};
struct ID3D11Texture2D{};struct ID3D11ShaderResourceView{};
namespace Microsoft::WRL {template<class T> struct ComPtr {T* p{};T* Get()const{return p;}T** GetAddressOf(){return &p;}explicit operator bool()const{return p!=nullptr;}};}
std::vector<unsigned char> input;
namespace RE { struct BSResourceNiBinaryStream {size_t pos=0;BSResourceNiBinaryStream(const char*){} explicit operator bool()const{return !input.empty();} size_t DoRead(void* out,size_t n){n=std::min(n,input.size()-pos);memcpy(out,input.data()+pos,n);pos+=n;return n;}};}
struct ID3D11Device {bool failTexture=false,failView=false;int calls=0;DXGI_FORMAT lastFormat=DXGI_FORMAT_UNKNOWN;HRESULT CreateTexture2D(const D3D11_TEXTURE2D_DESC* d,const D3D11_SUBRESOURCE_DATA* p,ID3D11Texture2D** out){++calls;lastFormat=d->Format;assert(d->ArraySize==6);for(unsigned i=0;i<6*d->MipLevels;++i)assert(p[i].pSysMem && p[i].SysMemPitch);static ID3D11Texture2D t;*out=&t;return failTexture?-1:0;}HRESULT CreateShaderResourceView(ID3D11Texture2D*,const D3D11_SHADER_RESOURCE_VIEW_DESC*,ID3D11ShaderResourceView** out){static ID3D11ShaderResourceView v;*out=&v;return failView?-1:0;}};
std::mutex g_cellFallbackMutex;Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_namedCellFallback[1];void Log(const std::string&){}
bool FallbackFormatPair(DXGI_FORMAT format,DXGI_FORMAT& linear,DXGI_FORMAT& srgb)
{
    switch(format) {
#define DCF4_FORMAT_PAIR(name) case DXGI_FORMAT_##name##_UNORM: case DXGI_FORMAT_##name##_UNORM_SRGB: \
    linear=DXGI_FORMAT_##name##_UNORM; srgb=DXGI_FORMAT_##name##_UNORM_SRGB; return true;
    DCF4_FORMAT_PAIR(R8G8B8A8)
    DCF4_FORMAT_PAIR(B8G8R8A8)
    DCF4_FORMAT_PAIR(B8G8R8X8)
    DCF4_FORMAT_PAIR(BC1)
    DCF4_FORMAT_PAIR(BC2)
    DCF4_FORMAT_PAIR(BC3)
    DCF4_FORMAT_PAIR(BC7)
#undef DCF4_FORMAT_PAIR
    default: return false;
    }
}

int observed=1;int ObservedFallbackEncoding(){return observed;}
bool RecoverNamedFallback(ID3D11Device* device, std::string& failure,
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>* candidate=nullptr)
{
    RE::BSResourceNiBinaryStream stream("textures\\shared\\cubemaps\\mipblur_DefaultOutside1.dds");
    if (!stream) { failure="resource stream unavailable"; return false; }
    auto read=[&](void* data,std::size_t size) { return stream.DoRead(data,size)==size; };
    std::uint32_t h[32]{};
    if (!read(h,sizeof(h)) || h[0]!=0x20534444 || h[1]!=124 || h[19]!=32) {
        failure="resource is not a supported DDS header"; return false;
    }
    const UINT width=h[4],height=h[3],mips=std::max(1u,h[7]);
    UINT maxMips=1; for(UINT n=width;n>1;n>>=1) ++maxMips;
    if (!width || width!=height || width>4096 || mips>maxMips || (h[28]&0x200000)) {
        failure="invalid cube dimensions or mip count"; return false;
    }
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    const auto fourcc=h[21];
    if ((h[20]&4) && fourcc==0x30315844) {
        std::uint32_t dx[5]{};
        if (!read(dx,sizeof(dx)) || dx[1]!=3 || !(dx[2]&4) || dx[3]!=1) {
            failure="DDS must contain one 2D cubemap"; return false;
        }
        format=static_cast<DXGI_FORMAT>(dx[0]);
    } else {
        if ((h[28]&0xFE00)!=0xFE00) { failure="DDS lacks six cube faces"; return false; }
        if (h[20]&4) {
            switch(fourcc) {
            case 0x31545844: format=DXGI_FORMAT_BC1_UNORM; break;
            case 0x33545844: format=DXGI_FORMAT_BC2_UNORM; break;
            case 0x35545844: format=DXGI_FORMAT_BC3_UNORM; break;
            case 0x31495441: case 0x55344342: format=DXGI_FORMAT_BC4_UNORM; break;
            case 0x32495441: case 0x55354342: format=DXGI_FORMAT_BC5_UNORM; break;
            case 113: format=DXGI_FORMAT_R16G16B16A16_FLOAT; break;
            case 116: format=DXGI_FORMAT_R32G32B32A32_FLOAT; break;
            }
        } else if ((h[20]&0x40) && h[22]==32 && h[24]==0xFF00 &&
                   (h[26]==0xFF000000 || h[26]==0)) {
            if(h[23]==0xFF && h[25]==0xFF0000 && h[26]==0xFF000000) format=DXGI_FORMAT_R8G8B8A8_UNORM;
            if(h[23]==0xFF0000 && h[25]==0xFF) format=h[26] ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_B8G8R8X8_UNORM;
        }
    }
    const DXGI_FORMAT fileFormat=format;
    DXGI_FORMAT linearFormat{},srgbFormat{};
    if(FallbackFormatPair(format,linearFormat,srgbFormat)) {
        const int encoding=ObservedFallbackEncoding();
        if(encoding<0) {
            failure=encoding==-2 ? "mixed array color spaces; reference encoding unresolved" :
                "waiting for an array sampling format";
            return false;
        }
        format=encoding==1 ? srgbFormat : linearFormat;
    }
    UINT block=0,pixel=0;
    switch(format) {
    case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: block=8; break;
    case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB: block=16; break;
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_R11G11B10_FLOAT: case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGI_FORMAT_R10G10B10A2_UNORM: pixel=4; break;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: pixel=8; break;
    case DXGI_FORMAT_R32G32B32A32_FLOAT: pixel=16; break;
    default: failure="unsupported DDS format"; return false;
    }
    // Reject nonstandard row padding rather than uploading incorrectly aligned data.
    if (!block && (h[2]&8) && h[5]!=width*pixel) {
        failure="unsupported DDS row pitch"; return false;
    }
    std::vector<std::vector<std::uint8_t>> bytes(6*mips);
    std::vector<D3D11_SUBRESOURCE_DATA> init(6*mips);
    std::size_t total=0;
    for(UINT face=0;face<6;++face) for(UINT mip=0;mip<mips;++mip) {
        const UINT w=std::max(1u,width>>mip),rows=block?std::max(1u,(w+3)/4):w;
        const UINT pitch=block?std::max(1u,(w+3)/4)*block:w*pixel;
        const std::size_t size=static_cast<std::size_t>(pitch)*rows;
        total+=size; if(total>64u*1024u*1024u) { failure="DDS exceeds 64 MiB recovery limit"; return false; }
        const UINT i=face*mips+mip; bytes[i].resize(size);
        if(!read(bytes[i].data(),size)) { failure="DDS payload unavailable or truncated"; return false; }
        init[i]={bytes[i].data(),pitch,static_cast<UINT>(size)};
    }
    D3D11_TEXTURE2D_DESC d{}; d.Width=width; d.Height=height; d.MipLevels=mips; d.ArraySize=6;
    d.Format=format; d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_IMMUTABLE;
    d.BindFlags=D3D11_BIND_SHADER_RESOURCE; d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    HRESULT hr=device->CreateTexture2D(&d,init.data(),texture.GetAddressOf());
    if(FAILED(hr)) { failure="CreateTexture2D HRESULT="+std::to_string(hr); return false; }
    D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=format; v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE;
    v.TextureCube.MipLevels=mips;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    hr=device->CreateShaderResourceView(texture.Get(),&v,view.GetAddressOf());
    if(FAILED(hr)) { failure="CreateShaderResourceView HRESULT="+std::to_string(hr); return false; }
    if (candidate) *candidate=view;
    else {
        std::lock_guard lock(g_cellFallbackMutex);
        if(!g_namedCellFallback[0]) g_namedCellFallback[0]=view;
    }
    Log("FALLBACK: named resource reopened; size="+std::to_string(width)+" mips="+std::to_string(mips)+
        " fileFormat="+std::to_string(static_cast<UINT>(fileFormat))+
        " sampleFormat="+std::to_string(static_cast<UINT>(format)));
    return true;
}
void make(bool dx=false) {std::uint32_t h[32]{};h[0]=0x20534444;h[1]=124;h[3]=h[4]=4;h[7]=3;h[19]=32;h[20]=4;h[21]=dx?0x30315844:0x31545844;h[28]=0xFE00;input.resize(128);memcpy(input.data(),h,128);if(dx){std::uint32_t d[5]{DXGI_FORMAT_BC7_UNORM,3,4,1,0};auto n=input.size();input.resize(n+20);memcpy(input.data()+n,d,20);}input.resize(input.size()+6*3*(dx?16:8));g_namedCellFallback[0].p=nullptr;}
int main(){ID3D11Device device;std::string why;
make();observed=0;
ID3D11ShaderResourceView original;
g_namedCellFallback[0].p=&original;
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> candidate;
assert(RecoverNamedFallback(&device,why,&candidate));
assert(candidate && candidate.Get()!=&original);
assert(g_namedCellFallback[0].Get()==&original);
device.calls=0;

DXGI_FORMAT l{},g{};assert(FallbackFormatPair(DXGI_FORMAT_B8G8R8A8_UNORM,l,g));
assert(l==DXGI_FORMAT_B8G8R8A8_UNORM&&g==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
assert(!FallbackFormatPair(DXGI_FORMAT_R16G16B16A16_FLOAT,l,g));
make();observed=-1;assert(!RecoverNamedFallback(&device,why)&&device.calls==0);
observed=-2;assert(!RecoverNamedFallback(&device,why)&&device.calls==0);
observed=0;make();assert(RecoverNamedFallback(&device,why)&&device.lastFormat==DXGI_FORMAT_BC1_UNORM);observed=1;
make();assert(RecoverNamedFallback(&device,why));assert(device.lastFormat==DXGI_FORMAT_BC1_UNORM_SRGB);make(true);assert(RecoverNamedFallback(&device,why));assert(device.lastFormat==DXGI_FORMAT_BC7_UNORM_SRGB);make();input.pop_back();assert(!RecoverNamedFallback(&device,why));make();input[0]=0;assert(!RecoverNamedFallback(&device,why));make();input[28]=20;assert(!RecoverNamedFallback(&device,why));make();device.failTexture=true;assert(!RecoverNamedFallback(&device,why)&&!g_namedCellFallback[0]);device.failTexture=false;make();device.failView=true;assert(!RecoverNamedFallback(&device,why)&&!g_namedCellFallback[0]);device.failView=false;make(true);input[140]=2;assert(!RecoverNamedFallback(&device,why));}
