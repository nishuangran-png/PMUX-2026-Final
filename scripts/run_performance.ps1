param(
    [Parameter(Mandatory = $true)]
    [string]$PythonExe,
    [int]$Repeats = 7
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Root "candidate"
$env:PATH = "$Bin;$env:PATH"
$env:ABC = Join-Path $Bin "yosys-abc.exe"
$env:PMUX_BIN_DIR = $Bin

& $PythonExe (Join-Path $PSScriptRoot "final_measure.py") `
    --root $Root `
    --repeats $Repeats `
    --out-dir (Join-Path $Root "logs\performance")
exit $LASTEXITCODE
