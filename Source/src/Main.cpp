// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 103Sakamoto
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, version 3. This program is distributed WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE. See LICENSE.txt included with this distribution.
//
// Portions of the cubemap inference and update shader logic are adapted from
// Dynamic Cubemaps in Skyrim Community Shaders by Doodlum and contributors:
// https://github.com/doodlum/skyrim-community-shaders
//
// Universal runtime profiles with guarded AE 1.11.240.0 binary preflight.
#include "PCH.hpp"
#include "RE/B/BSResourceNiBinaryStream.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <detours/detours.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include "ReflectionInterop.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <climits>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <limits>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <utility>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "user32.lib")

#ifdef near
#	undef near
#endif

#ifdef far
#	undef far
#endif

namespace
{
	enum class RuntimeFamily
	{
		unsupported,
		og,
		ng,
		ae
	};

	struct RuntimeProfile
	{
		RuntimeFamily family;
		const char* name;
		std::uint64_t deferredLightsID;
		std::uint64_t deferredDrawAnchorID;
		std::ptrdiff_t deferredDrawAnchorOffset;
		bool enableRendererHooks;
		std::uint64_t shaderManagerVtableID;
	};

	RuntimeProfile g_runtimeProfile{
		RuntimeFamily::unsupported, "UNSUPPORTED", 0, 0, 0, false, 0
	};
	std::string g_runtimeVersion{"unknown"};

	bool SelectRuntimeProfile(const F4SE::LoadInterface* f4se)
	{
		g_runtimeProfile={RuntimeFamily::unsupported,"UNSUPPORTED",0,0,0,false,0};
		g_runtimeVersion = f4se ? f4se->RuntimeVersion().string() : "unknown";
		if (!f4se) return false;
		const auto version = f4se->RuntimeVersion();
		if (version == REL::Version{1,10,163,0}) {
			g_runtimeProfile={RuntimeFamily::og,"OG",1108521,763320,0x9C,true,187043};
			return true;
		}
		if (version == REL::Version{1,10,984,0}) {
			g_runtimeProfile={RuntimeFamily::ng,"NG984",2318312,2276846,0x9A,true,187043};
			return true;
		}
		if (version == REL::Version{1,11,240,0}) {
			g_runtimeProfile={RuntimeFamily::ae,"AE240",2318312,2276846,0x9A,true,187043};
			return true;
		}
		return false;
	}

	constexpr bool g_excludeFirstPersonDepth = true;
	using namespace DirectX;

	constexpr std::uint32_t kMainRT = 3;
	constexpr std::uint32_t kMainDS = 2;

	constexpr std::uint32_t kCubeSize = 128;
	constexpr std::uint32_t kCubeMipCount = 8;
	static_assert((1u << (kCubeMipCount - 1)) == kCubeSize);
	
	constexpr const char* kLogPath =
		"Data/F4SE/Plugins/DynamicCubemapF4.log";
	constexpr const char* kPreviousLogPath = "Data/F4SE/Plugins/DynamicCubemapF4.previous.log";
	constexpr const char* kSettingsPath = "Data/MCM/Settings/DynamicCubemapF4.ini";
	constexpr const char* kDefaultsPath = "Data/MCM/Config/DynamicCubemapF4/settings.ini";

	std::atomic_bool g_gameplayReady{ false };
	std::atomic_bool g_gameplayActivationClaimed{ false };
	std::atomic_bool g_lifecycleProbePending{ false };
	std::atomic_bool g_lifecycleRecoveryConfirmed{ false };
	std::atomic_uint g_lifecycleActiveCellConfirmations{ 0 };
	std::atomic_uint64_t g_lifecycleBlockedCallbacks{ 0 };
	ULONGLONG g_nextLifecycleProbe = 0;
	std::atomic_bool g_accumulating{ false };
	// Capture history persists until replaced by a valid observation or reset.
	std::uint64_t g_previousCompositeSequence = 0;
	std::atomic_bool g_requestedEnabled{true};
    std::atomic_bool g_diagnosticsEnabled{false};
    void SetDiagnosticsEnabled(bool enabled);
	bool g_autoStartPending=false;
	bool g_reprojectEnabled=true;
	bool g_requestedReprojection=true;
    int g_requestedRoughness=2;
    UINT g_materialMip=2;
    constexpr UINT MaterialMipForMode(int mode) { return static_cast<UINT>(mode); }
    static_assert(MaterialMipForMode(0)==0 && MaterialMipForMode(1)==1 && MaterialMipForMode(2)==2 && MaterialMipForMode(3)==3 && MaterialMipForMode(4)==4);
	ULONGLONG g_nextSettingsPoll=0;
	constexpr float g_reprojectMinMove=2.0f;
	constexpr float g_teleportResetDistance=2048.0f;
	XMFLOAT3 g_historyOrigin{};
	bool g_haveHistoryOrigin=false;
	bool g_liveEnabled = false;
	bool g_liveReady = false;
	ID3D11DeviceContext* g_liveContext = nullptr; // engine-owned immediate context
	using PSResources_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11ShaderResourceView* const*);
	using DrawIndexed_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
	using Draw_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
	using DrawIndexedInstanced_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
	using DrawInstanced_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
	PSResources_t g_originalPSResources = nullptr;
	DrawIndexed_t g_originalDrawIndexed = nullptr;
	Draw_t g_originalDraw = nullptr;
	DrawIndexedInstanced_t g_originalDrawIndexedInstanced = nullptr;
	DrawInstanced_t g_originalDrawInstanced = nullptr;
	bool g_liveCubemapSlots[128]{};
	std::uint32_t g_liveCubemapSlotCount = 0;

	void SetLiveCubemapSlot(UINT slot, bool bound);
	Microsoft::WRL::ComPtr<ID3D11Buffer> g_rainNeutralFrameBuffer;
	std::uint64_t g_rainNeutralFrameSequence = 0;
	bool g_loggedRainDrawOverride = false;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> g_liveTexture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_liveSRV;
    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,8> g_liveMaterialViews;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_liveMipSource;
	std::atomic_uint32_t g_callbackCount{ 0 };
	std::atomic_uint32_t g_accumulatedFrames{ 0 };



	ID3D11ComputeShader* g_captureCS = nullptr;
	ID3D11Buffer* g_constants = nullptr;
	ID3D11SamplerState* g_linearSampler = nullptr;

	ID3D11Texture2D* g_colorTexture = nullptr;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> g_skyHistoryTexture;
	Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> g_skyHistoryUAV;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_skyHistorySRV;
	ID3D11Texture2D* g_rawTexture = nullptr;
	ID3D11Texture2D* g_positionTexture = nullptr;

	ID3D11UnorderedAccessView* g_colorUAV = nullptr;
	ID3D11UnorderedAccessView* g_rawUAV = nullptr;
	ID3D11UnorderedAccessView* g_positionUAV = nullptr;

	ID3D11ShaderResourceView* g_colorCubeSRV = nullptr;

	ID3D11Texture2D* g_inferredTexture = nullptr;
	ID3D11ShaderResourceView* g_inferredSRV = nullptr;
	ID3D11UnorderedAccessView* g_inferredUAV = nullptr;

	ID3D11Texture2D* g_defaultCubeTexture = nullptr;
	ID3D11ShaderResourceView* g_defaultCubeSRV = nullptr;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> g_interiorDarkCubeTexture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_interiorDarkCubeSRV;

	ID3D11ComputeShader* g_inferLiveCS = nullptr;
	ID3D11ComputeShader* g_inferLiveBinaryCS = nullptr;
	ID3D11ComputeShader* g_inferLiveBlendCS[7]{};
	bool g_fallbackLockStarted = false;
	std::uint32_t g_fallbackLockStartFrame = 0;
	constexpr std::uint32_t g_fallbackLockFrames = 60;

	ID3D11Texture2D* g_coverageTexture = nullptr;
	ID3D11UnorderedAccessView* g_coverageUAV = nullptr;


	XMFLOAT3 g_previousCameraOrigin{};
	bool g_havePreviousCameraOrigin = false;

	struct alignas(16) CaptureConstants
	{
		XMFLOAT4 viewToWorld[3];
		XMFLOAT4 cameraOrigin;
		XMFLOAT4 cameraPreviousOrigin;
		XMFLOAT4 ndcToViewMul;
		XMFLOAT4 ndcToViewAdd;
		XMFLOAT4X4 invProj;
		XMFLOAT4 colorViewport;
		XMFLOAT4 colorClamp;
		XMFLOAT4 depthViewport;
		XMFLOAT4 historyOffset;
		XMFLOAT4 captureOptions;
	};

	static_assert((sizeof(CaptureConstants) % 16) == 0);

	struct alignas(16) EngineFrameBuffer
	{
		XMFLOAT4 pad0_11[12];
		XMFLOAT4 viewToWorld[3];
		XMFLOAT4 pad15_19[5];
		XMFLOAT4 farReproj[4];
		XMFLOAT4 nearReproj[4];
		XMFLOAT4 pad28_29[2];
		XMFLOAT4 iblDesaturation;
		XMFLOAT4 prevFrameWorldToClip[4];
		XMFLOAT4 cameraPosAdjust;
		XMFLOAT4 cameraPreviousPosAdjust;
		XMFLOAT4 currFrameWorldToClip[4];
		XMFLOAT4 fogDistanceRamp;
		XMFLOAT4 fogNearLowColorAndPower;
		XMFLOAT4 fogNearHighColorAndClamp;
		XMFLOAT4 fogFarLowColorAndDensity;
		XMFLOAT4 fogFarHighColor;
		XMFLOAT4 fogHeightRamp;
	};

	static_assert(sizeof(EngineFrameBuffer) == 752);

	bool g_loggedFrameBuffer = false;
	bool g_loggedProjection = false;

	using Map_t = HRESULT(STDMETHODCALLTYPE*)(
		ID3D11DeviceContext*,
		ID3D11Resource*,
		UINT,
		D3D11_MAP,
		UINT,
		D3D11_MAPPED_SUBRESOURCE*);

	using Unmap_t = void(STDMETHODCALLTYPE*)(
		ID3D11DeviceContext*,
		ID3D11Resource*,
		UINT);

	using DeferredLightsImpl_t = void(*)();

	Map_t g_originalMap = nullptr;
	Unmap_t g_originalUnmap = nullptr;
	DeferredLightsImpl_t g_originalDeferredLightsImpl = nullptr;

	std::atomic<ID3D11Resource*> g_enginePerFrameIdentity{ nullptr };
	const void* g_enginePerFrameMapped = nullptr;

	EngineFrameBuffer g_latestMappedFrame{};
	EngineFrameBuffer g_worldFrame{};
	std::uint64_t g_latestMappedSequence = 0;
	std::uint64_t g_worldFrameSequence = 0;

	bool g_latestMappedValid = false;
	bool g_worldFrameValid = false;
	bool g_insideDeferredLights = false;
	std::uint64_t g_aeDrawVisits=0;
	std::uint64_t g_aeWorldDrawVisits=0;
	bool g_contextHooksInstalled = false;
	bool g_anchorHookInstalled = false;



	// Fallout 4 port adapted from Community Shaders InferCubemapCS.
	// This is the non-REFLECTIONS variant.
	const char* kInferShader = R"(
TextureCube<float4> EnvCaptureTexture : register(t0);
TextureCube<float4> SkyHistoryTexture : register(t1);
TextureCube<float4> DefaultCubemap : register(t2);

RWTexture2DArray<float4> EnvInferredTexture : register(u0);

SamplerState LinearSampler : register(s0);

float3 LinearToIrradiance(float3 color)
{
	return pow(abs(color), 1.0 / 1.6);
}

float3 GetSamplingVector(uint3 threadID)
{
	uint width;
	uint height;
	uint depth;
	EnvInferredTexture.GetDimensions(width, height, depth);

	float2 st =
		(float2(threadID.xy) + 0.5) /
		float2(width, height);

	float2 uv =
		2.0 * float2(st.x, 1.0 - st.y) - 1.0;

	float3 result = 0.0;

	switch (threadID.z)
	{
	case 0: result = float3(1.0, uv.y, -uv.x); break;
	case 1: result = float3(-1.0, uv.y, uv.x); break;
	case 2: result = float3(uv.x, 1.0, -uv.y); break;
	case 3: result = float3(uv.x, -1.0, uv.y); break;
	case 4: result = float3(uv.x, uv.y, 1.0); break;
	case 5: result = float3(-uv.x, uv.y, -1.0); break;
	}

	return normalize(result);
}

[numthreads(8, 8, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
	float3 uv = GetSamplingVector(threadID);

	float4 color =
		EnvCaptureTexture.SampleLevel(
			LinearSampler,
			uv,
			0);

	// Only local mips may fill small reprojection holes. Broad face averages
	// must not turn an unobserved direction into apparently valid coverage.
	float directionalConfidence = color.w;

	float mipLevel = 0.0;

	const float growth = 1.5;
	float brightness = growth;

	while (color.w < 1.0 && mipLevel < 2.0)
	{
		mipLevel++;

		float4 tempColor = EnvCaptureTexture.SampleLevel(
			LinearSampler, uv, mipLevel);
		directionalConfidence = max(directionalConfidence, tempColor.w);

		tempColor *= brightness;
		brightness *= growth;

		if (color.w + tempColor.w > 1.0)
		{
			mipLevel -= color.w;

			float alphaDifference =
				1.0 - color.w;

			tempColor *=
				alphaDifference /
				max(tempColor.w, 0.0001);

			color += tempColor;
			break;
		}

		color += tempColor;
	}

	// History RGB is confidence-weighted. Recover radiance before compositing;
	// local inference can end before its accumulated alpha reaches one.
	color.rgb /= max(color.w, 0.0001);
	// Fallback is display-only: never write it into capture/reprojection history.
	// Smooth weak coverage boundaries; fully covered directions have zero
	// fallback contribution after the existing startup transition completes.
	#ifndef FALLBACK_LOCK_WEIGHT
	#define FALLBACK_LOCK_WEIGHT 0.0
	#endif
	float fadeWeight = saturate(directionalConfidence);
	float validWeight = smoothstep(0.02, 0.25, directionalConfidence);
	float liveWeight = lerp(fadeWeight, validWeight, FALLBACK_LOCK_WEIGHT);
	float3 fallback =
		DefaultCubemap.SampleLevel(LinearSampler, uv, 0.0).rgb;
	// Sky is stored independently from finite geometry. Use it only as a
	// background: full-confidence geometry always wins. Alpha marks observed
	// sky directions; never invent sky in an unobserved direction.
	float4 sky = SkyHistoryTexture.SampleLevel(LinearSampler, uv, 0.0);
	float skyWeight = saturate(sky.a);
	float3 skyRadiance = sky.rgb / max(sky.a, 0.0001);
	fallback = lerp(fallback, skyRadiance, skyWeight);
	color.rgb = lerp(fallback, color.rgb, liveWeight);

#if !defined(LIVE_LINEAR_OUTPUT)
	color.rgb =
		LinearToIrradiance(
			color.rgb);
#endif

	EnvInferredTexture[threadID] =
		max(0.0, color);
}
)";

	// Fallout 4 port adapted from the Community Shaders UpdateCubemapCS capture path.
	// No Community Shaders framework/runtime code is linked or required.
	const char* kCaptureShader = R"(
RWTexture2DArray<float4> DynamicCubemap : register(u0);
RWTexture2DArray<float4> DynamicCubemapRaw : register(u1);
RWTexture2DArray<float4> DynamicCubemapPosition : register(u2);
RWTexture2DArray<uint> CaptureCoverage : register(u3);
RWTexture2DArray<float4> SkyHistory : register(u4);

Texture2D<float> DepthTexture : register(t0);
Texture2D<float4> ColorTexture : register(t1);

SamplerState LinearSampler : register(s0);

cbuffer UpdateData : register(b0)
{
	float4 ViewToWorld[3];
	float4 CameraOrigin;
	float4 CameraPreviousOrigin;
	float4 NDCToViewMul;
	float4 NDCToViewAdd;
	row_major float4x4 InvProj;
	float4 ColorViewport;
	float4 ColorClamp;
	float4 DepthViewport;
	float4 HistoryOffset;
	float4 CaptureOptions;
};

float3 IrradianceToLinear(float3 color)
{
	// Fixed capture calibration (-2 EV); applied once before history accumulation.
	return color * 0.25;
}

float3 GetSamplingVector(uint3 threadID)
{
	uint width;
	uint height;
	uint depth;
	DynamicCubemap.GetDimensions(width, height, depth);

	float2 st =
		(float2(threadID.xy) + 0.5) /
		float2(width, height);

	float2 uv =
		2.0 * float2(st.x, 1.0 - st.y) - 1.0;

	float3 result = 0.0;

	switch (threadID.z)
	{
	case 0: result = float3(1.0, uv.y, -uv.x); break;
	case 1: result = float3(-1.0, uv.y, uv.x); break;
	case 2: result = float3(uv.x, 1.0, -uv.y); break;
	case 3: result = float3(uv.x, -1.0, uv.y); break;
	case 4: result = float3(uv.x, uv.y, 1.0); break;
	case 5: result = float3(-uv.x, uv.y, -1.0); break;
	}

	return normalize(result);
}

float3 WorldToViewDirection(float3 direction)
{
	return float3(
		dot(float3(ViewToWorld[0].x, ViewToWorld[1].x, ViewToWorld[2].x), direction),
		dot(float3(ViewToWorld[0].y, ViewToWorld[1].y, ViewToWorld[2].y), direction),
		dot(float3(ViewToWorld[0].z, ViewToWorld[1].z, ViewToWorld[2].z), direction));
}

float3 ViewToWorldDirection(float3 direction)
{
	return float3(
		dot(ViewToWorld[0].xyz, direction),
		dot(ViewToWorld[1].xyz, direction),
		dot(ViewToWorld[2].xyz, direction));
}

float2 ViewToUV(float3 viewPosition)
{
	return ((viewPosition.xy / viewPosition.z) - NDCToViewAdd.xy) /
		NDCToViewMul.xy;
}

[numthreads(8, 8, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
	// Fallout's environment lookup is antipodal to the reconstructed world ray.
	float3 captureDirection = -GetSamplingVector(threadID);
	float3 viewDirection = WorldToViewDirection(captureDirection);
	float2 logicalUV = ViewToUV(viewDirection);

	bool inFrame =
		all(logicalUV >= 0.0) &&
		all(logicalUV <= 1.0);

	if (inFrame && viewDirection.z > 0.0)
	{
		float2 textureUV = clamp(
			ColorViewport.xy + logicalUV * ColorViewport.zw,
			ColorClamp.xy,
			ColorClamp.zw);

		uint2 depthOrigin = uint2(DepthViewport.xy);
		uint2 activeExtent = max(uint2(DepthViewport.zw), uint2(1, 1));
		uint2 depthPixel = min(
			depthOrigin + uint2(logicalUV * float2(activeExtent)),
			depthOrigin + activeExtent - 1);

		float depth =
			DepthTexture.Load(int3(depthPixel, 0));

		// Some first-person passes write color without a distinguishable DS2 value.
		// Preserve history in the configurable lower-screen band where weapons and
		// hands are rendered; those world directions can update from another view.
		if (CaptureOptions.x > 0.5 && logicalUV.y >= CaptureOptions.y) return;

		// FO4's first-person model occupies the reserved low-depth partition.
		// Color is sampled linearly while depth is loaded at one integer pixel, so
		// reject a one-pixel neighborhood to prevent weapon silhouettes bleeding
		// into otherwise clear sky samples. Rejected texels retain their history.
		if (CaptureOptions.x > 0.5)
		{
			bool nearFirstPerson = false;
			int2 maximumPixel = int2(activeExtent) - 1;
			[unroll]
			for (int y = -1; y <= 1; ++y)
			{
				[unroll]
				for (int x = -1; x <= 1; ++x)
				{
					int2 guardedPixel = clamp(int2(depthPixel) + int2(x, y), int2(0, 0), maximumPixel);
					nearFirstPerson = nearFirstPerson ||
						DepthTexture.Load(int3(guardedPixel, 0)) <= 0.010001;
				}
			}
			if (nearFirstPerson) return;
		}

		// Depth clear value is sky/background; store its direction without a finite position.
		bool isSky = depth >= 0.999999;
		if (isSky)
		{
			float3 skySample =
				ColorTexture.SampleLevel(LinearSampler, textureUV, 0).rgb;

			// At the post-deferred-lighting boundary DS2 can already identify the
			// background while RT3 has not received sky radiance yet. Accepting that
			// unresolved zero creates a viewport-shaped black patch in the cubemap.
			// Reject only effectively-zero samples; genuine dark skies remain valid.
			if (max(skySample.r, max(skySample.g, skySample.b)) <= 0.0001)
				return;

			float4 colorFinal = float4(
				IrradianceToLinear(skySample),
				1.0);
			// Only normal capture writes this layer. Geometry and reprojection
			// never erase it. Replace directly, avoiding temporal sky smearing.
			SkyHistory[threadID] = colorFinal;
			float4 positionFinal = float4(captureDirection, 0.0);
			const float lerpFactor = CaptureOptions.z;
			uint ignoredCoverageValue = 0;
			InterlockedAdd(CaptureCoverage[threadID], 1, ignoredCoverageValue);
			DynamicCubemapPosition[threadID] = positionFinal;
			DynamicCubemapRaw[threadID] = max(0.0, lerp(
				DynamicCubemapRaw[threadID], colorFinal, lerpFactor));
			DynamicCubemap[threadID] = max(0.0, lerp(
				DynamicCubemap[threadID], colorFinal, lerpFactor));
			return;
		}

		bool isWorldPartition =
			depth > 0.01 &&
			!isSky;

		if (isWorldPartition)
		{
			float localDepth =
				(depth - 0.01) / 0.99;

			float2 ndc =
				float2(
					logicalUV.x * 2.0 - 1.0,
					1.0 - logicalUV.y * 2.0);

			float4 positionH =
				mul(
					float4(ndc, localDepth, 1.0),
					InvProj);

			float3 positionView =
				positionH.xyz / positionH.w;

			if (positionView.z > 16.5 && all(isfinite(positionView)))
			{
				float3 position =
					ViewToWorldDirection(positionView);

				float3 color =
					ColorTexture.SampleLevel(
						LinearSampler,
						textureUV,
						0).rgb;

				float4 positionFinal =
					float4(
						position * 0.001 + HistoryOffset.xyz,
						1.0);

				float4 colorFinal =
					float4(
						IrradianceToLinear(color),
						1.0);

				// Keep screen-space history stable while the camera turns. Universal
				// publication consumes this temporally blended result directly on GPU.
				const float lerpFactor = CaptureOptions.z;

				uint ignoredCoverageValue = 0;
				InterlockedAdd(
					CaptureCoverage[threadID],
					1,
					ignoredCoverageValue);

				DynamicCubemapPosition[threadID] = positionFinal;

				DynamicCubemapRaw[threadID] =
					max(
						0.0,
						lerp(
							DynamicCubemapRaw[threadID],
							colorFinal,
							lerpFactor));

				colorFinal *=
					sqrt(
						saturate(
							0.5 *
							length(position)));

				DynamicCubemap[threadID] =
					max(
						0.0,
						lerp(
							DynamicCubemap[threadID],
							colorFinal,
							lerpFactor));

				return;
			}
		}
	}

	// No valid observation: retain color, confidence, position and coverage.
	// Moving away must not erase a direction that is currently off screen.
	// Never-observed texels stay empty and use the existing inference fallback.
	return;
}
)";


	std::mutex g_logMutex;
	const ULONGLONG g_logStart=GetTickCount64();
	void Log(const std::string& text)
	{
		if (!g_diagnosticsEnabled.load(std::memory_order_relaxed) &&
            text.find("FAIL")==std::string::npos && text.find("failed")==std::string::npos &&
            text.find("aborted")==std::string::npos && text.find("unsupported")==std::string::npos) return;
        std::lock_guard lock(g_logMutex);
		std::ofstream log(kLogPath, std::ios::out | std::ios::app);
		log << "[+" << (GetTickCount64()-g_logStart) << "ms] " << text << '\n';
	}

	std::string Ptr(const void* p);

	bool IsReadableSpan(std::uintptr_t address, std::size_t size)
	{
		if (!address || !size || address > std::numeric_limits<std::uintptr_t>::max() - size)
			return false;
		MEMORY_BASIC_INFORMATION info{};
		if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) != sizeof(info) ||
			info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
			return false;
		const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
		return address + size <= end;
	}

	bool IsExecutableAddress(std::uintptr_t address)
	{
		MEMORY_BASIC_INFORMATION info{};
		if (!address || VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) != sizeof(info) ||
			info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
			return false;
		return (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
			PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
	}

	std::string ProbeBytes(std::uintptr_t address, std::size_t count)
	{
		if (!IsReadableSpan(address, count)) return "<unreadable>";
		std::ostringstream out;
		out << std::hex << std::setfill('0');
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(address);
		for (std::size_t i=0; i<count; ++i) {
			if (i) out << ' ';
			out << std::setw(2) << static_cast<unsigned>(bytes[i]);
		}
		return out.str();
	}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 103Sakamoto
// These checks are for the inspected 1.11.240.0 binary only; no signature scanning.
template <std::size_t N>
bool MatchAE240Bytes(std::uintptr_t address, const std::uint8_t (&expected)[N])
{
    if (!IsReadableSpan(address,N)) return false;
    const auto* bytes=reinterpret_cast<const std::uint8_t*>(address);
    return std::equal(expected,expected+N,bytes);
}

bool VerifyAE240Mappings(std::uintptr_t deferred, std::uintptr_t anchor,
    std::uintptr_t callsite, std::uintptr_t shaderManager)
{
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!base || g_runtimeVersion != "1.11.240.0" ||
        deferred != base+0x21F4FD0 || anchor != base+0x1818E80 ||
        callsite != base+0x1818F1A || shaderManager != base+0x291D158) {
        Log("AE240 PREFLIGHT FAILED: executable version or Address Library mappings differ.");
        return false;
    }
    constexpr std::uint8_t deferredBytes[]={
        0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x8D,0xA8,0xC8,0xF9,0xFF,0xFF,0x48,0x81,0xEC,0xF8,0x06,0x00,0x00};
    constexpr std::uint8_t anchorBytes[]={
        0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
        0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x40};
    constexpr std::uint8_t callBytes[]={
        0x33,0xD2,0x33,0xC9,0xE8,0x01,0xB1,0x00,0x00,0xBF,0x18,0x0B,0x00,0x00,0x48,0x03,0xFE};
    constexpr std::uint8_t targetBytes[]={
        0x48,0x89,0x5C,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x83,0xEC,0x30};
    constexpr std::uint8_t loadBytes[]={
        0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x60,0x48,0x8B,0xDA,0x48,0x8D,0x4C};
    constexpr std::uint8_t readBytes[]={
        0x48,0x8B,0x52,0x38,0x48,0x85,0xD2,0x74,0x0C,0x48,0x8D,0x0D,0xA0,0xF7,0x7A,0x01,
        0xE9,0xDB,0x69,0x5C,0xFF,0x32,0xC0,0xC3};
    if (!MatchAE240Bytes(deferred,deferredBytes) || !MatchAE240Bytes(anchor,anchorBytes) ||
        !MatchAE240Bytes(callsite-4,callBytes) || !MatchAE240Bytes(base+0x1824020,targetBytes) ||
        !IsReadableSpan(shaderManager+26*sizeof(std::uintptr_t),3*sizeof(std::uintptr_t))) {
        Log("AE240 PREFLIGHT FAILED: hook bytes differ or memory is unreadable; no hooks installed.");
        return false;
    }
    const auto* table=reinterpret_cast<const std::uintptr_t*>(shaderManager);
    const auto expectedLoad=base+0x22763E0;
    const auto expectedRead=base+0x2276550;
    const auto rendererData=RE::ID::BSGraphics::GetRendererData.address();
    const auto worldRoot=RE::ID::Main::WorldRootNode.address();
    const auto playerSingleton=RE::ID::PlayerCharacter::Singleton.address();
    const auto expectedRendererData=base+0x38E13A0;
    const auto expectedWorldRoot=base+0x32DD338;
    const auto expectedPlayerSingleton=base+0x31EDE50;
    const bool loadAddressMatches=table[26]==expectedLoad;
    const bool readAddressMatches=table[28]==expectedRead;
    const bool loadBytesMatch=MatchAE240Bytes(table[26],loadBytes);
    const bool readBytesMatch=MatchAE240Bytes(table[28],readBytes);
    // Another F4SE plugin may replace this vtable entry before we load. The
    // slot still defines the calling convention, so Detours can safely chain
    // the executable target and return its trampoline as our original.
    const bool loadAlreadyHooked=!loadAddressMatches && IsExecutableAddress(table[26]);
    const bool loadRouteUsable=(loadAddressMatches && loadBytesMatch) || loadAlreadyHooked;
    const bool rendererDataMatches=rendererData==expectedRendererData;
    const bool worldRootMatches=worldRoot==expectedWorldRoot;
    const bool playerSingletonMatches=playerSingleton==expectedPlayerSingleton;
    if (!loadRouteUsable || !readAddressMatches || !readBytesMatch ||
        !rendererDataMatches || !worldRootMatches || !playerSingletonMatches) {
        Log("AE240 PREFLIGHT FAILED: texture hooks or engine global mappings differ; no hooks installed.");
        Log("AE240 CHECK LoadTexture: actual="+Ptr(reinterpret_cast<const void*>(table[26]))+
            " expected="+Ptr(reinterpret_cast<const void*>(expectedLoad))+
            " address="+(loadAddressMatches?"PASS":"FAIL")+
            " bytes="+(loadBytesMatch?"PASS":"FAIL")+
            " route="+(loadAlreadyHooked?"CHAIN":"VANILLA")+
            " actualBytes="+ProbeBytes(table[26],sizeof(loadBytes)));
        Log("AE240 CHECK ReadStreamingTextureData: actual="+Ptr(reinterpret_cast<const void*>(table[28]))+
            " expected="+Ptr(reinterpret_cast<const void*>(expectedRead))+
            " address="+(readAddressMatches?"PASS":"FAIL")+
            " bytes="+(readBytesMatch?"PASS":"FAIL")+
            " actualBytes="+ProbeBytes(table[28],sizeof(readBytes)));
        Log("AE240 CHECK GetRendererData: actual="+Ptr(reinterpret_cast<const void*>(rendererData))+
            " expected="+Ptr(reinterpret_cast<const void*>(expectedRendererData))+
            " result="+(rendererDataMatches?"PASS":"FAIL"));
        Log("AE240 CHECK WorldRootNode: actual="+Ptr(reinterpret_cast<const void*>(worldRoot))+
            " expected="+Ptr(reinterpret_cast<const void*>(expectedWorldRoot))+
            " result="+(worldRootMatches?"PASS":"FAIL"));
        Log("AE240 CHECK PlayerCharacter: actual="+Ptr(reinterpret_cast<const void*>(playerSingleton))+
            " expected="+Ptr(reinterpret_cast<const void*>(expectedPlayerSingleton))+
            " result="+(playerSingletonMatches?"PASS":"FAIL"));
        return false;
    }
    if (loadAlreadyHooked) {
        Log("AE240 COMPAT: LoadTexture vtable entry was already hooked; chaining existing executable target.");
        Log("AE240 CHAIN LoadTexture: actual="+Ptr(reinterpret_cast<const void*>(table[26]))+
            " vanilla="+Ptr(reinterpret_cast<const void*>(expectedLoad))+
            " bytes="+ProbeBytes(table[26],sizeof(loadBytes)));
    }
    Log("AE240 PREFLIGHT PASS: hook sites match.");
    return true;
}
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 103Sakamoto
// Runtime preflight checks verified against the NG 1.10.984 executable in memory.
template <std::size_t N>
bool MatchNG984Bytes(std::uintptr_t address, const std::uint8_t (&expected)[N])
{
    if (!IsReadableSpan(address,N)) return false;
    const auto* bytes=reinterpret_cast<const std::uint8_t*>(address);
    return std::equal(expected,expected+N,bytes);
}

bool VerifyNG984Mappings(std::uintptr_t deferred, std::uintptr_t anchor,
    std::uintptr_t callsite, std::uintptr_t shaderManager)
{
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!base || g_runtimeVersion!="1.10.984.0" ||
        deferred!=base+0x2097E30 || anchor!=base+0x16FDBA0 ||
        callsite!=base+0x16FDC3A || shaderManager!=base+0x2706208) {
        Log("NG984 PREFLIGHT FAILED: executable version or Address Library mappings differ.");
        return false;
    }
    constexpr std::uint8_t deferredBytes[]={
        0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x8D,0xA8,0xC8,0xF9,0xFF,0xFF,0x48,0x81,0xEC,0xF8,0x06,0x00,0x00};
    constexpr std::uint8_t anchorBytes[]={
        0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
        0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x40};
    constexpr std::uint8_t targetBytes[]={
        0x48,0x89,0x5C,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x83,0xEC,0x30};
    constexpr std::uint8_t readBytes[]={
        0x48,0x8B,0x52,0x38,0x48,0x85,0xD2,0x74,0x0C,0x48,0x8D,0x0D,0xB0,0x01,0x65,0x01,
        0xE9,0xCB,0x87,0x60,0xFF,0x32,0xC0,0xC3};
    if (!MatchNG984Bytes(deferred,deferredBytes) || !MatchNG984Bytes(anchor,anchorBytes) ||
        !IsReadableSpan(callsite,5) || *reinterpret_cast<const std::uint8_t*>(callsite)!=0xE8 ||
        !IsReadableSpan(shaderManager+26*sizeof(std::uintptr_t),3*sizeof(std::uintptr_t))) {
        Log("NG984 PREFLIGHT FAILED: hook bytes differ or memory is unreadable; no hooks installed.");
        return false;
    }
    const auto displacement=*reinterpret_cast<const std::int32_t*>(callsite+1);
    const auto target=callsite+5+static_cast<std::intptr_t>(displacement);
    const auto* table=reinterpret_cast<const std::uintptr_t*>(shaderManager);
    const auto rendererData=RE::ID::BSGraphics::GetRendererData.address();
    const auto worldRoot=RE::ID::Main::WorldRootNode.address();
    const auto player=RE::ID::PlayerCharacter::Singleton.address();
    if (target!=base+0x1708D50 || !MatchNG984Bytes(target,targetBytes) ||
        !IsExecutableAddress(table[26]) || table[28]!=base+0x2119440 ||
        !MatchNG984Bytes(table[28],readBytes) || rendererData!=base+0x3624CA0 ||
        worldRoot!=base+0x303AC68 || player!=base+0x2F6B8F0) {
        Log("NG984 PREFLIGHT FAILED: draw target, texture routes, or engine globals differ; no hooks installed.");
        return false;
    }
    Log("NG984 BINARY PREFLIGHT PASS: runtime mappings and hook targets match.");
    Log("NG984 COMPAT: LoadTexture uses verified executable route "+
        Ptr(reinterpret_cast<const void*>(table[26]))+"; Detours chaining enabled.");
    return true;
}

	bool ProbeRuntimeMappings()
	{
		if (!g_runtimeProfile.enableRendererHooks || !g_runtimeProfile.deferredLightsID ||
			!g_runtimeProfile.deferredDrawAnchorID || !g_runtimeProfile.shaderManagerVtableID) {
			Log("COMPAT: incomplete or unverified runtime profile; no relocation lookup attempted.");
			return false;
		}
		Log("COMPAT: runtime=" + std::string(g_runtimeProfile.name) +
			" version=" + g_runtimeVersion +
			" rendererHooks=" + (g_runtimeProfile.enableRendererHooks ? "ENABLED" : "DISABLED"));

		const auto deferred = REL::Relocation<std::uintptr_t>{
			REL::ID{g_runtimeProfile.deferredLightsID}}.address();
		const auto anchor = REL::Relocation<std::uintptr_t>{
			REL::ID{g_runtimeProfile.deferredDrawAnchorID}}.address();
		const auto callsite = anchor ? anchor + g_runtimeProfile.deferredDrawAnchorOffset : 0;
		const auto shaderManager = REL::ID{g_runtimeProfile.shaderManagerVtableID}.address();
		if (g_diagnosticsEnabled && g_runtimeProfile.family == RuntimeFamily::ae &&
			!VerifyAE240Mappings(deferred,anchor,callsite,shaderManager)) return false;
		if (g_runtimeProfile.family == RuntimeFamily::ng &&
			!VerifyNG984Mappings(deferred,anchor,callsite,shaderManager)) return false;

		Log("COMPAT: DeferredLights address=" + Ptr(reinterpret_cast<const void*>(deferred)) +
			" bytes=" + ProbeBytes(deferred,24));
		Log("COMPAT: DrawAnchor address=" + Ptr(reinterpret_cast<const void*>(anchor)) +
			" bytes=" + ProbeBytes(anchor,24));
		Log("COMPAT: DrawCallsite address=" + Ptr(reinterpret_cast<const void*>(callsite)) +
			" bytes=" + ProbeBytes(callsite,16));
		Log("COMPAT: BSShaderResourceManager vtable=" +
			Ptr(reinterpret_cast<const void*>(shaderManager)));

		bool valid = IsExecutableAddress(deferred) && IsExecutableAddress(anchor) &&
			IsReadableSpan(callsite,5) && *reinterpret_cast<const std::uint8_t*>(callsite)==0xE8 &&
			IsReadableSpan(shaderManager + 26*sizeof(std::uintptr_t),3*sizeof(std::uintptr_t));
		if (valid) {
			const auto displacement=*reinterpret_cast<const std::int32_t*>(callsite+1);
			const auto target=callsite+5+static_cast<std::intptr_t>(displacement);
			const auto* table=reinterpret_cast<const std::uintptr_t*>(shaderManager);
			valid=IsExecutableAddress(target) && IsExecutableAddress(table[26]) &&
				IsExecutableAddress(table[28]);
			Log("COMPAT: DrawCall target=" + Ptr(reinterpret_cast<const void*>(target)) +
				" bytes=" + ProbeBytes(target,24));
			Log("COMPAT: texture slots 26/28=" + Ptr(reinterpret_cast<const void*>(table[26])) +
				" / " + Ptr(reinterpret_cast<const void*>(table[28])));
		}
		Log(valid ? "COMPAT PREFLIGHT: candidate mappings structurally valid." :
			"COMPAT PREFLIGHT: candidate mappings rejected; renderer hooks remain disabled.");
		return valid;
	}

using Microsoft::WRL::ComPtr;

const char* kReprojectShader=R"(
Texture2DArray<float4> SourceColor : register(t0);
Texture2DArray<float4> SourcePosition : register(t1);
RWTexture2DArray<uint> NearestDepth : register(u0);
RWTexture2DArray<uint> NearestOwner : register(u1);
RWTexture2DArray<float4> OutRaw : register(u2);
RWTexture2DArray<float4> OutPosition : register(u3);
RWTexture2DArray<float4> OutColor : register(u4);
RWTexture2DArray<uint> OutCoverage : register(u5);
cbuffer ReprojectData : register(b0) { float4 Delta; }; // scaled world delta, .w = cube size

uint LinearIndex(uint3 p) { uint n=(uint)Delta.w; return (p.z*n+p.y)*n+p.x; }
uint3 FromIndex(uint i) { uint n=(uint)Delta.w; return uint3(i%n,(i/n)%n,i/(n*n)); }

// Inverse of GetSamplingVector. Lookup direction is NEGATIVE world ray.
uint3 Project(float3 d)
{
    float3 a=abs(d); float2 uv; uint face;
    if (a.x>=a.y && a.x>=a.z) {
        face=d.x>=0?0:1; uv=float2(d.x>=0?-d.z:d.z,d.y)/a.x;
    } else if (a.y>=a.z) {
        face=d.y>=0?2:3; uv=float2(d.x,d.y>=0?-d.z:d.z)/a.y;
    } else {
        face=d.z>=0?4:5; uv=float2(d.z>=0?d.x:-d.x,d.y)/a.z;
    }
    float2 st=float2(uv.x+1,1-uv.y)*0.5;
    return uint3(min((uint2)(saturate(st)*Delta.w),(uint)Delta.w-1),face);
}

bool Reproject(uint3 source,out uint3 target,out uint depth)
{
    target=source; depth=0;
    float4 c=SourceColor.Load(int4(source,0));
    float4 p=SourcePosition.Load(int4(source,0));
    if (c.a<=0.0001 || !all(isfinite(p)) || !all(isfinite(c))) return false;
    if (p.w<0.5) return false; // sky is retained separately, never reprojected
    float3 moved=p.xyz-Delta.xyz;
    float distance=length(moved);
    if (distance<0.0165 || !isfinite(distance)) return false;
    target=Project(-moved); depth=asuint(distance);
    return true;
}

// Separate dispatches prevent a race between depth ownership and color writes.
[numthreads(8,8,1)]
void DepthPass(uint3 id:SV_DispatchThreadID)
{
    uint3 target; uint depth;
    if (Reproject(id,target,depth)) InterlockedMin(NearestDepth[target],depth);
}
[numthreads(8,8,1)]
void OwnerPass(uint3 id:SV_DispatchThreadID)
{
    uint3 target; uint depth;
    if (Reproject(id,target,depth) && NearestDepth[target]==depth)
        InterlockedMin(NearestOwner[target],LinearIndex(id));
}
[numthreads(8,8,1)]
void ResolvePass(uint3 id:SV_DispatchThreadID)
{
    uint owner=NearestOwner[id];
    // Do not dilate history: repeated dilation would grow the distant entrance.
    // Disocclusion holes remain unobserved and use directional fallback inference.
    if (owner==0xffffffff) {
        OutRaw[id]=0; OutColor[id]=0; OutPosition[id]=0; OutCoverage[id]=0; return;
    }
    uint3 source=FromIndex(owner);
    float4 c=SourceColor.Load(int4(source,0));
    float4 p=SourcePosition.Load(int4(source,0));
    if (p.w>=0.5) p.xyz-=Delta.xyz;
    OutRaw[id]=c; OutColor[id]=c; OutPosition[id]=p; OutCoverage[id]=1;
}
)";

ComPtr<ID3D11ComputeShader> g_reprojectCS[3];
ComPtr<ID3D11Texture2D> g_reprojectSource[2], g_reprojectWinners[2];
ComPtr<ID3D11ShaderResourceView> g_reprojectSourceViews[2];
ComPtr<ID3D11UnorderedAccessView> g_reprojectWinnerViews[2];
ComPtr<ID3D11Buffer> g_reprojectConstants;
bool g_reprojectFailed=false;

bool CreateReprojectResources(ID3D11Device* device)
{
    if (g_reprojectConstants) return true;
    const char* entries[3]={"DepthPass","OwnerPass","ResolvePass"};
    for (unsigned i=0;i<3;++i) {
        ComPtr<ID3DBlob> code,error;
        const HRESULT hr=D3DCompile(kReprojectShader,strlen(kReprojectShader),"Reprojection",
            nullptr,nullptr,entries[i],"cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,code.GetAddressOf(),error.GetAddressOf());
        if (FAILED(hr)) {
            Log(std::string("REPROJECT shader failed: ")+entries[i]+(error ?
                std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):""));
            return false;
        }
        if (FAILED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,
            g_reprojectCS[i].ReleaseAndGetAddressOf()))) return false;
    }
    D3D11_TEXTURE2D_DESC d{};
    d.Width=kCubeSize; d.Height=kCubeSize; d.MipLevels=1; d.ArraySize=6;
    d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DEFAULT;
    d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    for (unsigned i=0;i<2;++i) {
        d.Format=i==0 ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
        if (FAILED(device->CreateTexture2D(&d,nullptr,g_reprojectSource[i].ReleaseAndGetAddressOf()))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC s{};
        s.Format=d.Format; s.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        s.Texture2DArray.MipLevels=1; s.Texture2DArray.ArraySize=6;
        if (FAILED(device->CreateShaderResourceView(g_reprojectSource[i].Get(),&s,
            g_reprojectSourceViews[i].ReleaseAndGetAddressOf()))) return false;
    }
    d.Format=DXGI_FORMAT_R32_UINT; d.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    for (unsigned i=0;i<2;++i) {
        if (FAILED(device->CreateTexture2D(&d,nullptr,g_reprojectWinners[i].ReleaseAndGetAddressOf()))) return false;
        D3D11_UNORDERED_ACCESS_VIEW_DESC u{};
        u.Format=d.Format; u.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2DARRAY; u.Texture2DArray.ArraySize=6;
        if (FAILED(device->CreateUnorderedAccessView(g_reprojectWinners[i].Get(),&u,
            g_reprojectWinnerViews[i].ReleaseAndGetAddressOf()))) return false;
    }
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth=16; cb.Usage=D3D11_USAGE_DEFAULT; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&cb,nullptr,g_reprojectConstants.ReleaseAndGetAddressOf()))) return false;
    Log("REPROJECT: resources ready.");
    return true;
}

bool ReprojectHistory(ID3D11Device* device,ID3D11DeviceContext* context,const XMFLOAT3& delta)
{
    if (!CreateReprojectResources(device)) return false;
    // Preserve all compute bindings touched below, including class instances.
    ID3D11ComputeShader* oldShader=nullptr; ID3D11ClassInstance* instances[256]{}; UINT instanceCount=256;
    ID3D11ShaderResourceView* oldViews[2]{};
    ID3D11UnorderedAccessView* oldUAV[6]{}; ID3D11Buffer* oldCB=nullptr;
    context->CSGetShader(&oldShader,instances,&instanceCount);
    context->CSGetShaderResources(0,2,oldViews); context->CSGetUnorderedAccessViews(0,6,oldUAV);
    context->CSGetConstantBuffers(0,1,&oldCB);
    ID3D11ShaderResourceView* nullViews[2]{}; ID3D11UnorderedAccessView* nullUAV[6]{};
    context->CSSetShaderResources(0,2,nullViews); context->CSSetUnorderedAccessViews(0,6,nullUAV,nullptr);
    context->CopyResource(g_reprojectSource[0].Get(),g_rawTexture);
    context->CopyResource(g_reprojectSource[1].Get(),g_positionTexture);
    const UINT clear[4]={0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu};
    for (auto& view:g_reprojectWinnerViews) context->ClearUnorderedAccessViewUint(view.Get(),clear);
    const XMFLOAT4 data(delta.x*0.001f,delta.y*0.001f,delta.z*0.001f,static_cast<float>(kCubeSize));
    context->UpdateSubresource(g_reprojectConstants.Get(),0,nullptr,&data,0,0);
    ID3D11ShaderResourceView* views[2]={g_reprojectSourceViews[0].Get(),g_reprojectSourceViews[1].Get()};
    ID3D11UnorderedAccessView* uavs[6]={g_reprojectWinnerViews[0].Get(),g_reprojectWinnerViews[1].Get(),
        g_rawUAV,g_positionUAV,g_colorUAV,g_coverageUAV};
    auto* cb=g_reprojectConstants.Get();
    context->CSSetShaderResources(0,2,views); context->CSSetConstantBuffers(0,1,&cb);
    context->CSSetUnorderedAccessViews(0,2,uavs,nullptr);
    for (unsigned pass=0;pass<3;++pass) {
        if (pass==2) context->CSSetUnorderedAccessViews(0,6,uavs,nullptr);
        context->CSSetShader(g_reprojectCS[pass].Get(),nullptr,0);
        context->Dispatch(kCubeSize/8,kCubeSize/8,6);
    }
    context->CSSetShaderResources(0,2,nullViews); context->CSSetUnorderedAccessViews(0,6,nullUAV,nullptr);
    context->CSSetShader(oldShader,instances,instanceCount);
    context->CSSetConstantBuffers(0,1,&oldCB);
    context->CSSetUnorderedAccessViews(0,6,oldUAV,nullptr); context->CSSetShaderResources(0,2,oldViews);
    if (oldShader) oldShader->Release(); if (oldCB) oldCB->Release();
    for (UINT i=0;i<instanceCount;++i) if (instances[i]) instances[i]->Release();
    for (auto* view:oldViews) if (view) view->Release();
    for (auto* view:oldUAV) if (view) view->Release();
    return true;
}
using Microsoft::WRL::ComPtr;

	int ReadMCMInt(const char* key, int fallback)
	{
		const auto defaults=std::filesystem::absolute(kDefaultsPath).string();
		const auto user=std::filesystem::absolute(kSettingsPath).string();
		const int base=static_cast<int>(GetPrivateProfileIntA("General",key,fallback,defaults.c_str()));
		return static_cast<int>(GetPrivateProfileIntA("General",key,base,user.c_str()));
	}

	void LoadSettings()
	{
		const bool enabled=ReadMCMInt("bEnabled",1)!=0;
		if (enabled!=g_requestedEnabled) g_autoStartPending=true;
		g_requestedEnabled=enabled;
        SetDiagnosticsEnabled(enabled && ReadMCMInt("bDiagnostics",0)!=0);
		g_requestedReprojection=ReadMCMInt("bReprojection",1)!=0;
        g_requestedRoughness=std::clamp(ReadMCMInt("iSharpnessMode",2),0,4);
	}

	std::string Ptr(const void* p)
	{
		char buffer[32]{};
		sprintf_s(buffer, "%p", p);
		return buffer;
	}

	RE::NiCamera* FindWorldNiCamera()
	{
		auto* sceneGraph =
			RE::Main::GetWorldRootNode();

		if (!sceneGraph ||
			!sceneGraph->camera) {
			return nullptr;
		}

		return sceneGraph->camera.get();
	}


	bool HasUsableEngineFrameBuffer(const EngineFrameBuffer& frame);

	void TintAnalysisTick(ID3D11Device* device);
	void ResetTintAnalysis();
// All operations use the same immediate-context render thread as the existing hook.
struct UniversalStyleConstants
{
	std::array<XMFLOAT4,128> tint{};
	std::array<XMFLOAT4,128> intensity{};
};
struct UniversalPool
{
    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,8> materialViews;
    UINT cubes = 0;
    int exteriorMatchState=-4;
	bool standalone=false;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> originalView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    std::array<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>, 8> mipUAVs;
	std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 8> originalMipSRVs;
	UniversalStyleConstants style{}; // original hue/saturation plus bounded luminance intensity
	Microsoft::WRL::ComPtr<ID3D11Buffer> tintConstants;
	bool tintDirty=true;
    bool ready = false; // Published contents passed initial analysis; pending style edits do not invalidate them.
    bool failed = false;
};
struct UniversalEntry
{
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> original;
    UINT pool = UINT_MAX;
	bool directCube=false;
	bool analyzed=false;
	bool keepOriginal=false;
	bool loggedStyledBinding=false;
    bool exteriorBrightness=false;
};
std::unordered_map<ID3D11ShaderResourceView*, UniversalEntry> g_universalViews;
struct NonCubeCacheEntry {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    std::size_t slot;
    ID3D11ShaderResourceView* operator->() const { return view.Get(); }
};
std::unordered_map<ID3D11ShaderResourceView*, NonCubeCacheEntry> g_universalNonCubeViews;
std::array<ID3D11ShaderResourceView*,1024> g_nonCubeClock{};
std::array<bool,1024> g_nonCubeRecent{};
std::size_t g_nonCubeHand=0;
std::vector<UniversalPool> g_universalPools;
std::unordered_map<ID3D11ShaderResourceView*, ID3D11ShaderResourceView*> g_materialViewLookup;
std::uint64_t g_cacheHits=0,g_cacheMisses=0,g_cacheInserts=0,g_cacheEvictions=0;
struct BindingResult { ID3D11ShaderResourceView* view; bool replacement; };
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_universalSlots[128];
Microsoft::WRL::ComPtr<ID3D11ComputeShader> g_universalReplicateCS;
std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 8> g_universalLiveMipSRVs;
bool g_universalComputeFailed = false;
UINT g_universalTotalCubes = 0;
bool UniversalFloatSampleFormat(DXGI_FORMAT f)
{
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM:
    case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB:
        return true;
    default: return false;
    }
}

void ResetNonCubeCache()
{
    g_nonCubeClock.fill(nullptr); g_nonCubeRecent.fill(false); g_nonCubeHand=0;
    g_universalNonCubeViews.clear();
}
void CacheNonCubeView(ID3D11ShaderResourceView* view)
{
    // Only called for a newly classified ordinary view on the render thread.
    while(g_nonCubeClock[g_nonCubeHand] && g_nonCubeRecent[g_nonCubeHand]) {
        g_nonCubeRecent[g_nonCubeHand]=false;
        g_nonCubeHand=(g_nonCubeHand+1)%g_nonCubeClock.size();
    }
    const auto slot=g_nonCubeHand;
    if(auto* old=g_nonCubeClock[slot]) { if(g_diagnosticsEnabled) ++g_cacheEvictions; g_universalNonCubeViews.erase(old); }
    if(g_diagnosticsEnabled) ++g_cacheInserts;
    g_universalNonCubeViews.emplace(view,NonCubeCacheEntry{view,slot});
    g_nonCubeClock[slot]=view; g_nonCubeRecent[slot]=true;
    g_nonCubeHand=(slot+1)%g_nonCubeClock.size();
}

// Rebuilt only when owned views change, never for ordinary texture bindings.
// Keys and values are borrowed from the live/pool ComPtrs and their mip caches.
void RebuildMaterialViewLookup()
{
    g_materialViewLookup.clear();
    const auto add=[](ID3D11ShaderResourceView* view, ID3D11ShaderResourceView* current) {
        if (view && current) g_materialViewLookup.emplace(view,current);
    };
    add(g_liveSRV.Get(),g_liveSRV.Get());
    for (const auto& pool:g_universalPools) add(pool.view.Get(),pool.view.Get());
    for (const auto& view:g_liveMaterialViews) add(view.Get(),g_liveSRV.Get());
    for (const auto& pool:g_universalPools)
        for (const auto& view:pool.materialViews) add(view.Get(),pool.view.Get());
}
ID3D11ShaderResourceView* CurrentMaterialView(ID3D11ShaderResourceView* view)
{
    if (!view) return nullptr;
    const auto found=g_materialViewLookup.find(view);
    return found==g_materialViewLookup.end() ? nullptr : found->second;
}
bool UniversalIsOurs(ID3D11ShaderResourceView* view)
{
    return CurrentMaterialView(view)!=nullptr;
}

bool g_namedReferenceReady=false;
std::unordered_set<ID3D11ShaderResourceView*> g_directAnalysisLogged;

void TagReflectionReplacement(ID3D11DeviceChild* resource)
{
    if (RealisticReflections::Interop::TagPeerReplacement(resource)) return;
    static bool loggedFailure=false;
    if (!loggedFailure) {
        Log("INTEROP: replacement ownership tag failed; peer recognition unavailable for this resource.");
        loggedFailure=true;
    }
}

bool IsExteriorFallbackView(ID3D11ShaderResourceView* view);

BindingResult UniversalBind(UINT slot, ID3D11ShaderResourceView* view)
{
    if (auto* selected=CurrentMaterialView(view)) {
        if (selected!=view) {
            static bool loggedRetained=false;
            if (!loggedRetained) { Log("MIP: retained replacement view redirected to current softness."); loggedRetained=true; }
        }
        return {selected,true};
    }
    if (!view) { g_universalSlots[slot].Reset(); return {view,false}; }
    if (const auto cached=g_universalNonCubeViews.find(view); cached != g_universalNonCubeViews.end()) {
        g_nonCubeRecent[cached->second.slot]=true;
        if(g_diagnosticsEnabled) ++g_cacheHits;
        g_universalSlots[slot].Reset();
        return {view,false};
    }
    if(g_diagnosticsEnabled) ++g_cacheMisses;
    auto found = g_universalViews.find(view);
    if (found != g_universalViews.end()) {
        g_universalSlots[slot] = view;
        if (!g_liveReady || !g_namedReferenceReady) return {view,false};
        if (g_universalComputeFailed) return {view,false};
		if (found->second.directCube)
		{
			if (!found->second.analyzed && IsExteriorFallbackView(view)) {
                // The named reference is probed separately; it needs no material tint.
                found->second.analyzed=true;
                found->second.keepOriginal=false;
                found->second.exteriorBrightness=false;
                Log("TINT BIND: named standalone exterior uses calibrated live cube; no output scaling.");
            }
            if (!found->second.analyzed || found->second.keepOriginal) return {view,false};
			if (found->second.pool!=UINT_MAX && found->second.pool<g_universalPools.size()) {
				const auto& pool=g_universalPools[found->second.pool];
				if (pool.failed) return {view,false};
				if (pool.ready) {
					if (!found->second.loggedStyledBinding) {
						Log("TINT BIND: standalone pool="+
							std::to_string(found->second.pool)+" scale="+
							std::to_string(pool.style.intensity[0].x));
						found->second.loggedStyledBinding=true;
					}
					return {pool.view.Get(),true};
				}
                return {view,false};
			}
            // A recognized exterior requires its intensity-scaled pool. Keep the
            // authored cube if capacity prevented allocating one.
			return found->second.exteriorBrightness ? BindingResult{view,false} : BindingResult{g_liveSRV.Get(),true};
		}
        if (found->second.pool != UINT_MAX) {
            const auto& pool = g_universalPools[found->second.pool];
            if (pool.ready && pool.exteriorMatchState!=-4 && pool.exteriorMatchState!=-3) return {pool.view.Get(),true};
        }
        return {view,false};
    }
    // Ownership tags must be set before the first bind and remain immutable.
    // Peer views stay uncached, so this check runs on each of their binds.
    if (RealisticReflections::Interop::IsOwned(view)) {
        // Preserve the authored view across a peer's temporary draw override.
        // When the peer restores our stand-in, UniversalIsOurs returns early;
        // clearing this slot here would make a later repair restore nullptr.
        // Never store the peer view itself.
        static bool loggedPeer=false;
        if (!loggedPeer) {
            Log("INTEROP: RR-owned view observed; binding preserved.");
            loggedPeer=true;
        }
        return {view,false};
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{}; view->GetDesc(&viewDesc);
    const bool isCube = viewDesc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBE;
    const bool isArray = viewDesc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
    if (!isCube && !isArray) {
        CacheNonCubeView(view);
        g_universalSlots[slot].Reset();
        return {view,false};
    }
    // Apply the same admission limit to both cube layouts. Existing entries
    // were handled above; rejected views remain original and untracked.
    if (g_universalViews.size() >= 4096 || g_universalComputeFailed) {
        g_universalSlots[slot].Reset();
        return {view,false};
    }
    // Standalone TextureCube users (notably BGEM glass and some lenses) can
    // consume the published cube directly. Register them as entries so the
    // tint analyzer can preserve saturated, inconsistent authored cubemaps.
    if (isCube) {
        g_universalSlots[slot] = view;
		UniversalEntry entry; entry.original=view; entry.directCube=true;
		g_universalViews.emplace(view,entry);
        static bool loggedDirectCube = false;
        if (!loggedDirectCube) {
            Log("UNIVERSAL: standalone TextureCube bindings use the live cube directly.");
            loggedDirectCube = true;
        }
        return {view,false};
    }
    {
        // This cache contains cube views only. New views remain vanilla if the
        // generous cap is ever reached, avoiding mid-draw eviction flashes.
        UniversalEntry entry; entry.original = view;
        const auto& v = viewDesc;
        if (UniversalFloatSampleFormat(v.Format)) {
            Microsoft::WRL::ComPtr<ID3D11Resource> resource;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            view->GetResource(resource.GetAddressOf());
            if (resource && SUCCEEDED(resource.As(&texture))) {
                D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d);
                // Atlas order is session-dependent. TintAnalysis locates a
                // unique sampled-content match to the named exterior texture.
                // Avoid depth, writable engine resources, and our capture/history outputs.
                if (d.SampleDesc.Count == 1 && (d.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) &&
                    !(d.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_UNORDERED_ACCESS))) {
                    if (isArray) {
                        const UINT cubes = v.TextureCubeArray.NumCubes;
                        // 128 total cubes is approximately 128 MiB at 128px RGBA16F with mips.
                        if (entry.pool == UINT_MAX && cubes > 0 && cubes <= 128 - g_universalTotalCubes) {
                            entry.pool = static_cast<UINT>(g_universalPools.size());
                            UniversalPool pool; pool.cubes = cubes; pool.originalView=view;
							for (auto& tint:pool.style.tint) tint=XMFLOAT4(1,1,1,-1);
							for (auto& intensity:pool.style.intensity) intensity=XMFLOAT4(1,0,0,0);
                            g_universalPools.push_back(std::move(pool));
                            g_universalTotalCubes += cubes;
                        }
                    }
                }
            }
        }
        found = g_universalViews.emplace(view, entry).first;
    }
    g_universalSlots[slot] = view;
    if (!g_liveReady || !g_namedReferenceReady) return {view,false};
    if (found->second.pool != UINT_MAX) {
        const auto& pool = g_universalPools[found->second.pool];
        if (pool.ready && pool.exteriorMatchState!=-4 && pool.exteriorMatchState!=-3) return {pool.view.Get(),true};
    }
    return {view,false};
}

void UniversalRepair(bool restore)
{
    if (!g_liveContext || !g_originalPSResources) return;
    ID3D11ShaderResourceView* views[128]{};
    g_liveContext->PSGetShaderResources(0, 128, views);
    for (UINT i = 0; i < 128; ++i) {
        auto* original = UniversalIsOurs(views[i]) ? g_universalSlots[i].Get() : views[i];
        const auto binding = (!restore && g_liveEnabled && g_liveReady) ? UniversalBind(i, original) : BindingResult{original,false};
        auto* next=binding.view;
        if (next != views[i]) g_originalPSResources(g_liveContext, i, 1, &next);
		SetLiveCubemapSlot(i, binding.replacement);
        if (views[i]) views[i]->Release();
    }
}

const char* kUniversalReplicateShader = R"(
Texture2DArray<float4> SourceMip : register(t0);
Texture2DArray<float4> OriginalMip : register(t1);
RWTexture2DArray<float4> DestinationMip : register(u0);
cbuffer TintData : register(b0) { float4 Tint[128]; float4 Intensity[128]; };

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width, height, layers;
    DestinationMip.GetDimensions(width, height, layers);
    if (id.x >= width || id.y >= height || id.z >= layers) return;
    float4 source=SourceMip.Load(int4(id.xy, id.z % 6, 0));
    float4 tint=Tint[id.z/6];
    // tint.w == -2 marks saturated, directionally inconsistent authored
    // cubemaps. Preserve their complete static texels instead of replacing
    // them with an untinted dynamic cube.
    if (tint.w<=-1.5) {
        uint ow,oh,olayers;
        OriginalMip.GetDimensions(ow,oh,olayers);
        uint2 originalXY=min(uint2((float2(id.xy)+0.5)*float2(ow,oh)/
            float2(width,height)),uint2(ow-1,oh-1));
        DestinationMip[id]=max(OriginalMip.Load(int4(originalXY,id.z,0)),0.0);
        return;
    }
    if (tint.w>=0.0) {
        const float3 weights=float3(0.2126,0.7152,0.0722);
        float oldY=dot(source.rgb,weights);
        float3 colored=source.rgb*tint.rgb;
        colored*=oldY/max(dot(colored,weights),0.0001);
        source.rgb=colored;
    }
	// Preserve the original cubemap's authored reflection intensity without
	// changing roughness, alpha, or the material's transparency parameters.
	source.rgb*=Intensity[id.z/6].x;
    DestinationMip[id]=max(source,0.0);
}
)";

bool UniversalEnsureCompute(ID3D11Device* device)
{
    if (g_universalReplicateCS) return true;
    if (g_universalComputeFailed || !device || !g_liveTexture) return false;
    Microsoft::WRL::ComPtr<ID3DBlob> shader, errors;
    const HRESULT compile = D3DCompile(kUniversalReplicateShader,
        strlen(kUniversalReplicateShader), "DCF4UniversalReplicateCS", nullptr, nullptr,
        "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
        shader.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(compile) || !shader || FAILED(device->CreateComputeShader(
        shader->GetBufferPointer(), shader->GetBufferSize(), nullptr,
        g_universalReplicateCS.GetAddressOf()))) {
        g_universalComputeFailed = true;
        std::string message = "UNIVERSAL: compute replication unavailable; original bindings retained.";
        if (errors) message += " " + std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        Log(message);
        return false;
    }
    for (UINT mip = 0; mip < 8; ++mip) {
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};
        v.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        v.Texture2DArray.MostDetailedMip = mip;
        v.Texture2DArray.MipLevels = 1;
        v.Texture2DArray.FirstArraySlice = 0;
        v.Texture2DArray.ArraySize = 6;
        if (FAILED(device->CreateShaderResourceView(g_liveTexture.Get(), &v,
            g_universalLiveMipSRVs[mip].GetAddressOf()))) {
            g_universalComputeFailed = true;
            g_universalReplicateCS.Reset();
            for (auto& srv : g_universalLiveMipSRVs) srv.Reset();
            Log("UNIVERSAL: source mip view creation failed; original bindings retained.");
            return false;
        }
    }
    Log("UNIVERSAL: compute array replication ready (selected mip only).");
    return true;
}

void UniversalReplicatePool(UniversalPool& pool, UINT mip)
{
    ID3D11ComputeShader* oldCS = nullptr;
    ID3D11ClassInstance* oldInstances[256]{}; UINT oldInstanceCount=256;
    ID3D11ShaderResourceView* oldSRVs[2]{};
    ID3D11UnorderedAccessView* oldUAV = nullptr;
	ID3D11Buffer* oldCB=nullptr;
    g_liveContext->CSGetShader(&oldCS, oldInstances, &oldInstanceCount);
    g_liveContext->CSGetShaderResources(0, 2, oldSRVs);
    g_liveContext->CSGetUnorderedAccessViews(0, 1, &oldUAV);
	g_liveContext->CSGetConstantBuffers(0,1,&oldCB);
    g_liveContext->CSSetShader(g_universalReplicateCS.Get(), nullptr, 0);
	if (pool.tintDirty && pool.tintConstants) {
		g_liveContext->UpdateSubresource(pool.tintConstants.Get(),0,nullptr,&pool.style,0,0);
		pool.tintDirty=false;
	}
	auto* tintCB=pool.tintConstants.Get();
	g_liveContext->CSSetConstantBuffers(0,1,&tintCB);
    {
        ID3D11ShaderResourceView* sources[2]{
            g_universalLiveMipSRVs[mip].Get(),pool.originalMipSRVs[mip].Get()};
        auto* destination = pool.mipUAVs[mip].Get();
        g_liveContext->CSSetShaderResources(0, 2, sources);
        g_liveContext->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
        const UINT size = 128u >> mip;
        g_liveContext->Dispatch((size + 7) / 8, (size + 7) / 8, pool.cubes * 6);
        ID3D11ShaderResourceView* nullSRVs[2]{};
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        g_liveContext->CSSetShaderResources(0, 2, nullSRVs);
        g_liveContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    }
    g_liveContext->CSSetShader(oldCS, oldInstances, oldInstanceCount);
    for (UINT i=0;i<oldInstanceCount;++i) if (oldInstances[i]) oldInstances[i]->Release();
    g_liveContext->CSSetShaderResources(0, 2, oldSRVs);
    g_liveContext->CSSetUnorderedAccessViews(0, 1, &oldUAV, nullptr);
	g_liveContext->CSSetConstantBuffers(0,1,&oldCB);
    if (oldCS) oldCS->Release();
    for (auto* srv:oldSRVs) if (srv) srv->Release();
    if (oldUAV) oldUAV->Release();
	if (oldCB) oldCB->Release();
}

void UniversalPublish()
{
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    g_liveContext->GetDevice(device.GetAddressOf());
    bool materialViewsChanged=false;
    // Keep arrays synchronized with each newly published, temporally accumulated cube.
    for (auto& pool : g_universalPools) {
        if (pool.texture || pool.failed) continue;
        materialViewsChanged=true;
        D3D11_TEXTURE2D_DESC d{};
        d.Width = d.Height = 128; d.MipLevels = 8; d.ArraySize = pool.cubes * 6;
        d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        d.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format = d.Format;
		if (pool.standalone) {
			v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE;
			v.TextureCube.MostDetailedMip=g_materialMip;
			v.TextureCube.MipLevels=1;
		} else {
			v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
			v.TextureCubeArray.MostDetailedMip = g_materialMip;
			v.TextureCubeArray.MipLevels = 1;
			v.TextureCubeArray.First2DArrayFace = 0;
			v.TextureCubeArray.NumCubes = pool.cubes;
		}
        HRESULT poolResult = device->CreateTexture2D(&d, nullptr, pool.texture.GetAddressOf());
        if (SUCCEEDED(poolResult)) TagReflectionReplacement(pool.texture.Get());
        if (SUCCEEDED(poolResult))
            poolResult = device->CreateShaderResourceView(pool.texture.Get(), &v, pool.view.GetAddressOf());
		D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(UniversalStyleConstants); cb.Usage=D3D11_USAGE_DEFAULT;
		cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
		if (SUCCEEDED(poolResult)) poolResult=device->CreateBuffer(&cb,nullptr,pool.tintConstants.GetAddressOf());
		if (SUCCEEDED(poolResult) && pool.originalView) {
			D3D11_SHADER_RESOURCE_VIEW_DESC original{}; pool.originalView->GetDesc(&original);
			Microsoft::WRL::ComPtr<ID3D11Resource> originalResource;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> originalTexture;
			pool.originalView->GetResource(originalResource.GetAddressOf());
			D3D11_TEXTURE2D_DESC originalTextureDesc{};
			if (!originalResource || FAILED(originalResource.As(&originalTexture))) poolResult=E_FAIL;
			if (SUCCEEDED(poolResult)) originalTexture->GetDesc(&originalTextureDesc);
			for (UINT mip=0;SUCCEEDED(poolResult) && mip<8;++mip) {
				D3D11_SHADER_RESOURCE_VIEW_DESC source{}; source.Format=original.Format;
				source.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				source.Texture2DArray.MostDetailedMip=std::min(
					(pool.standalone ? original.TextureCube.MostDetailedMip :
						original.TextureCubeArray.MostDetailedMip)+mip,originalTextureDesc.MipLevels-1);
				source.Texture2DArray.MipLevels=1;
				source.Texture2DArray.FirstArraySlice=pool.standalone ? 0 :
					original.TextureCubeArray.First2DArrayFace;
				source.Texture2DArray.ArraySize=pool.cubes*6;
				poolResult=device->CreateShaderResourceView(originalResource.Get(),&source,
					pool.originalMipSRVs[mip].GetAddressOf());
			}
		}
        for (UINT mip = 0; SUCCEEDED(poolResult) && mip < 8; ++mip) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC u{};
            u.Format = d.Format;
            u.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            u.Texture2DArray.MipSlice = mip;
            u.Texture2DArray.FirstArraySlice = 0;
            u.Texture2DArray.ArraySize = pool.cubes * 6;
            poolResult = device->CreateUnorderedAccessView(pool.texture.Get(), &u,
                pool.mipUAVs[mip].GetAddressOf());
        }
        if (FAILED(poolResult)) {
            pool.failed = true; pool.view.Reset(); pool.texture.Reset();
            for (auto& uav : pool.mipUAVs) uav.Reset();
            Log("UNIVERSAL: replacement array allocation failed; original retained.");
        }
    }
    if (materialViewsChanged) RebuildMaterialViewLookup();
    const bool useCompute = UniversalEnsureCompute(device.Get());
    if (!useCompute) {
        for (auto& pool : g_universalPools) pool.ready=false;
        return;
    }
    for (auto& pool : g_universalPools) {
        if (!pool.texture || pool.failed) continue;
        UniversalReplicatePool(pool, g_materialMip);
        // Initial arrays must be regenerated after reference analysis. Subsequent
        // tint edits leave this completed output available until the next update.
        pool.ready = pool.standalone ||
            (pool.exteriorMatchState!=-4 && pool.exteriorMatchState!=-3);
    }
    TintAnalysisTick(device.Get());
}

void UniversalReset()
{
    // Caller must first restore substituted bindings.
    for (auto& slot : g_universalSlots) slot.Reset();
	for (auto& bound : g_liveCubemapSlots) bound = false;
	g_liveCubemapSlotCount = 0;
	g_rainNeutralFrameSequence = 0;
    g_materialViewLookup.clear();
    g_universalViews.clear(); ResetNonCubeCache(); g_universalPools.clear();
    RebuildMaterialViewLookup();
    g_universalTotalCubes = 0;
    g_universalComputeFailed = false;
    g_universalReplicateCS.Reset();
    for (auto& srv : g_universalLiveMipSRVs) srv.Reset();
    ResetTintAnalysis();
}
// Capture the named exterior fallback texture.
struct CellFallbackLoadScope {
    int kind=0; // 1 exterior, 2 interior
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
};
thread_local CellFallbackLoadScope* g_cellFallbackScope=nullptr;
std::mutex g_cellFallbackMutex;
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_namedCellFallback[2];
std::atomic_int g_sampledCellKind{0};
std::atomic_bool g_cellTaskPending{false};
std::atomic_uint g_cellEpoch{0};
int g_activeCellKind=0;
ULONGLONG g_nextCellPoll=0;

using CellFallbackLoad_t=void(*)(void*,RE::NiTexture*);
using CellFallbackRead_t=bool(*)(void*,RE::NiTexture*,void*);
CellFallbackLoad_t g_originalCellFallbackLoad=nullptr;
CellFallbackRead_t g_originalCellFallbackRead=nullptr;
using CellFallbackCreateTexture_t=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const D3D11_TEXTURE2D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D**);
using CellFallbackCreateSRV_t=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,ID3D11Resource*,const D3D11_SHADER_RESOURCE_VIEW_DESC*,ID3D11ShaderResourceView**);
CellFallbackCreateTexture_t g_originalCellFallbackCreateTexture=nullptr;
CellFallbackCreateSRV_t g_originalCellFallbackCreateSRV=nullptr;
bool g_cellFallbackDeviceHookAttempted=false;
bool g_cellFallbackDeviceHooksInstalled=false;
std::mutex g_cellFallbackDeviceMutex;
// Own the named resource until a delayed SRV arrives; never retain raw NiTexture pointers.
Microsoft::WRL::ComPtr<ID3D11Texture2D> g_pendingNamedFallbackTexture;

std::string NormalizeFallbackPath(std::string value)
{
    for (auto& c:value) { if (c=='/') c='\\'; if (c>='A'&&c<='Z') c=static_cast<char>(c-'A'+'a'); }
    if (value.rfind("textures\\",0)==0) value.erase(0,9);
    return value;
}
int NamedFallbackKind(RE::NiTexture* texture)
{
    if (!texture) return 0;
    const auto* name=texture->name.c_str();
    if (!name) return 0;
    const auto path=NormalizeFallbackPath(name);
    if (path=="shared\\cubemaps\\mipblur_defaultoutside1.dds") return 1;
    return 0;
}
bool CellFallbackExecutable(void* method)
{
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(method,&info,sizeof(info)) && info.State==MEM_COMMIT &&
        !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD)) &&
        (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
HRESULT STDMETHODCALLTYPE CellFallbackCreateTextureHook(ID3D11Device* device,const D3D11_TEXTURE2D_DESC* desc,
    const D3D11_SUBRESOURCE_DATA* data,ID3D11Texture2D** output)
{
    const HRESULT result=g_originalCellFallbackCreateTexture(device,desc,data,output);
    auto* scope=g_cellFallbackScope;
    if (scope && SUCCEEDED(result) && output && *output) {
        D3D11_TEXTURE2D_DESC d{}; (*output)->GetDesc(&d);
        if (d.SampleDesc.Count==1 && d.ArraySize>=6 && (d.ArraySize%6)==0 &&
            (d.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE) && UniversalFloatSampleFormat(d.Format)) {
            scope->texture=*output;
            if (scope->kind==1) {
                std::lock_guard lock(g_cellFallbackMutex);
                g_pendingNamedFallbackTexture=*output;
            }
        }
    }
    return result;
}
HRESULT STDMETHODCALLTYPE CellFallbackCreateSRVHook(ID3D11Device* device,ID3D11Resource* resource,
    const D3D11_SHADER_RESOURCE_VIEW_DESC* desc,ID3D11ShaderResourceView** output)
{
    const HRESULT result=g_originalCellFallbackCreateSRV(device,resource,desc,output);
    auto* scope=g_cellFallbackScope;
    if (SUCCEEDED(result) && output && *output) {
        Microsoft::WRL::ComPtr<ID3D11Resource> r; (*output)->GetResource(r.GetAddressOf());
        D3D11_SHADER_RESOURCE_VIEW_DESC v{}; (*output)->GetDesc(&v);
        // A reference must describe exactly one cube, not an arbitrary atlas.
        if (v.ViewDimension==D3D11_SRV_DIMENSION_TEXTURECUBE ||
            (v.ViewDimension==D3D11_SRV_DIMENSION_TEXTURECUBEARRAY && v.TextureCubeArray.NumCubes==1)) {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            if (SUCCEEDED(r.As(&texture))) {
                D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d);
                if (d.SampleDesc.Count==1 && (d.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE) &&
                    UniversalFloatSampleFormat(d.Format)) {
                    if (scope && scope->kind && (!scope->texture || r.Get()==scope->texture.Get()))
                        scope->view=*output;
                    bool recovered=false;
                    {
                        std::lock_guard lock(g_cellFallbackMutex);
                        if (g_pendingNamedFallbackTexture.Get()==texture.Get()) {
                            g_namedCellFallback[0]=*output;
                            g_pendingNamedFallbackTexture.Reset();
                            recovered=true;
                        }
                    }
                    if (recovered) Log("FALLBACK: named exterior SRV recovered.");
                }
            }
        }
    }
    return result;
}
bool EnsureCellFallbackDeviceHooks()
{
    std::lock_guard lock(g_cellFallbackDeviceMutex);
    if (g_cellFallbackDeviceHookAttempted) return g_cellFallbackDeviceHooksInstalled;
    auto* renderer=RE::BSGraphics::GetRendererData();
    if (!renderer || !renderer->device) return false;
    g_cellFallbackDeviceHookAttempted=true;
    auto** table=*reinterpret_cast<void***>(reinterpret_cast<ID3D11Device*>(renderer->device));
    void* oldTexture=table[5]; void* oldSRV=table[7];
    if (!CellFallbackExecutable(oldTexture)||!CellFallbackExecutable(oldSRV)) return false;
    g_originalCellFallbackCreateTexture=reinterpret_cast<CellFallbackCreateTexture_t>(oldTexture);
    g_originalCellFallbackCreateSRV=reinterpret_cast<CellFallbackCreateSRV_t>(oldSRV);
    DWORD protection=0;
    if (!VirtualProtect(table+5,3*sizeof(void*),PAGE_READWRITE,&protection)) return false;
    const bool a=InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(table+5),
        reinterpret_cast<PVOID>(CellFallbackCreateTextureHook),oldTexture)==oldTexture;
    const bool b=a && InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(table+7),
        reinterpret_cast<PVOID>(CellFallbackCreateSRVHook),oldSRV)==oldSRV;
    if (!b && a) InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(table+5),oldTexture,
        reinterpret_cast<PVOID>(CellFallbackCreateTextureHook));
    DWORD ignored=0; VirtualProtect(table+5,3*sizeof(void*),protection,&ignored);
    if (!(a&&b)) { g_originalCellFallbackCreateTexture=nullptr; g_originalCellFallbackCreateSRV=nullptr; return false; }
    g_cellFallbackDeviceHooksInstalled=true;
    Log("CELL FALLBACK: D3D11 resource capture ready.");
    return true;
}
void SubmitCellFallback(const CellFallbackLoadScope& scope,const char* route)
{
    if (scope.kind==1 && !scope.view && scope.texture) {
        std::lock_guard lock(g_cellFallbackMutex);
        if (g_pendingNamedFallbackTexture.Get()==scope.texture.Get())
            Log("FALLBACK: named texture retained awaiting delayed SRV.");
    }
    if (!scope.kind || !scope.view) {
        Log(std::string("CELL FALLBACK: named load through ")+route+" produced no sampleable cube SRV."); return;
    }
    { std::lock_guard lock(g_cellFallbackMutex); g_namedCellFallback[scope.kind-1]=scope.view; }
    Log(std::string("CELL FALLBACK: captured exterior mipblur_DefaultOutside1.dds through ")+
        route+"; pixels and format are not fingerprinted.");
}
void CellFallbackLoadHook(void* self,RE::NiTexture* texture)
{
    // Observe engine loads even while disabled so later MCM enable has a reference.
    const int kind=NamedFallbackKind(texture);
    if (!kind || !EnsureCellFallbackDeviceHooks()) { g_originalCellFallbackLoad(self,texture); return; }
    CellFallbackLoadScope scope{}; scope.kind=kind; auto* prior=g_cellFallbackScope; g_cellFallbackScope=&scope;
    g_originalCellFallbackLoad(self,texture); g_cellFallbackScope=prior; SubmitCellFallback(scope,"LoadTexture");
}
bool CellFallbackReadHook(void* self,RE::NiTexture* texture,void* userData)
{
    // Streaming may load the reference only once during a disabled session.
    const int kind=NamedFallbackKind(texture);
    if (!kind || !EnsureCellFallbackDeviceHooks()) return g_originalCellFallbackRead(self,texture,userData);
    CellFallbackLoadScope scope{}; scope.kind=kind; auto* prior=g_cellFallbackScope; g_cellFallbackScope=&scope;
    const bool result=g_originalCellFallbackRead(self,texture,userData); g_cellFallbackScope=prior;
    if (result) SubmitCellFallback(scope,"ReadStreamingTextureData"); return result;
}
bool InstallCellFallbackHooks()
{
    if (!g_runtimeProfile.enableRendererHooks || !g_runtimeProfile.shaderManagerVtableID) return false;
    const auto address=REL::ID{g_runtimeProfile.shaderManagerVtableID}.address();
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
        (info.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
    if (!IsReadableSpan(address + 26*sizeof(std::uintptr_t),3*sizeof(std::uintptr_t))) return false;
    auto* table=reinterpret_cast<std::uintptr_t*>(address);
    if (!CellFallbackExecutable(reinterpret_cast<void*>(table[26])) || !CellFallbackExecutable(reinterpret_cast<void*>(table[28]))) return false;
    g_originalCellFallbackLoad=reinterpret_cast<CellFallbackLoad_t>(table[26]);
    g_originalCellFallbackRead=reinterpret_cast<CellFallbackRead_t>(table[28]);
    LONG error=DetourTransactionBegin();
    if (error==NO_ERROR) error=DetourUpdateThread(GetCurrentThread());
    if (error==NO_ERROR) error=DetourAttach(reinterpret_cast<PVOID*>(&g_originalCellFallbackLoad),reinterpret_cast<PVOID>(CellFallbackLoadHook));
    if (error==NO_ERROR) error=DetourAttach(reinterpret_cast<PVOID*>(&g_originalCellFallbackRead),reinterpret_cast<PVOID>(CellFallbackReadHook));
    if (error!=NO_ERROR) { DetourTransactionAbort(); return false; }
    error=DetourTransactionCommit();
    if (error==NO_ERROR) Log("CELL FALLBACK: LoadTexture and ReadStreamingTextureData hooks installed.");
    return error==NO_ERROR;
}
bool UpdateCellFallbackKind()
{
    const auto now=GetTickCount64();
    if (now>=g_nextCellPoll && !g_cellTaskPending.exchange(true)) {
        g_nextCellPoll=now+250; const auto epoch=g_cellEpoch.load();
        F4SE::GetTaskInterface()->AddTask(std::function<void()>([epoch]() {
            if (epoch==g_cellEpoch.load() && g_gameplayReady.load()) {
                const auto* player=RE::PlayerCharacter::GetSingleton(); const auto* cell=player?player->parentCell:nullptr;
                if (cell) g_sampledCellKind.store(cell->IsInterior()?2:1);
            }
            g_cellTaskPending.store(false);
        }));
    }
    const int kind=g_sampledCellKind.load();
    if (!kind || kind==g_activeCellKind) return false;
    g_activeCellKind=kind;
    Log(kind==2?"CELL FALLBACK: interior selected (internal dark brown).":"CELL FALLBACK: exterior selected (mipblur_DefaultOutside1.dds).");
    return true;
}
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> AcquireNamedCellFallback()
{
    std::lock_guard lock(g_cellFallbackMutex);
    if (g_activeCellKind==1) return g_namedCellFallback[0];
    return {};
}
// Shared authored-luminance policy for standalone and array cubemaps.
float UniversalAuthoredIntensity(float luminance, bool usable, bool consistent)
{
    return (usable && consistent) ?
        std::clamp(std::sqrt(std::clamp(luminance,0.0f,1.0f)),0.02f,1.0f) : 1.0f;
}
struct TintProbeResult { float r,g,b,saturation; float consistency,pad0,pad1,pad2;
    std::array<XMFLOAT4,96> signature{};
    std::array<XMFLOAT4,384> detail{}; };
static_assert(sizeof(TintProbeResult)==7712);
static_assert(offsetof(TintProbeResult,consistency)==16);
static_assert(offsetof(TintProbeResult,signature)==32);
static_assert(offsetof(TintProbeResult,detail)==1568);
constexpr UINT kTintProbeElementBytes=sizeof(XMFLOAT4);
constexpr UINT kTintProbeElementsPerCube=482;
static_assert(kTintProbeElementBytes==16);
static_assert(sizeof(TintProbeResult)==kTintProbeElementsPerCube*kTintProbeElementBytes);
static_assert(kTintProbeElementBytes<=D3D11_REQ_MULTI_ELEMENT_STRUCTURE_SIZE_IN_BYTES);
TintProbeResult g_exteriorReference{};
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_exteriorReferenceView;
bool g_exteriorReferenceValid=false, g_tintProbePendingExterior=false;
ULONGLONG g_exteriorReferenceNext=0;
Microsoft::WRL::ComPtr<ID3D11ComputeShader> g_tintProbeCS;
Microsoft::WRL::ComPtr<ID3D11Buffer> g_tintProbeOutput, g_tintProbeReadback;
Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> g_tintProbeUAV;
Microsoft::WRL::ComPtr<ID3D11Query> g_tintProbeQuery;
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_tintProbeSource;
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_tintProbePendingView;
std::unordered_set<ID3D11ShaderResourceView*> g_tintProbeDone;
std::unordered_map<ID3D11ShaderResourceView*,ULONGLONG> g_tintProbeNext;
UINT g_tintProbePendingCubes=0;
UINT g_tintProbePendingPool=UINT_MAX;
bool g_tintProbePending=false, g_tintProbeFailed=false;
struct InvalidReferenceRecovery {
    unsigned invalidSamples=0;
    ULONGLONG firstInvalid=0,nextAttempt=0;
    void Observe(bool valid,ULONGLONG now) {
        if (valid) { *this={}; return; }
        if (!invalidSamples) firstInvalid=now;
        if (invalidSamples<5) ++invalidSamples;
    }
    bool Due(ULONGLONG now) const {
        return invalidSamples>=5 && now-firstInvalid>=10000 && now>=nextAttempt;
    }
    void Attempt(ULONGLONG now) { nextAttempt=now+30000; }
};
InvalidReferenceRecovery g_invalidReference;
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_referenceCandidate,g_candidateOriginal;
bool g_tintProbePendingCandidate=false;
ULONGLONG g_candidateProbeNext=0;


const char* kTintProbeShader=R"(
Texture2DArray<float4> Source : register(t0);
// Each cube occupies 482 float4 elements; keep the GPU element stride small.
RWStructuredBuffer<float4> Results : register(u0);

float Saturation(float3 c)
{
    float hi=max(c.r,max(c.g,c.b)), lo=min(c.r,min(c.g,c.b));
    return hi>1e-5 ? (hi-lo)/hi : 0.0;
}
float3 Chromaticity(float3 c)
{
    float sum=c.r+c.g+c.b;
    return sum>1e-5 ? c/sum : float3(0.333333,0.333333,0.333333);
}
[numthreads(1,1,1)]
void main(uint3 id:SV_DispatchThreadID)
{
    uint width,height,layers,mips; Source.GetDimensions(0,width,height,layers,mips);
    uint cube=id.x; if (cube*6+5>=layers) return;
    uint last=mips-1, previous=mips>1?mips-2:last;
    uint pw,ph,pl,pm; Source.GetDimensions(previous,pw,ph,pl,pm);
    float3 faceColor[6]; float3 total=0; float saturation=0;
    [unroll] for (uint face=0;face<6;++face) {
        uint layer=cube*6+face;
        float3 c=Source.Load(int4(0,0,layer,last)).rgb;
        if (previous!=last) {
            c+=Source.Load(int4(0,0,layer,previous)).rgb;
            c+=Source.Load(int4(pw-1,0,layer,previous)).rgb;
            c+=Source.Load(int4(0,ph-1,layer,previous)).rgb;
            c+=Source.Load(int4(pw-1,ph-1,layer,previous)).rgb;
            c*=0.2;
        }
        faceColor[face]=max(c,0.0); total+=faceColor[face]; saturation+=Saturation(faceColor[face]);
    }
    float3 average=total/6.0, center=Chromaticity(average); float distance=0;
    [unroll] for (uint f=0;f<6;++f) distance+=length(Chromaticity(faceColor[f])-center);
    float consistency=saturate(1.0-distance/(6.0*0.816497));
    Results[cube*482]=float4(average,saturation/6.0);
    Results[cube*482+1]=float4(consistency,0,0,0);
    // Compare spatial content, not just the average color. Select the mip
    // nearest 4x4, allowing different source resolutions and mip counts.
    uint signatureMip=0, sw=width, sh=height;
    [loop] while (signatureMip+1<mips && (sw>4 || sh>4)) {
        ++signatureMip;
        Source.GetDimensions(signatureMip,sw,sh,pl,pm);
    }
    [loop] for (uint face=0;face<6;++face)
        [loop] for (uint y=0;y<4;++y)
            [loop] for (uint x=0;x<4;++x) {
                uint2 pixel=min(uint2((float2(x,y)+0.5)*float2(sw,sh)/4.0),uint2(sw-1,sh-1));
                Results[cube*482+2+face*16+y*4+x]=Source.Load(int4(pixel,cube*6+face,signatureMip));
            }
    // A second spatial signature distinguishes textures sharing coarse mips.
    uint detailMip=0, dw=width, dh=height;
    [loop] while (detailMip+1<mips && (dw>8 || dh>8)) {
        ++detailMip;
        Source.GetDimensions(detailMip,dw,dh,pl,pm);
    }
    [loop] for (uint face=0;face<6;++face)
        [loop] for (uint y=0;y<8;++y)
            [loop] for (uint x=0;x<8;++x) {
                uint2 pixel=min(uint2((float2(x,y)+0.5)*float2(dw,dh)/8.0),uint2(dw-1,dh-1));
                Results[cube*482+98+face*64+y*8+x]=Source.Load(int4(pixel,cube*6+face,detailMip));
            }
}
)";

bool ExteriorSignatureMatches(const TintProbeResult& a,const TintProbeResult& b)
{
    // Reject missing/non-finite data. Tight per-channel tolerance permits only
    // numerical conversion noise; there is no color/saturation heuristic here.
    bool nonzero=false;
    for (std::size_t i=0;i<a.signature.size();++i) {
        const float av[]={a.signature[i].x,a.signature[i].y,a.signature[i].z,a.signature[i].w};
        const float bv[]={b.signature[i].x,b.signature[i].y,b.signature[i].z,b.signature[i].w};
        for (int c=0;c<4;++c) {
            if (!std::isfinite(av[c]) || !std::isfinite(bv[c]) ||
                std::abs(av[c]-bv[c])>0.00001f+0.00001f*std::max(std::abs(av[c]),std::abs(bv[c]))) return false;
            if (c<3 && bv[c]>0.00001f) nonzero=true;
        }
    }
    return nonzero;
}

bool ExteriorDetailedMatches(const TintProbeResult& a,const TintProbeResult& b)
{
    // Reject missing/non-finite data. Tight per-channel tolerance permits only
    // numerical conversion noise; there is no color/saturation heuristic here.
    if (!ExteriorSignatureMatches(a,b)) return false;
    bool nonzero=false;
    for (std::size_t i=0;i<a.detail.size();++i) {
        const float av[]={a.detail[i].x,a.detail[i].y,a.detail[i].z,a.detail[i].w};
        const float bv[]={b.detail[i].x,b.detail[i].y,b.detail[i].z,b.detail[i].w};
        for (int c=0;c<4;++c) {
            if (!std::isfinite(av[c]) || !std::isfinite(bv[c]) ||
                std::abs(av[c]-bv[c])>0.00001f+0.00001f*std::max(std::abs(av[c]),std::abs(bv[c]))) return false;
            if (c<3 && bv[c]>0.00001f) nonzero=true;
        }
    }
    return nonzero;
}

UINT FindExteriorMatch(const TintProbeResult* values,UINT cubes,const TintProbeResult& reference,UINT& count)
{
    count=0; UINT selected=UINT_MAX;
    for (UINT cube=0;cube<cubes;++cube)
        if (ExteriorDetailedMatches(values[cube],reference)) { selected=cube; ++count; }
    return count==1 ? selected : UINT_MAX;
}

bool EnsureTintProbe(ID3D11Device* device)
{
    if (g_tintProbeFailed || !device) return false;
    if (g_tintProbeCS && g_tintProbeOutput && g_tintProbeReadback &&
        g_tintProbeUAV && g_tintProbeQuery) return true;
    const auto fail = [](const char* stage) {
        g_tintProbeCS.Reset(); g_tintProbeOutput.Reset();
        g_tintProbeReadback.Reset(); g_tintProbeUAV.Reset(); g_tintProbeQuery.Reset();
        g_tintProbeFailed=true;
        Log(std::string("TINT FAIL: initialization at ")+stage);
        return false;
    };
    Microsoft::WRL::ComPtr<ID3DBlob> code,errors;
    const HRESULT compileHr=D3DCompile(kTintProbeShader,strlen(kTintProbeShader),"DCF4TintProbe",nullptr,nullptr,
        "main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,code.GetAddressOf(),errors.GetAddressOf());
    if (FAILED(compileHr) || !code) {
        Log(std::string("TINT: compiler HRESULT=")+std::to_string(static_cast<long>(compileHr)));
        if (errors && errors->GetBufferSize())
            Log(std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()));
        return fail("shader compilation");
    }
    const HRESULT shaderHr=device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,
        g_tintProbeCS.GetAddressOf());
    if (FAILED(shaderHr)) {
        Log(std::string("TINT: CreateComputeShader HRESULT=")+std::to_string(static_cast<long>(shaderHr)));
        return fail("shader creation");
    }
    D3D11_BUFFER_DESC d{}; d.ByteWidth=sizeof(TintProbeResult)*128; d.Usage=D3D11_USAGE_DEFAULT;
    d.BindFlags=D3D11_BIND_UNORDERED_ACCESS; d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    d.StructureByteStride=kTintProbeElementBytes;
    if (FAILED(device->CreateBuffer(&d,nullptr,g_tintProbeOutput.GetAddressOf()))) return fail("output buffer");
    D3D11_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format=DXGI_FORMAT_UNKNOWN; u.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
    u.Buffer.NumElements=128*kTintProbeElementsPerCube;
    if (FAILED(device->CreateUnorderedAccessView(g_tintProbeOutput.Get(),&u,g_tintProbeUAV.GetAddressOf()))) return fail("output UAV");
    d.Usage=D3D11_USAGE_STAGING; d.BindFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ; d.MiscFlags=0;
    if (FAILED(device->CreateBuffer(&d,nullptr,g_tintProbeReadback.GetAddressOf()))) return fail("readback buffer");
    D3D11_QUERY_DESC q{}; q.Query=D3D11_QUERY_EVENT;
    if (FAILED(device->CreateQuery(&q,g_tintProbeQuery.GetAddressOf()))) return fail("completion query");
    Log("TINT: automatic coarse-mip analyzer ready."); return true;
}

bool IsExteriorFallbackView(ID3D11ShaderResourceView* view)
{
    std::lock_guard lock(g_cellFallbackMutex);
    if (!view || !g_namedCellFallback[0]) return false;
    Microsoft::WRL::ComPtr<ID3D11Resource> a,b;
    view->GetResource(a.GetAddressOf()); g_namedCellFallback[0]->GetResource(b.GetAddressOf());
    return a.Get()==b.Get();
}

// Reopen the installed resource when its original GPU creation was not observed.
// The engine stream resolves the game's resource namespace; no absolute disk path is used.
// Return the paired sampling formats without converting the stored DDS bytes.
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
void RecoverNamedFallbackTick(ID3D11Device* device)
{
    { std::lock_guard lock(g_cellFallbackMutex); if(g_namedCellFallback[0]) return; }
    static ULONGLONG next=0; static unsigned attempts=0;
    const auto now=GetTickCount64(); if(now<next) return;
    next=now+10000;
    std::string failure;
    if(!RecoverNamedFallback(device,failure)) {
        ++attempts;
        if(attempts==1 || attempts%6==0)
            Log("FALLBACK: recovery pending: "+failure+"; original bindings retained until reference is ready.");
    }
}

void TintAnalysisTick(ID3D11Device* device)
{
    RecoverNamedFallbackTick(device);
    if (!g_liveContext || !EnsureTintProbe(device)) return;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> exteriorView;
    { std::lock_guard lock(g_cellFallbackMutex); exteriorView=g_namedCellFallback[0]; }
    if (exteriorView.Get()!=g_exteriorReferenceView.Get()) {
        g_invalidReference={}; g_referenceCandidate.Reset(); g_candidateOriginal.Reset();
        g_namedReferenceReady=false;
        g_tintProbeDone.clear(); g_tintProbeNext.clear();
        for(auto& [view,entry]:g_universalViews) entry.analyzed=false;
        for(auto& pool:g_universalPools) {
                            pool.exteriorMatchState=-3; pool.ready=false;
                        }
        g_exteriorReferenceView=exteriorView; g_exteriorReferenceValid=false;
        g_exteriorReferenceNext=0;
    }
    if (g_tintProbePending) {
        const HRESULT probeStatus=g_liveContext->GetData(g_tintProbeQuery.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (probeStatus==S_FALSE) return;
        if (FAILED(probeStatus)) {
            Log("TINT FAIL: completion query HRESULT="+std::to_string(static_cast<long>(probeStatus))+
                "; analysis paused until reset.");
            g_tintProbePending=false; g_tintProbePendingView.Reset(); g_tintProbeSource.Reset();
            g_tintProbePendingPool=UINT_MAX; g_tintProbePendingExterior=false;
            g_tintProbeFailed=true;
            return;
        }
        D3D11_MAPPED_SUBRESOURCE map{};
		if (SUCCEEDED(g_liveContext->Map(g_tintProbeReadback.Get(),0,D3D11_MAP_READ,0,&map))) {
            const auto* values=static_cast<const TintProbeResult*>(map.pData);
            if (g_tintProbePendingCandidate) {
                if (g_referenceCandidate && g_tintProbePendingView.Get()==g_referenceCandidate.Get()) {
                    const bool valid=ExteriorDetailedMatches(values[0],values[0]);
                    bool committed=false;
                    if (valid && !g_exteriorReferenceValid) {
                        std::lock_guard lock(g_cellFallbackMutex);
                        if (g_namedCellFallback[0].Get()==g_candidateOriginal.Get()) {
                            g_namedCellFallback[0]=g_referenceCandidate;
                            committed=true;
                        }
                    }
                    if (committed) {
                        exteriorView=g_referenceCandidate; g_exteriorReferenceView=exteriorView;
                        g_exteriorReference=values[0]; g_exteriorReferenceValid=true; g_namedReferenceReady=true;
                        g_exteriorReferenceNext=GetTickCount64()+1000;
                        g_tintProbeDone.clear(); g_tintProbeNext.clear();
                        for (auto& [view,entry]:g_universalViews) entry.analyzed=false;
                        for (auto& pool:g_universalPools) { pool.exteriorMatchState=-3; pool.ready=false; }
                        g_invalidReference={};
                        Log("FALLBACK: validated recovery reference installed; rematching cubemaps.");
                    } else {
                        Log(valid ? "FALLBACK: recovery candidate discarded; native reference changed or recovered." :
                            "FALLBACK: recovery candidate invalid; existing reference retained.");
                    }
                    g_referenceCandidate.Reset(); g_candidateOriginal.Reset();
                }
            } else if (g_tintProbePendingExterior) {
                if (g_tintProbePendingView.Get()==g_exteriorReferenceView.Get()) {
                    const bool valid=ExteriorDetailedMatches(values[0],values[0]);
                    g_invalidReference.Observe(valid,GetTickCount64());
                    if (valid) { g_referenceCandidate.Reset(); g_candidateOriginal.Reset(); }
                    if (valid && !g_exteriorReferenceValid) {
                        g_tintProbeDone.clear(); g_tintProbeNext.clear();
                        for(auto& [view,entry]:g_universalViews) entry.analyzed=false;
                        for(auto& pool:g_universalPools) {
                            pool.exteriorMatchState=-3; pool.ready=false;
                        }
                        Log("EXTERIOR MATCH: named reference ready.");
                    }
                    g_exteriorReference=values[0]; g_exteriorReferenceValid=valid;
                    g_namedReferenceReady=valid;
                }
            } else {
            UINT exteriorMatch=UINT_MAX, matchCount=0;
            if (g_exteriorReferenceValid) {
                exteriorMatch=FindExteriorMatch(values,g_tintProbePendingCubes,g_exteriorReference,matchCount);
            }
            if (g_tintProbePendingPool<g_universalPools.size()) {
                auto& p=g_universalPools[g_tintProbePendingPool];
                const int state=!g_exteriorReferenceValid ? -3 : matchCount==0 ? -2 : matchCount>1 ? -1 : static_cast<int>(exteriorMatch);
                if (p.exteriorMatchState!=state) {
                    p.exteriorMatchState=state;
                    Log("EXTERIOR MATCH: pool="+std::to_string(g_tintProbePendingPool)+
                        " match="+std::to_string(state)+" candidates="+std::to_string(matchCount)+
                        " (-3=reference pending,-2=no detailed match,-1=multiple detailed matches).");
                }
            }
            for (UINT cube=0;cube<g_tintProbePendingCubes;++cube) {
                const auto& v=values[cube];
				const bool exteriorAtlasCube=g_exteriorReferenceValid && ExteriorDetailedMatches(v,g_exteriorReference);
                const bool uncertainExterior=g_exteriorReferenceValid && !exteriorAtlasCube &&
                    ExteriorSignatureMatches(v,g_exteriorReference);
				const float luminance=0.2126f*v.r+0.7152f*v.g+0.0722f*v.b;
                const bool hasLuminance=std::isfinite(luminance) && luminance>0.00001f;
				const bool usable=hasLuminance && !exteriorAtlasCube;
				const bool consistent=v.consistency>=0.72f;
                // Exterior recognition excludes authored color, not authored energy.
                const float authoredIntensity=exteriorAtlasCube ? 1.0f : UniversalAuthoredIntensity(
                    luminance,hasLuminance,consistent || exteriorAtlasCube);
				const bool colorful=usable && v.saturation>=0.12f;
				// With an unresolved exterior identity, retain neutral array entries rather than desaturating them.
                const bool unresolvedNeutral=g_tintProbePendingPool<g_universalPools.size() &&
                    !g_universalPools[g_tintProbePendingPool].standalone && matchCount==0 && !colorful && !exteriorAtlasCube;
                // A matched exterior remains dynamic; other unreadable array entries
                // retain original texels until a later analysis supplies authored intensity.
                const bool unresolvedArrayData=g_tintProbePendingPool<g_universalPools.size() &&
                    !g_universalPools[g_tintProbePendingPool].standalone && !usable && !exteriorAtlasCube;
                const bool keepOriginal=(usable && !consistent) || unresolvedNeutral || unresolvedArrayData || uncertainExterior;
				const char* classification=uncertainExterior?"UNCERTAIN_EXTERIOR_ORIGINAL":unresolvedArrayData?"PENDING_DATA_ORIGINAL":unresolvedNeutral?"UNRESOLVED_NEUTRAL_ORIGINAL":exteriorAtlasCube?"EXTERIOR_EXCLUDED":(!usable?"NO_DATA":(v.saturation<0.12f?"NEUTRAL_TINT":
                    (consistent?"CONSISTENT_TINT":"KEEP_ORIGINAL")));
				if (g_tintProbePendingPool==UINT_MAX) {
					auto direct=g_universalViews.find(g_tintProbePendingView.Get());
					if (direct!=g_universalViews.end() && direct->second.directCube) {
						const bool firstAnalysis=g_directAnalysisLogged.insert(g_tintProbePendingView.Get()).second;
						direct->second.analyzed=usable || exteriorAtlasCube;
                        direct->second.exteriorBrightness=exteriorAtlasCube;
						direct->second.keepOriginal=keepOriginal;
						if ((usable || exteriorAtlasCube) && !keepOriginal && direct->second.pool==UINT_MAX &&
							g_universalTotalCubes<128) {
							direct->second.pool=static_cast<UINT>(g_universalPools.size());
							UniversalPool pool; pool.cubes=1; pool.standalone=true;
							pool.originalView=g_tintProbePendingView;
							for (auto& tint:pool.style.tint) tint=XMFLOAT4(1,1,1,-1);
							for (auto& value:pool.style.intensity) value=XMFLOAT4(1,0,0,0);
							pool.style.tint[0]=(usable&&consistent) ?
								XMFLOAT4(v.r/luminance,v.g/luminance,v.b/luminance,
									std::clamp(v.saturation,0.0f,1.0f)) : XMFLOAT4(1,1,1,-1);
							// Use the same authored-luminance policy for every consistent color,
							// independent of saturation and resource layout.
							const float directIntensity=authoredIntensity;
							pool.style.intensity[0]=XMFLOAT4(directIntensity,0,0,0);
							g_universalPools.push_back(std::move(pool)); ++g_universalTotalCubes;
						}
						if ((usable || exteriorAtlasCube) && !keepOriginal && direct->second.pool!=UINT_MAX &&
							direct->second.pool<g_universalPools.size()) {
							auto& pool=g_universalPools[direct->second.pool];
							pool.style.tint[0]=exteriorAtlasCube ? XMFLOAT4(1,1,1,-1) :
                                XMFLOAT4(v.r/luminance,v.g/luminance,v.b/luminance,std::clamp(v.saturation,0.0f,1.0f));
							const float directIntensity=authoredIntensity;
							pool.style.intensity[0]=XMFLOAT4(directIntensity,0,0,0);
							pool.tintDirty=true;
						}
						// A streamed standalone cube may initially report NO_DATA. Retry it
						// instead of permanently retaining an unstyled live binding.
						if (usable || exteriorAtlasCube) g_tintProbeDone.insert(g_tintProbePendingView.Get());
						const float loggedIntensity=authoredIntensity;
						if (firstAnalysis || (usable && !direct->second.loggedStyledBinding)) {
							Log(std::string("TINT DIRECT: saturation=")+std::to_string(v.saturation)+
								" consistency="+std::to_string(v.consistency)+
								" sourceY="+std::to_string(luminance)+
								" scale="+std::to_string(loggedIntensity)+" class="+classification+
								(keepOriginal?"; original binding preserved.":
									(exteriorAtlasCube?"; exterior tint excluded.":(usable?"; styled standalone cube queued.":"; awaiting streamed data; retry scheduled."))));
						}
					}
				} else if (g_tintProbePendingPool<g_universalPools.size() && cube<g_universalPools[g_tintProbePendingPool].cubes) {
					auto& pool=g_universalPools[g_tintProbePendingPool];
					const auto newTint=keepOriginal ? XMFLOAT4(1,1,1,-2) : (usable&&consistent) ?
						XMFLOAT4(v.r/luminance,v.g/luminance,v.b/luminance,std::clamp(v.saturation,0.0f,1.0f)) :
						XMFLOAT4(1,1,1,-1);
					// sqrt keeps dark authored cubes visible while retaining their lower
					// energy. The floor avoids turning very dark reflections completely off.
					// Apply luminance scaling to all consistent authored colors.
					// Hue preservation and brightness preservation are independent.
					// Inconsistent saturated cubes still copy their original texels.
					const float intensity=authoredIntensity;
					const auto& oldTint=pool.style.tint[cube];
					const float oldIntensity=pool.style.intensity[cube].x;
					const bool changed=std::abs(oldTint.x-newTint.x)>0.0001f ||
						std::abs(oldTint.y-newTint.y)>0.0001f || std::abs(oldTint.z-newTint.z)>0.0001f ||
						std::abs(oldTint.w-newTint.w)>0.0001f || std::abs(oldIntensity-intensity)>0.0001f;
					pool.style.tint[cube]=newTint;
					pool.style.intensity[cube]=XMFLOAT4(intensity,0,0,0);
					if (changed) pool.tintDirty=true;
					if (changed && (colorful || intensity<0.95f || exteriorAtlasCube || uncertainExterior)) Log(std::string("TINT REFRESH: cube=")+std::to_string(cube)+
						" sourceY="+std::to_string(luminance)+" scale="+std::to_string(intensity)+
						" class="+classification);
				}
            }
            }
            g_liveContext->Unmap(g_tintProbeReadback.Get(),0);
        }
        g_tintProbePending=false; g_tintProbePendingView.Reset(); g_tintProbeSource.Reset(); g_tintProbePendingPool=UINT_MAX;
    }
	const auto now=GetTickCount64();
    if (exteriorView && !g_exteriorReferenceValid && !g_referenceCandidate && g_invalidReference.Due(now)) {
        g_invalidReference.Attempt(now);
        std::string failure;
        if (RecoverNamedFallback(device,failure,&g_referenceCandidate)) {
            g_candidateOriginal=exteriorView; g_candidateProbeNext=0;
            Log("FALLBACK: persistent invalid samples; recovery candidate queued for validation.");
        } else Log("FALLBACK: invalid-reference recovery deferred: "+failure);
    }
    // Reuse the existing asynchronous probe; reference and atlas never compete
    // for separate GPU readback jobs. Reference refresh is at most once/second.
    std::vector<std::pair<ID3D11ShaderResourceView*,const UniversalEntry*>> candidates;
    candidates.reserve(g_universalViews.size()+2);
    UniversalEntry referenceEntry; referenceEntry.directCube=true;
    if (g_referenceCandidate && now>=g_candidateProbeNext) candidates.emplace_back(g_referenceCandidate.Get(),&referenceEntry);
    if (exteriorView && now>=g_exteriorReferenceNext) {
        candidates.emplace_back(exteriorView.Get(),&referenceEntry);
    }
    for (const auto& item:g_universalViews) candidates.emplace_back(item.first,&item.second);
    for (auto& [raw,entryPointer]:candidates) {
        const auto& entry=*entryPointer;
        const bool recoveryCandidate=raw==g_referenceCandidate.Get();
        const bool reference=recoveryCandidate || raw==exteriorView.Get();
        if (!raw || (!reference && g_tintProbeDone.contains(raw))) continue;
        if (reference && !recoveryCandidate && now<g_exteriorReferenceNext) continue;
        if (recoveryCandidate) g_candidateProbeNext=now+30000;
		auto scheduled=g_tintProbeNext.find(raw);
		if (!reference && scheduled!=g_tintProbeNext.end() && now<scheduled->second) continue;
		if (!reference && IsExteriorFallbackView(raw)) {
			g_tintProbeDone.insert(raw);
			Log("TINT APPLY: named exterior fallback excluded from tint; dynamic replacement remains active.");
			continue;
		}
		if (!entry.directCube && (entry.pool==UINT_MAX || entry.pool>=g_universalPools.size())) {
			g_tintProbeDone.insert(raw); continue;
		}
        D3D11_SHADER_RESOURCE_VIEW_DESC original{}; raw->GetDesc(&original);
        UINT firstFace=0,cubes=0,mips=0,firstMip=0;
        if (original.ViewDimension==D3D11_SRV_DIMENSION_TEXTURECUBE) {
            cubes=1; mips=original.TextureCube.MipLevels; firstMip=original.TextureCube.MostDetailedMip;
        }
        else if (original.ViewDimension==D3D11_SRV_DIMENSION_TEXTURECUBEARRAY) {
            firstFace=original.TextureCubeArray.First2DArrayFace; cubes=original.TextureCubeArray.NumCubes;
            mips=original.TextureCubeArray.MipLevels; firstMip=original.TextureCubeArray.MostDetailedMip;
        } else continue;
        if (reference && cubes!=1) {
            g_exteriorReferenceNext=now+1000;
            continue; // A multi-cube named view does not identify one reference.
        }
        Microsoft::WRL::ComPtr<ID3D11Resource> resource; raw->GetResource(resource.GetAddressOf());
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (!resource || FAILED(resource.As(&texture))) continue;
        D3D11_TEXTURE2D_DESC textureDesc{}; texture->GetDesc(&textureDesc);
        if(firstMip>=textureDesc.MipLevels) continue;
        if (mips==UINT_MAX) mips=textureDesc.MipLevels-firstMip;
        if(mips>textureDesc.MipLevels-firstMip) continue;
        cubes=std::min(cubes,128u); if (!cubes || !mips || firstMip>=textureDesc.MipLevels) continue;
        D3D11_SHADER_RESOURCE_VIEW_DESC s{}; s.Format=original.Format; s.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        s.Texture2DArray.MostDetailedMip=firstMip;
        s.Texture2DArray.MipLevels=mips; s.Texture2DArray.FirstArraySlice=firstFace; s.Texture2DArray.ArraySize=cubes*6;
        if (FAILED(device->CreateShaderResourceView(resource.Get(),&s,g_tintProbeSource.GetAddressOf()))) continue;
        ID3D11ComputeShader* oldCS=nullptr; ID3D11ShaderResourceView* oldSRV=nullptr;
        ID3D11ClassInstance* oldInstances[256]{}; UINT oldInstanceCount=256;
        ID3D11UnorderedAccessView* oldUAV=nullptr;
        g_liveContext->CSGetShader(&oldCS,oldInstances,&oldInstanceCount);
        g_liveContext->CSGetShaderResources(0,1,&oldSRV);
        g_liveContext->CSGetUnorderedAccessViews(0,1,&oldUAV);
        auto* source=g_tintProbeSource.Get(); auto* output=g_tintProbeUAV.Get();
        g_liveContext->CSSetShader(g_tintProbeCS.Get(),nullptr,0); g_liveContext->CSSetShaderResources(0,1,&source);
        g_liveContext->CSSetUnorderedAccessViews(0,1,&output,nullptr); g_liveContext->Dispatch(cubes,1,1);
        ID3D11ShaderResourceView* nullSRV=nullptr; ID3D11UnorderedAccessView* nullUAV=nullptr;
        g_liveContext->CSSetShaderResources(0,1,&nullSRV); g_liveContext->CSSetUnorderedAccessViews(0,1,&nullUAV,nullptr);
        // Buffer box coordinates are bytes; only the dispatched prefix is read on completion.
        const D3D11_BOX probeRegion{0,0,0,static_cast<UINT>(sizeof(TintProbeResult))*cubes,1,1};
        g_liveContext->CopySubresourceRegion(g_tintProbeReadback.Get(),0,0,0,0,
            g_tintProbeOutput.Get(),0,&probeRegion);
        g_liveContext->End(g_tintProbeQuery.Get());
        g_liveContext->CSSetShader(oldCS,oldInstances,oldInstanceCount); g_liveContext->CSSetShaderResources(0,1,&oldSRV);
        for (UINT i=0;i<oldInstanceCount;++i) if (oldInstances[i]) oldInstances[i]->Release();
        g_liveContext->CSSetUnorderedAccessViews(0,1,&oldUAV,nullptr);
        if (oldCS) oldCS->Release(); if (oldSRV) oldSRV->Release(); if (oldUAV) oldUAV->Release();
        g_tintProbePendingView=raw; g_tintProbePendingCubes=cubes; g_tintProbePendingPool=entry.pool;
        g_tintProbePendingCandidate=recoveryCandidate;
        g_tintProbePendingExterior=reference && !recoveryCandidate;
        if (recoveryCandidate) g_candidateProbeNext=now+30000;
        else if (reference) g_exteriorReferenceNext=now+1000;
		g_tintProbeNext[raw]=now+(entry.directCube?500u:1000u);
		g_tintProbePending=true; return;
    }
}

void ResetTintAnalysis()
{
    g_invalidReference={}; g_referenceCandidate.Reset(); g_candidateOriginal.Reset();
    g_tintProbePendingCandidate=false; g_candidateProbeNext=0;
    g_namedReferenceReady=false;
    g_directAnalysisLogged.clear();
    g_exteriorReferenceValid=false; g_exteriorReferenceView.Reset();
    g_exteriorReferenceNext=0; g_tintProbePendingExterior=false;
    g_tintProbePending=false; g_tintProbePendingView.Reset(); g_tintProbeSource.Reset();
	g_tintProbePendingPool=UINT_MAX; g_tintProbeDone.clear(); g_tintProbeNext.clear();
        for(auto& [view,entry]:g_universalViews) entry.analyzed=false;
    // Permit a fresh attempt after a reset, never on every frame after failure.
    g_tintProbeFailed=false;
}

#include "PerformanceDiagnostics.h"
	void SetLiveCubemapSlot(UINT slot, bool bound)
	{
		if (slot >= 128 || g_liveCubemapSlots[slot] == bound) return;
		g_liveCubemapSlots[slot] = bound;
		if (bound) {
			++g_liveCubemapSlotCount;
		} else if (g_liveCubemapSlotCount > 0) {
			--g_liveCubemapSlotCount;
		}
	}

	bool PrepareRainNeutralFrameBuffer(ID3D11DeviceContext* context)
	{
		if (!context || !g_latestMappedValid ||
			!std::isfinite(g_latestMappedFrame.iblDesaturation.x) ||
			!std::isfinite(g_latestMappedFrame.iblDesaturation.y)) return false;
		if (g_latestMappedFrame.iblDesaturation.x <= 0.00001f &&
			g_latestMappedFrame.iblDesaturation.y <= 0.00001f) return false;
		if (g_rainNeutralFrameBuffer &&
			g_rainNeutralFrameSequence == g_latestMappedSequence) return true;

		if (!g_rainNeutralFrameBuffer) {
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			context->GetDevice(device.GetAddressOf());
			if (!device) return false;
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(EngineFrameBuffer);
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (FAILED(device->CreateBuffer(&desc, nullptr,
					g_rainNeutralFrameBuffer.ReleaseAndGetAddressOf()))) {
				Log("RAIN: failed to create neutral b12 buffer.");
				return false;
			}
		}

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(g_rainNeutralFrameBuffer.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped)) || !mapped.pData) return false;
		EngineFrameBuffer neutral = g_latestMappedFrame;
		const float originalX = neutral.iblDesaturation.x;
		const float originalY = neutral.iblDesaturation.y;
		neutral.iblDesaturation.x = 0.0f;
		neutral.iblDesaturation.y = 0.0f;
		memcpy(mapped.pData, &neutral, sizeof(neutral));
		context->Unmap(g_rainNeutralFrameBuffer.Get(), 0);
		g_rainNeutralFrameSequence = g_latestMappedSequence;
		if (!g_loggedRainDrawOverride) {
			Log("RAIN: reflective b12 override active; original xy=(" +
				std::to_string(originalX) + "," + std::to_string(originalY) +
				") applied xy=(0,0).");
			g_loggedRainDrawOverride = true;
		}
		return true;
	}

	struct RainDrawStats {
		std::uint64_t draws=0, noLive=0, noBuffer=0, wrongBuffer=0;
		std::uint64_t unavailable=0, neutral=0, applied=0, audits=0, repairs=0;
	};
	RainDrawStats g_rainStats{};
	bool g_rainAuditDue=true;
	ULONGLONG g_nextRainAudit=0;
	ULONGLONG g_nextRainReport=0;
	ULONGLONG g_nextRainStatusCheck=0;
	unsigned g_lastRainStatus=~0u;

	void ResetRainReporting()
	{
		g_rainStats={};
		g_nextRainReport=0;
		g_nextRainStatusCheck=0;
		g_lastRainStatus=~0u;
		g_rainAuditDue=true;
		g_nextRainAudit=0;
	}

	// Bounded read-only reconciliation also catches bindings changed outside our
	// PSSetShaderResources hook. Never guesses a buffer layout or changes an SRV.
	void AuditRainBindings(ID3D11DeviceContext* context)
	{
		if (!g_rainAuditDue) return;
		g_rainAuditDue=false;
		ID3D11ShaderResourceView* views[128]{};
		context->PSGetShaderResources(0,128,views);
		{ if(g_diagnosticsEnabled) ++g_rainStats.audits; }
		bool changed=false;
		for (UINT i=0;i<128;++i) {
			const bool ours=views[i] && UniversalIsOurs(views[i]);
			changed |= g_liveCubemapSlots[i]!=ours;
			SetLiveCubemapSlot(i,ours);
			if (views[i]) views[i]->Release();
		}
		if (changed) { if(g_diagnosticsEnabled) ++g_rainStats.repairs; }
	}

	void ReportRainDraws(ULONGLONG now)
	{
		if (now<g_nextRainStatusCheck) return;
		g_nextRainStatusCheck=now+1000;
		if (!g_rainStats.draws) return;
		const auto& r=g_rainStats;
		const unsigned status=(r.noBuffer?1u:0u)|(r.wrongBuffer?2u:0u)|
			(r.unavailable?4u:0u)|(r.repairs?8u:0u)|
			(g_liveEnabled?16u:0u)|(g_liveReady?32u:0u)|
			(g_latestMappedValid && (g_latestMappedFrame.iblDesaturation.x>0.00001f ||
			g_latestMappedFrame.iblDesaturation.y>0.00001f)?64u:0u);
		if (now<g_nextRainReport && status==g_lastRainStatus) return;
		g_lastRainStatus=status;
		g_nextRainReport=now+30000;
		Log("STATUS: enabled="+std::to_string(g_liveEnabled)+
			" livePublished="+std::to_string(g_liveReady)+" draws="+std::to_string(r.draws)+
			" applied="+std::to_string(r.applied)+" noLive="+std::to_string(r.noLive)+
			" noB12="+std::to_string(r.noBuffer)+" otherB12="+std::to_string(r.wrongBuffer)+
			" unavailable="+std::to_string(r.unavailable)+" alreadyNeutral="+std::to_string(r.neutral)+
			" audits="+std::to_string(r.audits)+" repaired="+std::to_string(r.repairs)+
			" mappedSequence="+std::to_string(g_latestMappedSequence)+
			" xy=("+std::to_string(g_latestMappedFrame.iblDesaturation.x)+","+
			std::to_string(g_latestMappedFrame.iblDesaturation.y)+")");
		g_rainStats={};
	}

	template <class DrawCall>
	void DrawWithRainNeutralB12(ID3D11DeviceContext* context, DrawCall&& draw)
	{
		if (context != g_liveContext || !g_liveEnabled || !g_gameplayReady.load()) {
			draw(); return;
		}
		{ if(g_diagnosticsEnabled) ++g_rainStats.draws; }
		AuditRainBindings(context);
		if (g_liveCubemapSlotCount==0) {
			{ if(g_diagnosticsEnabled) ++g_rainStats.noLive; }
			draw(); return;
		}
		// A valid neutral snapshot needs no b12 query or replacement.
		const auto& desaturation=g_latestMappedFrame.iblDesaturation;
		if (g_latestMappedValid && std::isfinite(desaturation.x) && std::isfinite(desaturation.y) &&
			desaturation.x<=0.00001f && desaturation.y<=0.00001f) {
			{ if(g_diagnosticsEnabled) ++g_rainStats.neutral; }
			draw(); return;
		}
		ID3D11Buffer* original = nullptr;
		context->PSGetConstantBuffers(12, 1, &original);
		if (!original) {
			{ if(g_diagnosticsEnabled) ++g_rainStats.noBuffer; }
			draw(); return;
		}
		if (original != g_enginePerFrameIdentity.load(std::memory_order_relaxed)) {
			{ if(g_diagnosticsEnabled) ++g_rainStats.wrongBuffer; }
			original->Release(); draw(); return;
		}
		if (!PrepareRainNeutralFrameBuffer(context)) {
			const auto& v=g_latestMappedFrame.iblDesaturation;
			if (g_latestMappedValid && std::isfinite(v.x) && std::isfinite(v.y) &&
				v.x<=0.00001f && v.y<=0.00001f) { if(g_diagnosticsEnabled) ++g_rainStats.neutral; }
			else { if(g_diagnosticsEnabled) ++g_rainStats.unavailable; }
			original->Release(); draw(); return;
		}
		{ if(g_diagnosticsEnabled) ++g_rainStats.applied; }
		ID3D11Buffer* neutral = g_rainNeutralFrameBuffer.Get();
		context->PSSetConstantBuffers(12, 1, &neutral);
		draw();
		context->PSSetConstantBuffers(12, 1, &original);
		if (original) original->Release();
	}

	void STDMETHODCALLTYPE Hook_PSResources(ID3D11DeviceContext* context, UINT start, UINT count,
		ID3D11ShaderResourceView* const* views)
	{
		if (!g_liveEnabled || !g_gameplayReady.load(std::memory_order_relaxed) ||
			context != g_liveContext || start >= 128 || count > 128-start) {
			g_originalPSResources(context,start,count,views); return;
		}
		PerfHookScope perfHook;
        ID3D11ShaderResourceView* replacement[128]{};
		for (UINT i=0;i<count;++i) {
			auto* input=views ? views[i] : nullptr;
			const auto binding=views ? UniversalBind(start+i,input) : BindingResult{input,false};
            replacement[i]=binding.view;
            SetLiveCubemapSlot(start+i,binding.replacement);
		}
		g_originalPSResources(context,start,count,replacement);
	}

	void STDMETHODCALLTYPE Hook_DrawIndexed(ID3D11DeviceContext* context, UINT indexCount,
		UINT startIndex, INT baseVertex)
	{
		DrawWithRainNeutralB12(context, [&] {
			g_originalDrawIndexed(context, indexCount, startIndex, baseVertex);
		});
	}

	void STDMETHODCALLTYPE Hook_Draw(ID3D11DeviceContext* context, UINT vertexCount, UINT startVertex)
	{
		DrawWithRainNeutralB12(context, [&] {
			g_originalDraw(context, vertexCount, startVertex);
		});
	}

	void STDMETHODCALLTYPE Hook_DrawIndexedInstanced(ID3D11DeviceContext* context,
		UINT indexCountPerInstance, UINT instanceCount, UINT startIndex,
		INT baseVertex, UINT startInstance)
	{
		DrawWithRainNeutralB12(context, [&] {
			g_originalDrawIndexedInstanced(context, indexCountPerInstance,
				instanceCount, startIndex, baseVertex, startInstance);
		});
	}

	void STDMETHODCALLTYPE Hook_DrawInstanced(ID3D11DeviceContext* context,
		UINT vertexCountPerInstance, UINT instanceCount, UINT startVertex, UINT startInstance)
	{
		DrawWithRainNeutralB12(context, [&] {
			g_originalDrawInstanced(context, vertexCountPerInstance,
				instanceCount, startVertex, startInstance);
		});
	}

	void RepairLiveBindings(bool restoreStatic)
	{
		UniversalRepair(restoreStatic);
	}

	HRESULT STDMETHODCALLTYPE Hook_Map(
		ID3D11DeviceContext* context,
		ID3D11Resource* resource,
		UINT subresource,
		D3D11_MAP mapType,
		UINT mapFlags,
		D3D11_MAPPED_SUBRESOURCE* mapped)
	{
		const HRESULT result =
			g_originalMap(
				context,
				resource,
				subresource,
				mapType,
				mapFlags,
				mapped);

		if (g_liveEnabled && resource ==
				g_enginePerFrameIdentity.load(
					std::memory_order_relaxed) &&
			subresource == 0) {
			g_enginePerFrameMapped =
				SUCCEEDED(result) && mapped ?
					mapped->pData :
					nullptr;
		}

		return result;
	}

	void STDMETHODCALLTYPE Hook_Unmap(
		ID3D11DeviceContext* context,
		ID3D11Resource* resource,
		UINT subresource)
	{
		if (g_liveEnabled && resource ==
				g_enginePerFrameIdentity.load(
					std::memory_order_relaxed) &&
			subresource == 0 &&
			g_enginePerFrameMapped) {
			memcpy(
				&g_latestMappedFrame,
				g_enginePerFrameMapped,
				sizeof(g_latestMappedFrame));

			g_enginePerFrameMapped = nullptr;
			g_latestMappedValid = true;
			++g_latestMappedSequence;
		}

		g_originalUnmap(
			context,
			resource,
			subresource);
	}

	void ManualCaptureTick();

	void Hook_DeferredLightsImpl()
	{
		g_insideDeferredLights = true;
		g_originalDeferredLightsImpl();
		g_insideDeferredLights = false;

		// Deferred lighting has completed and the exact world-camera snapshot was
		// published by the call-site anchor inside it. Capture here, before the
		// later first-person/composite passes can add weapon and hand geometry.
		ManualCaptureTick();
	}

	using DeferredDrawOriginal_t = void(*)(
		bool,
		bool);

	DeferredDrawOriginal_t g_originalDeferredDrawTarget = nullptr;
	void* g_deferredDrawRelay = nullptr;

	void Hook_DeferredDrawCallsite(
		bool force,
		bool clear,
		std::uint32_t,
		std::uint32_t residualR9d)
	{
		if (g_diagnosticsEnabled && g_liveEnabled && g_runtimeProfile.family == RuntimeFamily::ae && g_insideDeferredLights) {
			++g_aeDrawVisits;
			if (residualR9d == 2) ++g_aeWorldDrawVisits;
		}

		g_originalDeferredDrawTarget(
			force,
			clear);

		// This must be a call-site hook, not a target-function hook:
		// the fourth "argument" is residual R9D state at the caller.
		if (g_liveEnabled && g_insideDeferredLights &&
			residualR9d == 2 &&
			g_latestMappedValid) {
			g_worldFrame =
				g_latestMappedFrame;

			g_worldFrameValid =
				HasUsableEngineFrameBuffer(
					g_worldFrame);

			g_worldFrameSequence =
				g_latestMappedSequence;
		}
	}

	void* AllocateExecutableNear(
		std::uintptr_t origin,
		std::size_t size)
	{
		SYSTEM_INFO info{};
		GetSystemInfo(
			&info);

		const std::uintptr_t granularity =
			info.dwAllocationGranularity ?
				static_cast<std::uintptr_t>(
					info.dwAllocationGranularity) :
				0x10000u;

		constexpr std::uintptr_t kRel32Reach =
			0x7FFF0000ull;

		const std::uintptr_t minAddress =
			origin > kRel32Reach ?
				origin - kRel32Reach :
				reinterpret_cast<std::uintptr_t>(
					info.lpMinimumApplicationAddress);

		const std::uintptr_t maxUser =
			reinterpret_cast<std::uintptr_t>(
				info.lpMaximumApplicationAddress);

		const std::uintptr_t maxAddress =
			origin < maxUser - kRel32Reach ?
				origin + kRel32Reach :
				maxUser;

		const auto alignDown =
			[granularity](std::uintptr_t value) {
				return value &
					~(granularity - 1);
			};

		const std::uintptr_t center =
			alignDown(origin);

		for (std::uintptr_t distance = 0;
			 distance <= kRel32Reach;
			 distance += granularity) {
			const std::uintptr_t candidates[2]{
				center >= distance ?
					center - distance :
					0,
				center <= maxAddress - distance ?
					center + distance :
					0
			};

			for (const auto candidate :
				 candidates) {
				if (candidate < minAddress ||
					candidate > maxAddress ||
					candidate == 0) {
					continue;
				}

				MEMORY_BASIC_INFORMATION mbi{};

				if (VirtualQuery(
						reinterpret_cast<void*>(
							candidate),
						&mbi,
						sizeof(mbi)) !=
					sizeof(mbi)) {
					continue;
				}

				if (mbi.State != MEM_FREE) {
					continue;
				}

				void* memory =
					VirtualAlloc(
						reinterpret_cast<void*>(
							candidate),
						size,
						MEM_RESERVE |
							MEM_COMMIT,
						PAGE_EXECUTE_READWRITE);

				if (memory) {
					return memory;
				}
			}
		}

		return nullptr;
	}

	bool PatchRel32Call(
		std::uintptr_t callsite,
		void* destination)
	{
		if (!callsite ||
			!destination) {
			return false;
		}

		auto* bytes =
			reinterpret_cast<std::uint8_t*>(
				callsite);

		if (bytes[0] != 0xE8) {
			Log(
				"FAIL: expected E8 at deferred draw callsite, got opcode=" +
					std::to_string(
						static_cast<unsigned int>(
							bytes[0])));
			return false;
		}

		const auto nextInstruction =
			callsite + 5;

		const auto delta64 =
			static_cast<std::int64_t>(
				reinterpret_cast<std::uintptr_t>(
					destination)) -
			static_cast<std::int64_t>(
				nextInstruction);

		if (delta64 <
				std::numeric_limits<std::int32_t>::min() ||
			delta64 >
				std::numeric_limits<std::int32_t>::max()) {
			Log(
				"FAIL: relay is outside rel32 reach.");
			return false;
		}

		const auto delta =
			static_cast<std::int32_t>(
				delta64);

		DWORD oldProtect = 0;

		if (!VirtualProtect(
				bytes,
				5,
				PAGE_EXECUTE_READWRITE,
				&oldProtect)) {
			Log(
				"FAIL: VirtualProtect(callsite RWX) failed.");
			return false;
		}

		bytes[0] = 0xE8;

		memcpy(
			bytes + 1,
			&delta,
			sizeof(delta));

		FlushInstructionCache(
			GetCurrentProcess(),
			bytes,
			5);

		DWORD ignored = 0;

		VirtualProtect(
			bytes,
			5,
			oldProtect,
			&ignored);

		return true;
	}


	bool InstallContextMapHooks(
		ID3D11DeviceContext* context)
	{
		if (g_contextHooksInstalled) {
			return true;
		}

		if (!context) {
			return false;
		}

		void** vtable =
			*reinterpret_cast<void***>(
				context);

		if (!vtable || !vtable[8] || !vtable[12] || !vtable[13] ||
			!vtable[14] || !vtable[15] || !vtable[20] || !vtable[21]) {
			Log(
				"FAIL: invalid D3D11 context vtable for Map/Unmap.");
			return false;
		}

		g_originalMap =
			reinterpret_cast<Map_t>(
				vtable[14]);

		g_originalUnmap =
			reinterpret_cast<Unmap_t>(
				vtable[15]);
		g_originalPSResources = reinterpret_cast<PSResources_t>(vtable[8]);
		g_originalDrawIndexed = reinterpret_cast<DrawIndexed_t>(vtable[12]);
		g_originalDraw = reinterpret_cast<Draw_t>(vtable[13]);
		g_originalDrawIndexedInstanced = reinterpret_cast<DrawIndexedInstanced_t>(vtable[20]);
		g_originalDrawInstanced = reinterpret_cast<DrawInstanced_t>(vtable[21]);
		g_liveContext = context;

		LONG error =
			DetourTransactionBegin();

		if (error != NO_ERROR) {
			Log(
				"FAIL: Map/Unmap DetourTransactionBegin=" +
				std::to_string(error));
			return false;
		}

		DetourUpdateThread(
			GetCurrentThread());

		error =
			DetourAttach(
				reinterpret_cast<PVOID*>(
					&g_originalMap),
				reinterpret_cast<PVOID>(
					&Hook_Map));

		if (error == NO_ERROR) {
			error =
				DetourAttach(
					reinterpret_cast<PVOID*>(
						&g_originalUnmap),
					reinterpret_cast<PVOID>(
						&Hook_Unmap));
		}

		if (error == NO_ERROR) error = DetourAttach(reinterpret_cast<PVOID*>(&g_originalPSResources),
			reinterpret_cast<PVOID>(&Hook_PSResources));
		if (error == NO_ERROR) error = DetourAttach(reinterpret_cast<PVOID*>(&g_originalDrawIndexed),
			reinterpret_cast<PVOID>(&Hook_DrawIndexed));
		if (error == NO_ERROR) error = DetourAttach(reinterpret_cast<PVOID*>(&g_originalDraw),
			reinterpret_cast<PVOID>(&Hook_Draw));
		if (error == NO_ERROR) error = DetourAttach(reinterpret_cast<PVOID*>(&g_originalDrawIndexedInstanced),
			reinterpret_cast<PVOID>(&Hook_DrawIndexedInstanced));
		if (error == NO_ERROR) error = DetourAttach(reinterpret_cast<PVOID*>(&g_originalDrawInstanced),
			reinterpret_cast<PVOID>(&Hook_DrawInstanced));
		if (error != NO_ERROR) {
			Log(
				"FAIL: Map/Unmap/PSResources/Draw DetourAttach=" +
				std::to_string(error));
			DetourTransactionAbort();
			return false;
		}

		error =
			DetourTransactionCommit();

		if (error != NO_ERROR) {
			Log(
				"FAIL: Map/Unmap DetourTransactionCommit=" +
				std::to_string(error));
			return false;
		}

		g_contextHooksInstalled = true;
		Log("SUCCESS: LIVE PSSetShaderResources detour installed (context slot 8).");
		Log("RAIN: scoped draw hooks installed (slots 12/13/20/21).");

		Log(
			"SUCCESS: standalone D3D11 Map/Unmap snapshot hooks installed (vtable slots 14/15).");

		return true;
	}

	bool InstallDeferredLightsHook()
	{
		REL::Relocation<std::uintptr_t> target{
			REL::ID{
				g_runtimeProfile.deferredLightsID
			}
		};

		const auto address =
			target.address();

		if (!address) {
			Log(
				"FAIL: DeferredLightsImpl runtime profile address resolved to NULL.");
			return false;
		}

		g_originalDeferredLightsImpl =
			reinterpret_cast<DeferredLightsImpl_t>(
				address);

		LONG error =
			DetourTransactionBegin();

		if (error != NO_ERROR) {
			return false;
		}

		DetourUpdateThread(
			GetCurrentThread());

		error =
			DetourAttach(
				reinterpret_cast<PVOID*>(
					&g_originalDeferredLightsImpl),
				reinterpret_cast<PVOID>(
					&Hook_DeferredLightsImpl));

		if (error != NO_ERROR) {
			DetourTransactionAbort();
			Log(
				"FAIL: DeferredLightsImpl DetourAttach=" +
				std::to_string(error));
			return false;
		}

		error =
			DetourTransactionCommit();

		if (error != NO_ERROR) {
			Log(
				"FAIL: DeferredLightsImpl DetourTransactionCommit=" +
				std::to_string(error));
			return false;
		}

		Log(
			"SUCCESS: DeferredLightsImpl runtime profile detour installed.");

		return true;
	}

	bool InstallDeferredDrawAnchorHook()
	{
		if (g_anchorHookInstalled) {
			return true;
		}

		REL::Relocation<std::uintptr_t> anchor{
			REL::ID{
				g_runtimeProfile.deferredDrawAnchorID
			}
		};

		const auto callsite =
			anchor.address() +
			g_runtimeProfile.deferredDrawAnchorOffset;

		if (!callsite) {
			Log(
				"FAIL: deferred draw anchor callsite unavailable.");
			return false;
		}

		const auto* bytes =
			reinterpret_cast<const std::uint8_t*>(
				callsite);

		if (bytes[0] != 0xE8) {
			Log(
				"FAIL: deferred draw anchor is not E8 rel32; opcode=" +
					std::to_string(
						static_cast<unsigned int>(
							bytes[0])));
			return false;
		}

		const auto originalDisplacement =
			*reinterpret_cast<const std::int32_t*>(
				callsite + 1);

		const auto originalTarget =
			callsite +
			5 +
			static_cast<std::intptr_t>(
				originalDisplacement);

		g_originalDeferredDrawTarget =
			reinterpret_cast<DeferredDrawOriginal_t>(
				originalTarget);

		// 12-byte absolute jump relay:
		//   mov rax, <Hook_DeferredDrawCallsite>
		//   jmp rax
		g_deferredDrawRelay =
			AllocateExecutableNear(
				callsite,
				0x1000);

		if (!g_deferredDrawRelay) {
			Log(
				"FAIL: could not allocate executable relay within rel32 reach.");
			return false;
		}

		auto* relay =
			reinterpret_cast<std::uint8_t*>(
				g_deferredDrawRelay);

		relay[0] = 0x48;
		relay[1] = 0xB8;

		const auto hookAddress =
			reinterpret_cast<std::uint64_t>(
				&Hook_DeferredDrawCallsite);

		memcpy(
			relay + 2,
			&hookAddress,
			sizeof(hookAddress));

		relay[10] = 0xFF;
		relay[11] = 0xE0;

		FlushInstructionCache(
			GetCurrentProcess(),
			relay,
			12);

		if (!PatchRel32Call(
				callsite,
				g_deferredDrawRelay)) {
			return false;
		}

		g_anchorHookInstalled = true;

		Log(
			"Deferred draw anchor callsite=" +
				Ptr(
					reinterpret_cast<const void*>(
						callsite)));

		Log(
			"Deferred draw original target=" +
				Ptr(
					reinterpret_cast<const void*>(
						originalTarget)));

		Log(
			"Deferred draw relay=" +
				Ptr(
					g_deferredDrawRelay));

		Log(
			"SUCCESS: exact call-site relay installed using the selected runtime profile.");

		return true;
	}

	bool AdoptEnginePerFrameIdentity(
		ID3D11DeviceContext* context)
	{
		ID3D11Buffer* bound = nullptr;

		context->PSGetConstantBuffers(
			12,
			1,
			&bound);

		if (!bound) {
			return false;
		}

		D3D11_BUFFER_DESC desc{};
		bound->GetDesc(
			&desc);
		if (g_runtimeProfile.family == RuntimeFamily::ae) {
			static bool reported=false;
			if (!reported) {
				reported=true;
				Log("AE240: PS b12 ByteWidth=" + std::to_string(desc.ByteWidth) +
					" expected=752.");
			}
		}

		const bool usable =
			(desc.BindFlags &
			 D3D11_BIND_CONSTANT_BUFFER) != 0 &&
			desc.ByteWidth >=
				sizeof(EngineFrameBuffer) &&
			(g_runtimeProfile.family != RuntimeFamily::ae || desc.ByteWidth == sizeof(EngineFrameBuffer));

		if (usable) {
			g_enginePerFrameIdentity.store(
				bound,
				std::memory_order_relaxed);
		}

		if (g_enginePerFrameIdentity.load(
				std::memory_order_relaxed) !=
			bound) {
			bound->Release();
		}

		return usable;
	}

	bool EnsureFrameBufferCaptureReady()
	{
		auto* rendererData =
			RE::BSGraphics::GetRendererData();

		if (!rendererData ||
			!rendererData->context) {
			return false;
		}

		auto* context =
			reinterpret_cast<ID3D11DeviceContext*>(
				rendererData->context);

		if (!g_contextHooksInstalled &&
			!InstallContextMapHooks(
				context)) {
			return false;
		}

		if (!g_enginePerFrameIdentity.load(
				std::memory_order_relaxed) &&
			!AdoptEnginePerFrameIdentity(
				context)) {
			return false;
		}

		return true;
	}

	bool ReadEngineFrameBuffer(EngineFrameBuffer& out)
	{

		if (g_worldFrameValid) {
			out = g_worldFrame;

			if (!g_loggedFrameBuffer) {
				g_loggedFrameBuffer = true;

				Log("");
				Log(
					"=== EXACT WORLD FRAME SNAPSHOT ===");

				Log(
					"Published from Map/Unmap at fullscreen deferred-light anchor.");

				Log(
					"snapshot sequence=" +
						std::to_string(
							g_worldFrameSequence));

				for (std::size_t row = 0;
					 row < 3;
					 ++row) {
					const auto& v =
						out.viewToWorld[row];

					Log(
						"ViewToWorld[" +
						std::to_string(row) +
						"]=(" +
						std::to_string(v.x) + ", " +
						std::to_string(v.y) + ", " +
						std::to_string(v.z) + ", " +
						std::to_string(v.w) + ")");
				}
			}

			return true;
		}

		// Capture is gated on a fresh world snapshot. Never substitute another
		// pass's b12 through synchronous GPU readback.
		return false;
	}

	bool HasUsableEngineFrameBuffer(
		const EngineFrameBuffer& frame)
	{
		float basisMagnitude = 0.0f;

		for (std::size_t row = 0; row < 3; ++row) {
			const auto& v =
				frame.viewToWorld[row];

			if (!std::isfinite(v.x) ||
				!std::isfinite(v.y) ||
				!std::isfinite(v.z) ||
				!std::isfinite(v.w)) {
				return false;
			}

			basisMagnitude +=
				std::abs(v.x) +
				std::abs(v.y) +
				std::abs(v.z);
		}

		const XMFLOAT3 origin{
			frame.viewToWorld[0].w +
				frame.cameraPosAdjust.x,
			frame.viewToWorld[1].w +
				frame.cameraPosAdjust.y,
			frame.viewToWorld[2].w +
				frame.cameraPosAdjust.z
		};

		const float originMagnitude =
			std::sqrt(
				origin.x * origin.x +
				origin.y * origin.y +
				origin.z * origin.z);

		return
			basisMagnitude > 0.1f &&
			std::isfinite(originMagnitude) &&
			originMagnitude > 1.0f;
	}

	bool BuildCommunityShadersCameraData(
		const RE::NiCamera* camera,
		const EngineFrameBuffer& frame,
		CaptureConstants& constants)
	{
		if (!camera ||
			!HasUsableEngineFrameBuffer(frame)) {
			return false;
		}

		for (std::size_t row = 0; row < 3; ++row) {
			const auto& source =
				frame.viewToWorld[row];

			constants.viewToWorld[row] =
				XMFLOAT4(
					source.x,
					source.y,
					source.z,
					0.0f);
		}

		const XMFLOAT3 currentOrigin{
			frame.viewToWorld[0].w +
				frame.cameraPosAdjust.x,
			frame.viewToWorld[1].w +
				frame.cameraPosAdjust.y,
			frame.viewToWorld[2].w +
				frame.cameraPosAdjust.z
		};

		const XMFLOAT3 previousOrigin =
			g_havePreviousCameraOrigin ?
				g_previousCameraOrigin :
				currentOrigin;

		constants.cameraOrigin =
			XMFLOAT4(
				currentOrigin.x,
				currentOrigin.y,
				currentOrigin.z,
				0.0f);

		constants.cameraPreviousOrigin =
			XMFLOAT4(
				previousOrigin.x,
				previousOrigin.y,
				previousOrigin.z,
				0.0f);

		const auto& f =
			camera->viewFrustum;

		if (f.ortho ||
			!std::isfinite(f.left) ||
			!std::isfinite(f.right) ||
			!std::isfinite(f.top) ||
			!std::isfinite(f.bottom) ||
			!std::isfinite(f.near) ||
			!std::isfinite(f.far) ||
			f.right == f.left ||
			f.top == f.bottom ||
			f.far <= f.near ||
			f.near <= 0.0f) {
			Log(
				"FAIL: invalid perspective viewFrustum.");
			return false;
		}

		// Same NDC->view contract consumed by FO4 Community Shaders.
		constants.ndcToViewMul =
			XMFLOAT4(
				f.right - f.left,
				f.bottom - f.top,
				0.0f,
				0.0f);

		constants.ndcToViewAdd =
			XMFLOAT4(
				f.left,
				f.top,
				0.0f,
				0.0f);

		const float invWidth =
			1.0f /
			(f.right - f.left);

		const float invHeight =
			1.0f /
			(f.top - f.bottom);

		const float q =
			f.far /
			(f.far - f.near);

		XMFLOAT4X4 projection{
			2.0f * invWidth, 0.0f, 0.0f, 0.0f,
			0.0f, 2.0f * invHeight, 0.0f, 0.0f,
			-(f.right + f.left) * invWidth,
			-(f.top + f.bottom) * invHeight,
			q, 1.0f,
			0.0f, 0.0f,
			-f.near * q, 0.0f
		};

		XMVECTOR determinant{};

		const XMMATRIX inverse =
			XMMatrixInverse(
				&determinant,
				XMLoadFloat4x4(
					&projection));

		const float det =
			XMVectorGetX(
				determinant);

		if (!std::isfinite(det) ||
			std::abs(det) < 1e-8f) {
			Log(
				"FAIL: projection inversion failed.");
			return false;
		}

		XMStoreFloat4x4(
			&constants.invProj,
			inverse);

		if (!g_loggedProjection) {
			g_loggedProjection = true;

			Log("");
			Log(
				"=== WORLD PROJECTION SNAPSHOT ===");
			Log(
				"NDCToViewMul=(" +
					std::to_string(constants.ndcToViewMul.x) + ", " +
					std::to_string(constants.ndcToViewMul.y) + ")");
			Log(
				"NDCToViewAdd=(" +
					std::to_string(constants.ndcToViewAdd.x) + ", " +
					std::to_string(constants.ndcToViewAdd.y) + ")");
			Log(
				"Projection source: NiCamera::viewFrustum, matching FO4 CS TryGetWorldSceneProjection contract.");
		}

		g_previousCameraOrigin =
			currentOrigin;

		g_havePreviousCameraOrigin =
			true;

		return true;
	}



	template<class T> void ReleaseOwned(T*& object)
	{
		if (object) { object->Release(); object=nullptr; }
	}
	void ClearCaptureShaders()
	{
		ReleaseOwned(g_captureCS);
		ReleaseOwned(g_inferLiveCS);
		ReleaseOwned(g_inferLiveBinaryCS);
		for (auto& shader:g_inferLiveBlendCS) ReleaseOwned(shader);
	}
	void ClearIncompleteCaptureResources()
	{
		if (!g_colorTexture || !g_colorUAV || !g_colorCubeSRV) {
			ReleaseOwned(g_colorCubeSRV);
			ReleaseOwned(g_colorUAV);
			ReleaseOwned(g_colorTexture);
		}
		if (!g_rawTexture || !g_rawUAV) {
			ReleaseOwned(g_rawUAV);
			ReleaseOwned(g_rawTexture);
		}
		if (!g_positionTexture || !g_positionUAV) {
			ReleaseOwned(g_positionUAV);
			ReleaseOwned(g_positionTexture);
		}
		if (!g_coverageTexture || !g_coverageUAV) {
			ReleaseOwned(g_coverageUAV);
			ReleaseOwned(g_coverageTexture);
		}
		if (!g_inferredTexture || !g_inferredUAV || !g_inferredSRV) {
			ReleaseOwned(g_inferredSRV);
			ReleaseOwned(g_inferredUAV);
			ReleaseOwned(g_inferredTexture);
		}
		if (!g_defaultCubeTexture || !g_defaultCubeSRV) {
			ReleaseOwned(g_defaultCubeSRV);
			ReleaseOwned(g_defaultCubeTexture);
		}
		if (!g_interiorDarkCubeTexture || !g_interiorDarkCubeSRV) {
			g_interiorDarkCubeSRV.Reset(); g_interiorDarkCubeTexture.Reset();
		}
	}

	bool CompileShader(
		ID3D11Device* device)
	{
		const bool blendShadersReady=std::all_of(std::begin(g_inferLiveBlendCS),
			std::end(g_inferLiveBlendCS),[](auto* shader) { return shader!=nullptr; });
		if (g_captureCS && g_inferLiveCS && g_inferLiveBinaryCS && blendShadersReady) {
			return true;
		}

		ClearCaptureShaders();
		struct ShaderAttempt {
			bool complete=false;
			~ShaderAttempt() { if (!complete) ClearCaptureShaders(); }
		} attempt;

		ID3DBlob* shaderBlob = nullptr;
		ID3DBlob* errorBlob = nullptr;

		const HRESULT hr =
			D3DCompile(
				kCaptureShader,
				strlen(kCaptureShader),
				"StandaloneCSUpdateCubemapCS",
				nullptr,
				nullptr,
				"main",
				"cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS,
				0,
				&shaderBlob,
				&errorBlob);

		if (FAILED(hr)) {
			std::string message =
				"D3DCompile failed.";

			if (errorBlob) {
				message += " ";

				message.append(
					static_cast<const char*>(
						errorBlob->GetBufferPointer()),
					errorBlob->GetBufferSize());
			}

			Log(message);

			if (errorBlob) {
				errorBlob->Release();
			}

			if (shaderBlob) {
				shaderBlob->Release();
			}

			return false;
		}

		if (errorBlob) {
			errorBlob->Release();
		}

		const HRESULT createHr =
			device->CreateComputeShader(
				shaderBlob->GetBufferPointer(),
				shaderBlob->GetBufferSize(),
				nullptr,
				&g_captureCS);

		shaderBlob->Release();

		if (FAILED(createHr) ||
			!g_captureCS) {
			Log(
				"CreateComputeShader failed.");

			return false;
		}

		Log(
			"Standalone Community-Shaders-style UpdateCubemap compute shader compiled successfully.");


		ID3DBlob* liveInferBlob=nullptr;
		ID3DBlob* liveInferError=nullptr;
		const D3D_SHADER_MACRO liveInferMacros[]{
			{"LIVE_LINEAR_OUTPUT","1"},
			{nullptr,nullptr}
		};
		const HRESULT liveInferCompileHr=D3DCompile(
			kInferShader,strlen(kInferShader),"StandaloneCSInferCubemapLiveLinearCS",
			liveInferMacros,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,
			&liveInferBlob,&liveInferError);
		if (FAILED(liveInferCompileHr)) {
			std::string message="InferCubemap live-linear D3DCompile failed.";
			if (liveInferError) {
				message+=" ";
				message.append(static_cast<const char*>(liveInferError->GetBufferPointer()),
					liveInferError->GetBufferSize());
			}
			Log(message);
			if (liveInferError) liveInferError->Release();
			if (liveInferBlob) liveInferBlob->Release();
			return false;
		}
		if (liveInferError) liveInferError->Release();
		const HRESULT liveInferCreateHr=device->CreateComputeShader(
			liveInferBlob->GetBufferPointer(),liveInferBlob->GetBufferSize(),nullptr,&g_inferLiveCS);
		liveInferBlob->Release();
		if (FAILED(liveInferCreateHr) || !g_inferLiveCS) {
			Log("CreateComputeShader(InferCubemap live-linear) failed.");
			return false;
		}
		Log("Standalone live-linear InferCubemap compute shader compiled successfully.");

		ID3DBlob* binaryInferBlob=nullptr;
		ID3DBlob* binaryInferError=nullptr;
		const D3D_SHADER_MACRO binaryInferMacros[]{
			{"LIVE_LINEAR_OUTPUT","1"},
			{"FALLBACK_LOCK_WEIGHT","1.0"},
			{nullptr,nullptr}
		};
		const HRESULT binaryInferCompileHr=D3DCompile(
			kInferShader,strlen(kInferShader),"StandaloneCSInferCubemapLiveBinaryCS",
			binaryInferMacros,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,
			&binaryInferBlob,&binaryInferError);
		if (FAILED(binaryInferCompileHr) || !binaryInferBlob) {
			std::string message="InferCubemap binary-validity D3DCompile failed.";
			if (binaryInferError) {
				message+=" ";
				message.append(static_cast<const char*>(binaryInferError->GetBufferPointer()),
					binaryInferError->GetBufferSize());
			}
			Log(message);
			if (binaryInferError) binaryInferError->Release();
			if (binaryInferBlob) binaryInferBlob->Release();
			return false;
		}
		if (binaryInferError) binaryInferError->Release();
		const HRESULT binaryInferCreateHr=device->CreateComputeShader(
			binaryInferBlob->GetBufferPointer(),binaryInferBlob->GetBufferSize(),nullptr,
			&g_inferLiveBinaryCS);
		binaryInferBlob->Release();
		if (FAILED(binaryInferCreateHr) || !g_inferLiveBinaryCS) {
			Log("CreateComputeShader(InferCubemap binary-validity) failed.");
			return false;
		}
		Log("Standalone binary-validity InferCubemap compute shader compiled successfully.");

		const char* blendWeights[7]={"0.125","0.25","0.375","0.5","0.625","0.75","0.875"};
		for (unsigned i=0;i<7;++i) {
			ID3DBlob* blendBlob=nullptr;
			ID3DBlob* blendError=nullptr;
			const D3D_SHADER_MACRO blendMacros[]{
				{"LIVE_LINEAR_OUTPUT","1"},
				{"FALLBACK_LOCK_WEIGHT",blendWeights[i]},
				{nullptr,nullptr}
			};
			const HRESULT blendCompileHr=D3DCompile(kInferShader,strlen(kInferShader),
				"StandaloneCSInferCubemapLiveBlendCS",blendMacros,nullptr,"main","cs_5_0",
				D3DCOMPILE_ENABLE_STRICTNESS,0,&blendBlob,&blendError);
			if (FAILED(blendCompileHr) || !blendBlob) {
				std::string message="InferCubemap staged blend D3DCompile failed.";
				if (blendError) message+=" "+std::string(
					static_cast<const char*>(blendError->GetBufferPointer()),blendError->GetBufferSize());
				Log(message);
				if (blendError) blendError->Release();
				if (blendBlob) blendBlob->Release();
				return false;
			}
			if (blendError) blendError->Release();
			const HRESULT blendCreateHr=device->CreateComputeShader(blendBlob->GetBufferPointer(),
				blendBlob->GetBufferSize(),nullptr,&g_inferLiveBlendCS[i]);
			blendBlob->Release();
			if (FAILED(blendCreateHr) || !g_inferLiveBlendCS[i]) {
				Log("CreateComputeShader(InferCubemap staged blend) failed.");
				return false;
			}
		}
		Log("Seven staged fallback-blend InferCubemap shaders compiled successfully.");

		attempt.complete=true;
		return true;
	}

	bool CreateResources(
		ID3D11Device* device)
	{
		ClearIncompleteCaptureResources();
		struct ResourceAttempt {
			~ResourceAttempt() { ClearIncompleteCaptureResources(); }
		} attempt;

		if (!CompileShader(device)) {
			return false;
		}

		if (!g_constants) {
			D3D11_BUFFER_DESC cbDesc{};
			cbDesc.ByteWidth =
				sizeof(CaptureConstants);
			cbDesc.Usage =
				D3D11_USAGE_DYNAMIC;
			cbDesc.BindFlags =
				D3D11_BIND_CONSTANT_BUFFER;
			cbDesc.CPUAccessFlags =
				D3D11_CPU_ACCESS_WRITE;

			if (FAILED(
					device->CreateBuffer(
						&cbDesc,
						nullptr,
						&g_constants))) {
				Log(
					"CreateBuffer(constants) failed.");
				return false;
			}
		}

		if (!g_linearSampler) {
			D3D11_SAMPLER_DESC desc{};
			desc.Filter =
				D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			desc.AddressU =
				D3D11_TEXTURE_ADDRESS_WRAP;
			desc.AddressV =
				D3D11_TEXTURE_ADDRESS_WRAP;
			desc.AddressW =
				D3D11_TEXTURE_ADDRESS_WRAP;
			desc.MinLOD = 0.0f;
			desc.MaxLOD =
				D3D11_FLOAT32_MAX;

			if (FAILED(
					device->CreateSamplerState(
						&desc,
						&g_linearSampler))) {
				Log(
					"CreateSamplerState failed.");
				return false;
			}
		}

		const auto createCube =
			[device](
				ID3D11Texture2D*& texture,
				ID3D11UnorderedAccessView*& uav,
				ID3D11ShaderResourceView** srv,
				bool generateMips,
				const char* label) -> bool {
				if (texture && uav &&
					(!srv || *srv)) {
					return true;
				}

				D3D11_TEXTURE2D_DESC texDesc{};
				texDesc.Width = kCubeSize;
				texDesc.Height = kCubeSize;
				texDesc.MipLevels =
					generateMips ? kCubeMipCount : 1u;
				texDesc.ArraySize = 6;
				// Repeated translations require full precision for stored finite
				// positions; half precision can quantize distant tunnel movement away.
				texDesc.Format = strcmp(label,"Position")==0 ?
					DXGI_FORMAT_R32G32B32A32_FLOAT :
					DXGI_FORMAT_R16G16B16A16_FLOAT;
				texDesc.SampleDesc.Count = 1;
				texDesc.Usage =
					D3D11_USAGE_DEFAULT;
				texDesc.BindFlags =
					D3D11_BIND_SHADER_RESOURCE |
					D3D11_BIND_UNORDERED_ACCESS;

				if (generateMips) {
					texDesc.BindFlags |=
						D3D11_BIND_RENDER_TARGET;

					texDesc.MiscFlags |=
						D3D11_RESOURCE_MISC_GENERATE_MIPS;
				}

				texDesc.MiscFlags |=
					D3D11_RESOURCE_MISC_TEXTURECUBE;

				if (FAILED(
						device->CreateTexture2D(
							&texDesc,
							nullptr,
							&texture))) {
					Log(
						std::string(
							"CreateTexture2D failed: ") +
						label);
					return false;
				}

				D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
				uavDesc.Format =
					texDesc.Format;
				uavDesc.ViewDimension =
					D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
				uavDesc.Texture2DArray.MipSlice = 0;
				uavDesc.Texture2DArray.FirstArraySlice = 0;
				uavDesc.Texture2DArray.ArraySize = 6;

				if (FAILED(
						device->CreateUnorderedAccessView(
							texture,
							&uavDesc,
							&uav))) {
					Log(
						std::string(
							"CreateUnorderedAccessView failed: ") +
						label);
					return false;
				}

				if (srv) {
					D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
					srvDesc.Format =
						texDesc.Format;
					srvDesc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE;
					srvDesc.TextureCube.MostDetailedMip=0;
					srvDesc.TextureCube.MipLevels=texDesc.MipLevels;

					if (FAILED(
							device->CreateShaderResourceView(
								texture,
								&srvDesc,
								srv))) {
						Log(
							std::string(
								"CreateShaderResourceView failed: ") +
							label);
						return false;
					}
				}

				return true;
			};

		if (!createCube(
				g_colorTexture,
				g_colorUAV,
				&g_colorCubeSRV,
				true,
				"Color") ||
			!createCube(
				g_rawTexture,
				g_rawUAV,
				nullptr,
				false,
				"Raw") ||
			!createCube(
				g_positionTexture,
				g_positionUAV,
				nullptr,
				false,
				"Position")) {
			return false;
		}

		if (!g_skyHistoryTexture || !g_skyHistoryUAV || !g_skyHistorySRV) {
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width=kCubeSize; desc.Height=kCubeSize;
			desc.MipLevels=1; desc.ArraySize=6;
			desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
			desc.SampleDesc.Count=1;
			desc.Usage=D3D11_USAGE_DEFAULT;
			desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
			D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
			ud.Format=desc.Format; ud.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			ud.Texture2DArray.ArraySize=6;
			D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
			sd.Format=desc.Format; sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE;
			sd.TextureCube.MipLevels=1;
			if (FAILED(device->CreateTexture2D(&desc,nullptr,texture.GetAddressOf())) ||
				FAILED(device->CreateUnorderedAccessView(texture.Get(),&ud,uav.GetAddressOf())) ||
				FAILED(device->CreateShaderResourceView(texture.Get(),&sd,srv.GetAddressOf()))) {
				Log("SKY HISTORY: allocation failed; capture setup will retry.");
				return false;
			}
			g_skyHistoryTexture=texture; g_skyHistoryUAV=uav; g_skyHistorySRV=srv;
			// Force the existing first-capture clear before sampling this resource.
			g_haveHistoryOrigin=false;
			Log("SKY HISTORY: sky history resources ready.");
		}

		if (!g_coverageTexture ||
			!g_coverageUAV) {
			D3D11_TEXTURE2D_DESC texDesc{};
			texDesc.Width = kCubeSize;
			texDesc.Height = kCubeSize;
			texDesc.MipLevels = 1;
			texDesc.ArraySize = 6;
			texDesc.Format =
				DXGI_FORMAT_R32_UINT;
			texDesc.SampleDesc.Count = 1;
			texDesc.Usage =
				D3D11_USAGE_DEFAULT;
			texDesc.BindFlags =
				D3D11_BIND_UNORDERED_ACCESS;
			texDesc.MiscFlags =
				D3D11_RESOURCE_MISC_TEXTURECUBE;

			if (FAILED(
					device->CreateTexture2D(
						&texDesc,
						nullptr,
						&g_coverageTexture))) {
				Log(
					"CreateTexture2D failed: Coverage");
				return false;
			}

			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format =
				DXGI_FORMAT_R32_UINT;
			uavDesc.ViewDimension =
				D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			uavDesc.Texture2DArray.MipSlice = 0;
			uavDesc.Texture2DArray.FirstArraySlice = 0;
			uavDesc.Texture2DArray.ArraySize = 6;

			if (FAILED(
					device->CreateUnorderedAccessView(
						g_coverageTexture,
						&uavDesc,
						&g_coverageUAV))) {
				Log(
					"CreateUnorderedAccessView failed: Coverage");
				return false;
			}
		}

		if (!g_inferredTexture ||
			!g_inferredSRV ||
			!g_inferredUAV) {
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = kCubeSize;
			desc.Height = kCubeSize;
			desc.MipLevels = 1;
			desc.ArraySize = 6;
			desc.Format =
				DXGI_FORMAT_R16G16B16A16_FLOAT;
			desc.SampleDesc.Count = 1;
			desc.Usage =
				D3D11_USAGE_DEFAULT;
			desc.BindFlags =
				D3D11_BIND_SHADER_RESOURCE |
				D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags =
				D3D11_RESOURCE_MISC_TEXTURECUBE;

			if (FAILED(
					device->CreateTexture2D(
						&desc,
						nullptr,
						&g_inferredTexture))) {
				Log(
					"CreateTexture2D failed: Inferred");
				return false;
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = desc.Format;
			srvDesc.ViewDimension =
				D3D11_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.MipLevels = 1;

			if (FAILED(
					device->CreateShaderResourceView(
						g_inferredTexture,
						&srvDesc,
						&g_inferredSRV))) {
				Log(
					"CreateShaderResourceView failed: Inferred");
				return false;
			}

			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = desc.Format;
			uavDesc.ViewDimension =
				D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			uavDesc.Texture2DArray.MipSlice = 0;
			uavDesc.Texture2DArray.FirstArraySlice = 0;
			uavDesc.Texture2DArray.ArraySize = 6;

			if (FAILED(
					device->CreateUnorderedAccessView(
						g_inferredTexture,
						&uavDesc,
						&g_inferredUAV))) {
				Log(
					"CreateUnorderedAccessView failed: Inferred");
				return false;
			}
		}

		if (!g_defaultCubeTexture ||
			!g_defaultCubeSRV) {
			const std::uint16_t neutralHalfRGBA[4]{
				0x3400u,
				0x3400u,
				0x3400u,
				0x3C00u
			};

			D3D11_SUBRESOURCE_DATA initialData[6]{};

			for (auto& face :
				 initialData) {
				face.pSysMem =
					neutralHalfRGBA;
				face.SysMemPitch =
					sizeof(neutralHalfRGBA);
				face.SysMemSlicePitch =
					sizeof(neutralHalfRGBA);
			}

			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = 1;
			desc.Height = 1;
			desc.MipLevels = 1;
			desc.ArraySize = 6;
			desc.Format =
				DXGI_FORMAT_R16G16B16A16_FLOAT;
			desc.SampleDesc.Count = 1;
			desc.Usage =
				D3D11_USAGE_IMMUTABLE;
			desc.BindFlags =
				D3D11_BIND_SHADER_RESOURCE;
			desc.MiscFlags =
				D3D11_RESOURCE_MISC_TEXTURECUBE;

			if (FAILED(
					device->CreateTexture2D(
						&desc,
						initialData,
						&g_defaultCubeTexture))) {
				Log(
					"CreateTexture2D failed: neutral inference fallback");
				return false;
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = desc.Format;
			srvDesc.ViewDimension =
				D3D11_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.MipLevels = 1;

			if (FAILED(
					device->CreateShaderResourceView(
						g_defaultCubeTexture,
						&srvDesc,
						&g_defaultCubeSRV))) {
				Log(
					"CreateShaderResourceView failed: neutral inference fallback");
				return false;
			}
		}

		if (!g_interiorDarkCubeTexture || !g_interiorDarkCubeSRV) {
			// Scene-linear dark brown. This is intentionally generated in memory:
			// interiors never depend on watercreekcube.dds or another shipped file.
			const std::uint16_t darkBrownHalfRGBA[4]{
				0x287Bu, // R = 0.035
				0x249Cu, // G = 0.018
				0x2019u, // B = 0.008
				0x3C00u  // A = 1.0
			};
			D3D11_SUBRESOURCE_DATA initialData[6]{};
			for (auto& face : initialData) {
				face.pSysMem=darkBrownHalfRGBA;
				face.SysMemPitch=sizeof(darkBrownHalfRGBA);
				face.SysMemSlicePitch=sizeof(darkBrownHalfRGBA);
			}
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width=1; desc.Height=1; desc.MipLevels=1; desc.ArraySize=6;
			desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; desc.SampleDesc.Count=1;
			desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
			desc.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
			if (FAILED(device->CreateTexture2D(&desc,initialData,g_interiorDarkCubeTexture.ReleaseAndGetAddressOf()))) {
				Log("CreateTexture2D failed: internal dark-brown interior fallback"); return false;
			}
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			view.Format=desc.Format; view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE;
			view.TextureCube.MostDetailedMip=0; view.TextureCube.MipLevels=1;
			if (FAILED(device->CreateShaderResourceView(g_interiorDarkCubeTexture.Get(),&view,
				g_interiorDarkCubeSRV.ReleaseAndGetAddressOf()))) {
				Log("CreateShaderResourceView failed: internal dark-brown interior fallback"); return false;
			}
			Log("CELL FALLBACK: internal scene-linear dark-brown interior cube created.");
		}


		return true;
	}

	bool DispatchInference(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* captureSRV)
	{
		if (!context ||
			!g_captureCS ||
			!g_inferLiveCS ||
			!captureSRV ||
			!g_inferredUAV ||
			!g_skyHistorySRV ||
			!g_defaultCubeSRV ||
			!g_linearSampler) {
			Log(
				"FAIL: inference resources are incomplete.");
			return false;
		}

		// Upstream base-stream path: GenerateMips(capture color) before Infer.
		context->GenerateMips(
			captureSRV);

		ID3D11ComputeShader* oldCS = nullptr;
		ID3D11ShaderResourceView* oldSRVs[3]{
			nullptr, nullptr, nullptr
		};
		ID3D11ClassInstance* oldInstances[256]{};
		UINT oldInstanceCount=256;
		ID3D11UnorderedAccessView* oldUAV = nullptr;
		ID3D11SamplerState* oldSampler = nullptr;

		context->CSGetShader(&oldCS,oldInstances,&oldInstanceCount);

		context->CSGetShaderResources(
			0,
			3,
			oldSRVs);

		context->CSGetUnorderedAccessViews(
			0,
			1,
			&oldUAV);

		context->CSGetSamplers(
			0,
			1,
			&oldSampler);

		// The configured cell fallback is identified by engine path through both
		// loose-file and streaming routes. Its pixels, size and compression are
		// deliberately not fingerprinted; D3D samples it by normalized direction.
		// Hold an owning reference until the dispatch has accepted the binding.
		// A streaming reload may replace the global exterior fallback concurrently.
		auto fallbackOwner=AcquireNamedCellFallback();
		ID3D11ShaderResourceView* fallback=
			g_activeCellKind==2 ? g_interiorDarkCubeSRV.Get() : fallbackOwner.Get();
		if (!fallback) fallback=g_defaultCubeSRV;
		ID3D11ShaderResourceView* srvs[3]{
			captureSRV,
			g_activeCellKind==1 ? g_skyHistorySRV.Get() : nullptr,
			fallback
		};

		ID3D11UnorderedAccessView* uav =
			g_inferredUAV;

		context->CSSetShaderResources(
			0,
			3,
			srvs);

		context->CSSetUnorderedAccessViews(
			0,
			1,
			&uav,
			nullptr);

		context->CSSetSamplers(
			0,
			1,
			&g_linearSampler);

		ID3D11ComputeShader* inferShader=g_inferLiveCS;
		{
			if (g_fallbackLockStarted) {
				const auto frame=g_accumulatedFrames.load(std::memory_order_relaxed);
				const auto elapsed=frame>g_fallbackLockStartFrame ? frame-g_fallbackLockStartFrame : 0u;
				const unsigned stage=std::min(8u,
					static_cast<unsigned>((static_cast<std::uint64_t>(elapsed)*8u)/g_fallbackLockFrames));
				if (stage>=8) inferShader=g_inferLiveBinaryCS;
				else if (stage>0) inferShader=g_inferLiveBlendCS[stage-1];
			}
		}
		context->CSSetShader(
			inferShader,
			nullptr,
			0);

		context->Dispatch(
			(kCubeSize + 7u) / 8u,
			(kCubeSize + 7u) / 8u,
			6);

		ID3D11ShaderResourceView* nullSRVs[3]{
			nullptr, nullptr, nullptr
		};

		ID3D11UnorderedAccessView* nullUAV =
			nullptr;

		ID3D11SamplerState* nullSampler =
			nullptr;

		context->CSSetShaderResources(
			0,
			3,
			nullSRVs);

		context->CSSetUnorderedAccessViews(
			0,
			1,
			&nullUAV,
			nullptr);

		context->CSSetSamplers(
			0,
			1,
			&nullSampler);

		context->CSSetShader(
			nullptr,
			nullptr,
			0);

		context->CSSetShader(oldCS,oldInstances,oldInstanceCount);
		for (UINT i=0;i<oldInstanceCount;++i) if (oldInstances[i]) oldInstances[i]->Release();

		context->CSSetShaderResources(
			0,
			3,
			oldSRVs);

		context->CSSetUnorderedAccessViews(
			0,
			1,
			&oldUAV,
			nullptr);

		context->CSSetSamplers(
			0,
			1,
			&oldSampler);

		if (oldCS) {
			oldCS->Release();
		}

		for (auto*& srv :
			 oldSRVs) {
			if (srv) {
				srv->Release();
			}
		}

		if (oldUAV) {
			oldUAV->Release();
		}

		if (oldSampler) {
			oldSampler->Release();
		}

		if (!g_liveEnabled) Log("InferCubemapCS dispatch complete.");

		return true;
	}



	bool DispatchAccumulationFrame(
		bool clearFirstFrame)
	{
		auto* rendererData =
			RE::BSGraphics::GetRendererData();

		if (!rendererData ||
			!rendererData->device ||
			!rendererData->context) {
			Log(
				"RendererData/device/context unavailable.");
			return false;
		}

		auto* camera =
			FindWorldNiCamera();

		if (!camera) {
			Log(
				"FAIL: Could not get world NiCamera.");
			return false;
		}

		auto* device =
			reinterpret_cast<ID3D11Device*>(
				rendererData->device);

		auto* context =
			reinterpret_cast<ID3D11DeviceContext*>(
				rendererData->context);

		auto& mainRT =
			rendererData->renderTargets[kMainRT];

		auto& mainDS =
			rendererData->depthStencilTargets[kMainDS];

		auto* sceneColorSRV =
			reinterpret_cast<ID3D11ShaderResourceView*>(
				mainRT.srView);

		auto* sceneDepthSRV =
			reinterpret_cast<ID3D11ShaderResourceView*>(
				mainDS.srViewDepth);

		auto* sceneColorTexture =
			reinterpret_cast<ID3D11Texture2D*>(
				mainRT.texture);

		auto* sceneDepthTexture =
			reinterpret_cast<ID3D11Texture2D*>(
				mainDS.texture);

		if (!sceneColorSRV ||
			!sceneDepthSRV ||
			!sceneColorTexture ||
			!sceneDepthTexture) {
			Log(
				"FAIL: RT3 color or DS2 depth unavailable.");
			return false;
		}

		D3D11_TEXTURE2D_DESC sceneDesc{};
		sceneColorTexture->GetDesc(
			&sceneDesc);
		D3D11_TEXTURE2D_DESC depthDesc{};
		sceneDepthTexture->GetDesc(&depthDesc);

		if (!CreateResources(device)) {
			return false;
		}

		EngineFrameBuffer engineFrame{};

		if (!ReadEngineFrameBuffer(engineFrame)) {
			return false;
		}


		CaptureConstants constants{};

		if (!BuildCommunityShadersCameraData(
				camera,
				engineFrame,
				constants)) {
			Log(
				"FAIL: bound PS b12 did not contain a usable world camera snapshot.");
			return false;
		}

		const XMFLOAT3 currentOrigin{
			constants.cameraOrigin.x,
			constants.cameraOrigin.y,
			constants.cameraOrigin.z
		};
		if (!std::isfinite(currentOrigin.x) || !std::isfinite(currentOrigin.y) ||
			!std::isfinite(currentOrigin.z)) return false;

		clearFirstFrame=clearFirstFrame || !g_haveHistoryOrigin;
		XMFLOAT3 historyDelta{};
		if (!clearFirstFrame) {
			historyDelta=XMFLOAT3(currentOrigin.x-g_historyOrigin.x,
				currentOrigin.y-g_historyOrigin.y,currentOrigin.z-g_historyOrigin.z);
			const float distance=std::sqrt(historyDelta.x*historyDelta.x+
				historyDelta.y*historyDelta.y+historyDelta.z*historyDelta.z);
			if (!std::isfinite(distance) || distance>g_teleportResetDistance) {
				clearFirstFrame=true;
				Log("HISTORY: large camera displacement; cleared instead of reprojecting a teleport.");
			} else if (g_reprojectEnabled && !g_reprojectFailed &&
				distance>=g_reprojectMinMove) {
				if (ReprojectHistory(device,context,historyDelta)) {
					g_historyOrigin=currentOrigin;
					historyDelta=XMFLOAT3{};
					if (!g_fallbackLockStarted) {
						g_fallbackLockStarted=true;
						g_fallbackLockStartFrame=g_accumulatedFrames.load(std::memory_order_relaxed);
						Log("FALLBACK: first reprojection started staged validity transition.");
					}
				} else {
					g_reprojectFailed=true;
					Log("REPROJECT disabled after setup failure; retaining directional history.");
				}
			} else if (!g_reprojectEnabled && distance>=g_reprojectMinMove) {
				// In direct-capture mode, keep newly stored finite positions local to
				// the current camera rather than accumulating an unused old origin.
				g_historyOrigin=currentOrigin;
				historyDelta=XMFLOAT3{};
			}
		}

		if (clearFirstFrame) {
			g_fallbackLockStarted=false;
			g_fallbackLockStartFrame=0;
			const float clear[4]{0.0f,0.0f,0.0f,0.0f};
			context->ClearUnorderedAccessViewFloat(g_colorUAV,clear);
			context->ClearUnorderedAccessViewFloat(g_rawUAV,clear);
			context->ClearUnorderedAccessViewFloat(g_skyHistoryUAV.Get(),clear);
			context->ClearUnorderedAccessViewFloat(g_positionUAV,clear);
			const UINT clearCoverage[4]{0u,0u,0u,0u};
			context->ClearUnorderedAccessViewUint(g_coverageUAV,clearCoverage);
			g_havePreviousCameraOrigin=false;
			g_historyOrigin=currentOrigin;
			g_haveHistoryOrigin=true;
			historyDelta=XMFLOAT3{};
			Log("CAPTURE: history cleared; starting fresh accumulation.");
		}

		constants.historyOffset=XMFLOAT4(historyDelta.x*0.001f,
			historyDelta.y*0.001f,historyDelta.z*0.001f,0.0f);

		float normalizedX=0.0f, normalizedY=0.0f;
		float normalizedWidth=1.0f, normalizedHeight=1.0f;
		D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
		UINT viewportCount=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		context->RSGetViewports(&viewportCount,viewports);
		const D3D11_VIEWPORT* selected=nullptr;
		float selectedArea=0.0f;
		for (UINT i=0;i<viewportCount;++i) {
			const auto& vp=viewports[i];
			const bool finite=std::isfinite(vp.TopLeftX)&&std::isfinite(vp.TopLeftY)&&
				std::isfinite(vp.Width)&&std::isfinite(vp.Height);
			const bool fits=finite&&vp.TopLeftX>=0.0f&&vp.TopLeftY>=0.0f&&
				vp.Width>=1.0f&&vp.Height>=1.0f&&
				vp.TopLeftX+vp.Width<=static_cast<float>(sceneDesc.Width)+0.5f&&
				vp.TopLeftY+vp.Height<=static_cast<float>(sceneDesc.Height)+0.5f;
			const float area=vp.Width*vp.Height;
			if (fits&&area>selectedArea) { selected=&vp; selectedArea=area; }
		}
		if (selected) {
			normalizedX=selected->TopLeftX/static_cast<float>(sceneDesc.Width);
			normalizedY=selected->TopLeftY/static_cast<float>(sceneDesc.Height);
			normalizedWidth=selected->Width/static_cast<float>(sceneDesc.Width);
			normalizedHeight=selected->Height/static_cast<float>(sceneDesc.Height);
		}
		const UINT depthOriginX=std::min(depthDesc.Width-1,
			static_cast<UINT>(std::floor(normalizedX*depthDesc.Width)));
		const UINT depthOriginY=std::min(depthDesc.Height-1,
			static_cast<UINT>(std::floor(normalizedY*depthDesc.Height)));
		const UINT depthWidth=std::clamp(
			static_cast<UINT>(std::floor(normalizedWidth*depthDesc.Width+0.5f)),
			1u,depthDesc.Width-depthOriginX);
		const UINT depthHeight=std::clamp(
			static_cast<UINT>(std::floor(normalizedHeight*depthDesc.Height+0.5f)),
			1u,depthDesc.Height-depthOriginY);
		constants.colorViewport=XMFLOAT4(normalizedX,normalizedY,normalizedWidth,normalizedHeight);
		constants.colorClamp=XMFLOAT4(
			normalizedX+0.5f/sceneDesc.Width,
			normalizedY+0.5f/sceneDesc.Height,
			normalizedX+normalizedWidth-0.5f/sceneDesc.Width,
			normalizedY+normalizedHeight-0.5f/sceneDesc.Height);
		constants.depthViewport=XMFLOAT4(static_cast<float>(depthOriginX),
			static_cast<float>(depthOriginY),static_cast<float>(depthWidth),
			static_cast<float>(depthHeight));
		// Fresh observations blend
		// by 25 percent even when finite position history has just been reprojected.
		constants.captureOptions = XMFLOAT4(g_excludeFirstPersonDepth ? 1.0f : 0.0f,
			2.0f, 0.25f, 0.0f);

		D3D11_MAPPED_SUBRESOURCE mapped{};

		if (FAILED(
				context->Map(
					g_constants,
					0,
					D3D11_MAP_WRITE_DISCARD,
					0,
					&mapped))) {
			Log(
				"Map(constants) failed.");
			return false;
		}

		memcpy(
			mapped.pData,
			&constants,
			sizeof(constants));

		context->Unmap(
			g_constants,
			0);

		ID3D11ComputeShader* oldCS = nullptr;
		ID3D11ShaderResourceView* oldSRVs[2]{
			nullptr, nullptr
		};
		ID3D11UnorderedAccessView* oldUAVs[5]{
			nullptr, nullptr, nullptr, nullptr
		};
		ID3D11Buffer* oldCB = nullptr;
		ID3D11SamplerState* oldSampler = nullptr;

		ID3D11ClassInstance* oldInstances[256]{};
		UINT oldInstanceCount=256;
		context->CSGetShader(
			&oldCS,
			oldInstances,
			&oldInstanceCount);

		context->CSGetShaderResources(
			0,
			2,
			oldSRVs);

		context->CSGetUnorderedAccessViews(
			0,
			5,
			oldUAVs);

		context->CSGetConstantBuffers(
			0,
			1,
			&oldCB);

		context->CSGetSamplers(
			0,
			1,
			&oldSampler);

		ID3D11ShaderResourceView* srvs[2]{
			sceneDepthSRV,
			sceneColorSRV
		};

		ID3D11UnorderedAccessView* uavs[5]{
			g_colorUAV,
			g_rawUAV,
			g_positionUAV,
			g_coverageUAV,
			g_skyHistoryUAV.Get()
		};

		context->CSSetShader(
			g_captureCS,
			nullptr,
			0);

		context->CSSetShaderResources(
			0,
			2,
			srvs);

		context->CSSetUnorderedAccessViews(
			0,
			5,
			uavs,
			nullptr);

		context->CSSetConstantBuffers(
			0,
			1,
			&g_constants);

		context->CSSetSamplers(
			0,
			1,
			&g_linearSampler);

		context->Dispatch(
			(kCubeSize + 7u) / 8u,
			(kCubeSize + 7u) / 8u,
			6);

		ID3D11ShaderResourceView* nullSRVs[2]{
			nullptr, nullptr
		};
		ID3D11UnorderedAccessView* nullUAVs[5]{
			nullptr, nullptr, nullptr, nullptr
		};
		ID3D11Buffer* nullCB = nullptr;
		ID3D11SamplerState* nullSampler = nullptr;

		context->CSSetShaderResources(
			0,
			2,
			nullSRVs);

		context->CSSetUnorderedAccessViews(
			0,
			5,
			nullUAVs,
			nullptr);

		context->CSSetConstantBuffers(
			0,
			1,
			&nullCB);

		context->CSSetSamplers(
			0,
			1,
			&nullSampler);

		context->CSSetShader(
			nullptr,
			nullptr,
			0);

		context->CSSetShader(
			oldCS,
			oldInstances,
			oldInstanceCount);
		for (UINT i=0;i<oldInstanceCount;++i) if (oldInstances[i]) oldInstances[i]->Release();

		context->CSSetShaderResources(
			0,
			2,
			oldSRVs);

		context->CSSetUnorderedAccessViews(
			0,
			5,
			oldUAVs,
			nullptr);

		context->CSSetConstantBuffers(
			0,
			1,
			&oldCB);

		context->CSSetSamplers(
			0,
			1,
			&oldSampler);

		if (oldCS) {
			oldCS->Release();
		}

		for (auto*& srv : oldSRVs) {
			if (srv) {
				srv->Release();
			}
		}

		for (auto*& uav : oldUAVs) {
			if (uav) {
				uav->Release();
			}
		}

		if (oldCB) {
			oldCB->Release();
		}

		if (oldSampler) {
			oldSampler->Release();
		}

		return true;
	}

    bool ApplyMaterialMip(UINT mip)
    {
        if (mip>=kCubeMipCount) return false;
        if (mip==g_materialMip) return true;
        if (!g_liveContext) { g_materialMip=mip; return true; }
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        g_liveContext->GetDevice(device.GetAddressOf());
        if (!device) return false;
        using View=Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>;
        struct PendingView { View* target; std::array<View,8>* cache; View replacement; };
        std::vector<PendingView> pending;
        pending.reserve(g_universalPools.size()+1);
        const auto prepare=[&](View& view, std::array<View,8>& cache) {
            if (!view) return true;
            cache[g_materialMip]=view;
            D3D11_SHADER_RESOURCE_VIEW_DESC desc{}; view->GetDesc(&desc);
            if (desc.ViewDimension==D3D11_SRV_DIMENSION_TEXTURECUBE) {
                desc.TextureCube.MostDetailedMip=mip; desc.TextureCube.MipLevels=1;
            } else if (desc.ViewDimension==D3D11_SRV_DIMENSION_TEXTURECUBEARRAY) {
                desc.TextureCubeArray.MostDetailedMip=mip; desc.TextureCubeArray.MipLevels=1;
            } else return false;
            Microsoft::WRL::ComPtr<ID3D11Resource> resource; view->GetResource(resource.GetAddressOf());
            PendingView entry{&view,&cache,{}};
            if (cache[mip]) entry.replacement=cache[mip];
            else if (FAILED(device->CreateShaderResourceView(resource.Get(),&desc,entry.replacement.GetAddressOf()))) return false;
            pending.push_back(std::move(entry));
            return true;
        };
        if (!prepare(g_liveSRV,g_liveMaterialViews)) return false;
        for (auto& pool:g_universalPools) if (!prepare(pool.view,pool.materialViews)) return false;
        // Validate before changing bindings; a pool can exist before its first publication.
        for (auto& pool:g_universalPools) {
            if (!pool.texture || pool.failed) continue;
            if (!g_universalReplicateCS || !g_universalLiveMipSRVs[mip] ||
                !pool.mipUAVs[mip] || !pool.tintConstants) return false;
        }
        // Commit only after all views are available; restore bindings while old views remain identifiable.
        RepairLiveBindings(true);
        // Unselected pool mips may be stale or uninitialized. Fill before exposing them,
        // including settings changes on callbacks that skip capture.
        for (auto& pool:g_universalPools) {
            if (pool.texture && !pool.failed) UniversalReplicatePool(pool, mip);
        }
        for (auto& entry:pending) {
            (*entry.cache)[mip]=entry.replacement;
            *entry.target=std::move(entry.replacement);
        }
        g_materialMip=mip;
        RebuildMaterialViewLookup();
        RepairLiveBindings(false);
        return true;
    }

	bool PublishLiveCube()
	{
		if (!g_liveContext || !g_inferredTexture || !g_colorCubeSRV) return false;
		using Microsoft::WRL::ComPtr;
		ComPtr<ID3D11Device> device; g_liveContext->GetDevice(device.GetAddressOf());
		if (!g_liveTexture || !g_liveSRV || !g_liveMipSource) {
			UINT support=0;
			if (FAILED(device->CheckFormatSupport(DXGI_FORMAT_R16G16B16A16_FLOAT,&support)) ||
				!(support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN)) return false;
			D3D11_TEXTURE2D_DESC d{}; d.Width=kCubeSize; d.Height=kCubeSize; d.MipLevels=kCubeMipCount; d.ArraySize=6;
			d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DEFAULT;
			d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
			d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE|D3D11_RESOURCE_MISC_GENERATE_MIPS;
			g_liveTexture.Reset(); g_liveSRV.Reset(); g_liveMipSource.Reset();
            for (auto& view:g_liveMaterialViews) view.Reset();
            RebuildMaterialViewLookup();
			if (FAILED(device->CreateTexture2D(&d,nullptr,g_liveTexture.GetAddressOf()))) return false;
			TagReflectionReplacement(g_liveTexture.Get());
			D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=d.Format;
			v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE; v.TextureCube.MipLevels=kCubeMipCount;
			if (FAILED(device->CreateShaderResourceView(g_liveTexture.Get(),&v,g_liveMipSource.GetAddressOf()))) return false;
			// Expose the selected material mip; internal mip views stay complete.
			v.TextureCube.MostDetailedMip=g_materialMip; v.TextureCube.MipLevels=1;
			if (FAILED(device->CreateShaderResourceView(g_liveTexture.Get(),&v,g_liveSRV.GetAddressOf()))) return false;
            RebuildMaterialViewLookup();
		}
		RepairLiveBindings(true);
		const bool inferred=DispatchInference(g_liveContext,g_colorCubeSRV);
        if(inferred) PerfStage(2);
		if (!inferred) { RepairLiveBindings(false); return false; }
		for (UINT face=0;face<6;++face) g_liveContext->CopySubresourceRegion(g_liveTexture.Get(),face*kCubeMipCount,
			0,0,0,g_inferredTexture,face,nullptr);
		// Preserve inferred mip zero; generate only the lower material-facing mips.
		g_liveContext->GenerateMips(g_liveMipSource.Get());
		UniversalPublish();
		g_liveReady=true;
		RepairLiveBindings(false);
		return true;
	}

	void BeginGameplaySession(const char* source)
	{
		// F4SE normally supplies PostLoadGame/NewGame.  The claim prevents a
		// delayed message and the renderer-side recovery from initializing the
		// same session twice.
		if (g_gameplayActivationClaimed.exchange(true)) return;
		++g_cellEpoch; g_sampledCellKind.store(0); g_activeCellKind=0; g_nextCellPoll=0;
		g_lifecycleRecoveryConfirmed.store(false);
		g_lifecycleActiveCellConfirmations.store(0);
		g_lifecycleBlockedCallbacks.store(0);
		RepairLiveBindings(true); UniversalReset();
		g_haveHistoryOrigin=false;
		g_reprojectFailed=false;
		g_autoStartPending=true;
		g_nextSettingsPoll=0;
		g_liveEnabled=false;
		g_liveReady=false;
		ResetRainReporting();
		g_callbackCount.store(0,std::memory_order_relaxed);
		g_accumulatedFrames.store(0,std::memory_order_relaxed);
		g_accumulating.store(false,std::memory_order_relaxed);
		g_previousCompositeSequence=g_worldFrameSequence;
		g_gameplayReady.store(true,std::memory_order_release);
		Log("");
		Log(std::string("LIFECYCLE: gameplay activation source=")+(source?source:"unknown"));
		Log("MCM: gameplay ready; applying saved settings.");
		Log("Camera rotation comes from fresh engine b12 snapshots; projection comes from NiCamera::viewFrustum.");
	}

	void ProbeGameplayRecovery(bool foreground)
	{
		const auto blocked=g_lifecycleBlockedCallbacks.fetch_add(1,std::memory_order_relaxed)+1;
		if (g_lifecycleRecoveryConfirmed.exchange(false,std::memory_order_acq_rel)) {
			Log("LIFECYCLE RECOVERY: main thread confirmed PlayerCharacter with active cell.");
			BeginGameplaySession("renderer recovery after missing F4SE load message");
			return;
		}
		if (!foreground) return;
		const auto now=GetTickCount64();
		if (now<g_nextLifecycleProbe || g_lifecycleProbePending.exchange(true)) return;
		g_nextLifecycleProbe=now+500;
		if (blocked==1) Log("LIFECYCLE: renderer callbacks active while gameplay activation is blocked.");
		auto* tasks=F4SE::GetTaskInterface();
		if (!tasks) {
			g_lifecycleProbePending.store(false);
			if (blocked==1) Log("LIFECYCLE: task interface unavailable; recovery probe not scheduled.");
			return;
		}
		const auto epoch=g_cellEpoch.load();
		tasks->AddTask(std::function<void()>([epoch]() {
			const auto* player=RE::PlayerCharacter::GetSingleton();
			const bool active=player && player->parentCell;
			if (epoch==g_cellEpoch.load() && active) {
				const auto confirmations=g_lifecycleActiveCellConfirmations.fetch_add(1)+1;
				if (confirmations>=2) g_lifecycleRecoveryConfirmed.store(true,std::memory_order_release);
			} else {
				g_lifecycleActiveCellConfirmations.store(0);
			}
			g_lifecycleProbePending.store(false,std::memory_order_release);
		}));
	}


    bool PerfRunAccumulation(bool reset) {
        PerfBeginCapture();
        const bool ok=DispatchAccumulationFrame(reset);
        if(ok) PerfStage(1); else PerfEndCapture();
        return ok;
    }
    bool PerfRunPublication() {
        const bool ok=PublishLiveCube(); PerfEndCapture(); return ok;
    }
	void ManualCaptureTick()
	{

		// Bindings are restored at disable/load/failure transitions, not every
		// callback while idle. Keep only settings polling to allow MCM re-enable.
		if (!g_liveEnabled && g_gameplayReady.load(std::memory_order_acquire) &&
			!g_autoStartPending && GetTickCount64()<g_nextSettingsPoll) return;
		DWORD foregroundProcess = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
		const bool foreground = foregroundProcess == GetCurrentProcessId();

		if (!g_gameplayReady.load(std::memory_order_acquire)) {
			ProbeGameplayRecovery(foreground);
		}
		if (!g_gameplayReady.load(std::memory_order_acquire)) {
			g_previousCompositeSequence = g_worldFrameSequence;
			return;
		}
		// All changes run on the existing render callback.
		const auto now=GetTickCount64();
		if(g_diagnosticsEnabled) ReportRainDraws(now);
        if(g_diagnosticsEnabled && g_liveEnabled) PerfTick(now);
		if (g_liveEnabled && now>=g_nextRainAudit) {
			g_rainAuditDue=true;
			g_nextRainAudit=now+1000;
		}
		if (foreground && now>=g_nextSettingsPoll) {
			g_nextSettingsPoll=now+500;
			LoadSettings();
		}
		if (!g_requestedEnabled) {
            if (g_liveEnabled || g_autoStartPending) {
                RepairLiveBindings(true);
                g_liveEnabled=false; g_liveReady=false; g_autoStartPending=false;
                g_accumulating.store(false); g_accumulatedFrames.store(0);
                g_enginePerFrameMapped=nullptr; g_latestMappedValid=false; g_worldFrameValid=false;
                g_haveHistoryOrigin=false; g_rainAuditDue=false;
                UniversalReset(); ResetRainReporting();
            }
            return;
        }
        if (!g_liveEnabled && !g_autoStartPending) return;
        const UINT requestedMip=MaterialMipForMode(g_requestedRoughness);
        static ULONGLONG nextMipAttempt=0;
        static int reportedFailedMode=-1;
        if (requestedMip!=g_materialMip && now>=nextMipAttempt) {
            nextMipAttempt=now+1000;
            if (ApplyMaterialMip(requestedMip)) {
                reportedFailedMode=-1;
                Log("MCM: reflection softness mode="+std::to_string(g_requestedRoughness)+
                    " materialMip="+std::to_string(g_materialMip));
            } else if (reportedFailedMode!=g_requestedRoughness) {
                reportedFailedMode=g_requestedRoughness;
                Log("MCM: reflection mip change deferred; previous views retained.");
            }
        }
		if (g_reprojectEnabled!=g_requestedReprojection) {
			g_reprojectEnabled=g_requestedReprojection;
			g_reprojectFailed=false;
			g_haveHistoryOrigin=false;
			g_fallbackLockStarted=false;
			g_fallbackLockStartFrame=0;
			Log(std::string("MCM: reprojection ")+(g_reprojectEnabled ? "ON" : "OFF"));
		}


		const auto callback = g_callbackCount.fetch_add(1, std::memory_order_relaxed) + 1;
		if (g_diagnosticsEnabled && g_runtimeProfile.family == RuntimeFamily::ae &&
			(callback == 1 || callback == 120 || callback == 600)) {
			Log("AE240 STATUS: callback=" + std::to_string(callback) +
				" drawVisits=" + std::to_string(g_aeDrawVisits) +
				" r9Equals2=" + std::to_string(g_aeWorldDrawVisits) +
				" mappedSequence=" + std::to_string(g_latestMappedSequence) +
				" worldSequence=" + std::to_string(g_worldFrameSequence) +
				" worldValid=" + std::to_string(g_worldFrameValid) +
				" liveReady=" + std::to_string(g_liveReady) +
				" accumulated=" + std::to_string(g_accumulatedFrames.load()));
		}
		if (!g_contextHooksInstalled || !g_enginePerFrameIdentity.load())
			EnsureFrameBufferCaptureReady();

		// Require publication of different mapped data since the preceding
		// composite callback, even if capture was idle at that callback.
		// This detects stale reuse; it does not prove the camera matrix convention.
		const auto sequence = g_worldFrameSequence;
		const bool fresh = !g_autoStartPending && g_worldFrameValid && sequence != 0 &&
			sequence > g_previousCompositeSequence;
		g_previousCompositeSequence = sequence;
		if (g_autoStartPending && g_contextHooksInstalled && foreground) {
			g_autoStartPending=false;
			RepairLiveBindings(true);
			ResetRainReporting();
			g_liveEnabled=g_requestedEnabled;
			// Map/Unmap and the world anchor pause while disabled. Never use a
			// pre-toggle mapped pointer or snapshot when resuming.
			g_enginePerFrameMapped=nullptr;
			g_latestMappedValid=false;
			g_worldFrameValid=false;
			g_rainNeutralFrameSequence=0;
			g_rainAuditDue=true;
			g_nextRainAudit=now+1000;
			if (g_liveEnabled) {
				UniversalReset();
				g_liveReady=false;
				g_haveHistoryOrigin=false;
				g_reprojectFailed=false;
				g_accumulatedFrames.store(0); g_accumulating.store(true);
				ResetTintAnalysis();
				Log("LIVE ON: observation-driven fallback fade enabled.");
			} else {
				g_liveReady=false;
				g_accumulating.store(false);
				UniversalReset();
				Log("MCM: LIVE OFF; original cubemap bindings restored, capture stopped.");
			}
		}
		if (!g_liveEnabled) return;
		if (UpdateCellFallbackKind()) {
			// Do not carry exterior history into an interior (or vice versa).
			g_haveHistoryOrigin=false;
			g_accumulatedFrames.store(0);
		}
		if (g_accumulating.load() && foreground) {
			if (!fresh) {
			} else {
				const auto frame = g_accumulatedFrames.load();
				// Capture every third composite callback to bound GPU work.
				const bool captureTurn=!g_liveEnabled || frame==0 || callback%3u==0u;
				if (!captureTurn) {
					// Settings and status checks still run on skipped capture frames.
				} else if (PerfRunAccumulation(frame == 0)) {
					g_accumulatedFrames.fetch_add(1);
					if (g_liveEnabled) {
						if (!PerfRunPublication()) {
							g_liveEnabled=false; g_liveReady=false; g_accumulating.store(false); RepairLiveBindings(true);
							Log("FAIL: live publish failed; restored static. Switch Dynamic reflections off/on in MCM to retry.");
						}
					}
				} else {
					RepairLiveBindings(true); g_liveEnabled=false; g_liveReady=false;
					g_accumulating.store(false);
					Log("FAIL: capture setup failed; stopping live effect.");
				}
			}
		}

	}

	void OnF4SEMessage(
		F4SE::MessagingInterface::Message* message)
	{
		if (!message) {
			return;
		}

		using MessageType =
			F4SE::MessagingInterface;
		Log("LIFECYCLE MESSAGE: type="+std::to_string(message->type)+
			" sender="+(message->sender?std::string(message->sender):std::string("<null>")));

		switch (message->type) {
		case MessageType::kPostLoadGame:
		case MessageType::kNewGame:
			BeginGameplaySession(message->type==MessageType::kNewGame?"F4SE NewGame":"F4SE PostLoadGame");
			break;

		case MessageType::kPreLoadGame:
			++g_cellEpoch; g_sampledCellKind.store(0); g_activeCellKind=0; g_nextCellPoll=0;
			RepairLiveBindings(true); UniversalReset();
			g_haveHistoryOrigin=false;
			g_reprojectFailed=false;
			g_autoStartPending=false;
			g_liveEnabled=false;
			g_liveReady=false;
			ResetRainReporting();
			g_accumulating.store(false);
			g_accumulatedFrames.store(0);
			g_worldFrameValid = false;
			g_latestMappedValid = false;
			g_enginePerFrameMapped = nullptr;
			g_gameplayReady.store(
				false,
				std::memory_order_relaxed);
			g_gameplayActivationClaimed.store(false);
			g_lifecycleRecoveryConfirmed.store(false);
			g_lifecycleProbePending.store(false);
			g_lifecycleActiveCellConfirmations.store(0);
			g_lifecycleBlockedCallbacks.store(0);
			g_nextLifecycleProbe=0;

			break;

		default:
			break;
		}
	}
}

F4SE_PLUGIN_LOAD(
	const F4SE::LoadInterface* a_f4se)
{
	if (!a_f4se || a_f4se->IsEditor()) {
		return false;
	}

	const bool supportedRuntime=SelectRuntimeProfile(a_f4se);
	{
		// Replace previous only when moving current succeeds. On rotation failure,
		// append a session separator instead of erasing the existing evidence.
		bool append=false;
		DWORD rotationError=0;
		if (GetFileAttributesA(kLogPath)!=INVALID_FILE_ATTRIBUTES &&
			!MoveFileExA(kLogPath,kPreviousLogPath,MOVEFILE_REPLACE_EXISTING)) {
			append=true; rotationError=GetLastError();
		}
		std::ofstream log(kLogPath,std::ios::out | (append?std::ios::app:std::ios::trunc));
		SYSTEMTIME utc{}; GetSystemTime(&utc);
		log << "\n=== SESSION UTC " << utc.wYear << "-" << utc.wMonth << "-" << utc.wDay
			<< " " << utc.wHour << ":" << utc.wMinute << ":" << utc.wSecond << " ===\n";
		if (rotationError) log << "LOG: rotation failed; preserved current by appending; Windows error=" << rotationError << "\n";

		log << "DynamicCubemapF4 1.4\n"
			<< "Runtime family: " << g_runtimeProfile.name << "\n"
			<< "Runtime version: " << g_runtimeVersion << "\n"
			<< "Support: send DynamicCubemapF4.log and DynamicCubemapF4.previous.log from Data/F4SE/Plugins.\n";
	}
	if (!supportedRuntime) {
		Log("COMPAT: unsupported runtime family; VR is intentionally excluded.");
		return false;
	}
	F4SE::Init(a_f4se);
	const bool mappingsValid=ProbeRuntimeMappings();
	if (!mappingsValid) {
		Log("Plugin load aborted because runtime compatibility preflight failed.");
		return false;
	}
	LoadSettings();
	Log("SETTINGS: enabled="+std::to_string(g_requestedEnabled.load())+
		" reprojection="+std::to_string(g_requestedReprojection)+
		" softness="+std::to_string(g_requestedRoughness)+
        " requestedMaterialMip="+std::to_string(MaterialMipForMode(g_requestedRoughness))+" cubeSize=128");
	Log("MCM: settings loaded.");
	if (!InstallCellFallbackHooks()) {
		Log("Plugin load aborted because cell-fallback path hooks could not be installed.");
		return false;
	}

	const auto messaging =
		F4SE::GetMessagingInterface();
	if (!messaging) {
		Log("LIFECYCLE: F4SE messaging interface unavailable; guarded renderer recovery remains enabled.");
	} else {
		const bool listenerRegistered=messaging->RegisterListener(OnF4SEMessage);
		Log(std::string("LIFECYCLE: F4SE listener registration ")+
			(listenerRegistered?"SUCCEEDED.":"FAILED; guarded renderer recovery remains enabled."));
	}
	if (!InstallDeferredLightsHook()) {
		Log(
			"Plugin load aborted because DeferredLightsImpl hook installation failed.");
		return false;
	}

	if (!InstallDeferredDrawAnchorHook()) {
		Log(
			"Plugin load aborted because fullscreen-light anchor installation failed.");
		return false;
	}

	Log("STATUS: plugin loaded; waiting for gameplay and capture publication.");
	return true;
}

// The load entry point independently rejects unknown runtimes before library init.
F4SE_PLUGIN_VERSION = []() noexcept {
    F4SE::PluginVersionData v{};
    v.PluginVersion({1, 4, 0, 0});
    v.PluginName("DynamicCubemapF4");
    v.AuthorName("103Sakamoto");
    v.UsesAddressLibraryAE(true);
    v.UsesAddressLibraryNG(true);
    v.UsesSigScanning(false);
    v.IsLayoutDependentAE(true);
    v.IsLayoutDependentNG(true);
    v.HasNoStructUse(false);
    v.CompatibleVersions({
        {1,10,163,0}, {1,10,984,0}, {1,11,240,0}
    });
    return v;
}();

// OG F4SE uses the legacy query export; NG/AE use version metadata above.
F4SE_PLUGIN_QUERY(const F4SE::QueryInterface* f4se, F4SE::PluginInfo* info)
{
    if (!f4se || !info) return false;
    info->infoVersion = F4SE::PluginInfo::kVersion;
    info->name = "DynamicCubemapF4";
    info->version = REL::Version{1,4,0,0}.pack();
    return !f4se->IsEditor() &&
        (f4se->RuntimeVersion() == REL::Version{1,10,163,0} ||
         f4se->RuntimeVersion() == REL::Version{1,10,984,0} ||
         f4se->RuntimeVersion() == REL::Version{1,11,240,0});
}
