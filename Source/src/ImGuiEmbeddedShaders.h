// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <d3dcompiler.h>
#include <cstring>
#include "../generated/EmbeddedShaders.h"

// Keep the existing backend and Microsoft blob ownership; only supply bytecode.
inline HRESULT WINAPI DCF4EmbeddedImGuiShader(LPCVOID source, SIZE_T size,
    LPCSTR sourceName, const D3D_SHADER_MACRO* macros, ID3DInclude* include,
    LPCSTR entry, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob** code, ID3DBlob** errors)
{
    if(errors) *errors=nullptr;
    if(!code) return E_POINTER;
    *code=nullptr;
    if(!source || sourceName || macros || include || !entry || !target ||
        strcmp(entry,"main") || flags1 || flags2) return E_INVALIDARG;
    DCF4Shaders::Bytecode embedded{};
    if(!strcmp(target,"vs_4_0") && size==sizeof(DCF4Shaders::ImGuiVertexSource)-1 &&
        !memcmp(source,DCF4Shaders::ImGuiVertexSource,size)) embedded=DCF4Shaders::ImGuiVertex;
    else if(!strcmp(target,"ps_4_0") && size==sizeof(DCF4Shaders::ImGuiPixelSource)-1 &&
        !memcmp(source,DCF4Shaders::ImGuiPixelSource,size)) embedded=DCF4Shaders::ImGuiPixel;
    else return E_INVALIDARG;
    const HRESULT result=D3DCreateBlob(embedded.size,code);
    if(FAILED(result)) return result;
    if(!*code) return E_FAIL;
    memcpy((*code)->GetBufferPointer(),embedded.data,embedded.size);
    return S_OK;
}

#define D3DCompile DCF4EmbeddedImGuiShader
