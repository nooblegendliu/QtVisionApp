param(
    [Parameter(Mandatory=$true)][ValidateSet('Auto','Interactive')][string]$Mode,
    [string]$OutputDirectory
)
# Compatibility entry point; all test isolation belongs to the unified runner.
$arguments = @{ Module='App'; Group=$(if($Mode -eq 'Auto'){'Ui'}else{'ManualUi'}) }
if($OutputDirectory){ $arguments.OutputDirectory=$OutputDirectory }
& (Join-Path $PSScriptRoot 'run_tests.ps1') @arguments
exit $LASTEXITCODE
