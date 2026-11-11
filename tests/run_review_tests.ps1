$ErrorActionPreference = 'Stop'
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$out = Join-Path $env:TEMP ('nektarina-review-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $out | Out-Null
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $common = @('-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-Itests/usb_midi_host/stubs')
    $codec = 'managed_components/espressif__esp_codec_dev'
    $readback = Join-Path $out 'es8311.exe'
    $codecIncludes = @("-I$codec/include", "-I$codec/interface", '-Icomponents/audio_hal')
    & gcc @common @codecIncludes tests/storage/test_es8311_readback.c -o $readback
    if ($LASTEXITCODE -ne 0) { throw 'ES8311 test compilation failed' }
    & $readback
    if ($LASTEXITCODE -ne 0) { throw 'ES8311 test failed' }

    $boot = Join-Path $out 'ota_boot.exe'
    $bootIncludes = @('-Itests/storage/ota_boot_stubs', '-Imain', '-Icomponents/audio_hal/include',
                      '-Icomponents/soundfont/include', '-Icomponents/synth/include',
                      '-Icomponents/midi/include')
    & gcc @bootIncludes @common tests/storage/test_ota_boot.c main/app_ota_boot.c -o $boot
    if ($LASTEXITCODE -ne 0) { throw 'OTA boot test compilation failed' }
    & $boot
    if ($LASTEXITCODE -ne 0) { throw 'OTA boot test failed' }
} finally {
    Pop-Location
}
