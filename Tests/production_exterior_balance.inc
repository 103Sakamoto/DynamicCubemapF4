float UniversalAuthoredIntensity(float luminance, bool usable, bool consistent)
{
    return (usable && consistent) ?
        std::clamp(std::sqrt(std::clamp(luminance,0.0f,1.0f)),0.02f,1.0f) : 1.0f;
}

Result evaluate(const Probe& v, UINT matchCount) {
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
				const auto newTint=keepOriginal ? XMFLOAT4(1,1,1,-2) : (usable&&consistent) ?
						XMFLOAT4(v.r/luminance,v.g/luminance,v.b/luminance,std::clamp(v.saturation,0.0f,1.0f)) :
						XMFLOAT4(1,1,1,-1);
return {authoredIntensity,newTint,keepOriginal}; }
