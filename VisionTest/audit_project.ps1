$ErrorActionPreference = 'Stop'
[xml]$project = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'VisionTest.vcxproj')
[xml]$filters = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'VisionTest.vcxproj.filters')
$kinds = @('ClCompile','ClInclude','QtMoc','QtUic','None','ProjectReference')
$entries = @($project.SelectNodes('//*[@Include]') | Where-Object LocalName -in $kinds)
$missing = @()
$seen = @{}
foreach ($entry in $entries) {
    $path = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $entry.Include))
    if (-not (Test-Path -LiteralPath $path)) { $missing += $entry.Include }
    $key = $entry.LocalName + ':' + $entry.Include
    if ($seen.ContainsKey($key)) { throw "Duplicate project entry: $key" }
    $seen[$key] = $true
    if ($entry.LocalName -ne 'ProjectReference') {
        $matched = @($filters.SelectNodes('//*[@Include]') | Where-Object {
            $_.LocalName -eq $entry.LocalName -and $_.Include -eq $entry.Include
        })
        if ($matched.Count -ne 1) { throw "Missing or duplicate filter entry: $key" }
    }
}
if ($missing.Count) { throw "Missing files: $($missing -join ', ')" }
foreach ($directory in 'source','include') {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot $directory) -File -Recurse) {
        if ($file.Extension -notin '.h','.cpp') { continue }
        $relative = [IO.Path]::GetRelativePath($PSScriptRoot,$file.FullName)
        if (-not @($entries | Where-Object Include -eq $relative).Count) {
            throw "Unregistered source file: $relative"
        }
    }
}
Write-Host "Project audit passed: $($entries.Count) entries; all source files and filters match."
