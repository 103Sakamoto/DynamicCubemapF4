$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $projectRoot 'Source\build\windows\x64\releasedbg\DynamicCubemapF4.dll'
if (-not (Test-Path -LiteralPath $dll)) { throw 'Build the release DLL with Source\BUILD.cmd first.' }
$dist = Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$stage = Join-Path $dist ('stage-' + [guid]::NewGuid().ToString('N'))
try {
    $plugins = Join-Path $stage 'F4SE\Plugins'
    New-Item -ItemType Directory -Force -Path $plugins | Out-Null
    Copy-Item -LiteralPath $dll -Destination $plugins
    $mcm = Join-Path $stage 'MCM'
    New-Item -ItemType Directory -Force -Path $mcm | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot 'Data\MCM\Config') -Destination $mcm -Recurse
    foreach ($name in @('LICENSE.txt','THIRD_PARTY_NOTICES.md','CHANGELOG.md','Licenses')) {
        Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $stage -Recurse
    }
    $readme = @'
DynamicCubemapF4 1.3.1
Install this archive with a mod manager, or merge F4SE and MCM into Fallout 4's Data directory.
Requires matching F4SE and Address Library. Install MCM for the settings menu.
Supported executables: 1.10.163, 1.10.984, 1.11.240. VR is not supported.
Softness: Default=mip 2, Sharper=mip 0, Softer=mip 4. Reprojection adds GPU work.
Support logs: Data/F4SE/Plugins/DynamicCubemapF4.log and DynamicCubemapF4.previous.log.
Source: use the source download or repository tag accompanying this release.
'@
    Set-Content -LiteralPath (Join-Path $stage 'README.txt') -Value $readme -Encoding UTF8
    $zip = Join-Path $dist 'DynamicCubemapF4-1.3.1.zip'
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
    Write-Output "Created: $zip"
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
