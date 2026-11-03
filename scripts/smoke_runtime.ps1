param(
    [string]$PythonExe = "python"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Baseline = Join-Path $Root "baseline\yosys.exe"
$Candidate = Join-Path $Root "candidate\yosys.exe"
$Abc = Join-Path $Root "candidate\yosys-abc.exe"
$env:PATH = (Join-Path $Root "candidate") + ";" + $env:PATH
$env:ABC = $Abc

& $Baseline -V
if ($LASTEXITCODE -ne 0) { throw "baseline version check failed" }
& $Candidate -V
if ($LASTEXITCODE -ne 0) { throw "candidate version check failed" }

$Cases = @(
    "cases\official\test1\test1.v",
    "cases\official\test4\test4.v",
    "cases\five\pmux_case02_pipeline.v"
)
foreach ($Relative in $Cases) {
    $Source = Join-Path $Root $Relative
    $Top = [IO.Path]::GetFileNameWithoutExtension($Source)
    Push-Location (Split-Path -Parent $Source)
    try {
        & $Candidate -q -p "read_verilog $([IO.Path]::GetFileName($Source)); synth_intel -family cycloneiv -top $Top; stat"
        if ($LASTEXITCODE -ne 0) { throw "candidate smoke failed: $Top" }
        Write-Output "candidate smoke PASS: $Top"
    }
    finally {
        Pop-Location
    }
}
