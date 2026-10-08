# Translate only the 2026-10-08 relocation's known prefixes. Historical receipt
# bytes remain unchanged; callers must still validate hashes and owned boundaries.
function Convert-LegacyBuildPath([string]$Path, [string]$Repository) {
    if (!$Path) { throw 'Missing build artifact path.' }
    $repoPath = [IO.Path]::GetFullPath($Repository).TrimEnd('\', '/')
    $fullPath = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($Path)) { $Path } else { Join-Path $repoPath $Path }))
    $externalRoot = [IO.Path]::GetFullPath((Join-Path $repoPath '../builds/steamlink-patches'))
    foreach ($mapping in @(
        @{ Old='patches/build'; New='gradle/patches' },
        @{ Old='build'; New='build' }
    )) {
        $oldRoot = [IO.Path]::GetFullPath((Join-Path $repoPath $mapping.Old))
        $newRoot = Join-Path $externalRoot $mapping.New
        if ($fullPath.Equals($oldRoot, [StringComparison]::OrdinalIgnoreCase)) { return $newRoot }
        if ($fullPath.StartsWith($oldRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            return Join-Path $newRoot $fullPath.Substring($oldRoot.Length + 1)
        }
    }
    return $fullPath
}
