param([string]$BuildDirectory = 'build', [string]$SdkConfig = '', [string[]]$SdkConfigDefaults = @())
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'check_environment.ps1')
if ($env:IDF_TARGET -and $env:IDF_TARGET -ne 'esp32s31') { throw 'Wrong IDF_TARGET in environment' }
Push-Location $root
try {
    $configArgs = @()
    if ($SdkConfig) {
        $configPath = if ([IO.Path]::IsPathRooted($SdkConfig)) { $SdkConfig } else { Join-Path $root $SdkConfig }
        $configArgs = @('-D', ('SDKCONFIG=' + [IO.Path]::GetFullPath($configPath)))
    }
    if ($SdkConfigDefaults) {
        $configArgs += @('-D', ('SDKCONFIG_DEFAULTS=' + ($SdkConfigDefaults -join ';')))
    }
    & python (Join-Path $env:IDF_PATH 'tools/idf.py') --preview -B $BuildDirectory -D IDF_TARGET=esp32s31 @configArgs build
    if ($LASTEXITCODE -ne 0) { throw "S31 build failed ($LASTEXITCODE)" }
} finally { Pop-Location }

