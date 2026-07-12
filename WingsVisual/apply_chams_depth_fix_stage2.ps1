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

# Stage 2 must be applied after the working double-buffered wheel checkpoint.
if (-not $source.Contains('CreateDIBSection')) {
    throw 'Stage 1 color-wheel fix was not found. ESP.cpp was not changed.'
}
if ($source.Contains('Hidden pass must run before the original model writes depth.')) {
    throw 'Stage 2 appears to be already applied. ESP.cpp was not changed.'
}

$pattern = '(?s)\s*g_m2Anchors\[idx\]\.hits\+\+;.*?\n\}\s*\n\s*static void AppendGameObjectsToEsp'
$matches = [regex]::Matches($source, $pattern)
if ($matches.Count -ne 1) {
    throw "Expected one hkDIP render block, found $($matches.Count). ESP.cpp was not changed."
}

$replacement = @'

    g_m2Anchors[idx].hits++;
    g_paintedFrame++;

    HRESULT baseResult = D3D_OK;
    bool originalDrawn = false;
    ChamsState saved{};
    bool stateSaved = false;

    __try {
        // Hidden pass must run before the original model writes depth.
        SaveState(dev, saved);
        stateSaved = true;

        SetSoftOverlayState(
            dev,
            ColorWithAlpha(g_chamsHiddenByCategory[g_espCategory], 82),
            D3DCMP_GREATER);

        oDIP(
            dev,
            type,
            baseVI,
            minVI,
            numV,
            startIdx,
            primCount);

        RestoreState(dev, saved);
        stateSaved = false;

        // Draw the normal game model with its original states and shaders.
        baseResult = oDIP(
            dev,
            type,
            baseVI,
            minVI,
            numV,
            startIdx,
            primCount);

        originalDrawn = true;
        if (FAILED(baseResult)) {
            return baseResult;
        }

        // Add a soft tint only to the visible part.
        SaveState(dev, saved);
        stateSaved = true;

        SetSoftOverlayState(
            dev,
            ColorWithAlpha(g_chamsVisibleByCategory[g_espCategory], 54),
            D3DCMP_LESSEQUAL);

        oDIP(
            dev,
            type,
            baseVI,
            minVI,
            numV,
            startIdx,
            primCount);

        RestoreState(dev, saved);
        stateSaved = false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (stateSaved) {
            RestoreState(dev, saved);
            stateSaved = false;
        }

        if (!originalDrawn) {
            return oDIP(
                dev,
                type,
                baseVI,
                minVI,
                numV,
                startIdx,
                primCount);
        }

        return baseResult;
    }

    return baseResult;
}

static void AppendGameObjectsToEsp
'@

$updated = [regex]::Replace($source, $pattern, $replacement, 1)

# Validate the resulting source before writing it.
if (-not $updated.Contains('D3DCMP_GREATER')) {
    throw 'Hidden depth pass validation failed. ESP.cpp was not changed.'
}
if (-not $updated.Contains('D3DCMP_LESSEQUAL')) {
    throw 'Visible depth pass validation failed. ESP.cpp was not changed.'
}
if (-not $updated.Contains('originalDrawn = true;')) {
    throw 'Original draw validation failed. ESP.cpp was not changed.'
}

$backupPath = "$espPath.before-chams-depth-fix.bak"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($backupPath, $source, $utf8NoBom)
[System.IO.File]::WriteAllText($espPath, $updated, $utf8NoBom)

Write-Host "Updated: $espPath"
Write-Host "Backup:  $backupPath"
Write-Host 'Stage 2 applied: hidden pass -> original model -> visible pass.'
Write-Host 'NPC/Player filtering and ObjectManager were not changed.'
