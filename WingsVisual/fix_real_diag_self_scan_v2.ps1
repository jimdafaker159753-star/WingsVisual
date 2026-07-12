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

if (-not $source.Contains('CollectObjectManagerBases')) {
    throw 'The REAL object diagnostic was not found. ESP.cpp was not changed.'
}
if ($source.Contains('static uintptr_t g_diagObjectBases[1024]')) {
    Write-Host 'The self-scan fix is already installed. No change is required.'
    exit 0
}

$collectorAnchor = 'static int CollectObjectManagerBases(uintptr_t* bases, int capacity) {'
$collectorCount = ([regex]::Matches($source, [regex]::Escape($collectorAnchor))).Count
if ($collectorCount -ne 1) {
    throw "Expected one CollectObjectManagerBases function, found $collectorCount. ESP.cpp was not changed."
}

$updated = $source.Replace(
    $collectorAnchor,
    "static uintptr_t g_diagObjectBases[1024]{};`r`n`r`n$collectorAnchor")

$localPattern = 'uintptr_t\s+objectBases\s*\[\s*1024\s*\]\s*\{\s*\}\s*;\s*const\s+int\s+objectCount\s*=\s*CollectObjectManagerBases\s*\(\s*objectBases\s*,\s*1024\s*\)\s*;'
$localMatches = [regex]::Matches($updated, $localPattern)
if ($localMatches.Count -ne 1) {
    throw "Expected one local objectBases array, found $($localMatches.Count). ESP.cpp was not changed."
}

$localReplacement = @'
ZeroMemory(g_diagObjectBases, sizeof(g_diagObjectBases));
    const int objectCount = CollectObjectManagerBases(g_diagObjectBases, 1024);
'@
$updated = [regex]::Replace($updated, $localPattern, $localReplacement, 1)

$argumentPattern = '(?m)^(\s*)objectBases,(\s*)$'
$argumentMatches = [regex]::Matches($updated, $argumentPattern)
if ($argumentMatches.Count -ne 2) {
    throw "Expected two objectBases arguments, found $($argumentMatches.Count). ESP.cpp was not changed."
}
$updated = [regex]::Replace($updated, $argumentPattern, '$1g_diagObjectBases,$2')

if ($updated.Contains('uintptr_t objectBases[1024]')) {
    throw 'The local object list is still present. ESP.cpp was not changed.'
}
if (-not $updated.Contains('ZeroMemory(g_diagObjectBases')) {
    throw 'Global object-list initialization validation failed. ESP.cpp was not changed.'
}

$backupPath = "$espPath.before-real-diag-self-scan-fix.bak"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($backupPath, $source, $utf8NoBom)
[System.IO.File]::WriteAllText($espPath, $updated, $utf8NoBom)

Write-Host "Updated: $espPath"
Write-Host "Backup:  $backupPath"
Write-Host 'REAL diagnostic self-scan fixed.'
Write-Host 'The ObjectManager pointer list is now stored outside the scanned stack.'
