param([string]$IdfPath = $env:IDF_PATH)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$lock = Get-Content -LiteralPath (Join-Path $root 'config/source-baseline.json') -Raw | ConvertFrom-Json
if (-not $IdfPath) { throw 'Activate ESP-IDF 6.1.0 first; IDF_PATH is not set.' }
if (-not (Test-Path -LiteralPath (Join-Path $IdfPath 'components/soc/esp32s31'))) {
    throw "ESP32-S31 support not found in $IdfPath"
}
$sha = & git -C $IdfPath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $sha -ne $lock.target_idf_commit) {
    throw "IDF revision mismatch: expected $($lock.target_idf_commit), found $sha"
}
$dirty = & git -C $IdfPath status --porcelain --untracked-files=no
if ($LASTEXITCODE -ne 0 -or $dirty) { throw 'Pinned IDF has tracked modifications.' }
foreach ($command in @('python', 'cmake', 'ninja', 'riscv32-esp-elf-gcc')) {
    $found = Get-Command $command -ErrorAction Stop
    Write-Output "$command = $($found.Source)"
}
& python (Join-Path $IdfPath 'tools/idf.py') --version
if ($LASTEXITCODE -ne 0) { throw 'idf.py version check failed' }
& python (Join-Path $IdfPath 'tools/idf.py') --preview --list-targets
if ($LASTEXITCODE -ne 0) { throw 'Target listing failed' }
Write-Output "Environment PASS: IDF $sha; S31 is a preview target. No device accessed."

