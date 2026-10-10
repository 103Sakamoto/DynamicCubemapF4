$ErrorActionPreference='Stop'
$source=Get-Content -Raw (Join-Path (Split-Path -Parent $PSScriptRoot) 'Source/src/Main.cpp')
$start=$source.IndexOf('float UniversalAuthoredIntensity(')
$end=$source.IndexOf('struct TintProbeResult', $start)
$result=$source.Substring($start,$end-$start)
$start=$source.IndexOf('const bool exteriorAtlasCube=')
$end=$source.IndexOf('if (g_tintProbePendingPool==UINT_MAX)', $start)
$result+="`nResult evaluate(const Probe& v, UINT matchCount) {`n"+$source.Substring($start,$end-$start)
$start=$source.IndexOf('const auto newTint=keepOriginal')
$end=$source.IndexOf(';',$start)+1
$result+=$source.Substring($start,$end-$start)+"`nreturn {authoredIntensity,newTint,keepOriginal}; }`n"
Set-Content (Join-Path $PSScriptRoot 'production_exterior_balance.inc') $result
