param(
    [Parameter(Mandatory=$true)][string]$ImagePath
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$savedPath = $env:PATH
try {
    $env:PATH = (Join-Path $repoRoot 'x64/Release') + ';' + $savedPath
    & (Join-Path $PSScriptRoot 'probe.exe') $ImagePath (Join-Path $PSScriptRoot 'algorithm.yaml') (Join-Path $PSScriptRoot 'output')
    if ($LASTEXITCODE -ne 0) { throw "Offline algorithm probe failed: $LASTEXITCODE" }
}
finally { $env:PATH = $savedPath }
