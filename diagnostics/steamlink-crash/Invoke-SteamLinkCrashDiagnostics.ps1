[CmdletBinding()]
param(
    [ValidateSet('Static', 'Capture', 'SelfTest')]
    [string]$Mode = 'Static',

    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$DecodedDirectory,
    [string]$PatchSmaliDirectory,
    [switch]$FailOnFinding,

    [ValidatePattern('^[A-Za-z0-9._]+$')]
    [string]$Package = 'com.valvesoftware.steamlinkvr.gxr',
    [string]$DeviceSerial,
    [string]$AdbPath = 'adb',
    [datetime]$Since = (Get-Date).AddMinutes(-10),
    [string]$OutputDirectory,
    [string]$PatchedApkPath,
    [string]$PatchReceiptPath
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Write-Utf8 {
    param([string]$Path, [AllowEmptyString()][string]$Text)

    $parent = Split-Path -Parent $Path
    if ($parent -and -not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding($false)))
}

function Get-StringSha256 {
    param([AllowEmptyString()][string]$Text)

    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text)))).Replace('-', '')
    }
    finally {
        $sha.Dispose()
    }
}

function Protect-DiagnosticText {
    param(
        [AllowEmptyString()][string]$Text,
        [string[]]$KnownSensitive = @()
    )

    $result = $Text
    foreach ($value in $KnownSensitive) {
        if ($value) { $result = $result -replace [regex]::Escape($value), '<redacted>' }
    }
    if ($env:USERPROFILE) {
        $result = $result -replace [regex]::Escape($env:USERPROFILE), '%USERPROFILE%'
    }
    $result = $result -replace '(?i)\b(?:10(?:\.\d{1,3}){3}|192\.168(?:\.\d{1,3}){2}|172\.(?:1[6-9]|2\d|3[01])(?:\.\d{1,3}){2})\b', '<private-ip>'
    $result = $result -replace '(?i)\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b', '<mac>'
    $result = $result -replace '(?i)(["''](?:password|authorization|bearer|token|pairing[_ -]?token|secret)["'']\s*:\s*)["''][^"''\r\n]*["'']', '$1"<redacted>"'
    $result = $result -replace '(?i)\b(password|authorization|bearer|token|pairing[_ -]?token|secret)\b\s*[:=]\s*\S+', '$1=<redacted>'
    return $result
}

function Get-SmaliDescriptor {
    param([string]$Text)

    $match = [regex]::Match($Text, '(?m)^\.class\b[^\r\n]*\s(?<class>L[^;]+;)\s*$')
    if (-not $match.Success) { throw 'Smali file has no class descriptor.' }
    return $match.Groups['class'].Value
}

function Get-SmaliMethods {
    param([string]$Text)

    return @([regex]::Matches($Text, '(?m)^\.method\b[^\r\n]*\s(?<name>[^\s(]+)(?<descriptor>\([^)]*\)\S+)\s*$') |
        ForEach-Object { $_.Groups['name'].Value + $_.Groups['descriptor'].Value })
}

function Get-SmaliSuperclass {
    param([string]$Text)

    $match = [regex]::Match($Text, '(?m)^\.super\s+(?<class>L[^;]+;)\s*$')
    return $(if ($match.Success) { $match.Groups['class'].Value } else { $null })
}

function Get-SmaliInvokes {
    param([string]$Text, [string]$Path, [string]$CallerClass)

    $items = New-Object System.Collections.Generic.List[object]
    $pattern = '(?m)^\s*invoke-(?<kind>\S+)\s+\{[^}]*\},\s+(?<class>L[^;]+;)->(?<name>[^\s(]+)(?<descriptor>\([^)]*\)\S+)\s*$'
    foreach ($match in [regex]::Matches($Text, $pattern)) {
        $line = 1 + ([regex]::Matches($Text.Substring(0, $match.Index), "`n").Count)
        $items.Add([PSCustomObject][ordered]@{
            callerClass = $CallerClass
            path = $Path
            line = $line
            invokeKind = $match.Groups['kind'].Value
            targetClass = $match.Groups['class'].Value
            targetMethod = $match.Groups['name'].Value + $match.Groups['descriptor'].Value
        })
    }
    return $items.ToArray()
}

function Count-BytePattern {
    param([byte[]]$Bytes, [byte[]]$Pattern)

    if ($Pattern.Count -eq 0 -or $Pattern.Count -gt $Bytes.Count) { return 0 }
    $count = 0
    for ($offset = 0; $offset -le $Bytes.Count - $Pattern.Count; $offset++) {
        $matched = $true
        for ($index = 0; $index -lt $Pattern.Count; $index++) {
            if ($Bytes[$offset + $index] -ne $Pattern[$index]) { $matched = $false; break }
        }
        if ($matched) { $count++ }
    }
    return $count
}

function Invoke-StaticAudit {
    if (-not $DecodedDirectory) {
        $script:DecodedDirectory = Join-Path $RepoRoot 'decoded-apk-android-steamlinkvr-release-base-2.0.20-5001712'
    }
    if (-not $PatchSmaliDirectory) {
        $script:PatchSmaliDirectory = Join-Path $RepoRoot 'patches\src\main\resources\steamlink\androidxr\smali'
    }
    if (-not (Test-Path -LiteralPath $DecodedDirectory -PathType Container)) {
        throw "Decoded 5001712 directory not found: $DecodedDirectory"
    }
    if (-not (Test-Path -LiteralPath $PatchSmaliDirectory -PathType Container)) {
        throw "Patch smali directory not found: $PatchSmaliDirectory"
    }

    $apktoolYaml = Join-Path $DecodedDirectory 'apktool.yml'
    $yaml = if (Test-Path -LiteralPath $apktoolYaml) { Get-Content -LiteralPath $apktoolYaml -Raw } else { '' }
    if ($yaml -notmatch "versionCode:\s*'?5001712'?" -or $yaml -notmatch "versionName:\s*'?2\.0\.20'?") {
        throw 'Decoded directory is not exact Steam Link 2.0.20/5001712.'
    }

    $definitions = @{}
    $superclasses = @{}
    $baseFiles = @(Get-ChildItem -LiteralPath $DecodedDirectory -Directory -Filter 'smali*' |
        ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -Recurse -File -Filter '*.smali' })
    foreach ($file in $baseFiles) {
        $text = Get-Content -LiteralPath $file.FullName -Raw
        $class = Get-SmaliDescriptor -Text $text
        $definitions[$class] = @(Get-SmaliMethods -Text $text)
        $superclasses[$class] = Get-SmaliSuperclass -Text $text
    }

    $assembledHelperFiles = @(
        'GxrSdlBridge.smali',
        'GalaxyXRPermissionActivity.smali',
        'GxrOverlayBridge.smali',
        'GxrResolutionProbe.smali'
    )
    $patchFiles = @(Get-ChildItem -LiteralPath $PatchSmaliDirectory -Recurse -File -Filter '*.smali' |
        Where-Object { $_.Name -in $assembledHelperFiles })
    $invokes = New-Object System.Collections.Generic.List[object]
    foreach ($file in $patchFiles) {
        $text = Get-Content -LiteralPath $file.FullName -Raw
        $class = Get-SmaliDescriptor -Text $text
        $definitions[$class] = @(Get-SmaliMethods -Text $text)
        $superclasses[$class] = Get-SmaliSuperclass -Text $text
        foreach ($invoke in Get-SmaliInvokes -Text $text -Path $file.FullName -CallerClass $class) {
            $invokes.Add($invoke)
        }
    }

    $projectPrefixes = @('Lorg/libsdl/app/', 'Lcom/valvesoftware/steamlink/')
    $unresolved = @($invokes | Where-Object {
        $target = $_.targetClass
        $inheritedFrameworkCall = $target -eq $_.callerClass -and
            $_.invokeKind -match '^(?:virtual|super)' -and
            $superclasses.ContainsKey($target) -and
            $superclasses[$target] -match '^L(?:android|java)/'
        @($projectPrefixes | Where-Object { $target.StartsWith($_) }).Count -gt 0 -and
        -not $inheritedFrameworkCall -and
        (-not $definitions.ContainsKey($target) -or $_.targetMethod -notin @($definitions[$target]))
    } | Sort-Object targetClass, targetMethod, path, line)

    $unique = @($unresolved | Group-Object targetClass, targetMethod | ForEach-Object {
        $first = $_.Group[0]
        [PSCustomObject][ordered]@{
            targetClass = $first.targetClass
            targetMethod = $first.targetMethod
            callSiteCount = $_.Count
            callSites = @($_.Group | ForEach-Object { "$(Split-Path -Leaf $_.path):$($_.line)" })
        }
    })

    $loaderPath = Join-Path $DecodedDirectory 'lib\arm64-v8a\libopenxr_loader.so'
    $astPath = Join-Path $RepoRoot 'patches\src\main\resources\steamlink\androidxr\libgxr_ast.so'
    $astManifestPath = Join-Path $RepoRoot 'patches\src\main\resources\steamlink\androidxr\XR_APILAYER_local_GalaxyXR_android_surface_trigger_passthrough_v1.json'
    $v10 = [byte[]](0, 0, 0, 0, 0, 0, 1, 0)
    $v11 = [byte[]](0, 0, 0, 0, 1, 0, 1, 0)
    $openXrRisk = $null
    if ((Test-Path -LiteralPath $loaderPath) -and (Test-Path -LiteralPath $astPath)) {
        $loader = [IO.File]::ReadAllBytes($loaderPath)
        $ast = [IO.File]::ReadAllBytes($astPath)
        $manifest = if (Test-Path -LiteralPath $astManifestPath) { Get-Content -LiteralPath $astManifestPath -Raw } else { '' }
        $openXrRisk = [PSCustomObject][ordered]@{
            status = 'warning-not-runtime-proof'
            loaderRawPacked10PatternCount = Count-BytePattern -Bytes $loader -Pattern $v10
            loaderRawPacked11PatternCount = Count-BytePattern -Bytes $loader -Pattern $v11
            astRawPacked11PatternCount = Count-BytePattern -Bytes $ast -Pattern $v11
            manifestDeclares11 = [bool]($manifest -match '"api_version"\s*:\s*"1\.1"')
            limitation = 'Raw byte-pattern counts do not establish loader API support or negotiated runtime behavior.'
            hypothesis = 'The pattern mismatch suggests checking high-resolution API-layer negotiation in current runtime logs.'
        }
    }

    $report = [PSCustomObject][ordered]@{
        schemaVersion = 1
        kind = 'steamlink-5001712-static-runtime-link-audit'
        versionName = '2.0.20'
        versionCode = 5001712
        status = $(if ($unique.Count -eq 0) { 'compatible' } else { 'incompatible' })
        scannedPatchSmaliFiles = $patchFiles.Count
        unresolvedCallSites = $unresolved.Count
        uniqueUnresolvedMethods = $unique.Count
        unresolved = $unique
        openxrApiRisk = $openXrRisk
    }

    $json = $report | ConvertTo-Json -Depth 8
    if ($OutputDirectory) {
        New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
        Write-Utf8 -Path (Join-Path $OutputDirectory 'static-runtime-link-audit.json') -Text $json
    }
    $json
    if ($FailOnFinding -and $unique.Count -gt 0) { exit 2 }
}

function Invoke-Adb {
    param([string[]]$Arguments)

    $output = & $AdbPath -s $DeviceSerial @Arguments 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "adb failed ($LASTEXITCODE): $($Arguments -join ' ')`n$output" }
    return $output
}

function Select-LogContext {
    param([AllowEmptyString()][string]$Text, [string]$Pattern, [int]$Radius = 12)

    $lines = @($Text -split '\r?\n')
    $selected = New-Object 'System.Collections.Generic.SortedSet[int]'
    for ($index = 0; $index -lt $lines.Count; $index++) {
        if ($lines[$index] -notmatch $Pattern) { continue }
        $start = [Math]::Max(0, $index - $Radius)
        $end = [Math]::Min($lines.Count - 1, $index + $Radius)
        for ($line = $start; $line -le $end; $line++) { [void]$selected.Add($line) }
    }
    return (($selected | ForEach-Object { $lines[$_] }) -join "`r`n")
}

function Get-CrashClassification {
    param([AllowEmptyString()][string]$Text)

    $rules = @(
        @{ Kind = 'java-abi'; Confidence = 'high'; Pattern = '(?i)NoSuchMethodError|VerifyError|NoClassDefFoundError|ClassNotFoundException' },
        @{ Kind = 'loader'; Confidence = 'high'; Pattern = '(?i)UnsatisfiedLinkError|dlopen failed|cannot locate symbol|xrNegotiateLoaderApiLayerInterface' },
        @{ Kind = 'native'; Confidence = 'high'; Pattern = '(?i)Fatal signal|Abort message|backtrace:|SIGSEGV|SIGABRT' },
        @{ Kind = 'anr'; Confidence = 'high'; Pattern = '(?i)ANR in|REASON_ANR' },
        @{ Kind = 'low-memory-or-system-kill'; Confidence = 'medium'; Pattern = '(?i)lmkd|low memory|REASON_LOW_MEMORY' },
        @{ Kind = 'clean-or-user-stop'; Confidence = 'medium'; Pattern = '(?i)REASON_USER_REQUESTED|user requested|force-stop' }
    )
    foreach ($rule in $rules) {
        $matches = @([regex]::Matches($Text, $rule.Pattern) | Select-Object -First 8 | ForEach-Object { $_.Value })
        if ($matches.Count -gt 0) {
            return [PSCustomObject][ordered]@{ kind = $rule.Kind; confidence = $rule.Confidence; evidence = $matches }
        }
    }
    return [PSCustomObject][ordered]@{ kind = 'unknown'; confidence = 'low'; evidence = @() }
}

function Invoke-Capture {
    if (-not $DeviceSerial) { throw '-DeviceSerial is required for Capture mode.' }
    if (-not (Get-Command $AdbPath -ErrorAction SilentlyContinue)) { throw "adb not found: $AdbPath" }

    $state = (Invoke-Adb -Arguments @('get-state')).Trim()
    if ($state -ne 'device') { throw "ADB target is not ready: $state" }

    $hostUtc = [DateTime]::UtcNow
    $deviceEpoch = (Invoke-Adb -Arguments @('shell', 'date', '+%s')).Trim()
    $packageDump = Invoke-Adb -Arguments @('shell', 'dumpsys', 'package', $Package)
    $versionName = [regex]::Match($packageDump, '(?m)^\s*versionName=(?<value>\S+)').Groups['value'].Value
    $versionCodeMatch = [regex]::Match($packageDump, '(?m)^\s*versionCode=(?<value>\d+)')
    if (-not $versionCodeMatch.Success -or $versionName -ne '2.0.20' -or [int64]$versionCodeMatch.Groups['value'].Value -ne 5001712) {
        throw "Installed package is not exact Steam Link 2.0.20/5001712: $versionName/$($versionCodeMatch.Groups['value'].Value)"
    }

    if (-not $OutputDirectory) {
        $script:OutputDirectory = Join-Path ([IO.Path]::GetTempPath()) ('SteamLinkCrash-' + $hostUtc.ToString('yyyyMMdd-HHmmssZ'))
    }
    if (Test-Path -LiteralPath $OutputDirectory) {
        throw "Output directory must not already exist: $OutputDirectory"
    }
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null

    $serialHash = Get-StringSha256 -Text $DeviceSerial
    $packagePathsText = Invoke-Adb -Arguments @('shell', 'pm', 'path', $Package)
    $packagePaths = @($packagePathsText -split '\r?\n' | Where-Object { $_ -match '^package:' } | ForEach-Object { $_.Substring(8).Trim() })
    $installedFiles = @($packagePaths | ForEach-Object {
        $hashText = Invoke-Adb -Arguments @('shell', 'sha256sum', $_)
        [PSCustomObject][ordered]@{ devicePath = $_; sha256 = ($hashText -split '\s+')[0].ToUpperInvariant() }
    })

    $sinceEpoch = [DateTimeOffset]$Since
    $sinceEpochText = $sinceEpoch.ToUnixTimeSeconds().ToString([Globalization.CultureInfo]::InvariantCulture) + '.000'
    $allLogcat = Invoke-Adb -Arguments @(
        'logcat', '-d', '-v', 'epoch', '-T', $sinceEpochText,
        '-b', 'crash', '-b', 'main', '-b', 'system'
    )
    $packagePattern = [regex]::Escape($Package)
    $pattern = "(?i)$packagePattern|com\.valvesoftware\.steamlinkvr|GxrSdlBridge|SDLControllerManager|SteamLinkGXR|libvrlink_scene|libgxr_|GXRSurfaceTrigger|GXRXrBridge|GXRFaceBridge|\bVRLink\b"
    $targetedLogcat = Select-LogContext -Text $allLogcat -Pattern $pattern
    $exitInfo = Invoke-Adb -Arguments @('shell', 'dumpsys', 'activity', 'exit-info', $Package)

    $receipt = $null
    if ($PatchReceiptPath) {
        if (-not (Test-Path -LiteralPath $PatchReceiptPath -PathType Leaf)) { throw "Patch receipt not found: $PatchReceiptPath" }
        $receipt = Get-Content -LiteralPath $PatchReceiptPath -Raw | ConvertFrom-Json
        $receiptFields = @($receipt.PSObject.Properties.Name)
        if ('versionName' -in $receiptFields -and [string]$receipt.versionName -ne '2.0.20') {
            throw "Patch receipt versionName is not 2.0.20: $($receipt.versionName)"
        }
        if ('versionCode' -in $receiptFields -and [int64]$receipt.versionCode -ne 5001712) {
            throw "Patch receipt versionCode is not 5001712: $($receipt.versionCode)"
        }
        if ('packageName' -in $receiptFields -and [string]$receipt.packageName -ne $Package) {
            throw "Patch receipt packageName does not match $Package`: $($receipt.packageName)"
        }
    }
    $localApk = $null
    if ($PatchedApkPath) {
        if (-not (Test-Path -LiteralPath $PatchedApkPath -PathType Leaf)) { throw "Patched APK not found: $PatchedApkPath" }
        $localApk = [PSCustomObject][ordered]@{
            path = Split-Path -Leaf $PatchedApkPath
            size = (Get-Item -LiteralPath $PatchedApkPath).Length
            sha256 = (Get-FileHash -LiteralPath $PatchedApkPath -Algorithm SHA256).Hash
        }
    }

    $knownSensitive = @($DeviceSerial)
    $combined = $targetedLogcat + "`r`n" + $exitInfo
    $classification = Get-CrashClassification -Text $combined
    $provenance = [PSCustomObject][ordered]@{
        schemaVersion = 1
        hostUtc = $hostUtc.ToString('o')
        deviceEpoch = $deviceEpoch
        deviceIdHash = $serialHash
        packageName = $Package
        versionName = $versionName
        versionCode = 5001712
        installedFiles = $installedFiles
        localApk = $localApk
        patchSelectionKnown = [bool]($null -ne $receipt)
        patchReceipt = $receipt
    }

    $createdFiles = @(
        Join-Path $OutputDirectory '00-scope.txt'
        Join-Path $OutputDirectory '01-provenance.json'
        Join-Path $OutputDirectory '10-logcat-targeted.txt'
        Join-Path $OutputDirectory '11-exit-info.txt'
        Join-Path $OutputDirectory '20-classification.json'
    )
    Write-Utf8 -Path $createdFiles[0] -Text "Read-only post-reproduction capture. No launch, force-stop, install, grant, log clear, bugreport, screenshot, network dump, DropBox query, or tombstone pull was performed.`r`n"
    Write-Utf8 -Path $createdFiles[1] -Text (Protect-DiagnosticText -Text ($provenance | ConvertTo-Json -Depth 10) -KnownSensitive $knownSensitive)
    Write-Utf8 -Path $createdFiles[2] -Text (Protect-DiagnosticText -Text $targetedLogcat -KnownSensitive $knownSensitive)
    Write-Utf8 -Path $createdFiles[3] -Text (Protect-DiagnosticText -Text $exitInfo -KnownSensitive $knownSensitive)
    Write-Utf8 -Path $createdFiles[4] -Text ($classification | ConvertTo-Json -Depth 5)

    $secretPatterns = @(
        '(?i)["''](?:password|authorization|bearer|token|pairing[_ -]?token|secret)["'']\s*:\s*["''](?!<redacted>)[^"''\r\n]+["'']',
        '(?i)\b(?:password|authorization|bearer|token|pairing[_ -]?token|secret)\b\s*[:=]\s*(?!<redacted>)\S+'
    )
    $leaks = @($createdFiles | Select-String -Pattern $secretPatterns)
    if ($leaks.Count -gt 0) { throw 'Secret scan failed; capture was not archived.' }

    $archive = "$OutputDirectory.zip"
    if (Test-Path -LiteralPath $archive) { throw "Archive must not already exist: $archive" }
    Compress-Archive -LiteralPath $createdFiles -DestinationPath $archive
    [PSCustomObject][ordered]@{ outputDirectory = $OutputDirectory; archive = $archive; classification = $classification } |
        ConvertTo-Json -Depth 6
}

function Invoke-SelfTest {
    $script:checks = 0
    function Assert-True([bool]$Condition, [string]$Message) {
        if (-not $Condition) { throw "SelfTest failed: $Message" }
        $script:checks++
    }

    Assert-True ((Get-CrashClassification 'java.lang.NoSuchMethodError').kind -eq 'java-abi') 'Java ABI classification'
    Assert-True ((Get-CrashClassification 'dlopen failed: libgxr_ast.so').kind -eq 'loader') 'loader classification'
    Assert-True ((Get-CrashClassification 'Fatal signal 11 (SIGSEGV)').kind -eq 'native') 'native classification'
    Assert-True ((Get-CrashClassification 'ANR in com.valvesoftware.steamlinkvr').kind -eq 'anr') 'ANR classification'
    $redacted = Protect-DiagnosticText -Text "serial-123 192.168.1.2 aa:bb:cc:dd:ee:ff token=abc" -KnownSensitive @('serial-123')
    Assert-True ($redacted -notmatch 'serial-123|192\.168\.1\.2|aa:bb:cc:dd:ee:ff|token=abc') 'redaction'
    $jsonRedacted = Protect-DiagnosticText -Text '{"token":"abc","nested":{"password":"xyz"}}'
    Assert-True ($jsonRedacted -notmatch '"abc"|"xyz"') 'JSON redaction'
    Assert-True ((Count-BytePattern -Bytes ([byte[]](1, 2, 1, 2, 1)) -Pattern ([byte[]](1, 2))) -eq 2) 'byte pattern count'
    "PASS SelfTest checks=$checks"
}

switch ($Mode) {
    'Static' { Invoke-StaticAudit }
    'Capture' { Invoke-Capture }
    'SelfTest' { Invoke-SelfTest }
}
