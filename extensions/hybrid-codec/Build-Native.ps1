[CmdletBinding()]
param([string]$NdkDirectory, [string]$OutputDirectory, [switch]$Stage)
$ErrorActionPreference = 'Stop'
# Resolve everything from this script's own location, never from the caller's cwd.
$csRepo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$steamlinkBuildRoot = [IO.Path]::GetFullPath((Join-Path $csRepo '../builds/steamlink-patches'))
$csWork = Join-Path $steamlinkBuildRoot 'build/hybrid-codec-work'
if (!$NdkDirectory) { $NdkDirectory = Join-Path $csRepo '.android-sdk/ndk/27.2.12479018' }
# Never overwrite the pinned resource by default: build into the external work root, then
# stage a validated binary explicitly with -Stage or -OutputDirectory.
if (!$OutputDirectory) {
    $OutputDirectory = if ($Stage) {
        Join-Path $csRepo 'patches/src/main/resources/steamlink/androidxr'
    } else {
        Join-Path $csWork 'lib'
    }
}
$csBin = Join-Path $NdkDirectory 'toolchains/llvm/prebuilt/windows-x86_64/bin'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $csWork | Out-Null

$csLibrary = Join-Path $OutputDirectory 'libgxr_hybrid_codec.so'
$csSources = @(
    (Join-Path $PSScriptRoot 'src/gxh1_parse.cpp'),
    (Join-Path $PSScriptRoot 'src/astc_decode.cpp'),
    (Join-Path $PSScriptRoot 'src/pyrowave_decode.cpp'),
    (Join-Path $PSScriptRoot 'src/hybrid_session.cpp'),
    (Join-Path $PSScriptRoot 'src/hybrid_entry.cpp')
)

& (Join-Path $csBin 'aarch64-linux-android29-clang++.cmd') -std=c++17 -O2 -fPIC -shared -fvisibility=hidden `
    -static-libstdc++ -Wall -Wextra -Werror -Wno-missing-field-initializers `
    "-I$(Join-Path $PSScriptRoot 'protocol')" `
    $csSources -o $csLibrary `
    '-Wl,-soname,libgxr_hybrid_codec.so' '-Wl,-z,max-page-size=16384' '-Wl,--exclude-libs,ALL'
if ($LASTEXITCODE -ne 0) { throw 'Hybrid codec Android compilation failed' }
& (Join-Path $csBin 'llvm-strip.exe') --strip-unneeded $csLibrary
if ($LASTEXITCODE -ne 0) { throw 'Hybrid codec strip failed' }
Get-FileHash -LiteralPath $csLibrary -Algorithm SHA256
Write-Host "Built $csLibrary"
Write-Host 'Build output only. Stage the validated helper and update its Kotlin SHA pin before packaging.'
