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
if (-not $source.Contains('DumpStackEntityCandidates')) {
    throw 'The previous ENTITY diagnostic was not found. ESP.cpp was not changed.'
}
if ($source.Contains('CollectObjectManagerBases')) {
    throw 'Real-object diagnostics are already installed. ESP.cpp was not changed.'
}

$pattern = '(?s)static bool EntityCandidateAlreadySeen\s*\(.*?\n\s*static void DumpFullStack\s*\(\s*char tag\s*\)'
$matches = [regex]::Matches($source, $pattern)
if ($matches.Count -ne 1) {
    throw "Expected one old ENTITY diagnostic block, found $($matches.Count). ESP.cpp was not changed."
}

$replacement = @'
static int CollectObjectManagerBases(uintptr_t* bases, int capacity) {
    if (!bases || capacity <= 0) return 0;

    uintptr_t manager = GetObjectManager();
    if (!manager) return 0;

    uintptr_t object = 0;
    if (!SafeRead(manager + Off::FIRST_OBJECT, object)) return 0;

    int count = 0;
    int guard = 0;
    while (object && !(object & 1) && count < capacity && guard++ < 8192) {
        bases[count++] = object;

        uintptr_t nextObject = 0;
        if (!SafeRead(object + Off::NEXT_OBJECT, nextObject)) break;
        if (nextObject == object) break;
        object = nextObject;
    }

    return count;
}

static bool IsExactObjectManagerBase(
    uintptr_t candidate,
    const uintptr_t* bases,
    int baseCount) {
    for (int i = 0; i < baseCount; ++i) {
        if (bases[i] == candidate) return true;
    }
    return false;
}

static bool RealEntityAlreadySeen(
    const uintptr_t* seen,
    int seenCount,
    uintptr_t candidate) {
    for (int i = 0; i < seenCount; ++i) {
        if (seen[i] == candidate) return true;
    }
    return false;
}

static bool LogRealEntityCandidate(
    char tag,
    int stackSlot,
    const char* sourceKind,
    uintptr_t candidate,
    const uintptr_t* objectBases,
    int objectCount,
    uintptr_t* seen,
    int& seenCount) {
    if (!IsExactObjectManagerBase(candidate, objectBases, objectCount)) return false;
    if (RealEntityAlreadySeen(seen, seenCount, candidate)) return false;
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
        "[REAL_%c] slot=%d via=%s ptr=0x%08X type=%u guid=%08X%08X pos=(%.2f,%.2f,%.2f)\n",
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
    uintptr_t objectBases[1024]{};
    const int objectCount = CollectObjectManagerBases(objectBases, 1024);

    if (objectCount <= 0) {
        char buffer[64];
        _snprintf_s(buffer, _TRUNCATE, "[REAL_%c] no-object-manager\n", tag);
        OutputDebugStringA(buffer);
        return;
    }

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
            if (LogRealEntityCandidate(
                    tag,
                    i,
                    "direct",
                    direct,
                    objectBases,
                    objectCount,
                    seen,
                    seenCount)) {
                ++foundCount;
            }

            uintptr_t indirect = 0;
            if (SafeRead(direct, indirect) &&
                LogRealEntityCandidate(
                    tag,
                    i,
                    "indirect",
                    indirect,
                    objectBases,
                    objectCount,
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
        _snprintf_s(buffer, _TRUNCATE, "[REAL_%c] none\n", tag);
        OutputDebugStringA(buffer);
    }
}

static void DumpFullStack(char tag)
'@

$updated = [regex]::Replace($source, $pattern, $replacement, 1)

$required = @(
    'CollectObjectManagerBases',
    'IsExactObjectManagerBase',
    'LogRealEntityCandidate',
    '[REAL_%c]',
    'no-object-manager',
    'objectBases[1024]'
)
foreach ($item in $required) {
    if (-not $updated.Contains($item)) {
        throw "Validation failed for: $item. ESP.cpp was not changed."
    }
}

$backupPath = "$espPath.before-real-object-diag.bak"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($backupPath, $source, $utf8NoBom)
[System.IO.File]::WriteAllText($espPath, $updated, $utf8NoBom)

Write-Host "Updated: $espPath"
Write-Host "Backup:  $backupPath"
Write-Host 'Real ObjectManager pointer diagnostics installed.'
Write-Host 'Use [ for REAL_A and ] for REAL_B captures.'
Write-Host 'Rendering and category filtering were not changed.'
