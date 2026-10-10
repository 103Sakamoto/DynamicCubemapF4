#pragma once
// Realistic Reflections interop contract, protocol version 1.
//
// A public, header-only contract between Realistic Reflections renderers
// (Glass, Mirrors, Metal, Water) and any other mod that intercepts Direct3D 11
// bindings, for example a detour of ID3D11DeviceContext::PSSetShaderResources
// that substitutes environment cubes. It depends only on <d3d11.h> and
// <guiddef.h>. Copy it unchanged: the GUIDs and their meanings are permanent,
// and a later protocol only raises the version stored with them.
//
// Both tags are ID3D11DeviceChild private data (SetPrivateData) holding a
// UINT32 protocol version (1). A reader treats the tag as present when the
// view itself or the resource behind it (ID3D11View::GetResource) carries it.
//
// RR_OWNED {36ADDE13-0A44-4968-905B-CBF1599A8108}
//   Set by a Realistic Reflections renderer on every D3D11 resource and view
//   it creates. Meaning: owned by a reflection renderer. A D3D-level
//   interceptor must not substitute such a view (leave the binding as the
//   renderer made it) and must not retain a reference to it after the call
//   in which it saw the view.
//
// RR_PEER_REPLACEMENT {F37115C6-6B4D-46F4-BF4E-09EF00B31E98}
//   Set by another mod on a view it binds in place of an original (authored)
//   cube. Meaning: a stand-in. A reflection renderer that owns the surface
//   being drawn may replace the stand-in with its own image for that draw; it
//   puts the stand-in back afterwards if the slot still holds its image.
//
// What a Realistic Reflections renderer does with a slot that should hold the
// original cube of a surface it owns:
//   - the original, or an RR_OWNED view: its own image is bound as usual;
//   - an RR_PEER_REPLACEMENT view: replaced by its own image once one exists;
//   - any other (untagged) view, or its own bind is intercepted: the peer has
//     taken the surface. The renderer yields it: no substitution, no captures,
//     nothing restored, until the original cube is bound there again.
#include <d3d11.h>
#include <guiddef.h>

namespace RealisticReflections::Interop
{
    inline constexpr UINT32 kProtocolVersion = 1;

    // {36ADDE13-0A44-4968-905B-CBF1599A8108}
    inline constexpr GUID RR_OWNED{ 0x36ADDE13, 0x0A44, 0x4968, { 0x90, 0x5B, 0xCB, 0xF1, 0x59, 0x9A, 0x81, 0x08 } };
    // {F37115C6-6B4D-46F4-BF4E-09EF00B31E98}
    inline constexpr GUID RR_PEER_REPLACEMENT{ 0xF37115C6, 0x6B4D, 0x46F4, { 0xBF, 0x4E, 0x09, 0xEF, 0x00, 0xB3, 0x1E, 0x98 } };

    namespace Detail
    {
        inline bool Tag(ID3D11DeviceChild* object, const GUID& tag) noexcept
        {
            if (!object)
                return false;
            const UINT32 version = kProtocolVersion;
            return SUCCEEDED(object->SetPrivateData(tag, sizeof(version), &version));
        }

        // The object itself carries the tag. A later protocol may store a larger
        // payload; its presence still counts.
        inline bool Carries(ID3D11DeviceChild* object, const GUID& tag) noexcept
        {
            if (!object)
                return false;
            UINT32 version = 0;
            UINT size = sizeof(version);
            const HRESULT result = object->GetPrivateData(tag, &size, &version);
            if (result == DXGI_ERROR_MORE_DATA)
                return true;
            return SUCCEEDED(result) && size >= sizeof(version) && version >= 1;
        }

        // The object, or for a view the resource behind it, carries the tag.
        inline bool ViewOrResource(ID3D11DeviceChild* object, const GUID& tag) noexcept
        {
            if (Carries(object, tag))
                return true;
            if (!object)
                return false;
            ID3D11View* view = nullptr;
            if (FAILED(object->QueryInterface(__uuidof(ID3D11View), reinterpret_cast<void**>(&view))) || !view)
                return false;
            ID3D11Resource* resource = nullptr;
            view->GetResource(&resource);
            view->Release();
            const bool tagged = Carries(resource, tag);
            if (resource)
                resource->Release();
            return tagged;
        }
    }

    // A Realistic Reflections renderer tags what it creates. Tags only this object.
    inline bool TagOwned(ID3D11DeviceChild* object) noexcept { return Detail::Tag(object, RR_OWNED); }
    // Another mod tags the stand-in view (or its resource) it binds in place of an original cube.
    inline bool TagPeerReplacement(ID3D11DeviceChild* object) noexcept { return Detail::Tag(object, RR_PEER_REPLACEMENT); }

    // Checks the object and, for a view, its resource.
    inline bool IsOwned(ID3D11DeviceChild* object) noexcept { return Detail::ViewOrResource(object, RR_OWNED); }
    inline bool IsPeerReplacement(ID3D11DeviceChild* object) noexcept { return Detail::ViewOrResource(object, RR_PEER_REPLACEMENT); }
}
