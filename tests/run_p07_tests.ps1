param(
    [string]$Image = 'build-p07/nektarina_s31.bin',
    [string]$OldImage = 'build-p06b-local/nektarina_s31.bin'
)
$ErrorActionPreference = 'Stop'
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $env:TEMP ('nektarina-p07-' + [guid]::NewGuid() + '.exe')
Push-Location $root
try {
    & gcc --version | Select-Object -First 1
    & gcc -std=c11 -O2 -Wall -Wextra -Werror -Imain tests/storage/test_p07.c main/ota_image.c main/ota_transaction.c -lbcrypt -o $exe
    if ($LASTEXITCODE -ne 0) {throw 'P07 test compile failed'}
    & $exe (Resolve-Path -LiteralPath $Image).Path (Resolve-Path -LiteralPath $OldImage).Path
    if ($LASTEXITCODE -ne 0) {throw 'P07 host tests failed'}
} finally {Pop-Location}
