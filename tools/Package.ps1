$ErrorActionPreference = "Stop"
python (Join-Path $PSScriptRoot "package_release.py")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
