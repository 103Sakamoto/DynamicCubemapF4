
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
