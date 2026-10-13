// SPDX-License-Identifier: GPL-3.0-only
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <iterator>
#include <iostream>
#include "../Source/generated/EmbeddedShaders.h"
#include "temp/ShaderReference.h"
#include "../Source/src/ImGuiEmbeddedShaders.h"
#undef D3DCompile

int main()
{
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,nullptr,&context))) return 1;
    static_assert(std::size(Reference::Shaders)==std::size(DCF4Shaders::All));
    for (unsigned i=0;i<std::size(Reference::Shaders);++i) {
        const auto& reference=Reference::Shaders[i];
        const auto& embedded=DCF4Shaders::All[i];
        ComPtr<ID3DBlob> bytecode,error;
        const HRESULT compile=D3DCompile(reference.source,strlen(reference.source),
            reference.sourceName,reference.macros,nullptr,reference.entry,reference.target,
            reference.flags,0,&bytecode,&error);
        if (FAILED(compile) || !bytecode) {
            std::cerr<<reference.name<<": reference compilation failed\n";
            return 2;
        }
        if (strcmp(reference.name,embedded.name)!=0 ||
            bytecode->GetBufferSize()!=embedded.code.size ||
            memcmp(bytecode->GetBufferPointer(),embedded.code.data,embedded.code.size)!=0) {
            std::cerr<<reference.name<<": bytecode differs from verified build input\n";
            return 3;
        }
        HRESULT created=E_FAIL;
        if (!strcmp(reference.target,"cs_5_0")) {
            ComPtr<ID3D11ComputeShader> shader;
            created=device->CreateComputeShader(embedded.code.data,embedded.code.size,nullptr,&shader);
            if (SUCCEEDED(created) && !shader) created=E_FAIL;
        } else {
            ComPtr<ID3DBlob> supplied;
            if (FAILED(DCF4EmbeddedImGuiShader(reference.source,strlen(reference.source),nullptr,nullptr,
                nullptr,"main",reference.target,0,0,&supplied,nullptr)) || !supplied ||
                supplied->GetBufferSize()!=embedded.code.size ||
                memcmp(supplied->GetBufferPointer(),embedded.code.data,embedded.code.size)) return 5;
            if (!strcmp(reference.target,"vs_4_0")) {
                ComPtr<ID3D11VertexShader> shader;
                created=device->CreateVertexShader(supplied->GetBufferPointer(),supplied->GetBufferSize(),nullptr,&shader);
            } else {
                ComPtr<ID3D11PixelShader> shader;
                created=device->CreatePixelShader(supplied->GetBufferPointer(),supplied->GetBufferSize(),nullptr,&shader);
            }
        }
        if (FAILED(created)) {
            std::cerr<<reference.name<<": D3D11 rejected embedded bytecode\n";
            return 4;
        }
    }
    ID3DBlob* invalid=nullptr;
    if (DCF4EmbeddedImGuiShader("invalid",7,nullptr,nullptr,nullptr,"main","vs_4_0",0,0,&invalid,nullptr)!=E_INVALIDARG || invalid) return 6;
    if (DCF4EmbeddedImGuiShader(DCF4Shaders::ImGuiVertexSource,sizeof(DCF4Shaders::ImGuiVertexSource)-1,
        nullptr,nullptr,nullptr,"main","vs_4_0",1,0,&invalid,nullptr)!=E_INVALIDARG || invalid) return 7;
    std::cout<<"PASS: all 13 embedded shaders byte-identical to verified build inputs; D3D11 WARP shader creation and guarded ImGui bytecode adapter passed\n";
}
