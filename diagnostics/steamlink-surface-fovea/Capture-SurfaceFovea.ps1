param(
    [string]$AdbPath = 'adb',
    [string]$Serial,
    [string]$Package = 'com.valvesoftware.steamlinkvr'
)
$ErrorActionPreference = 'Stop'
if ($Package -notmatch '^[A-Za-z0-9_.]+$') { throw 'Invalid package name.' }
$adbCommand = Get-Command $AdbPath -ErrorAction SilentlyContinue
if (-not $adbCommand) { throw 'ADB not found. Supply -AdbPath with the full adb.exe path.' }
$adbExe = $adbCommand.Source
$devices = & $adbExe devices
if ($LASTEXITCODE -ne 0) { throw 'ADB devices failed.' }
$connected = @($devices | ForEach-Object { if ($_ -match '^(\S+)\s+device$') { $Matches[1] } })
if (-not $Serial) {
    if ($connected.Count -ne 1) { throw 'Connect one authorized device, or specify -Serial.' }
    $Serial = $connected[0]
}
if ($Serial -notmatch '^[A-Za-z0-9_.:-]+$' -or $Serial -notin $connected) { throw 'Selected device is not authorized and connected.' }
$runRoot = Join-Path ([Environment]::GetFolderPath('MyDocuments')) ('GalaxyXR-Diagnostics/Surface-Fovea-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $runRoot | Out-Null
function Invoke-ReadAdb([string[]]$Arguments) {
    $result = & $adbExe -s $Serial @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw ('ADB failed: ' + ($Arguments -join ' ') + ': ' + ($result -join "`n")) }
    return ($result -join "`n")
}
function Ask-Quality([string]$Prompt) {
    do { $answer = Read-Host ($Prompt + ' [1=SOFTER, 2=MATCHES REFERENCE, 3=UNSURE]') } until ($answer -in '1','2','3')
    return @{ timeUtc = [DateTime]::UtcNow.ToString('o'); rating = @{'1'='SOFTER';'2'='MATCHES_REFERENCE';'3'='UNSURE'}[$answer] }
}
$packageInfo = Invoke-ReadAdb -Arguments @('shell','dumpsys','package',$Package)
$packageInfo | Set-Content (Join-Path $runRoot 'package.txt')
if ($packageInfo -notmatch 'versionName=2\.0\.22(?:\s|$)' -or $packageInfo -notmatch 'versionCode=5002322(?:\s|$)') {
    throw 'This experiment requires exact Steam Link 2.0.22/5002322.'
}
# Inspect every installed APK split. A leftover production helper invalidates this A/B.
$apkPaths = (Invoke-ReadAdb -Arguments @('shell','pm','path',$Package)) -split "`r?`n" | Where-Object { $_ -like 'package:*' }
if (-not $apkPaths) { throw 'Installed APK paths unavailable.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$hasExperiment = $false
$hasProduction = $false
$apkIndex = 0
foreach ($apkLine in $apkPaths) {
    $localApk = Join-Path $runRoot ('installed-' + $apkIndex + '.apk')
    Invoke-ReadAdb -Arguments @('pull',$apkLine.Substring(8),$localApk) | Out-Null
    $zip = [IO.Compression.ZipFile]::OpenRead($localApk)
    try {
        $hasExperiment = $hasExperiment -or ($null -ne $zip.GetEntry('lib/arm64-v8a/libgxr_asf.so'))
        $hasProduction = $hasProduction -or ($null -ne $zip.GetEntry('lib/arm64-v8a/libgxr_ast.so'))
    } finally { $zip.Dispose() }
    # Only this newly pulled temporary APK is removed; logs and summary remain.
    Remove-Item -LiteralPath $localApk
    $apkIndex++
}
if (-not $hasExperiment -or $hasProduction) { throw 'Install the fovea experiment alone: libgxr_asf.so required; libgxr_ast.so must be absent.' }
$appOps = Invoke-ReadAdb -Arguments @('shell','appops','get',$Package,'SYSTEM_ALERT_WINDOW')
$appOps | Set-Content (Join-Path $runRoot 'appops-before.txt')
$permissionContamination = $appOps -match '(?im)SYSTEM_ALERT_WINDOW:\s*(allow|foreground)\b'
Write-Host 'Close Steam Link fully. Disable Appear on top yourself. Hide recording, performance overlays, and other floating windows.'
Write-Host 'Use the same scene, host profile, refresh rate and 8-bit settings as your native/Appear-on-top reference.'
Read-Host 'Press Enter when Steam Link is closed and the ordinary baseline has no system overlay' | Out-Null
$deviceStamp = (Invoke-ReadAdb -Arguments @('shell','date "+%m-%d %H:%M:%S.000"')).Trim()
if ($deviceStamp -notmatch '^\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.000$') { throw 'Cannot establish device-clock capture start.' }
$captureStartUtc = [DateTime]::UtcNow.ToString('o')
$logPath = Join-Path $runRoot 'fovea-logcat.txt'
$logError = Join-Path $runRoot 'logcat-errors.txt'
$logArgs = @('-s',$Serial,'logcat','-v','threadtime','-T',('"' + $deviceStamp + '"'),'GXRFoveaSurface:I','*:S')
$logProcess = Start-Process -FilePath $adbExe -ArgumentList $logArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput $logPath -RedirectStandardError $logError
$phases = [System.Collections.Generic.List[object]]::new()
$completed = $false
try {
    Write-Host 'Capture armed. Manually open Steam Link and start streaming.'
    Read-Host 'Press Enter when the stream is stable' | Out-Null
    $phases.Add(@{ name='baseline'; observation=(Ask-Quality 'Before palm') })
    foreach ($service in @('window','display')) {
        Invoke-ReadAdb -Arguments @('shell','dumpsys',$service) | Set-Content (Join-Path $runRoot ($service + '-baseline.txt'))
    }
    Read-Host 'Raise your palm; press Enter only while the system element is visibly present' | Out-Null
    $phases.Add(@{ name='palm-visible'; observation=(Ask-Quality 'While palm element is visible') })
    Read-Host 'Lower your palm; press Enter after the system element disappears' | Out-Null
    $phases.Add(@{ name='palm-hidden'; observation=(Ask-Quality 'After palm disappears') })
    do { $dfr = Read-Host 'Run DFR-UI attach/detach test? [Y/N]' } until ($dfr -match '^[YyNn]$')
    if ($dfr -match '^[Yy]$') {
        Read-Host 'Attach DFR-UI manually; press Enter when stable' | Out-Null
        $phases.Add(@{ name='dfr-attached'; observation=(Ask-Quality 'With DFR-UI attached') })
        Read-Host 'Detach DFR-UI manually; press Enter when stable' | Out-Null
        $phases.Add(@{ name='dfr-detached'; observation=(Ask-Quality 'After DFR-UI detaches') })
    }
    $completed = $true
} finally {
    $captureDiedEarly = $logProcess.HasExited
    if (-not $logProcess.HasExited) { Stop-Process -Id $logProcess.Id -ErrorAction SilentlyContinue; $logProcess.WaitForExit(5000) | Out-Null }
    $phases.ToArray() | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $runRoot 'observations.json')
}
$appOpsAfter = Invoke-ReadAdb -Arguments @('shell','appops','get',$Package,'SYSTEM_ALERT_WINDOW')
$appOpsAfter | Set-Content (Join-Path $runRoot 'appops-after.txt')
$permissionContamination = $permissionContamination -or ($appOpsAfter -match '(?im)SYSTEM_ALERT_WINDOW:\s*(allow|foreground)\b')
$lines = Get-Content -LiteralPath $logPath
# Runtime schema is deliberately parsed below; text mentioning the experiment alone is insufficient.
$events = [System.Collections.Generic.List[object]]::new()
foreach ($line in $lines) {
    $jsonStart = $line.IndexOf('{')
    if ($jsonStart -ge 0) {
        try {
            $event = $line.Substring($jsonStart) | ConvertFrom-Json
            if ($event.mode -eq 'android_surface_fovea_v1' -and $event.buildId -eq 'android-surface-fovea-v1-20260907') { $events.Add($event) }
        } catch {}
    }
}
$initialized = @($events.ToArray() | Where-Object { $_.event -eq 'layer_initialized' -and $_.extensionEnabled -eq $true })
$transfers = @($events.ToArray() | Where-Object { $_.event -eq 'fovea_surface_submitted' -and $_.layerCount -eq 3 -and $_.dummyQuad -eq $false -and $_.copyPasses -eq 2 -and $_.bitDepth -eq 8 })
$fallbacks = @($events.ToArray() | Where-Object { $_.event -eq 'fallback' })
$summary = [ordered]@{
    schema=1; package=$Package; serial=$Serial; captureStartUtc=$captureStartUtc; complete=$completed
    permissionContamination=$permissionContamination; phases=$phases.ToArray(); parsedEventCount=$events.Count
    captureDiedEarly=$captureDiedEarly; successfulSampledTransfers=$transfers.Count; fallbacks=$fallbacks
    sourceTransfer='NOT_PROVEN'; visualParity='NOT_ESTABLISHED'; finalResolution='NOT_MEASURED'; tenBit='UNSUPPORTED_EXPERIMENT'
    verdict='INCOMPLETE'; limitation='User ratings compare perceived sharpness. No optical, compositor-output or panel-resolution measurement is performed.'
}
if ($initialized.Count -gt 0 -and $transfers.Count -gt 0) { $summary.sourceTransfer='SOURCE_TRANSFER_OBSERVED' }
if ($phases.Count -ge 3 -and @($phases.ToArray() | Where-Object { $_.observation.rating -ne 'MATCHES_REFERENCE' }).Count -eq 0) {
    $summary.visualParity='USER_REPORTED_PARITY'
}
if ($permissionContamination) { $summary.verdict='CONTAMINATED_PERMISSION' }
elseif ($fallbacks.Count -gt 0) { $summary.verdict='FALLBACK_OBSERVED' }
elseif ($completed -and -not $captureDiedEarly -and $summary.sourceTransfer -eq 'SOURCE_TRANSFER_OBSERVED') {
    if ($summary.visualParity -eq 'USER_REPORTED_PARITY') { $summary.verdict='SOURCE_TRANSFER_OBSERVED_WITH_USER_PARITY' }
    else { $summary.verdict='SOURCE_TRANSFER_OBSERVED_VISUAL_PARITY_NOT_ESTABLISHED' }
}
$events.ToArray() | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $runRoot 'events.json')
$summary | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $runRoot 'summary.json')
Write-Host ('Capture saved: ' + $runRoot)
Write-Host ('Verdict: ' + $summary.verdict)
Write-Host ('Visual result: ' + $summary.visualParity + '; final resolution: NOT_MEASURED')
