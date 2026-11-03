param(
    [Parameter(Mandatory = $true)]
    [string]$AbcExe,
    [Parameter(Mandatory = $true)]
    [string]$ToolchainBin,
    [string]$WorkDir = ""
)

$ErrorActionPreference = "Stop"
$SourceRoot = Split-Path -Parent $PSScriptRoot
if (-not $WorkDir) {
    $WorkDir = Join-Path $SourceRoot "build-reproduced"
}
$WorkDir = [IO.Path]::GetFullPath($WorkDir)
$AbcExe = (Resolve-Path $AbcExe).Path
$ToolchainBin = (Resolve-Path $ToolchainBin).Path
$env:PATH = "$ToolchainBin;$env:PATH"

New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $WorkDir "baseline-src") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $WorkDir "candidate-src") -Force | Out-Null

tar.exe -xzf (Join-Path $PSScriptRoot "baseline_source_tree.tar.gz") `
    -C (Join-Path $WorkDir "baseline-src")
if ($LASTEXITCODE -ne 0) { throw "baseline source extraction failed" }

Copy-Item -Path (Join-Path $WorkDir "baseline-src\*") `
    -Destination (Join-Path $WorkDir "candidate-src") -Recurse -Force
Copy-Item -Path (Join-Path $PSScriptRoot "final\overlay\*") `
    -Destination (Join-Path $WorkDir "candidate-src") -Recurse -Force

function Build-Yosys([string]$Name) {
    $Src = Join-Path $WorkDir "$Name-src"
    $Build = Join-Path $WorkDir "$Name-build"
    cmake -S $Src -B $Build `
        -G "MinGW Makefiles" `
        -DCMAKE_BUILD_TYPE=Release `
        -DYOSYS_WITHOUT_SLANG=ON `
        -DYOSYS_ABC_EXECUTABLE=$AbcExe
    if ($LASTEXITCODE -ne 0) { throw "$Name configure failed" }
    cmake --build $Build --target yosys --config Release -j 8
    if ($LASTEXITCODE -ne 0) { throw "$Name build failed" }
}

Build-Yosys "baseline"
Build-Yosys "candidate"

foreach ($Name in @("baseline", "candidate")) {
    $Out = Join-Path $WorkDir "$Name-runtime"
    New-Item -ItemType Directory -Path $Out -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $WorkDir "$Name-build\yosys.exe") `
        -Destination (Join-Path $Out "yosys.exe")
    Copy-Item -LiteralPath $AbcExe -Destination (Join-Path $Out "yosys-abc.exe")
    Copy-Item -LiteralPath (Join-Path $WorkDir "$Name-build\share") `
        -Destination (Join-Path $Out "share") -Recurse
    foreach ($Dll in @("libgcc_s_seh-1.dll", "libstdc++-6.dll",
                       "libwinpthread-1.dll", "libreadline8.dll",
                       "libtermcap-0.dll")) {
        $Path = Join-Path $ToolchainBin $Dll
        if (Test-Path -LiteralPath $Path) {
            Copy-Item -LiteralPath $Path -Destination $Out
        }
    }
}

Write-Output "Build output: $WorkDir"
