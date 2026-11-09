param([string]$BuildDirectory = 'build')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'check_environment.ps1')
if ($env:IDF_TARGET -and $env:IDF_TARGET -ne 'esp32s31') { throw 'Wrong IDF_TARGET in environment' }
Push-Location $root
try {
    & python (Join-Path $env:IDF_PATH 'tools/idf.py') --preview -B $BuildDirectory -D IDF_TARGET=esp32s31 build
    if ($LASTEXITCODE -ne 0) { throw "S31 build failed ($LASTEXITCODE)" }
} finally { Pop-Location }

