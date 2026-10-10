#pragma once
// Portable test doubles only. Never add this directory to the plugin include path.
#include "guiddef.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using UINT = unsigned;
using UINT32 = std::uint32_t;
using HRESULT = std::int32_t;
constexpr HRESULT S_OK=0, E_FAIL=-1, DXGI_ERROR_MORE_DATA=-2;
#define SUCCEEDED(hr) ((hr) >= 0)
#define FAILED(hr) ((hr) < 0)
#define __uuidof(type) 0
#define STDMETHODCALLTYPE
enum { D3D11_SRV_DIMENSION_TEXTURE2D, D3D11_SRV_DIMENSION_TEXTURECUBE,
       D3D11_SRV_DIMENSION_TEXTURECUBEARRAY };
constexpr UINT D3D11_RESOURCE_MISC_TEXTURECUBE=1;
constexpr UINT D3D11_BIND_RENDER_TARGET=2, D3D11_BIND_DEPTH_STENCIL=4,
               D3D11_BIND_UNORDERED_ACCESS=8;
struct D3D11_SHADER_RESOURCE_VIEW_DESC {
    int Format=1, ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE;
    struct { UINT NumCubes=1; } TextureCubeArray;
};
struct D3D11_TEXTURE2D_DESC {
    struct { UINT Count=1; } SampleDesc;
    UINT MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE, BindFlags=0;
};
struct ID3D11DeviceChild {
    int refs=1, privateReads=0;
    bool failTag=false;
    std::map<std::string,std::vector<unsigned char>> data;
    virtual ~ID3D11DeviceChild() = default;
    void AddRef() { ++refs; }
    void Release() { assert(refs>0); --refs; }
    virtual HRESULT QueryInterface(int,void** out) { *out=nullptr; return E_FAIL; }
    HRESULT SetPrivateData(const GUID& key,UINT n,const void* value) {
        if(failTag) return E_FAIL;
        const auto* p=static_cast<const unsigned char*>(value);
        data[std::string(reinterpret_cast<const char*>(&key),sizeof(key))]={p,p+n};
        return S_OK;
    }
    HRESULT GetPrivateData(const GUID& key,UINT* n,void* value) {
        ++privateReads;
        auto it=data.find(std::string(reinterpret_cast<const char*>(&key),sizeof(key)));
        if(it==data.end()) return E_FAIL;
        const UINT capacity=*n; *n=static_cast<UINT>(it->second.size());
        if(capacity<*n) return DXGI_ERROR_MORE_DATA;
        std::memcpy(value,it->second.data(),*n); return S_OK;
    }
};
struct ID3D11Resource : ID3D11DeviceChild {};
struct ID3D11Texture2D : ID3D11Resource {
    D3D11_TEXTURE2D_DESC desc;
    void GetDesc(D3D11_TEXTURE2D_DESC* d) { *d=desc; }
};
struct ID3D11View : ID3D11DeviceChild {
    ID3D11Resource* resource;
    explicit ID3D11View(ID3D11Resource* r):resource(r) { if(r)r->AddRef(); }
    ~ID3D11View() override { if(resource)resource->Release(); }
    void GetResource(ID3D11Resource** out) { *out=resource; if(resource)resource->AddRef(); }
    HRESULT QueryInterface(int,void** out) override {
        *out=static_cast<ID3D11View*>(this); AddRef(); return S_OK;
    }
};
struct ID3D11ShaderResourceView : ID3D11View {
    using ID3D11View::ID3D11View;
    D3D11_SHADER_RESOURCE_VIEW_DESC desc;
    void GetDesc(D3D11_SHADER_RESOURCE_VIEW_DESC* d) { *d=desc; }
};
struct ID3D11UnorderedAccessView : ID3D11DeviceChild {};
struct ID3D11Buffer : ID3D11DeviceChild {};
namespace Microsoft::WRL {
template<class T> class ComPtr {
    T* p=nullptr;
public:
    ComPtr()=default;
    ComPtr(T* value):p(value) { if(p)p->AddRef(); }
    ComPtr(const ComPtr& other):ComPtr(other.p) {}
    ComPtr(ComPtr&& other) noexcept:p(other.p) { other.p=nullptr; }
    ~ComPtr() { Reset(); }
    ComPtr& operator=(T* value) { if(value)value->AddRef(); Reset(); p=value; return *this; }
    ComPtr& operator=(const ComPtr& other) { return *this=other.p; }
    T* Get() const { return p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p!=nullptr; }
    void Reset() { if(p)p->Release(); p=nullptr; }
    T** GetAddressOf() { assert(!p); return &p; }
    template<class U> HRESULT As(ComPtr<U>* out) {
        *out=dynamic_cast<U*>(p); return *out ? S_OK : E_FAIL;
    }
};
}
