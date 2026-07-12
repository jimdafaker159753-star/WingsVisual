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

if ($source.Contains('DumpStackEntityCandidates')) {
    throw 'Entity stack diagnostics are already installed. ESP.cpp was not changed.'
}
if ($source.Contains('enum M2RenderKind')) {
    throw 'The failed NPC/Player classifier is still present. Restore the Stage 3 backup first.'
}

$diagnosticCode = @'

static bool EntityCandidateAlreadySeen(
    const uintptr_t* seen,
    int seenCount,
    uintptr_t candidate) {
    for (int i = 0; i < seenCount; ++i) {
        if (seen[i] == candidate) return true;
    }
    return false;
}

static bool LogStackEntityCandidate(
    char tag,
    int stackSlot,
    const char* sourceKind,
    uintptr_t candidate,
    uintptr_t* seen,
    int& seenCount) {
    if (candidate < 0x10000) return false;
    if (EntityCandidateAlreadySeen(seen, seenCount, candidate)) return false;
    if (!IsValidEntity(candidate)) return false;

    if (seenCount < 32) seen[seenCount++] = candidate;

    uint8_t objectType = OT_NONE;
    uint64_t guid = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    SafeRead(candidate + Off::OBJ_TYPE, objectType);
    SafeRead(candidate + Off::OBJ_GUID, guid);

    if (objectType == OT_PLAYER || objectType == OT_UNIT) {
        SafeRead(candidate + Off::POS_X, x);
        SafeRead(candidate + Off::POS_Y, y);
        SafeRead(candidate + Off::POS_Z, z);
    } else if (objectType == OT_GAMEOBJECT) {
        SafeRead(candidate + 0xE8, x);
        SafeRead(candidate + 0xEC, y);
        SafeRead(candidate + 0xF0, z);
    }

    char buffer[256];
    _snprintf_s(
        buffer,
        _TRUNCATE,
        "[ENTITY_%c] slot=%d via=%s ptr=0x%08X type=%u guid=%08X%08X pos=(%.2f,%.2f,%.2f)\n",
        tag,
        stackSlot,
        sourceKind,
        static_cast<unsigned>(candidate),
        static_cast<unsigned>(objectType),
        static_cast<unsigned>(guid >> 32),
        static_cast<unsigned>(guid & 0xFFFFFFFFu),
        x,
        y,
        z);
    OutputDebugStringA(buffer);
    return true;
}

static void DumpStackEntityCandidates(char tag) {
    uintptr_t currentEsp = 0;
    __asm {
        mov currentEsp, esp
    }

    uintptr_t* stack = reinterpret_cast<uintptr_t*>(currentEsp);
    if (!stack) return;

    uintptr_t seen[32]{};
    int seenCount = 0;
    int foundCount = 0;

    __try {
        for (int i = 0; i < 600 && foundCount < 32; ++i) {
            const uintptr_t direct = stack[i];
            if (LogStackEntityCandidate(
                    tag,
                    i,
                    "direct",
                    direct,
                    seen,
                    seenCount)) {
                ++foundCount;
            }

            uintptr_t indirect = 0;
            if (SafeRead(direct, indirect) &&
                LogStackEntityCandidate(
                    tag,
                    i,
                    "indirect",
                    indirect,
                    seen,
                    seenCount)) {
                ++foundCount;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    if (foundCount == 0) {
        char buffer[64];
        _snprintf_s(buffer, _TRUNCATE, "[ENTITY_%c] none\n", tag);
        OutputDebugStringA(buffer);
    }
}
'@

$anchor = 'static void DumpFullStack(char tag)'
$anchorCount = ([regex]::Matches($source, [regex]::Escape($anchor))).Count
if ($anchorCount -ne 1) {
    throw "Expected one DumpFullStack function, found $anchorCount. ESP.cpp was not changed."
}
$updated = $source.Replace($anchor, $diagnosticCode + "`r`n`r`n" + $anchor)

$oldCaptureA = "if (g_captureA > 0) { g_captureA--; DumpFullStack('A'); }"
$newCaptureA = "if (g_captureA > 0) { g_captureA--; DumpFullStack('A'); DumpStackEntityCandidates('A'); }"
$oldCaptureB = "else if (g_captureB > 0) { g_captureB--; DumpFullStack('B'); }"
$newCaptureB = "else if (g_captureB > 0) { g_captureB--; DumpFullStack('B'); DumpStackEntityCandidates('B'); }"

if (-not $updated.Contains($oldCaptureA)) {
    throw 'STACK_A capture location was not found. ESP.cpp was not changed.'
}
if (-not $updated.Contains($oldCaptureB)) {
    throw 'STACK_B capture location was not found. ESP.cpp was not changed.'
}

$updated = $updated.Replace($oldCaptureA, $newCaptureA)
$updated = $updated.Replace($oldCaptureB, $newCaptureB)

$required = @(
    'DumpStackEntityCandidates',
    '[ENTITY_%c]',
    'type=%u',
    'via=%s',
    '"direct"',
    '"indirect"',
    "DumpStackEntityCandidates('A')",
    "DumpStackEntityCandidates('B')"
)
foreach ($item in $required) {
    if (-not $updated.Contains($item)) {
        throw "Validation failed for: $item. ESP.cpp was not changed."
    }
}

$backupPath = "$espPath.before-entity-stack-diag.bak"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($backupPath, $source, $utf8NoBom)
[System.IO.File]::WriteAllText($espPath, $updated, $utf8NoBom)

Write-Host "Updated: $espPath"
Write-Host "Backup:  $backupPath"
Write-Host 'Entity stack diagnostics installed.'
Write-Host 'Use [ for ENTITY_A and ] for ENTITY_B captures.'
Write-Host 'Rendering and category filtering were not changed.'
