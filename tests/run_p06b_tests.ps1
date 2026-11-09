$ErrorActionPreference = 'Stop'
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $env:TEMP ("nektarina-p06b-" + [guid]::NewGuid() + ".exe")
Push-Location $root
try {
    & gcc --version | Select-Object -First 1
    $sources = @('tests/bt_output/test_bt_output.c','components/bt_output/bt_pcm.c','components/bt_output/bt_route.c')
    & gcc -std=c11 -O2 -Wall -Wextra -Werror -Icomponents/bt_output/include @sources -o $out
    if ($LASTEXITCODE -ne 0) { throw 'P06B host build failed' }
    & $out
    if ($LASTEXITCODE -ne 0) { throw 'P06B host tests failed' }
} finally {
    Pop-Location
}
