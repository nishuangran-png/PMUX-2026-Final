param(
    [Parameter(Mandatory = $true)]
    [string]$PythonExe
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
& $PythonExe (Join-Path $PSScriptRoot "collect_synthesis_logs.py") $Root
exit $LASTEXITCODE
