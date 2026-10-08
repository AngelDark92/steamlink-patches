# Exercise the actual settings routing and Windows wrapper with Gradle's bundled
# base plugin. This proves path/clean behavior independently of Morphe resolution.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$external = [IO.Path]::GetFullPath((Join-Path $repo '../builds/steamlink-patches'))
$sandbox = Join-Path $external ('build/layout-tests-' + [guid]::NewGuid().ToString('N'))
$fixture = Join-Path $sandbox 'checkout/steamlink-patches'
$fixtureOutput = Join-Path $sandbox 'checkout/builds/steamlink-patches'
$null = New-Item -ItemType Directory -Force -Path (Join-Path $fixture 'patches')
$null = New-Item -ItemType Directory -Force -Path (Join-Path $fixture 'gradle/wrapper')
foreach ($relative in @('gradlew.bat', 'gradle/wrapper/gradle-wrapper.jar', 'gradle/wrapper/gradle-wrapper.properties')) {
    Copy-Item -LiteralPath (Join-Path $repo $relative) -Destination (Join-Path $fixture $relative)
}
$settings = Get-Content -LiteralPath (Join-Path $repo 'settings.gradle.kts') -Raw
$routingStart = $settings.IndexOf('// Keep compiler output separate')
if ($routingStart -lt 0) { throw 'Production settings routing not found.' }
('rootProject.name = "layout-fixture"' + "`n" + 'include(":patches")' + "`n" + $settings.Substring($routingStart)) |
    Set-Content -LiteralPath (Join-Path $fixture 'settings.gradle.kts') -Encoding utf8
@'
allprojects {
    apply(plugin = "base")
    tasks.register("verifyOutput") {
        doLast {
            val base = rootDir.resolve("../builds/steamlink-patches").canonicalFile
            val name = if (path == ":verifyOutput") "root" else "patches"
            check(project.layout.buildDirectory.get().asFile.canonicalFile == base.resolve("gradle/$name"))
            check(project.providers.gradleProperty("kotlin.project.persistent.dir").get() == base.resolve(".kotlin").path)
            val marker = project.layout.buildDirectory.file("output.txt").get().asFile
            marker.parentFile.mkdirs()
            marker.writeText("external-output")
        }
    }
}
'@ | Set-Content -LiteralPath (Join-Path $fixture 'build.gradle.kts') -Encoding utf8
$sentinel = Join-Path $fixtureOutput 'build/decoded-fixture-apks/sentinel.apk'
$null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $sentinel)
[IO.File]::WriteAllText($sentinel, 'retained-fixture')
$log = Join-Path $external ('build/layout-test-' + [guid]::NewGuid().ToString('N') + '.log')
$checks = 0
function Assert($Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
    $script:checks++
}
try {
    Push-Location -LiteralPath $fixture
    try {
        & .\gradlew.bat verifyOutput :patches:verifyOutput --offline --no-daemon 2>&1 | Out-File -LiteralPath $log -Encoding utf8
        if ($LASTEXITCODE -ne 0) { throw "Gradle layout check failed. Log: $log" }
        foreach ($project in @('root', 'patches')) {
            Assert (Test-Path -LiteralPath (Join-Path $fixtureOutput "gradle/$project/output.txt")) "Missing external $project output"
        }
        Assert (Test-Path -LiteralPath (Join-Path $fixtureOutput '.gradle')) 'Project cache stayed inside checkout'
        & .\gradlew.bat clean :patches:clean --offline --no-daemon 2>&1 | Out-File -LiteralPath $log -Append -Encoding utf8
        if ($LASTEXITCODE -ne 0) { throw "Gradle clean check failed. Log: $log" }
    } finally { Pop-Location }
    foreach ($project in @('root', 'patches')) {
        Assert (!(Test-Path -LiteralPath (Join-Path $fixtureOutput "gradle/$project"))) "Gradle clean retained $project output"
    }
    Assert (([IO.File]::ReadAllText($sentinel)) -eq 'retained-fixture') 'Gradle clean removed retained input'
    foreach ($relative in @('build', 'patches/build', '.gradle', '.kotlin')) {
        Assert (!(Test-Path -LiteralPath (Join-Path $fixture $relative))) "Recreated project-local $relative"
    }
    . (Join-Path $repo 'tools/Build-Paths.ps1')
    foreach ($entry in @(@{ Old='build/case/input.apk'; New='build/case/input.apk' },
        @{ Old='patches/build/libs/bundle.mpp'; New='gradle/patches/libs/bundle.mpp' })) {
        Assert ((Convert-LegacyBuildPath $entry.Old $repo) -eq (Join-Path $external $entry.New)) 'Historical path mapping failed'
    }
    Write-Host "PASS: $checks assertions plus Gradle root/module output and Kotlin property checks. Log: $log"
} finally {
    $full = [IO.Path]::GetFullPath($sandbox)
    if (!$full.StartsWith($external + '\build\layout-tests-', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe fixture cleanup path' }
    if (@(Get-ChildItem -LiteralPath $full -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Fixture contains a reparse point; retained.' }
    Remove-Item -LiteralPath $full -Recurse -Force
}
