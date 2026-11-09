$ErrorActionPreference = 'Stop'
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $env:TEMP ('nektarina-p06-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $out | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'soundfont/make_fixtures.py') -Destination $out
& python (Join-Path $out 'make_fixtures.py')
if ($LASTEXITCODE -ne 0) { throw 'SF2 fixture generation failed' }

Push-Location $root
try {
    $fixtureDir = Join-Path $out 'fixtures'
    $common = @(
        'components/soundfont/soundfont_lifecycle.c', 'components/audio_hal/audio_sampler.c',
        'components/synth/synth.c', 'components/synth/synth_looper.c', 'components/synth/synth_sound.c',
        'tests/soundfont/test_p06.c'
    )
    $includes = @(
        'components/soundfont/include', 'components/audio_hal', 'components/audio_hal/include',
        'components/synth/include', 'components/midi/include', 'tests/usb_midi_host/stubs'
    ) | ForEach-Object { "-I$_" }
    $engines = @{
        reference = 'reference/nektar-p4/components/soundfont'
        normal = 'components/soundfont'
        telemetry = 'components/soundfont'
    }
    & gcc --version | Select-Object -First 1
    foreach ($variant in @('reference', 'normal', 'telemetry')) {
        $engine = $engines[$variant]
        $sources = $common + @("$engine/soundfont_engine.c", "$engine/sf2_validate.c")
        $flags = @('-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function')
        if ($variant -eq 'telemetry') {
            $sources += 'components/audio_hal/audio_perf.c'
            $flags += '-DNEKTAR_P06_TELEMETRY=1'
        }
        $exe = Join-Path $out "$variant.exe"
        & gcc @flags @includes @sources -lm -o $exe
        if ($LASTEXITCODE -ne 0) { throw "P06 $variant compile failed ($LASTEXITCODE)" }
    }

    $fixtures = [ordered]@{mono = 1; dynamic = 1; two_layers = 2}
    foreach ($fixture in $fixtures.Keys) {
        $bank = Join-Path $fixtureDir "$fixture.sf2"
        $bankSha = (Get-FileHash -LiteralPath $bank -Algorithm SHA256).Hash.ToLowerInvariant()
        $hashes = @()
        foreach ($variant in @('reference', 'normal', 'telemetry')) {
            $pcm = Join-Path $out "$fixture-$variant.f32"
            Write-Host "Running $variant/$fixture"
            & (Join-Path $out "$variant.exe") $bank $fixtures[$fixture] $pcm
            if ($LASTEXITCODE -ne 0) { throw "P06 $variant/$fixture failed ($LASTEXITCODE)" }
            if ((Get-Item -LiteralPath $pcm).Length -ne (137 * 128 * 4)) { throw "Invalid PCM length for $variant/$fixture" }
            $hashes += (Get-FileHash -LiteralPath $pcm -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        if (($hashes | Select-Object -Unique).Count -ne 1) { throw "PCM differs for $fixture" }
        Write-Host "PCM IDENTICAL $fixture fixture_sha256=$bankSha frames=17536 pcm_sha256=$($hashes[0])"
    }
    Write-Host "P06 host replay PASS ($out)"
} finally {
    Pop-Location
}
