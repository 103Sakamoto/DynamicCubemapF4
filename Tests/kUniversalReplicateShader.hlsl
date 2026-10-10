
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
