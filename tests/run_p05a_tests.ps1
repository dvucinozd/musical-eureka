param([switch]$P05b)
$ErrorActionPreference = 'Stop'
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$root = Split-Path $PSScriptRoot -Parent
$temp = Join-Path $env:TEMP ('nektarina-p05a-' + [guid]::NewGuid())
$fixtures = Join-Path $temp 'tests/soundfont'
New-Item -ItemType Directory -Path $fixtures -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'tests/soundfont/make_fixtures.py') -Destination $fixtures
& python (Join-Path $fixtures 'make_fixtures.py')
if ($LASTEXITCODE -ne 0) { throw 'SF2 fixture generation failed' }

function Invoke-Suite($name, $include, $sources, $argsList) {
    $binary = Join-Path $temp ($name + '.exe')
    $absoluteSources = $sources | ForEach-Object { Join-Path $root $_ }
    $includes = @($include | ForEach-Object { "-I$_" })
    & gcc -std=c11 -O1 @includes @absoluteSources -lm -o $binary
    if ($LASTEXITCODE -ne 0) { throw "$name compile failed ($LASTEXITCODE)" }
    & $binary @argsList
    if ($LASTEXITCODE -ne 0) { throw "$name failed ($LASTEXITCODE)" }
}

Push-Location $temp
try {
    Invoke-Suite 'bank-store' (Join-Path $root 'components/bank_store/include') @(
        'tests/storage/test_bank_store.c', 'components/bank_store/bank_store.c') @()
    $include = Join-Path $root 'components/soundfont/include'
    $engine = @('components/soundfont/sf2_validate.c','components/soundfont/soundfont_engine.c')
    Invoke-Suite 'sf-contract' $include ($engine + 'tests/soundfont/test_contract.c') @()
    Invoke-Suite 'sf-lifecycle' $include ($engine + @(
        'components/soundfont/soundfont_lifecycle.c','tests/soundfont/test_lifecycle.c')) @()
    Invoke-Suite 'sf-alloc-render' $include ($engine + 'tests/soundfont/test_soundfont.c') @(
        'tests/soundfont/fixtures/mono.sf2')
    Write-Host 'P05a host suites PASS'
    if ($P05b) {
        $dirs = @(
            'components/soundfont/include','components/bank_store/include','components/audio_hal',
            'components/audio_hal/include','components/synth/include','components/midi/include',
            'tests/usb_midi_host/stubs','main') | ForEach-Object { Join-Path $root $_ }
        Invoke-Suite 'p05b-audio' $dirs ($engine + @(
            'components/soundfont/soundfont_lifecycle.c','components/audio_hal/audio_sampler.c',
            'components/synth/synth.c','components/synth/synth_looper.c','components/synth/synth_sound.c',
            'main/preset_codec.c','tests/storage/test_p05b.c')) @('tests/soundfont/fixtures/mono.sf2')
        Write-Host 'P05b host suites PASS'
    }
} finally { Pop-Location }
