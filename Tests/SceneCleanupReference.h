// SPDX-License-Identifier: GPL-3.0-only
#pragma once
inline constexpr char AcceptedInference[] = R"CLEANREF(
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

    // FO4 keeps sky in a separate history; compose it behind observed geometry.
    float4 sky = SkyHistoryTexture.SampleLevel(LinearSampler, uv, 0.0);
    color += sky * (1.0 - saturate(color.a));

    // CS non-REFLECTIONS inference, pinned at b81bf11cf126f41aaa53d7318b61293677d9c0cc.
	float mipLevel = 0.0;

#if !defined(REFLECTIONS)
	float k = 1.5;
	float brightness = k;
#endif

	while (color.w < 1.0 && mipLevel <= 9) {
		mipLevel++;

		float4 tempColor = 0.0;
		if (mipLevel < 9) {
			tempColor = EnvCaptureTexture.SampleLevel(LinearSampler, uv, mipLevel);
		} else {
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(-1.0, 0.0, 0.0), 10);
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(1.0, 0.0, 0.0), 10);
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(0.0, -1.0, 0.0), 10);
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(0.0, 1.0, 0.0), 10);
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(0.0, 0.0, -1.0), 10);
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(0.0, 0.0, 1.0), 10);
		}

#if !defined(REFLECTIONS)
		tempColor *= brightness;
		brightness *= k;
#endif

		if ((color.w + tempColor.w) > 1.0) {
			mipLevel -= color.w;
			float alphaDiff = 1.0 - color.w;
			tempColor.xyzw *= alphaDiff / tempColor.w;
			color.xyzw += tempColor;
			break;
		} else {
			color.xyzw += tempColor;
		}
	}

    // History RGB is confidence-weighted. Recover captured radiance when the
    // inference loop ends with incomplete coverage; this is not exposure gain.
    // Static fallback is used only when no usable captured sample exists.
    if (color.a <= 0.0001)
        color.rgb = DefaultCubemap.SampleLevel(LinearSampler, uv, 0.0).rgb;
    else
        color.rgb /= max(color.a, 0.0001);

#if !defined(LIVE_LINEAR_OUTPUT)
	color.rgb =
		LinearToIrradiance(
			color.rgb);
#endif

	EnvInferredTexture[threadID] =
		max(0.0, color);
}
)CLEANREF";
