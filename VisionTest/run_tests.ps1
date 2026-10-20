param(
    [ValidateSet('All', 'Core', 'Algorithm', 'Hardware', 'Tcp', 'App')]
    [string]$Module = 'All',
    [ValidateSet('Default', 'Ui', 'MvsEnvironment', 'ManualUi')]
    [string]$Group = 'Default',
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
if (($Group -in 'Ui', 'ManualUi') -and $Module -ne 'App') {
    throw 'Ui and ManualUi require -Module App.'
}
if ($Group -eq 'MvsEnvironment' -and $Module -ne 'Hardware') {
    throw 'MvsEnvironment requires -Module Hardware.'
}
$executable = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../x64/Release/VisionTest.exe'))
if (-not (Test-Path -LiteralPath $executable)) {
    throw 'VisionTest.exe is missing. Build Release|x64 explicitly; this script does not build.'
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path ([IO.Path]::GetTempPath()) 'QtVisionApp-VisionTest-Results'
}
$runDirectory = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) (
    (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $Module + '-' + $Group + '-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($runDirectory) | Out-Null
$filter = switch ($Group) {
    'Ui' { '*DISABLED_App_Ui_*.*' }
    'ManualUi' { '*DISABLED_App_ManualUi_*.*' }
    'MvsEnvironment' { '*DISABLED_Hardware_Mvs_*.*' }
    default {
        if ($Module -eq 'All') { '*-DISABLED_*.*:*/DISABLED_*.*' }
        else { "*${Module}_*.*-DISABLED_*.*:*/DISABLED_*.*" }
    }
}
$xmlPath = Join-Path $runDirectory 'results.xml'
[IO.File]::WriteAllText((Join-Path $runDirectory 'stdout.log'), '')
[IO.File]::WriteAllText((Join-Path $runDirectory 'stderr.log'), '')
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = $executable
$info.WorkingDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$info.StandardOutputEncoding = [Text.Encoding]::UTF8
$info.StandardErrorEncoding = [Text.Encoding]::UTF8
$info.ArgumentList.Add("--gtest_filter=$filter")
$info.ArgumentList.Add("--gtest_output=xml:$xmlPath")
$info.ArgumentList.Add('--gtest_color=no')
if ($Group -ne 'Default') { $info.ArgumentList.Add('--gtest_also_run_disabled_tests') }
$info.Environment['VISIONTEST_GROUP'] = $Group
$info.Environment['VISIONTEST_OUTPUT'] = $runDirectory
$scratch = Join-Path $runDirectory 'scratch'
[IO.Directory]::CreateDirectory($scratch) | Out-Null
$info.Environment['TEMP'] = $scratch
$info.Environment['TMP'] = $scratch
$process = [Diagnostics.Process]::new()
$process.StartInfo = $info
$summary = [ordered]@{
    module = $Module; group = $Group; filter = $filter; output = $runDirectory
    executable = $executable; executableSha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    startedUtc = [DateTime]::UtcNow.ToString('o'); status = 'infrastructure_failure'
    executed = 0; failures = 0; modules = @(); exitCode = 3
}
Write-Host "Output: $runDirectory"
try {
    if (-not $process.Start()) { throw 'Failed to start VisionTest.' }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(600000)) {
        # The guard owns only this test process; it never terminates a production QThread.
        $summary.status = 'timeout'
        $process.Kill()
        $process.WaitForExit()
    }
    $stdout.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $runDirectory 'stdout.log') -Encoding utf8
    $stderr.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $runDirectory 'stderr.log') -Encoding utf8
    if ($summary.status -eq 'timeout') { throw 'Test process exceeded the 600 second guard.' }
    if (-not (Test-Path -LiteralPath $xmlPath)) { throw 'Test process did not produce XML.' }
    [xml]$report = Get-Content -Raw -LiteralPath $xmlPath
    $cases = @($report.SelectNodes('//testcase') | Where-Object { $_.status -eq 'run' })
    $summary.executed = $cases.Count
    $summary.failures = [int]$report.testsuites.failures
    if ($cases.Count -eq 0) { throw 'Filter selected zero executable tests.' }
    foreach ($name in 'Core', 'Algorithm', 'Hardware', 'Tcp', 'App') {
        $selected = @($cases | Where-Object { $_.classname -like "*${name}_*" })
        if ($selected.Count -gt 0) {
            $failed = @($selected | Where-Object { $_.SelectNodes('failure').Count -gt 0 }).Count
            $summary.modules += [ordered]@{ module = $name; executed = $selected.Count; failed = $failed }
        }
    }
    $summary.exitCode = if ($process.ExitCode -eq 0 -and $summary.failures -eq 0) { 0 } else { 1 }
    $summary.status = if ($summary.exitCode -eq 0) { 'passed' } else { 'failed' }
}
catch {
    $summary['diagnostic'] = $_.Exception.Message
    Write-Warning $_.Exception.Message
}
finally {
    $summary['finishedUtc'] = [DateTime]::UtcNow.ToString('o')
    $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runDirectory 'summary.json') -Encoding utf8
    $process.Dispose()
}
Write-Host "$($summary.status): executed=$($summary.executed), failures=$($summary.failures)"
exit $summary.exitCode
