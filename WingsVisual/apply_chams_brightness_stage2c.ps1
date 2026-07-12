$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$espPath = Join-Path $scriptDir 'WingsVisual\ESP.cpp'
if (-not (Test-Path $espPath)) {
    $espPath = Join-Path $scriptDir 'ESP.cpp'
}
if (-not (Test-Path $espPath)) {
    throw 'ESP.cpp not found. Put this script in the repository root or beside ESP.cpp.'
}

$source = [System.IO.File]::ReadAllText($espPath)

if (-not $source.Contains('Stable hidden pass: ignore the current depth-buffer contents.')) {
    throw 'Stage 2B D3DCMP_ALWAYS fix was not found. ESP.cpp was not changed.'
}

$pattern = '(?s)ColorWithAlpha\(\s*g_chamsHiddenByCategory\[g_espCategory\]\s*,\s*(?<alpha>\d+)\s*\)\s*,\s*//\s*Stable hidden pass: ignore the current depth-buffer contents\.\s*D3DCMP_ALWAYS'
$match = [regex]::Match($source, $pattern)
if (-not $match.Success) {
    throw 'The existing hidden CHAMS block was not recognized. ESP.cpp was not changed.'
}

$currentAlpha = [int]$match.Groups['alpha'].Value
if ($currentAlpha -eq 160) {
    Write-Host 'Hidden CHAMS alpha is already 160. No change is required.'
    exit 0
}

$replacement = @'
ColorWithAlpha(g_chamsHiddenByCategory[g_espCategory], 160),
            // Stable hidden pass: ignore the current depth-buffer contents.
            D3DCMP_ALWAYS
'@

$updated = [regex]::Replace($source, $pattern, $replacement, 1)
if (-not $updated.Contains('g_chamsHiddenByCategory[g_espCategory], 160')) {
    throw 'Alpha validation failed. ESP.cpp was not changed.'
}

$backupPath = "$espPath.before-chams-brightness-fix.bak"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($backupPath, $source, $utf8NoBom)
[System.IO.File]::WriteAllText($espPath, $updated, $utf8NoBom)

Write-Host "Updated: $espPath"
Write-Host "Backup:  $backupPath"
Write-Host "Hidden CHAMS alpha changed from $currentAlpha to 160."
Write-Host 'Depth mode remains D3DCMP_ALWAYS. Other code was not changed.'
