param(
    [string]$PythonExe = "python"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot

& $PythonExe (Join-Path $PSScriptRoot "final_equiv.py") `
    --root $Root `
    --out-dir (Join-Path $Root "logs\formal-yosys") `
    --timeout 600

exit $LASTEXITCODE
