param(
    [string]$ProjectDir = "."
)

$ErrorActionPreference = "Stop"
$path = Join-Path $ProjectDir "dllmain.cpp"
if (-not (Test-Path $path)) {
    throw "dllmain.cpp not found: $path"
}

$backup = "$path.before-heroes-checkboxes-tremor.bak"
Copy-Item $path $backup -Force

$text = [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)
$hadCrLf = $text.Contains("`r`n")
# Normalize line endings so exact patches work with Visual Studio CRLF files.
$text = $text.Replace("`r`n", "`n")

function Replace-Exact([string]$name, [string]$old, [string]$new) {
    if (-not $script:text.Contains($old)) {
        throw "Patch location not found: $name. Restored file remains in backup: $backup"
    }
    $script:text = $script:text.Replace($old, $new)
}

# 1. Remove the old AutoKick keyboard handler completely.
$hotkeyPattern = 'static const char\* kWarlockAutoKickHotkeyLua = R"WARLOCK_HOTKEY\(.*?\)WARLOCK_HOTKEY";\r?\n\r?\n'
$before = $text
$text = [regex]::Replace($text, $hotkeyPattern, '', [System.Text.RegularExpressions.RegexOptions]::Singleline)
if ($text -eq $before) {
    throw "Old AutoKick hotkey block was not found. No output file was written. Backup: $backup"
}

# 2. Add TremorBreaker Lua (no keyboard hotkey; controlled only by UI checkbox).
$tremorLua = @'

static const char* kTremorBreakerInitLua = R"TREMOR_INIT(-- TremorBreaker (WARLOCK): automatic Tremor Totem search
if not TB_Init then
TB_Init = true
TB_TremorNames = { "Тотем трепета", "Tremor Totem" }
TB_LastScan = 0
if TB_Enabled == nil then TB_Enabled = true end

function TB_IsTremorName(name)
if not name then return false end
for _, n in ipairs(TB_TremorNames) do
if name == n then return true end
end
if string.find(string.lower(name), "tremor") then return true end
if string.find(name, "трепет") then return true end
return false
end

function TB_IsTremor(unit)
if not UnitExists(unit) then return false end
if UnitIsPlayer(unit) then return false end
return TB_IsTremorName(UnitName(unit))
end

if DEFAULT_CHAT_FRAME then
DEFAULT_CHAT_FRAME:AddMessage("|cFF00FF00[TremorBreaker]|r ready. Controlled from Heroes -> Warlock.")
end
end
return false)TREMOR_INIT";

static const char* kTremorBreakerMainLua = R"TREMOR_MAIN(-- Pet Break Tremor: continuous automatic search
if not TB_Enabled then return false end
if not UnitExists("pet") or UnitIsDead("pet") then return false end
if UnitIsDeadOrGhost("player") then return false end

-- Do not change target while the player is casting/channeling.
if UnitCastingInfo("player") or UnitChannelInfo("player") then return false end

-- Pet is already attacking Tremor Totem: do not interfere.
if UnitExists("pettarget") and TB_IsTremor("pettarget") and not UnitIsDead("pettarget") then
return false
end

if TB_LastScan == nil then TB_LastScan = 0 end
local now = GetTime()
if (now - TB_LastScan) < 0.35 then return false end
TB_LastScan = now

-- Preserve the player's current target through focus when focus is free.
local savedGUID = UnitGUID("target")
local hadTarget = (savedGUID ~= nil)
local hadFocus = UnitExists("focus")
local stashed = false
if hadTarget and not hadFocus then
RunMacroText("/focus")
stashed = true
end

-- Search hostile units and stop only on Tremor Totem.
local found = false
local firstGUID = nil
for i = 1, 20 do
RunMacroText("/targetenemy")
if not UnitExists("target") then break end
local g = UnitGUID("target")
if not firstGUID then
firstGUID = g
elseif g == firstGUID then
break
end
if TB_IsTremor("target") and UnitCanAttack("player", "target") and not UnitIsDead("target") then
found = true
break
end
end

if found then
local g = UnitGUID("target")
local petOnIt = UnitExists("pettarget") and (UnitGUID("pettarget") == g)
if not petOnIt then PetAttack() end
end

-- Restore the player's target.
if stashed then
RunMacroText("/target focus")
RunMacroText("/clearfocus")
elseif hadTarget then
if UnitGUID("target") ~= savedGUID then
RunMacroText("/targetlasttarget")
end
else
if UnitExists("target") then
RunMacroText("/cleartarget")
end
end

-- Tremor Totem must never remain selected by the player.
if UnitExists("target") and TB_IsTremor("target") then
RunMacroText("/cleartarget")
end
return false)TREMOR_MAIN";
'@

Replace-Exact "insert TremorBreaker Lua" 'return false)WARLOCK_MAIN";' ('return false)WARLOCK_MAIN";' + $tremorLua)

# 3. Add new controls and state variables.
Replace-Exact "Warlock HWND declarations" @'
HWND hHeroWarlockRow = NULL, hHeroWarlockCheck = NULL;
HWND hHeroWarlockTitle = NULL, hHeroWarlockInfo = NULL, hHeroWarlockHint = NULL;
'@ @'
HWND hHeroWarlockRow = NULL, hHeroWarlockCheck = NULL;
HWND hHeroWarlockKickAllCheck = NULL, hHeroWarlockInstantCheck = NULL;
HWND hHeroTremorBreakerCheck = NULL;
HWND hHeroWarlockTitle = NULL, hHeroWarlockInfo = NULL, hHeroWarlockHint = NULL;
'@

Replace-Exact "Warlock state declarations" @'
bool g_warlockExpanded = false;
bool g_warlockEnabled = false;
bool g_warlockInitPending = false;
'@ @'
bool g_warlockExpanded = false;
bool g_warlockEnabled = false;
bool g_warlockInitPending = false;
bool g_warlockKickAll = false;
bool g_warlockInstant = true;
bool g_tremorBreakerEnabled = false;
bool g_tremorBreakerInitPending = false;
'@

# 4. Replace the runtime tick: no key polling, checkbox state is authoritative.
Replace-Exact "RunHeroScriptsTick" @'
void RunHeroScriptsTick() {
 if (!g_warlockEnabled) return;
 if (g_warlockInitPending) {
 ExecuteLuaNow(kWarlockAutoKickInitLua);
 g_warlockInitPending = false;
 }
 ExecuteLuaNow(kWarlockAutoKickHotkeyLua);
 ExecuteLuaNow(kWarlockAutoKickMainLua);
}
'@ @'
void RunHeroScriptsTick() {
 if (g_warlockEnabled) {
 if (g_warlockInitPending) {
 ExecuteLuaNow(kWarlockAutoKickInitLua);
 ExecuteLuaNow(g_warlockKickAll ? "AK_KickAll=true" : "AK_KickAll=false");
 ExecuteLuaNow(g_warlockInstant ? "AK_Instant=true" : "AK_Instant=false");
 ExecuteLuaNow("AK_Enabled=true");
 g_warlockInitPending = false;
 }
 ExecuteLuaNow(kWarlockAutoKickMainLua);
 }

 if (g_tremorBreakerEnabled) {
 if (g_tremorBreakerInitPending) {
 ExecuteLuaNow(kTremorBreakerInitLua);
 ExecuteLuaNow("TB_Enabled=true");
 g_tremorBreakerInitPending = false;
 }
 ExecuteLuaNow(kTremorBreakerMainLua);
 }
}
'@

# 5. Show/hide every Warlock checkbox with the expanded section.
Replace-Exact "Heroes layout" @'
 ShowWindow(hHeroWarlockCheck, warlockSection);
 ShowWindow(hHeroWarlockTitle, warlockSection);
 ShowWindow(hHeroWarlockInfo, warlockSection);
 ShowWindow(hHeroWarlockHint, warlockSection);
'@ @'
 ShowWindow(hHeroWarlockCheck, warlockSection);
 ShowWindow(hHeroWarlockKickAllCheck, warlockSection);
 ShowWindow(hHeroWarlockInstantCheck, warlockSection);
 ShowWindow(hHeroTremorBreakerCheck, warlockSection);
 ShowWindow(hHeroWarlockTitle, warlockSection);
 ShowWindow(hHeroWarlockInfo, warlockSection);
 ShowWindow(hHeroWarlockHint, warlockSection);
'@

# 6. Owner-draw labels for the four checkbox controls.
Replace-Exact "Heroes draw items" @'
 case 201: DrawCheck(d, "Enable AutoKick Spell Lock", g_warlockEnabled, false); return TRUE;
'@ @'
 case 201: DrawCheck(d, "Enable AutoKick Spell Lock", g_warlockEnabled, false); return TRUE;
 case 202: DrawCheck(d, "Kick all interruptible casts", g_warlockKickAll, false); return TRUE;
 case 203: DrawCheck(d, "Instant interrupt mode", g_warlockInstant, false); return TRUE;
 case 204: DrawCheck(d, "Pet Break Tremor Totem", g_tremorBreakerEnabled, false); return TRUE;
'@

# 7. Handle checkbox clicks and synchronize short Lua assignments on the game thread.
Replace-Exact "Heroes commands" @'
 else if (id == 201) {
 g_warlockEnabled = !g_warlockEnabled;
 if (g_warlockEnabled) g_warlockInitPending = true;
 InvalidateRect(hHeroWarlockCheck, NULL, TRUE);
 }
 else if (id >= 10 && id <= 22) {
'@ @'
 else if (id == 201) {
 g_warlockEnabled = !g_warlockEnabled;
 if (g_warlockEnabled) {
 g_warlockInitPending = true;
 QueueLua("AK_Enabled=true");
 }
 else {
 QueueLua("AK_Enabled=false");
 }
 InvalidateRect(hHeroWarlockCheck, NULL, TRUE);
 }
 else if (id == 202) {
 g_warlockKickAll = !g_warlockKickAll;
 QueueLua(g_warlockKickAll ? "AK_KickAll=true" : "AK_KickAll=false");
 InvalidateRect(hHeroWarlockKickAllCheck, NULL, TRUE);
 }
 else if (id == 203) {
 g_warlockInstant = !g_warlockInstant;
 QueueLua(g_warlockInstant ? "AK_Instant=true" : "AK_Instant=false");
 InvalidateRect(hHeroWarlockInstantCheck, NULL, TRUE);
 }
 else if (id == 204) {
 g_tremorBreakerEnabled = !g_tremorBreakerEnabled;
 if (g_tremorBreakerEnabled) {
 g_tremorBreakerInitPending = true;
 QueueLua("TB_Enabled=true");
 }
 else {
 QueueLua("TB_Enabled=false");
 }
 InvalidateRect(hHeroTremorBreakerCheck, NULL, TRUE);
 }
 else if (id >= 10 && id <= 22) {
'@

# 8. Create the new controls. All former hotkey modes are now checkboxes.
Replace-Exact "Heroes control creation" @'
 hHeroWarlockCheck = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 40, 172, 388, 24, hMenu, (HMENU)201, hSelf, NULL);
 hHeroWarlockTitle = mkLabel("AutoKick Spell Lock (Felhunter)", 40, 204, 388, hFontHead);
 hHeroWarlockInfo = mkLabel("Focus -> Target priority. Uses pet Spell Lock.", 40, 232, 388, hFontNorm);
 hHeroWarlockHint = mkLabel("RShift on/off | RAlt kick all | RCtrl instant/anti-fake", 40, 258, 388, hFontNorm);
'@ @'
 hHeroWarlockCheck = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 40, 172, 388, 24, hMenu, (HMENU)201, hSelf, NULL);
 hHeroWarlockKickAllCheck = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 40, 202, 388, 24, hMenu, (HMENU)202, hSelf, NULL);
 hHeroWarlockInstantCheck = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 40, 232, 388, 24, hMenu, (HMENU)203, hSelf, NULL);
 hHeroTremorBreakerCheck = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 40, 272, 388, 24, hMenu, (HMENU)204, hSelf, NULL);
 hHeroWarlockTitle = mkLabel("Warlock automation", 40, 306, 388, hFontHead);
 hHeroWarlockInfo = mkLabel("Spell Lock: Focus -> Target. Tremor: pet auto-search.", 40, 334, 388, hFontNorm);
 hHeroWarlockHint = mkLabel("No hotkeys: every mode is controlled by checkboxes.", 40, 360, 388, hFontNorm);
'@

# Validation: old keyboard polling must no longer be referenced.
if ($text.Contains("kWarlockAutoKickHotkeyLua") -or
    $text.Contains("IsRightShiftKeyDown()") -or
    $text.Contains("IsRightAltKeyDown()") -or
    $text.Contains("IsRightControlKeyDown()")) {
    throw "Validation failed: an old Heroes hotkey reference remains. Backup: $backup"
}

$required = @(
    "hHeroWarlockKickAllCheck",
    "hHeroWarlockInstantCheck",
    "hHeroTremorBreakerCheck",
    "kTremorBreakerInitLua",
    "kTremorBreakerMainLua",
    "Pet Break Tremor Totem"
)
foreach ($token in $required) {
    if (-not $text.Contains($token)) {
        throw "Validation failed for token: $token. Backup: $backup"
    }
}

# Restore the original Visual Studio line-ending style.
if ($hadCrLf) {
    $text = $text.Replace("`n", "`r`n")
}

# Write UTF-8 without BOM (safe for the existing C++ source).
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($path, $text, $utf8NoBom)

Write-Host "Done: Heroes hotkeys replaced with checkboxes."
Write-Host "Added: Pet Break Tremor Totem checkbox and Lua logic."
Write-Host "Backup: $backup"
Write-Host "Build: Release | x86 (Win32)"
