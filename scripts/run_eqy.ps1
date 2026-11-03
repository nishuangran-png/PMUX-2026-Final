param(
    [Parameter(Mandatory = $true)]
    [string]$PythonExe,
    [Parameter(Mandatory = $true)]
    [string]$EqyExe
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Root "candidate"
$env:PATH = "$Bin;$env:PATH"
$env:ABC = Join-Path $Bin "yosys-abc.exe"
$env:PMUX_BIN_DIR = $Bin
$WorkDir = Join-Path $Root (".eqy-work-" + (Get-Date -Format "yyyyMMdd-HHmmss"))

& $PythonExe (Join-Path $PSScriptRoot "final_eqy.py") `
    --root $Root `
    --baseline (Join-Path $Root "baseline\yosys.exe") `
    --candidate (Join-Path $Root "candidate\yosys.exe") `
    --eqy $EqyExe `
    --cell-model (Join-Path $Root "source\final\dffeas_formal.v") `
    --out-dir (Join-Path $Root "logs\eqy-reproduced") `
    --work-dir $WorkDir `
    --timeout 600
exit $LASTEXITCODE
