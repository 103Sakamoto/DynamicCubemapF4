#include <vector>
#include <utility>
#include <cassert>
enum DXGI_FORMAT {DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8X8_UNORM,DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,DXGI_FORMAT_BC1_UNORM,DXGI_FORMAT_BC1_UNORM_SRGB,DXGI_FORMAT_BC2_UNORM,DXGI_FORMAT_BC2_UNORM_SRGB,DXGI_FORMAT_BC3_UNORM,DXGI_FORMAT_BC3_UNORM_SRGB,DXGI_FORMAT_BC4_SNORM,DXGI_FORMAT_BC4_UNORM,DXGI_FORMAT_BC5_SNORM,DXGI_FORMAT_BC5_UNORM,DXGI_FORMAT_BC6H_SF16,DXGI_FORMAT_BC6H_UF16,DXGI_FORMAT_BC7_UNORM,DXGI_FORMAT_BC7_UNORM_SRGB,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R11G11B10_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_UNORM,DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R9G9B9E5_SHAREDEXP,DXGI_FORMAT_UNKNOWN};
struct D3D11_SHADER_RESOURCE_VIEW_DESC{DXGI_FORMAT Format;};
struct View{DXGI_FORMAT format;void GetDesc(D3D11_SHADER_RESOURCE_VIEW_DESC* d){d->Format=format;}};
struct Entry{bool directCube=false;};
std::vector<std::pair<View*,Entry>> g_universalViews;
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
int ObservedFallbackEncoding()
{
    // Arrays are the authoritative sampling path for this recovery. Do not
    // infer encoding from sampled brightness or from an arbitrary first view.
    int encoding=-1;
    for(const auto& [raw,entry]:g_universalViews) {
        if(entry.directCube || !raw) continue;
        D3D11_SHADER_RESOURCE_VIEW_DESC d{}; raw->GetDesc(&d);
        DXGI_FORMAT linear{},srgb{};
        if(!FallbackFormatPair(d.Format,linear,srgb)) continue;
        const int current=d.Format==srgb ? 1 : 0;
        if(encoding>=0 && encoding!=current) return -2;
        encoding=current;
    }
    return encoding;
}

int main(){assert(ObservedFallbackEncoding()==-1);View a{DXGI_FORMAT_B8G8R8A8_UNORM_SRGB},b{DXGI_FORMAT_BC7_UNORM_SRGB},c{DXGI_FORMAT_BC1_UNORM};g_universalViews={{&a,{false}},{&b,{false}}};assert(ObservedFallbackEncoding()==1);g_universalViews.push_back({&c,{true}});assert(ObservedFallbackEncoding()==1);g_universalViews.back().second.directCube=false;assert(ObservedFallbackEncoding()==-2);g_universalViews={{&c,{false}}};assert(ObservedFallbackEncoding()==0);}
