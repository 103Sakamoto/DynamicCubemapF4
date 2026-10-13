// Estimated logical storage, not resident VRAM or incremental memory retained by this cache.
unsigned CacheFormatBytes(DXGI_FORMAT f, bool& block) {
    block=false;
    switch(f) {
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: block=true; return 8;
    case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB: block=true; return 16;
    default: break;
    }
    // DXGI's contiguous format families share a storage size, including typeless variants.
    if(f>=DXGI_FORMAT_R32G32B32A32_TYPELESS && f<=DXGI_FORMAT_R32G32B32A32_SINT) return 16;
    if(f>=DXGI_FORMAT_R32G32B32_TYPELESS && f<=DXGI_FORMAT_R32G32B32_SINT) return 12;
    if(f>=DXGI_FORMAT_R16G16B16A16_TYPELESS && f<=DXGI_FORMAT_X32_TYPELESS_G8X24_UINT) return 8;
    if(f>=DXGI_FORMAT_R10G10B10A2_TYPELESS && f<=DXGI_FORMAT_X24_TYPELESS_G8_UINT) return 4;
    if(f>=DXGI_FORMAT_R8G8_TYPELESS && f<=DXGI_FORMAT_R16_SINT) return 2;
    if(f>=DXGI_FORMAT_R8_TYPELESS && f<=DXGI_FORMAT_A8_UNORM) return 1;
    switch(f) {
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP: case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM: return 4;
    case DXGI_FORMAT_B5G6R5_UNORM: case DXGI_FORMAT_B5G5R5A1_UNORM: case DXGI_FORMAT_B4G4R4A4_UNORM: return 2;
    default: return 0;
    }
}
std::uint64_t CacheTextureBytes(const D3D11_TEXTURE2D_DESC& d) {
    bool block=false; const auto unit=CacheFormatBytes(d.Format,block); if(!unit) return 0;
    std::uint64_t bytes=0;
    for(UINT m=0;m<d.MipLevels;++m) {
        const auto w=std::max(1u,d.Width>>m),h=std::max(1u,d.Height>>m);
        bytes+=block ? std::uint64_t((w+3)/4)*((h+3)/4)*unit : std::uint64_t(w)*h*unit;
    }
    return bytes*d.ArraySize*d.SampleDesc.Count;
}
void ReportNonCubeCache() {
    std::unordered_set<ID3D11Resource*> seen;
    std::uint64_t bytes=0; unsigned unknown=0;
    for(const auto& [key,view]:g_universalNonCubeViews) {
        Microsoft::WRL::ComPtr<ID3D11Resource> resource; view->GetResource(resource.GetAddressOf());
        if(!resource || !seen.insert(resource.Get()).second) continue;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        if(SUCCEEDED(resource.As(&texture))) {
            D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d);
            const auto size=CacheTextureBytes(d); bytes+=size; if(!size) ++unknown;
        } else if(SUCCEEDED(resource.As(&buffer))) { D3D11_BUFFER_DESC d{}; buffer->GetDesc(&d); bytes+=d.ByteWidth; }
        else ++unknown;
    }
    const auto attempts=g_cacheHits+g_cacheMisses;
    Log("PERF CACHE: hits="+std::to_string(g_cacheHits)+" misses="+std::to_string(g_cacheMisses)+
        " hitPct="+(attempts?std::to_string(100.0*g_cacheHits/attempts):"NA")+
        " inserts="+std::to_string(g_cacheInserts)+" evictions="+std::to_string(g_cacheEvictions)+
        " clockSteps="+std::to_string(g_cacheClockSteps)+" clockMax="+std::to_string(g_cacheClockMax)+ // BINDCPU
        " buckets="+std::to_string(g_universalNonCubeViews.bucket_count())+ // BINDCPU
        " loadFactor="+std::to_string(g_universalNonCubeViews.load_factor())+ // BINDCPU
        " entries="+std::to_string(g_universalNonCubeViews.size())+" uniqueResources="+std::to_string(seen.size())+
        " referencedLogicalMiB_est="+std::to_string(double(bytes)/1048576.0)+" unknownResources="+std::to_string(unknown));
    g_cacheHits=g_cacheMisses=g_cacheInserts=g_cacheEvictions=0;
    g_cacheClockSteps=0;g_cacheClockMax=0; // BINDCPU
}
