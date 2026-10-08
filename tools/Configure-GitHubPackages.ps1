#Requires -Version 5.1
<#
.SYNOPSIS
Validates GitHub Packages credentials and saves them in user Gradle properties.
.DESCRIPTION
Use a GitHub personal access token (classic) with read:packages permission.
The token is entered through a hidden prompt. Gradle stores it as plaintext in
the user's gradle.properties, outside this repository. Existing unrelated
properties are retained. Newly created credential files have restricted ACLs.
#>
[CmdletBinding()]
param(
    [string]$Username = 'AngelDark92',
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
$markerUrl = 'https://maven.pkg.github.com/MorpheApp/registry/app/morphe/patches/app.morphe.patches.gradle.plugin/1.3.4/app.morphe.patches.gradle.plugin-1.3.4.pom'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$gradleHome = if ($env:GRADLE_USER_HOME) { $env:GRADLE_USER_HOME } else { Join-Path ([Environment]::GetFolderPath('UserProfile')) '.gradle' }
$gradleHome = [IO.Path]::GetFullPath($gradleHome)
$propertiesPath = Join-Path $gradleHome 'gradle.properties'
if ($gradleHome.TrimEnd('\', '/') -eq $repositoryRoot -or $gradleHome.StartsWith($repositoryRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'GRADLE_USER_HOME must be outside this repository before configuring credentials.'
}

Write-Host 'Use a GitHub personal access token (classic) with read:packages permission.'
if (-not $ValidateOnly) { Write-Host "The token will be stored as plaintext in $propertiesPath. Do not share that file." }
$enteredUsername = Read-Host "GitHub username [$Username]"
if (-not [string]::IsNullOrWhiteSpace($enteredUsername)) { $Username = $enteredUsername.Trim() }
if ($Username -notmatch '^[A-Za-z0-9-]+$') { throw 'Enter a valid GitHub username.' }
$secureToken = Read-Host 'GitHub token (input hidden)' -AsSecureString
$tokenPointer = [IntPtr]::Zero
$token = $null
$authorization = $null
$previousProtocol = [Net.ServicePointManager]::SecurityProtocol
try {
    $tokenPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureToken)
    $token = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($tokenPointer)
    if ([string]::IsNullOrWhiteSpace($token) -or $token -match '\s') { throw 'Enter a nonempty GitHub token without whitespace.' }
    $authorization = 'Basic ' + [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Username + ':' + $token))
    [Net.ServicePointManager]::SecurityProtocol = $previousProtocol -bor [Net.SecurityProtocolType]::Tls12
    try {
        $response = Invoke-WebRequest -Uri $markerUrl -Headers @{ Authorization = $authorization } -UseBasicParsing -TimeoutSec 30
        $responseContent = if ($response.Content -is [byte[]]) { [Text.Encoding]::UTF8.GetString($response.Content) } else { [string]$response.Content }
        if ($response.StatusCode -ne 200 -or $responseContent -notmatch '<project[\s>]') { throw 'Unexpected package response.' }
    }
    catch {
        $failure = if ($_.Exception.Response) { 'HTTP ' + [int]$_.Exception.Response.StatusCode } else { $_.Exception.GetType().Name }
        throw "GitHub Packages validation failed ($failure). Check the username, classic token read:packages permission, token expiration, and network access. Nothing was saved."
    }
    if ($ValidateOnly) {
        Write-Host "GitHub Packages access verified for $Username. Nothing was saved."
        return
    }

    $existingContent = if (Test-Path -LiteralPath $propertiesPath) { [IO.File]::ReadAllText($propertiesPath) } else { '' }
    $newline = if ($existingContent.Contains("`r`n")) { "`r`n" } else { "`n" }
    $lines = @($existingContent -split '\r?\n' | Where-Object { $_ -notmatch '^\s*gpr\.(user|key)(?:\s*[=:]|\s+|$)' })
    # Remove only the trailing split sentinel before appending the credentials.
    if ($lines.Count -gt 0 -and $lines[-1] -eq '') { $lines = @($lines | Select-Object -SkipLast 1) }
    # Escape property delimiters and backslashes; GitHub token characters normally
    # need no escaping, but Java properties must retain the exact supplied value.
    $escapedToken = $token.Replace('\', '\\').Replace(':', '\:').Replace('=', '\=').Replace('#', '\#').Replace('!', '\!')
    $content = (@($lines) + @("gpr.user=$Username", "gpr.key=$escapedToken")) -join $newline
    [IO.Directory]::CreateDirectory($gradleHome) | Out-Null
    if (-not (Test-Path -LiteralPath $propertiesPath)) {
        $stream = [IO.File]::Create($propertiesPath)
        $stream.Dispose()
        $acl = New-Object Security.AccessControl.FileSecurity
        $acl.SetAccessRuleProtection($true, $false)
        $currentUser = [Security.Principal.WindowsIdentity]::GetCurrent().User
        $acl.SetOwner($currentUser)
        foreach ($sid in @($currentUser, (New-Object Security.Principal.SecurityIdentifier 'S-1-5-18'), (New-Object Security.Principal.SecurityIdentifier 'S-1-5-32-544'))) {
            $rule = New-Object Security.AccessControl.FileSystemAccessRule($sid, 'FullControl', 'Allow')
            $acl.AddAccessRule($rule)
        }
        Set-Acl -LiteralPath $propertiesPath -AclObject $acl
    }
    [IO.File]::WriteAllText($propertiesPath, $content + $newline, (New-Object Text.UTF8Encoding($false)))
    Write-Host "GitHub Packages access verified for $Username. Credentials saved in $propertiesPath."
}
finally {
    [Net.ServicePointManager]::SecurityProtocol = $previousProtocol
    if ($tokenPointer -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($tokenPointer) }
    $secureToken.Dispose()
    $token = $null
    $authorization = $null
    $escapedToken = $null
    $content = $null
    $existingContent = $null
    $lines = $null
}
