[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$JavaHome,
    [string]$GradleUserHome = (Join-Path $env:USERPROFILE '.gradle'),
    [string]$DependencyDirectory,
    [string]$D8Jar
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$DependencyDirectory) { $DependencyDirectory = Join-Path $repo 'build/startup-boundary-tools' }
if (!$D8Jar) { $D8Jar = Join-Path $repo 'build/tooling/r8-9.4.17.jar' }
$java = Join-Path $JavaHome 'bin/java.exe'
$jar = Join-Path $JavaHome 'bin/jar.exe'
$compiler = Get-ChildItem (Join-Path $GradleUserHome 'wrapper/dists') -Recurse -Filter 'kotlin-compiler-embeddable-*.jar' | Sort-Object FullName | Select-Object -Last 1
if (!$compiler) { throw 'Cached Kotlin compiler required.' }
$fatJar = Join-Path $DependencyDirectory 'morphe-desktop-1.13.1-all.jar'
foreach ($path in @($java,$jar,$fatJar,$D8Jar)) { if (!(Test-Path -LiteralPath $path)) { throw "Missing build dependency: $path" } }
$output = Join-Path $repo 'build/surface-fovea-package'
$classes = Join-Path $output 'classes'
$tests = Join-Path $output 'test-classes'
$resources = Join-Path $output 'resources'
# Unique work directories avoid old class files entering a new package.
$work = Join-Path $output ([guid]::NewGuid().ToString('N'))
$classes = Join-Path $work 'classes'
$tests = Join-Path $work 'test-classes'
$resources = Join-Path $work 'resources'
foreach ($dir in @($classes,$tests,$resources)) { $null=New-Item -ItemType Directory -Path $dir -Force }
$dependencies = (Get-ChildItem -LiteralPath $DependencyDirectory -Filter '*.jar').FullName -join ';'
function Invoke-CheckedJava([string[]]$Arguments) {
    & $java @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Java build step failed ($LASTEXITCODE)" }
}
function Compile-Kotlin([string]$SourceRoot,[string]$Destination,[string]$Classpath,[string]$FriendPath='') {
    $argsList = @('-no-stdlib','-no-reflect','-Xcontext-parameters','-jvm-target','17','-classpath',$Classpath,'-d',$Destination)
    if ($FriendPath) { $argsList += "-Xfriend-paths=$FriendPath" }
    $argsList += (Get-ChildItem -LiteralPath $SourceRoot -Recurse -Filter '*.kt').FullName
    $argumentFile = Join-Path $Destination 'compiler.args'
    [IO.File]::WriteAllLines($argumentFile, [string[]]($argsList | ForEach-Object { '"' + $_.Replace('\','/').Replace('"','\"') + '"' }), [Text.UTF8Encoding]::new($false))
    Invoke-CheckedJava -Arguments @('-cp',"$($compiler.DirectoryName)/*",'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler',"@$argumentFile")
    Remove-Item -LiteralPath $argumentFile
}
Compile-Kotlin (Join-Path $repo 'patches/src/main/kotlin') $classes $dependencies
Copy-Item -Path (Join-Path $repo 'patches/src/main/resources/*') -Destination $resources -Recurse
$extensionDir=Join-Path $resources 'extensions'
$null=New-Item -ItemType Directory -Path $extensionDir -Force
$smaliRoot=Join-Path $repo 'patches/src/main/resources/steamlink/androidxr/smali'
Invoke-CheckedJava -Arguments @('-cp',$dependencies,'com.android.tools.smali.smali.Main','a','-a','33','-o',"$extensionDir/extension.mpe","$smaliRoot/org/libsdl/app/GxrSdlBridge.smali")
Invoke-CheckedJava -Arguments @('-cp',$dependencies,'com.android.tools.smali.smali.Main','a','-a','33','-o',"$extensionDir/minimal-extension.mpe","$smaliRoot/com/valvesoftware/steamlink/GalaxyXRPermissionActivity.smali","$smaliRoot/com/valvesoftware/steamlink/GxrOverlayBridge.smali","$smaliRoot/com/valvesoftware/steamlink/GxrResolutionProbe.smali")
Compile-Kotlin (Join-Path $repo 'patches/src/test/kotlin') $tests "$dependencies;$classes" $classes
$testResources=Join-Path $repo 'patches/src/test/resources'
Push-Location $repo
try {
    Invoke-CheckedJava -Arguments @('-cp',"$dependencies;$classes;$tests;$resources;$testResources",'org.junit.platform.console.ConsoleLauncher','execute','--scan-class-path','--disable-banner','--details=summary')
} finally { Pop-Location }
$baseVersion = ((Get-Content (Join-Path $repo 'gradle.properties') | Where-Object { $_ -match '^version\s*=' }) -replace '^version\s*=\s*','').Trim()
$version="$baseVersion-fovea-test.1"
$manifest=Join-Path $work 'MANIFEST.MF'
[IO.File]::WriteAllText($manifest,"Manifest-Version: 1.0`nVersion: $version`nDescription: Local Android Surface Fovea test build`n`n",[Text.UTF8Encoding]::new($false))
$package=Join-Path $output "patches-$version.mpp"
& $jar --create --file $package --manifest $manifest -C $classes . -C $resources .
if ($LASTEXITCODE -ne 0) { throw 'JAR packaging failed' }
$dex=Join-Path $work 'dex'
$null=New-Item -ItemType Directory -Path $dex
$dexInput = Join-Path $work 'patches.jar'
Copy-Item -LiteralPath $package -Destination $dexInput
Invoke-CheckedJava -Arguments @('-cp',$D8Jar,'com.android.tools.r8.D8','--release','--min-api','26','--lib',$JavaHome,'--classpath',$fatJar,'--output',$dex,$dexInput)
& $jar --update --file $package -C $dex .
if ($LASTEXITCODE -ne 0) { throw 'DEX packaging failed' }
# Verify/audit using current classes and resources; normal Gradle remains the release path.
foreach($index in 0..3) {
    Invoke-CheckedJava -Arguments @('-cp',"$dependencies;$classes;$resources",'util.DecodedSteamLinkPatchAudit',"$repo/build/decoded-fixture-apks","$output/audits",'surface-fovea',"$index")
}
# Run catalog generator in an isolated directory, where it expects build/libs.
$catalogWork=Join-Path $work 'catalog/work'
$null=New-Item -ItemType Directory -Path "$catalogWork/build/libs" -Force
$catalogManifest=Join-Path $work 'CATALOG.MF'
[IO.File]::WriteAllText($catalogManifest,"Manifest-Version: 1.0`nVersion: $baseVersion`n`n",[Text.UTF8Encoding]::new($false))
& $jar --create --file "$catalogWork/build/libs/catalog.mpp" --manifest $catalogManifest -C $classes . -C $resources .
if ($LASTEXITCODE -ne 0) { throw 'Catalog packaging failed' }
Push-Location $catalogWork
try { Invoke-CheckedJava -Arguments @('-cp',"$dependencies;$classes",'util.PatchListGeneratorKt','stable') } finally { Pop-Location }
foreach($name in @('patches-list.json','patches-list-all.json','patches-list-stable.json','patches-list-experimental.json')) {
    Copy-Item -LiteralPath (Join-Path (Split-Path $catalogWork) $name) -Destination (Join-Path $repo $name)
}
Write-Host "Test package: $package"
Write-Host 'Local compiler/Morphe 1.9.0 validation build. No Gradle-plugin, installation, or headset validation claimed.'
