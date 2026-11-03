[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
# Resolve everything from this script's own location, never from the caller's cwd.
$csRepo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$steamlinkBuildRoot = [IO.Path]::GetFullPath((Join-Path $csRepo '../builds/steamlink-patches'))
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $steamlinkBuildRoot 'build/hybrid-codec-work/host-tests' }
New-Item -ItemType Directory -Force -Path $out | Out-Null

if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $envScript = Join-Path $csRepo '../GalaxyXR-Driver/tools/Enter-PortableBuildEnvironment.ps1'
    if (!(Test-Path $envScript)) { throw "Portable build environment not found: $envScript" }
    . $envScript -NoDownload
}
if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'cl.exe unavailable after environment setup' }

$sources = @(
    (Join-Path $PSScriptRoot 'pure_tests.cpp'),
    (Join-Path $PSScriptRoot '../src/gxh1_parse.cpp'),
    (Join-Path $PSScriptRoot '../src/astc_decode.cpp'),
    (Join-Path $PSScriptRoot '../src/pyrowave_decode.cpp'),
    (Join-Path $PSScriptRoot '../src/hybrid_session.cpp'),
    (Join-Path $PSScriptRoot '../src/hybrid_entry.cpp')
)
$exe = Join-Path $out 'hybrid_pure_tests.exe'
& cl.exe /nologo /std:c++17 /EHsc /O2 /W4 "/Fe:$exe" "/Fo:$out\\" $sources
if ($LASTEXITCODE -ne 0) { throw 'Hybrid codec host test compilation failed' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Hybrid codec host tests failed' }

# Hot-path measurements (opt-in; they are timings, not assertions).
$perfSources = @(
    (Join-Path $PSScriptRoot 'perf_tests.cpp'),
    (Join-Path $PSScriptRoot '../src/gxh1_parse.cpp'),
    (Join-Path $PSScriptRoot '../src/astc_decode.cpp'),
    (Join-Path $PSScriptRoot '../src/pyrowave_decode.cpp'),
    (Join-Path $PSScriptRoot '../src/hybrid_session.cpp'),
    (Join-Path $PSScriptRoot '../src/hybrid_entry.cpp')
)
$perfExe = Join-Path $out 'hybrid_perf_tests.exe'
New-Item -ItemType Directory -Force -Path (Join-Path $out 'perf') | Out-Null
& cl.exe /nologo /std:c++17 /EHsc /O2 /W4 "/Fe:$perfExe" "/Fo:$out\\perf\\" $perfSources
if ($LASTEXITCODE -ne 0) { throw 'Hybrid codec perf test compilation failed' }
& $perfExe
if ($LASTEXITCODE -ne 0) { throw 'Hybrid codec perf probe failed (demux-only)' }
& $perfExe --decode
if ($LASTEXITCODE -ne 0) { throw 'Hybrid codec perf probe failed (cpu-decode)' }
