$ErrorActionPreference = "Stop"
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$outputDir = Join-Path $env:TEMP ("nektarina-tests-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $outputDir | Out-Null
function Run-Test($name, $includes, $sources, $strict = $true) {
    Write-Host "Running $name"
    $exe = Join-Path $outputDir ($name + ".exe")
    $flags = @('-Wall', '-Wextra')
    if ($strict) { $flags += '-Werror' }
    # synth.c now delegates source transitions to the shared RT core.
    if ($sources -contains "components/synth/synth.c" -and $sources -notcontains "components/synth/synth_sound.c") {
        $sources += "components/synth/synth_sound.c"
    }
    & gcc @flags @includes @sources -o $exe
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed ($LASTEXITCODE)" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "$name failed ($LASTEXITCODE)" }
}

Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $usbIncludes = @('-I','tests/usb_midi_host/stubs','-I','components/usb_midi_host/include','-I','components/usb_midi_host')
    Run-Test 'usb_midi_desc' $usbIncludes @('tests/usb_midi_host/test_usb_midi_desc.c','components/usb_midi_host/usb_midi_desc.c')
    Run-Test 'usb_midi_lifecycle' $usbIncludes @('tests/usb_midi_host/test_usb_midi_lifecycle.c','components/usb_midi_host/usb_midi_desc.c') $false
    $ingressIncludes = $usbIncludes + @('-I','components/midi_ingress/include','-I','components/midi/include','-I','components/synth/include')
    Run-Test 'midi_ingress' $ingressIncludes @('tests/midi/test_midi_ingress.c','components/midi/midi_parser.c','components/midi/midi_queue.c','components/synth/synth.c','components/synth/synth_looper.c')
    Run-Test 'midi_parser' @('-I','components/midi/include') @('tests/midi/test_midi_parser.c','components/midi/midi_parser.c')
    Run-Test 'midi_queue' @('-I','tests/usb_midi_host/stubs','-I','components/midi/include') @('tests/midi/test_midi_queue.c','components/midi/midi_queue.c')
    Run-Test 'synth' @('-I','components/synth/include','-I','components/midi/include') @('tests/synth/test_synth.c','components/synth/synth.c','components/synth/synth_looper.c')
    Run-Test 'finite_clamp' @('-I','components/synth/include') @('tests/synth/test_finite_clamp.c')
    Run-Test 'review_dsp' @('-I','tests/usb_midi_host/stubs','-I','components/synth/include','-I','components/midi/include') @('tests/synth/test_review_20261019.c','components/synth/synth.c','components/synth/synth_looper.c','components/midi/midi_parser.c','components/midi/midi_queue.c')
    Run-Test 'web_keys' @('-I','components/synth/include','-I','components/midi/include') @('tests/synth/test_web_keys.c','components/synth/synth.c','components/synth/synth_looper.c','components/synth/synth_web_keys.c')
    Run-Test 'note_ownership' @('-I','components/synth/include','-I','components/midi/include') @('tests/synth/test_note_ownership.c','components/synth/synth.c','components/synth/synth_looper.c','components/synth/synth_sound.c','components/synth/synth_web_keys.c')
    Run-Test 'looper' @('-I','components/synth/include','-I','components/midi/include') @('tests/synth/test_looper.c','components/synth/synth.c','components/synth/synth_looper.c')
    Run-Test 'review_fixes' @('-I','components/synth/include','-I','components/midi/include') @('tests/synth/test_review_fixes.c','components/synth/synth.c','components/synth/synth_looper.c','components/synth/synth_web_keys.c')
    Run-Test 'electronic_bank' @('-I','components/synth/include','-I','components/midi/include') @('tests/synth/test_electronic_bank.c','components/synth/synth.c','components/synth/synth_looper.c')
    Run-Test 'sound_transition' @('-I','components/synth/include') @('tests/synth/test_sound_transition.c','components/synth/synth_sound.c')
    Write-Host "Host suites PASS (outputs: $outputDir)"
} finally {
    Pop-Location
}
