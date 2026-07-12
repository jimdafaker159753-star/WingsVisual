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

if (-not $source.Contains('Hidden pass must run before the original model writes depth.')) {
    throw 'Stage 2 depth-order fix was not found. ESP.cpp was not changed.'
}
if ($source.Contains('Stable hidden pass: ignore the current depth-buffer contents.')) {
    throw 'Stage 2B appears to be already applied. ESP.cpp was not changed.'
}

$pattern = '(?s)ColorWithAlpha\(\s*g_chamsHiddenByCategory\[g_espCategory\]\s*,\s*82\s*\)\s*,\s*D3DCMP_GREATER'
$matches = [regex]::Matches($source, $pattern)
if ($matches.Count -ne 1) {
    throw "Expected one hidden GREATER pass, found $($matches.Count). ESP.cpp was not changed."
}

$replacement = @'
ColorWithAlpha(g_chamsHiddenByCategory[g_espCategory], 160),
            // Stable hidden pass: ignore the current depth-buffer contents.
            D3DCMP_ALWAYS
'@

$updated = [regex]::Replace($source, $pattern, $replacement, 1)

if ($updated.Contains('D3DCMP_GREATER')) {
    throw 'A GREATER depth comparison is still present. ESP.cpp was not changed.'
}
if (-not $updated.Contains('D3DCMP_ALWAYS')) {
    throw 'ALWAYS depth comparison validation failed. ESP.cpp was not changed.'
}

$backupPath = "$espPath.before-chams-always-fix.bak"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($backupPath, $source, $utf8NoBom)
[System.IO.File]::WriteAllText($espPath, $updated, $utf8NoBom)

Write-Host "Updated: $espPath"
Write-Host "Backup:  $backupPath"
Write-Host 'Stage 2B applied: hidden CHAMS now uses D3DCMP_ALWAYS with alpha 160.'
Write-Host 'Visible CHAMS, filters, ObjectManager, and color wheel were not changed.'
