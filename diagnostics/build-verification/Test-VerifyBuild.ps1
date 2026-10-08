# Orchestration regression tests: fake Gradle, real wrapper and filesystem operations.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$externalRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $repo) 'builds/steamlink-patches'))
$sandbox = Join-Path $externalRoot ('build/verification-tests-' + [guid]::NewGuid().ToString('N'))
$cursor = $sandbox
while ($cursor) {
    if ((Test-Path -LiteralPath $cursor) -and
        ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Unsafe test ancestor: $cursor"
    }
    $cursor = [IO.Path]::GetDirectoryName($cursor)
}
$null = New-Item -ItemType Directory -Path $sandbox
$previousMode = $env:STEAMLINK_TEST_GRADLE_MODE
$previousHost = $env:STEAMLINK_TEST_PWSH
$env:STEAMLINK_TEST_PWSH = (Get-Process -Id $PID).Path
$checks = 0
function Assert($Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
    $script:checks++
}
function New-Fixture([string]$Name) {
    $fixture = Join-Path $sandbox $Name
    $null = New-Item -ItemType Directory -Path $fixture
    Copy-Item -LiteralPath (Join-Path $repo 'Verify-Build.ps1') -Destination $fixture
    @'
@echo off
set "STEAMLINK_TEST_WORK_ARGUMENT=%*"
"%STEAMLINK_TEST_PWSH%" -NoProfile -File "%~dp0fake-gradle.ps1"
exit /b %errorlevel%
'@ | Set-Content -LiteralPath (Join-Path $fixture 'gradlew.bat') -Encoding ascii
    @'
$ErrorActionPreference = 'Stop'
$argValue = $env:STEAMLINK_TEST_WORK_ARGUMENT
if ($argValue -notmatch '"?-PverificationWorkDirectory=(.*?)"? --no-daemon') { throw 'Missing scratch argument' }
$scratch = $Matches[1]
$outputRoot = [IO.Path]::GetFullPath($PSScriptRoot + '-output')
if (!$scratch.StartsWith($outputRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Wrong external output directory' }
if ($scratch.StartsWith($PSScriptRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Output remained inside project' }
if ((Get-Location).Path -ne $PSScriptRoot) { throw 'Wrong working directory' }
foreach ($relative in @('patches/libs/bundle.mpp', 'patches/reports/tests/index.html',
    'patches/test-results/test/TEST-one.xml', 'patches/classes/intermediate.class',
    'decoded-patch-audit/case/temporary/large.bin',
    'sdr10-shader-assemble-12345678-1234-1234-1234-123456789abc/report.txt',
    'sdr10-shader-assemble-12345678-1234-1234-1234-123456789abc/pair.glsl')) {
    $path = Join-Path $scratch $relative
    $null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path)
    [IO.File]::WriteAllText($path, 'fixture-output')
}
if ($env:STEAMLINK_TEST_GRADLE_MODE -in @('unsafe', 'unsafe-failure')) {
    $null = New-Item -ItemType Directory -Path (Join-Path $scratch '.git')
}
if ($env:STEAMLINK_TEST_GRADLE_MODE -eq 'junction') {
    $null = New-Item -ItemType Junction -Path (Join-Path $scratch 'escape') -Target (Join-Path $PSScriptRoot 'outside')
}
Write-Output 'FAKE_GRADLE_STDOUT'
[Console]::Error.WriteLine('FAKE_GRADLE_STDERR')
if ($env:STEAMLINK_TEST_GRADLE_MODE -in @('failure', 'unsafe-failure')) { exit 23 }
exit 0
'@ | Set-Content -LiteralPath (Join-Path $fixture 'fake-gradle.ps1') -Encoding utf8
    foreach ($relative in @('build/decoded-fixture-apks/input.apk', 'build/startup-boundary-tools/tool.jar',
        'patches/build/libs/existing.mpp', 'patches/src/main/resources/native.so', 'outside/sentinel.txt')) {
        $path = Join-Path $fixture $relative
        $null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path)
        [IO.File]::WriteAllText($path, 'preserve-me')
    }
    return $fixture
}
function Invoke-Case([string]$Name, [string]$Mode, [switch]$Keep) {
    $fixture = New-Fixture $Name
    $outputRoot = $fixture + '-output'
    $env:STEAMLINK_TEST_GRADLE_MODE = $Mode
    $failure = $null
    try { & (Join-Path $fixture 'Verify-Build.ps1') -BuildRoot $outputRoot -KeepBuildOutputs:$Keep | Out-Null }
    catch { $failure = $_.Exception.Message }
    $run = @(Get-ChildItem -LiteralPath (Join-Path $outputRoot 'build/verification') -Directory)
    Assert ($run.Count -eq 1) 'Expected 1 invocation-owned run'
    Assert (!(Test-Path -LiteralPath (Join-Path $fixture 'build/verification'))) 'Created project-local verification output'
    Assert ($run[0].FullName.StartsWith($outputRoot + '\', [StringComparison]::OrdinalIgnoreCase)) 'Run is outside external output root'
    $summary = Get-Content -LiteralPath (Join-Path $run[0].FullName 'summary.json') -Raw | ConvertFrom-Json
    foreach ($relative in @('build/decoded-fixture-apks/input.apk', 'build/startup-boundary-tools/tool.jar',
        'patches/build/libs/existing.mpp', 'patches/src/main/resources/native.so', 'outside/sentinel.txt')) {
        Assert ((Get-Content -LiteralPath (Join-Path $fixture $relative) -Raw) -eq 'preserve-me') "Changed protected input: $relative"
    }
    $log = Get-Content -LiteralPath $summary.log -Raw
    Assert ($log.Contains('FAKE_GRADLE_STDOUT') -and $log.Contains('FAKE_GRADLE_STDERR')) "Lost Gradle output: $log"
    return @{ Root=$fixture; Run=$run[0].FullName; Summary=$summary; Failure=$failure }
}
try {
    foreach ($mode in @('success', 'failure')) {
        $case = Invoke-Case "normal $mode" $mode
        Assert ($case.Summary.cleanup -eq 'removed') 'Scratch was not cleaned'
        Assert (!(Test-Path -LiteralPath $case.Summary.scratch)) 'Scratch still exists'
        Assert ($case.Summary.removedBytes -eq 28) 'Cleanup byte count includes retained artifacts'
        foreach ($relative in @('artifacts/patches/libs/bundle.mpp', 'artifacts/patches/reports/tests/index.html',
            'artifacts/patches/test-results/test/TEST-one.xml',
            'artifacts/audits/sdr10-shader-assemble-12345678-1234-1234-1234-123456789abc/pair.glsl')) {
            Assert (Test-Path -LiteralPath (Join-Path $case.Run $relative)) "Missing preserved artifact: $relative"
        }
        if ($mode -eq 'failure') {
            Assert ($case.Failure -match 'exit code 23') 'Original Gradle failure lost'
            Assert (!$case.Summary.buildSucceeded) 'Failed build reported success'
        } else { Assert (!$case.Failure -and $case.Summary.buildSucceeded) "Successful build failed: $($case.Failure)" }
    }
    $case = Invoke-Case 'keep outputs' 'success' -Keep
    Assert (!$case.Failure) 'KeepBuildOutputs failed'
    Assert (Test-Path -LiteralPath (Join-Path $case.Summary.scratch 'patches/classes/intermediate.class')) 'Keep lost intermediate'
    Assert (Test-Path -LiteralPath (Join-Path $case.Run 'artifacts/patches/libs/bundle.mpp')) 'Keep lost deliverable'
    foreach ($mode in @('unsafe', 'unsafe-failure', 'junction')) {
        $case = Invoke-Case $mode $mode
        Assert ($case.Summary.cleanup -eq 'retained-after-error') 'Unsafe scratch was not retained'
        Assert (Test-Path -LiteralPath $case.Summary.scratch) 'Unsafe scratch deleted'
        if ($mode -eq 'unsafe-failure') { Assert ($case.Failure -match 'exit code 23') 'Cleanup masked build failure' }
        else { Assert ($null -ne $case.Failure) 'Unsafe cleanup returned success' }
        if ($mode -eq 'junction') {
            # Remove only this test-owned junction itself, never its target or contents.
            [IO.Directory]::Delete((Join-Path $case.Summary.scratch 'escape'))
        }
    }
    $fixture = New-Fixture 'invalid arguments'
    $outputRoot = $fixture + '-output'
    foreach ($parameters in @(@{Tasks=@('clean')}, @{GradleArguments=@('-PverificationWorkDirectory=C:\')}, @{Tasks=@('publish')})) {
        $rejected = $false
        try { & (Join-Path $fixture 'Verify-Build.ps1') -BuildRoot $outputRoot @parameters | Out-Null } catch { $rejected = $true }
        Assert $rejected 'Unsafe arguments accepted'
        Assert (!(Test-Path -LiteralPath $outputRoot)) 'Invalid arguments created output'
    }
    foreach ($invalidRoot in @($fixture, (Join-Path $fixture 'new-output'))) {
        $rejected = $false
        try { & (Join-Path $fixture 'Verify-Build.ps1') -BuildRoot $invalidRoot | Out-Null } catch { $rejected = $true }
        Assert $rejected 'Project-local BuildRoot accepted'
        Assert (!(Test-Path -LiteralPath (Join-Path $fixture 'new-output'))) 'Rejected BuildRoot created output'
    }
    $fixture = New-Fixture 'ancestor junction'
    $outputRoot = $fixture + '-output'
    $null = New-Item -ItemType Directory -Force -Path (Join-Path $outputRoot 'build')
    $null = New-Item -ItemType Junction -Path (Join-Path $outputRoot 'build/verification') -Target (Join-Path $fixture 'outside')
    $rejected = $false
    try { & (Join-Path $fixture 'Verify-Build.ps1') -BuildRoot $outputRoot | Out-Null } catch { $rejected = $true }
    Assert $rejected 'Ancestor junction accepted'
    Assert (@(Get-ChildItem -LiteralPath (Join-Path $fixture 'outside')).Count -eq 1) 'Wrote through ancestor junction'
    [IO.Directory]::Delete((Join-Path $outputRoot 'build/verification'))
    Write-Host "PASS: $checks cleanup/preservation assertions (fake Gradle; no project build claim)."
} finally {
    $env:STEAMLINK_TEST_GRADLE_MODE = $previousMode
    $env:STEAMLINK_TEST_PWSH = $previousHost
    $full = [IO.Path]::GetFullPath($sandbox)
    if (!$full.StartsWith($externalRoot + '\build\verification-tests-', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe test cleanup path' }
    $links = @(Get-ChildItem -LiteralPath $full -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
    if ($links.Count) { throw "Test fixtures retained because links remain: $full" }
    Remove-Item -LiteralPath $full -Recurse -Force
}
