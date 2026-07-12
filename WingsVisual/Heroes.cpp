#include "Heroes.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

    static HWND hMenu = NULL;
    static HMODULE hSelf = NULL;
    static HBRUSH hbrBack = NULL;
    static HFONT hFontHead = NULL;
    static HFONT hFontNorm = NULL;
    static HeroesUiTheme g_theme = {};
    static HeroesExecuteLuaFn g_executeLua = nullptr;
    static HeroesQueueLuaFn g_queueLua = nullptr;

    static HWND hLblHeroes = NULL;
    static HWND hHeroWarlockRow = NULL;
    static HWND hHeroWarlockCheck = NULL;
    static HWND hHeroWarlockKickAllCheck = NULL;
    static HWND hHeroWarlockInstantCheck = NULL;
    static HWND hHeroWarlockTremorCheck = NULL;
    static HWND hHeroWarlockTremorKey = NULL;
    static HWND hHeroWarlockTitle = NULL;
    static HWND hHeroWarlockInfo = NULL;

    static bool g_moduleVisible = false;
    static bool g_warlockExpanded = false;
    static bool g_warlockEnabled = false;
    static bool g_warlockKickAll = false;
    static bool g_warlockInstant = true;
    static bool g_warlockTremorEnabled = false;
    static bool g_warlockInitPending = false;
    static bool g_warlockTremorInitPending = false;

    enum TremorBindState {
        TREMOR_BIND_IDLE = 0,
        TREMOR_BIND_WAIT_RELEASE = 1,
        TREMOR_BIND_CAPTURE = 2,
        TREMOR_BIND_WAIT_SELECTED_RELEASE = 3
    };

    static volatile LONG g_tremorBindState = TREMOR_BIND_IDLE;
    static volatile bool g_tremorBindingKey = false;
    static volatile int g_tremorHotkey = 0;
    static volatile int g_tremorPendingHotkey = 0;
    static volatile LONG g_tremorManualActiveUntil = 0;
    static bool g_tremorHotkeyWasDown = false;
    static char g_configPath[MAX_PATH] = { 0 };

    struct NativeTremorSlot {
        uint64_t guid;
        uintptr_t objectBase;
        DWORD nextCommandAt;
        bool seenNow;
    };

    static const int MAX_NATIVE_TREMORS = 32;
    static NativeTremorSlot g_tbSlots[MAX_NATIVE_TREMORS] = {};
    static DWORD g_tbNextNativeScan = 0;

    static void ExecuteLuaNow(const char* code) {
        if (g_executeLua && code && *code) g_executeLua(code);
    }

    static void QueueLua(const char* code) {
        if (g_queueLua && code && *code) g_queueLua(code);
    }

    static HWND CreateLabel(const char* text, int x, int y, int width, HFONT font) {
        HWND control = CreateWindowA(
            "STATIC", text, WS_VISIBLE | WS_CHILD,
            x, y, width, 22, hMenu, NULL, hSelf, NULL);
        SendMessageA(control, WM_SETFONT, (WPARAM)font, TRUE);
        return control;
    }

    static HWND CreateCheck(int id, int x, int y, int width) {
        return CreateWindowA(
            "BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            x, y, width, 24, hMenu, (HMENU)(INT_PTR)id, hSelf, NULL);
    }

    static void DrawCheck(LPDRAWITEMSTRUCT d, const char* label, bool checked, bool bigFont) {
        RECT rc = d->rcItem;
        FillRect(d->hDC, &rc, hbrBack);
        const int boxSize = 18;
        const int boxY = rc.top + ((rc.bottom - rc.top) - boxSize) / 2;
        RECT box = { rc.left, boxY, rc.left + boxSize, boxY + boxSize };
        HBRUSH inside = CreateSolidBrush(g_theme.box);
        FillRect(d->hDC, &box, inside);
        DeleteObject(inside);
        HBRUSH frame = CreateSolidBrush(g_theme.gold);
        FrameRect(d->hDC, &box, frame);
        DeleteObject(frame);
        if (checked) {
            RECT mark = { box.left + 4, box.top + 4, box.right - 4, box.bottom - 4 };
            HBRUSH fill = CreateSolidBrush(g_theme.gold);
            FillRect(d->hDC, &mark, fill);
            DeleteObject(fill);
        }
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, checked ? g_theme.gold : g_theme.text);
        HFONT oldFont = (HFONT)SelectObject(d->hDC, bigFont ? hFontHead : hFontNorm);
        RECT textRect = { box.right + 10, rc.top, rc.right, rc.bottom };
        DrawTextA(d->hDC, label, -1, &textRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(d->hDC, oldFont);
    }

    // ==========================================
    // --- LUA: WARLOCK AUTOKICK ---
    // ==========================================
    static const char* kWarlockAutoKickInitLua = R"WARLOCK_INIT(-- AutoKick v9 - arena healer intelligence
if AK_Init ~= 9 then
AK_Init = 9
AK_SpellLockID = 19647
AK_SpellLockName = GetSpellInfo(AK_SpellLockID) or "Spell Lock"
AK_Instant = true
AK_KickAll = false
AK_Announce = true
AK_CheckImmune = true
AK_Throttle = 0.20
AK_Units = { "focus", "arena1", "arena2", "arena3", "target" }
AK_LastTry = 0
AK_LastSlotScan = 0
AK_SpellLockSlot = nil
AK_BlockTremorUntil = 0
AK_Casts = {}
AK_Roles = {}
AK_ArenaByGUID = {}
AK_LastHealTarget = {}
if AK_Enabled == nil then AK_Enabled = true end

AK_KickNames = {}
AK_SpellBaseScore = {}
AK_SpellCategory = {}
AK_HealingEvidence = {}
AK_KickCount = 0

local function AddKickSpells(ids, baseScore, category)
    for _, id in ipairs(ids) do
        local name = GetSpellInfo(id)
        if name then
            AK_KickNames[name] = true
            if not AK_SpellBaseScore[name] or baseScore > AK_SpellBaseScore[name] then
                AK_SpellBaseScore[name] = baseScore
                AK_SpellCategory[name] = category
            end
        end
    end
end

local function AddHealingEvidence(ids)
    for _, id in ipairs(ids) do
        local name = GetSpellInfo(id)
        if name then AK_HealingEvidence[name] = true end
    end
end

-- Resurrection / battle resurrection.
AddKickSpells({ 2006, 7328, 2008, 50769, 20484 }, 1100, "resurrection")
-- Casted and channeled healing.
AddKickSpells({ 635, 19750, 2060, 2061, 47540, 596, 32546, 331, 8004, 1064, 5185, 50464, 8936, 740, 64843 }, 1000, "heal")
-- Crowd control.
AddKickSpells({ 118, 51514, 605, 6358, 2637, 710, 5782, 33786, 20066 }, 750, "control")
-- Dangerous damage channels/casts.
AddKickSpells({ 51505, 50796, 6353, 47610, 8092, 15407, 689 }, 500, "burst")

-- Evidence includes instant healing spells and healing auras.
AddHealingEvidence({
    635, 19750, 20473, 53563, 53601,
    2060, 2061, 32546, 596, 47540, 139, 33076, 64843,
    331, 8004, 1064, 61295, 974,
    5185, 8936, 50464, 740, 774, 33763, 48438, 18562
})
for _ in pairs(AK_KickNames) do AK_KickCount = AK_KickCount + 1 end

AK_ImmuneNames = {}
for _, id in ipairs({ 31821, 642 }) do
    local name = GetSpellInfo(id)
    if name then AK_ImmuneNames[name] = true end
end

AK_HealerClasses = { PALADIN = true, PRIEST = true, SHAMAN = true, DRUID = true }

function AK_UpdateArenaMap()
    local current = {}
    for index = 1, 3 do
        local unit = "arena" .. index
        if UnitExists(unit) then
            local guid = UnitGUID(unit)
            if guid then
                current[guid] = unit
                local _, class = UnitClass(unit)
                local role = AK_Roles[guid]
                if not role then
                    role = { score = AK_HealerClasses[class] and 10 or 0, confirmed = false, class = class, lastEvidence = 0 }
                    AK_Roles[guid] = role
                else
                    role.class = class or role.class
                end
            end
        end
    end
    AK_ArenaByGUID = current
end

function AK_AddHealerEvidence(sourceGUID, amount, destGUID)
    if not sourceGUID then return end
    AK_UpdateArenaMap()
    if not AK_ArenaByGUID[sourceGUID] then return end
    local role = AK_Roles[sourceGUID] or { score = 0, confirmed = false }
    role.score = math.min((role.score or 0) + amount, 140)
    role.confirmed = role.score >= 80
    role.lastEvidence = GetTime()
    AK_Roles[sourceGUID] = role
    if destGUID then AK_LastHealTarget[sourceGUID] = { guid = destGUID, time = GetTime() } end
end

function AK_GetRoleBonus(guid)
    local role = guid and AK_Roles[guid]
    if not role then return 0 end
    if role.confirmed then return 250 end
    return math.min((role.score or 0) * 2, 160)
end

function AK_GetHealHealthBonus(sourceGUID)
    local info = sourceGUID and AK_LastHealTarget[sourceGUID]
    if not info or (GetTime() - (info.time or 0)) > 3 then return 0, nil end
    local unit = AK_ArenaByGUID[info.guid]
    if not unit or not UnitExists(unit) then return 0, nil end
    local maximum = UnitHealthMax(unit)
    if not maximum or maximum <= 0 then return 0, nil end
    local ratio = UnitHealth(unit) / maximum
    if ratio < 0.25 then return 350, ratio end
    if ratio < 0.40 then return 220, ratio end
    if ratio < 0.60 then return 100, ratio end
    return 0, ratio
end

function AK_FindSpellLockSlot(force)
    local now = GetTime()
    if not force and (now - (AK_LastSlotScan or 0)) < 1 then return AK_SpellLockSlot end
    AK_LastSlotScan = now
    AK_SpellLockSlot = nil
    for slot = 1, 10 do
        local actionName, _, texture, isToken = GetPetActionInfo(slot)
        local resolvedName = actionName
        if isToken and actionName and _G[actionName] then resolvedName = _G[actionName] end
        local textureText = string.lower(tostring(texture or ""))
        if resolvedName == AK_SpellLockName or actionName == AK_SpellLockName or
           string.find(textureText, "spell_shadow_mindrot", 1, true) then
            AK_SpellLockSlot = slot
            break
        end
    end
    return AK_SpellLockSlot
end

function AK_CooldownActive()
    local slot = AK_FindSpellLockSlot(false)
    if slot then
        local startTime, duration = GetPetActionCooldown(slot)
        if startTime and startTime > 0 and duration and duration > 1.5 then return true end
    end
    local startTime, duration = GetSpellCooldown(AK_SpellLockName, BOOKTYPE_PET or "pet")
    return startTime and startTime > 0 and duration and duration > 1.5
end

function AK_Interruptable(unit)
    local name, _, _, _, startTime, endTime, _, _, notInterruptible = UnitCastingInfo(unit)
    if not name then
        local channelName, _, _, _, channelStart, channelEnd, _, channelNotInterruptible = UnitChannelInfo(unit)
        name = channelName
        startTime = channelStart
        endTime = channelEnd
        notInterruptible = channelNotInterruptible
    end
    if not name or notInterruptible == true or notInterruptible == 1 then return nil end
    return name, startTime, endTime
end

function AK_IsImmune(unit)
    if not AK_CheckImmune then return false end
    for index = 1, 40 do
        local buffName = UnitBuff(unit, index)
        if not buffName then break end
        if AK_ImmuneNames[buffName] then return true end
    end
    return false
end

function AK_ClassifySpell(spellName)
    if AK_KickNames[spellName] then
        return true, AK_SpellBaseScore[spellName] or 500, AK_SpellCategory[spellName] or "other"
    end
    if AK_KickAll then return true, 200, "other" end
    return false, 0, "none"
end

function AK_GetThreshold(category, healthRatio)
    if AK_Instant then return 0 end
    if category == "resurrection" then return math.random(45, 60) end
    if category == "heal" then
        if healthRatio and healthRatio < 0.25 then return math.random(45, 58) end
        if healthRatio and healthRatio < 0.40 then return math.random(50, 64) end
        return math.random(60, 78)
    end
    if category == "control" then return math.random(55, 75) end
    if category == "burst" then return math.random(68, 86) end
    return math.random(72, 90)
end

function AK_GetCast(unit)
    if not UnitExists(unit) or UnitIsDeadOrGhost(unit) or not UnitCanAttack("player", unit) then return nil end
    local spellName, startTime, endTime = AK_Interruptable(unit)
    if not spellName or not startTime or not endTime or endTime <= startTime then return nil end
    local accepted, baseScore, category = AK_ClassifySpell(spellName)
    if not accepted or AK_IsImmune(unit) then return nil end

    local guid = UnitGUID(unit) or unit
    local healthBonus, healthRatio = 0, nil
    if category == "heal" then healthBonus, healthRatio = AK_GetHealHealthBonus(guid) end
    local key = tostring(guid) .. ":" .. spellName .. ":" .. tostring(startTime) .. ":" .. tostring(endTime)
    local state = AK_Casts[key]
    if not state then
        state = { threshold = AK_GetThreshold(category, healthRatio), lastTry = 0, lastSeen = GetTime() }
        AK_Casts[key] = state
    end
    state.lastSeen = GetTime()

    local now = GetTime()
    local duration = (endTime - startTime) / 1000
    local elapsed = now - startTime / 1000
    local remaining = endTime / 1000 - now
    local progress = duration > 0 and elapsed / duration * 100 or 100
    if progress < 0 then progress = 0 elseif progress > 100 then progress = 100 end
    local _, _, homeLatency, worldLatency = GetNetStats()
    local emergencyWindow = math.max(homeLatency or 0, worldLatency or 0) / 1000 + 0.18
    local fire = AK_Instant or progress >= state.threshold or remaining <= emergencyWindow
    local roleBonus = AK_GetRoleBonus(guid)
    local focusBonus = unit == "focus" and 180 or 0
    local urgencyBonus = remaining <= emergencyWindow and 300 or 0
    local score = baseScore + roleBonus + focusBonus + healthBonus + urgencyBonus + progress * 2

    return {
        unit = unit,
        unitName = UnitName(unit) or unit,
        guid = guid,
        spellName = spellName,
        category = category,
        progress = progress,
        remaining = remaining,
        state = state,
        fire = fire,
        score = score,
    }
end

function AK_Send(candidate)
    if not candidate or not candidate.fire then return false end
    local now = GetTime()
    if (now - (candidate.state.lastTry or 0)) < 0.20 then return false end
    candidate.state.lastTry = now
    AK_LastTry = now
    AK_BlockTremorUntil = now + 0.45
    local slot = AK_FindSpellLockSlot(true)
    if slot then
        CastPetAction(slot, candidate.unit)
    else
        RunMacroText("/cast [pet:felhunter,target=" .. candidate.unit .. ",harm,nodead] " .. AK_SpellLockName)
    end
    if AK_Announce and DEFAULT_CHAT_FRAME then
        local role = AK_Roles[candidate.guid]
        local roleText = role and role.confirmed and " HEALER" or ""
        DEFAULT_CHAT_FRAME:AddMessage("|cFFFFFF00[AutoKick TRY]|r " .. candidate.spellName .. " -> " .. candidate.unitName .. roleText)
    end
    return true
end

function AK_Tick()
    if not AK_Enabled or not UnitExists("pet") or UnitIsDead("pet") or UnitIsDeadOrGhost("player") then return false end
    if AK_CooldownActive() then return false end
    local now = GetTime()
    if (now - (AK_LastTry or 0)) < (AK_Throttle or 0.20) then return false end
    AK_UpdateArenaMap()

    local best = nil
    for index = 1, table.getn(AK_Units) do
        local candidate = AK_GetCast(AK_Units[index])
        if candidate and candidate.fire and (not best or candidate.score > best.score) then best = candidate end
    end
    if best then AK_Send(best) end

    for key, state in pairs(AK_Casts) do
        if now - (state.lastSeen or now) > 2 then AK_Casts[key] = nil end
    end
    return false
end

if AK_EventFrame then
    AK_EventFrame:UnregisterAllEvents()
    AK_EventFrame:SetScript("OnEvent", nil)
else
    AK_EventFrame = CreateFrame("Frame")
end
AK_EventFrame:RegisterEvent("COMBAT_LOG_EVENT_UNFILTERED")
AK_EventFrame:RegisterEvent("PLAYER_ENTERING_WORLD")
AK_EventFrame:SetScript("OnEvent", function(self, event, ...)
    if event == "PLAYER_ENTERING_WORLD" then
        AK_Roles = {}
        AK_ArenaByGUID = {}
        AK_LastHealTarget = {}
        return
    end
    AK_UpdateArenaMap()
    local _, subEvent, sourceGUID, _, _, destGUID, _, _, spellID, spellName = ...
    spellName = spellName or (spellID and GetSpellInfo(spellID))
    if sourceGUID and AK_ArenaByGUID[sourceGUID] and spellName and AK_HealingEvidence[spellName] then
        local amount = 20
        if subEvent == "SPELL_CAST_START" then amount = 35
        elseif subEvent == "SPELL_CAST_SUCCESS" then amount = 60
        elseif subEvent == "SPELL_AURA_APPLIED" or subEvent == "SPELL_AURA_REFRESH" then amount = 25 end
        AK_AddHealerEvidence(sourceGUID, amount, destGUID)
    end
end)

AK_FindSpellLockSlot(true)
if DEFAULT_CHAT_FRAME then
    local mode = AK_SpellLockSlot and ("pet slot " .. AK_SpellLockSlot) or "macro fallback"
    DEFAULT_CHAT_FRAME:AddMessage("|cFF00FF00[AutoKick v9]|r arena healer engine: " .. mode)
end
end
return false)WARLOCK_INIT";

    static const char* kWarlockAutoKickMainLua = R"WARLOCK_MAIN(if AK_Tick then AK_Tick() end
return false)WARLOCK_MAIN";

    // ==========================================
    // --- LUA: TREMORBREAKER (EVENT-DRIVEN) ---
    // ==========================================
    static const char* kTremorBreakerInitLua = R"TREMOR_INIT(-- TremorBreaker v4: persistent per-GUID state
if TB_Init ~= 6 then
TB_Init = 6
TB_TremorNames = { "Тотем трепета", "Tremor Totem" }
TB_Visible = {}
TB_Handled = {}
TB_RequestAttack = false
TB_PendingGUID = nil
TB_NextAttempt = 0
TB_SwappingTarget = false
TB_NativeAllowed = false
if TB_Enabled == nil then TB_Enabled = true end

function TB_IsTremorName(name)
    if not name or name == "" then return false end
    for _, tremorName in ipairs(TB_TremorNames) do
        if name == tremorName then return true end
    end
    local lowerName = string.lower(name)
    if string.find(lowerName, "tremor", 1, true) then return true end
    if string.find(lowerName, "трепет", 1, true) then return true end
    return false
end

function TB_IsTremor(unit)
    if not UnitExists(unit) or UnitIsPlayer(unit) then return false end
    return TB_IsTremorName(UnitName(unit))
end

function TB_NormalizeGUID(guid)
    if not guid then return nil end
    return string.upper(guid)
end

function TB_SelectPending()
    if not TB_Visible then TB_Visible = {} end
    if not TB_Handled then TB_Handled = {} end
    for guid, visible in pairs(TB_Visible) do
        if visible and not TB_Handled[guid] then
            TB_PendingGUID = guid
            TB_RequestAttack = true
            return true
        end
    end
    TB_PendingGUID = nil
    TB_RequestAttack = false
    return false
end

function TB_AutoKickNeedsPriority()
    if not AK_Enabled or AK_Interruptable == nil or AK_ShouldKick == nil then return false end
    if AK_BlockTremorUntil and GetTime() < AK_BlockTremorUntil then return true end
    if AK_Pending then return true end
    if AK_PetCooldownActive and AK_PetCooldownActive() then return false end
    local units = AK_Units or { "focus", "target" }
    for i = 1, table.getn(units) do
        local unit = units[i]
        if UnitExists(unit) and not UnitIsDeadOrGhost(unit) and UnitCanAttack("player", unit) == 1 then
            local spellName = AK_Interruptable(unit)
            if spellName and AK_ShouldKick(spellName) then return true end
        end
    end
    return false
end

if DEFAULT_CHAT_FRAME then
    DEFAULT_CHAT_FRAME:AddMessage("|cFF00FF00[TremorBreaker v4]|r persistent GUID tracker ready.")
end
end
return false)TREMOR_INIT";

    static const char* kTremorBreakerMainLua = R"TREMOR_MAIN(-- TremorBreaker v5: permission check before native pet command
TB_NativeAllowed = false
if not TB_Enabled then return false end
if GetCurrentKeyBoardFocus and GetCurrentKeyBoardFocus() then return false end
if not UnitExists("pet") or UnitIsDead("pet") then return false end
if UnitIsDeadOrGhost("player") then return false end
if UnitCastingInfo("player") or UnitChannelInfo("player") then return false end
if UnitCastingInfo("pet") or UnitChannelInfo("pet") then return false end
if AK_BlockTremorUntil and GetTime() < AK_BlockTremorUntil then return false end
if TB_AutoKickNeedsPriority and TB_AutoKickNeedsPriority() then return false end
TB_NativeAllowed = true
return false)TREMOR_MAIN";

    static const char* kTremorBreakerNativeAttackLua = R"TREMOR_ATTACK(-- Native code temporarily bridges target GUID without target events.
if TB_NativeAllowed
and UnitExists("target")
and not UnitIsDead("target")
and UnitCanAttack("player", "target") == 1
and TB_IsTremor("target") then
PetAttack()
end
TB_NativeAllowed = false
return false)TREMOR_ATTACK";

    namespace TremorObjectManager {

        static const uintptr_t CLIENT_CONNECTION = 0x00C79CE0;

        static const uintptr_t CURMGR_OFFSET = 0x2ED0;

        static const uintptr_t FIRST_OBJECT = 0xAC;

        static const uintptr_t NEXT_OBJECT = 0x3C;

        static const uintptr_t OBJ_TYPE = 0x14;

        static const uintptr_t OBJ_GUID = 0x30;

        static const uintptr_t OBJ_DESCRIPTORS = 0x08;

        static const uintptr_t UNIT_NAME_PTR1 = 0x964;

        static const uintptr_t UNIT_NAME_PTR2 = 0x5C;

        static const uint32_t UNIT_FIELD_HEALTH = 0x18;

        static const uint8_t OT_UNIT = 3;

    }

    template <typename T>
    static bool TremorSafeRead(uintptr_t address, T& output) {

        if (address < 0x10000) return false;

        __try {

            output = *reinterpret_cast<T*>(address);

            return true;

        }

        __except (EXCEPTION_EXECUTE_HANDLER) {

            return false;

        }

    }

    static uintptr_t GetTremorObjectManager() {

        uintptr_t connection = 0;

        if (!TremorSafeRead(TremorObjectManager::CLIENT_CONNECTION, connection) || !connection) return 0;

        uintptr_t manager = 0;

        if (!TremorSafeRead(connection + TremorObjectManager::CURMGR_OFFSET, manager)) return 0;

        return manager;

    }

    static bool ReadTremorUnitField(uintptr_t object, uint32_t fieldIndex, uint32_t& output) {

        uintptr_t descriptors = 0;

        if (!TremorSafeRead(object + TremorObjectManager::OBJ_DESCRIPTORS, descriptors) || !descriptors) return false;

        return TremorSafeRead(descriptors + static_cast<uintptr_t>(fieldIndex) * 4, output);

    }

    static const char* GetTremorUnitName(uintptr_t object) {

        __try {

            uintptr_t nameObject = *reinterpret_cast<uintptr_t*>(object + TremorObjectManager::UNIT_NAME_PTR1);

            if (!nameObject) return "";

            const char* name = *reinterpret_cast<const char**>(nameObject + TremorObjectManager::UNIT_NAME_PTR2);

            return name ? name : "";

        }

        __except (EXCEPTION_EXECUTE_HANDLER) {

            return "";

        }

    }

    static bool IsNativeTremorName(const char* name) {

        if (!name || !*name) return false;

        if (strcmp(name, "Tremor Totem") == 0 || strcmp(name, "Тотем трепета") == 0) return true;

        char lower[96] = {
            0
        };

        strncpy_s(lower, sizeof(lower), name, _TRUNCATE);

        for (size_t i = 0; lower[i]; ++i) {

            if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] = static_cast<char>(lower[i] - 'A' + 'a');

        }

        return strstr(lower, "tremor") != NULL;

    }

    struct NativeTremorObject {
        uint64_t guid;
        uintptr_t objectBase;
    };

    static int CollectNativeTremors(NativeTremorObject* output, int capacity) {
        if (!output || capacity <= 0) return 0;
        uintptr_t manager = GetTremorObjectManager();
        if (!manager) return 0;
        uintptr_t object = 0;
        if (!TremorSafeRead(manager + TremorObjectManager::FIRST_OBJECT, object)) return 0;
        int count = 0;
        int guard = 0;
        while (object && !(object & 1) && guard++ < 8192) {
            uint8_t type = 0;
            TremorSafeRead(object + TremorObjectManager::OBJ_TYPE, type);
            if (type == TremorObjectManager::OT_UNIT) {
                uint32_t health = 0;
                ReadTremorUnitField(object, TremorObjectManager::UNIT_FIELD_HEALTH, health);
                const char* name = GetTremorUnitName(object);
                if (health > 0 && IsNativeTremorName(name)) {
                    uint64_t guid = 0;
                    TremorSafeRead(object + TremorObjectManager::OBJ_GUID, guid);
                    if (guid && count < capacity) {
                        output[count].guid = guid;
                        output[count].objectBase = object;
                        ++count;
                    }
                }
            }
            uintptr_t nextObject = 0;
            if (!TremorSafeRead(object + TremorObjectManager::NEXT_OBJECT, nextObject) ||
                !nextObject || nextObject == object) break;
            object = nextObject;
        }
        return count;
    }

    typedef void(__cdecl* PetCommandDispatch_t)(DWORD*, uint64_t*, int);
    static PetCommandDispatch_t DispatchPetCommand = (PetCommandDispatch_t)0x005D4210;

    static bool CommandPetAttackByGuid(uint64_t targetGuid) {
        if (!targetGuid || !DispatchPetCommand) return false;
        DWORD petAttackCommand = 0x07000002;
        __try {
            DispatchPetCommand(&petAttackCommand, &targetGuid, 1);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static int FindNativeTremorSlot(uint64_t guid, uintptr_t objectBase) {
        for (int i = 0; i < MAX_NATIVE_TREMORS; ++i)
            if (g_tbSlots[i].guid == guid && g_tbSlots[i].objectBase == objectBase) return i;
        return -1;
    }

    static int AllocateNativeTremorSlot() {
        for (int i = 0; i < MAX_NATIVE_TREMORS; ++i)
            if (!g_tbSlots[i].guid) return i;
        return -1;
    }

    static void ResetNativeTremorSlots(bool /*notifyLua*/) {
        for (int i = 0; i < MAX_NATIVE_TREMORS; ++i) {
            g_tbSlots[i].guid = 0;
            g_tbSlots[i].objectBase = 0;
            g_tbSlots[i].nextCommandAt = 0;
            g_tbSlots[i].seenNow = false;
        }
    }

    static void UpdateNativeTremorDetector() {
        static bool manualWindowWasActive = false;
        if (!g_warlockTremorEnabled) return;

        DWORD now = (DWORD)GetTickCount64();
        const DWORD manualUntil = (DWORD)InterlockedCompareExchange(&g_tremorManualActiveUntil, 0, 0);
        const bool manualWindowActive = (LONG)(manualUntil - now) >= 0;
        if (!manualWindowActive) {
            if (manualWindowWasActive) ResetNativeTremorSlots(false);
            manualWindowWasActive = false;
            return;
        }
        if (!manualWindowWasActive) {
            ResetNativeTremorSlots(false);
            g_tbNextNativeScan = 0;
            manualWindowWasActive = true;
        }
        if ((LONG)(now - g_tbNextNativeScan) < 0) return;
        g_tbNextNativeScan = now + 100;

        for (int i = 0; i < MAX_NATIVE_TREMORS; ++i) g_tbSlots[i].seenNow = false;
        NativeTremorObject objects[MAX_NATIVE_TREMORS] = {};
        const int count = CollectNativeTremors(objects, MAX_NATIVE_TREMORS);

        for (int i = 0; i < count; ++i) {
            int slot = FindNativeTremorSlot(objects[i].guid, objects[i].objectBase);
            if (slot < 0) {
                slot = AllocateNativeTremorSlot();
                if (slot < 0) continue;
                g_tbSlots[slot].guid = objects[i].guid;
                g_tbSlots[slot].objectBase = objects[i].objectBase;
                g_tbSlots[slot].nextCommandAt = 0;
            }
            g_tbSlots[slot].seenNow = true;
        }

        for (int i = 0; i < MAX_NATIVE_TREMORS; ++i) {
            if (g_tbSlots[i].guid && !g_tbSlots[i].seenNow) {
                g_tbSlots[i].guid = 0;
                g_tbSlots[i].objectBase = 0;
                g_tbSlots[i].nextCommandAt = 0;
            }
        }

        // One command per scan; only the pet receives a target.
        for (int i = 0; i < MAX_NATIVE_TREMORS; ++i) {
            NativeTremorSlot& slot = g_tbSlots[i];
            if (!slot.guid || !slot.seenNow) continue;
            if ((LONG)(now - slot.nextCommandAt) < 0) continue;
            slot.nextCommandAt = now + 350;
            CommandPetAttackByGuid(slot.guid);
            break;
        }
    }

    static void DrawHeroRow(LPDRAWITEMSTRUCT d, const char* label, bool expanded) {
        RECT rc = d->rcItem;
        HBRUSH bg = CreateSolidBrush(expanded ? g_theme.goldDark : g_theme.box);
        FillRect(d->hDC, &rc, bg);
        DeleteObject(bg);
        HBRUSH fr = CreateSolidBrush(g_theme.gold);
        FrameRect(d->hDC, &rc, fr);
        DeleteObject(fr);
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, expanded ? g_theme.gold : g_theme.text);
        HFONT of = (HFONT)SelectObject(d->hDC, hFontHead);
        char b[128];
        sprintf_s(b, "%s %s", expanded ? "[-]" : "[+]", label);
        RECT tr = {
            rc.left + 12,rc.top,rc.right - 12,rc.bottom
        };
        DrawTextA(d->hDC, b, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(d->hDC, of);
    }

    static void GetTremorHotkeyName(char* output, size_t outputSize) {
        if (!output || outputSize == 0) return;
        if (g_tremorBindingKey) {
            strcpy_s(output, outputSize, "Press key... ESC cancel, DEL clear");
            return;
        }
        const int key = g_tremorHotkey;
        if (!key) {
            strcpy_s(output, outputSize, "Tremor hotkey: [Not set]");
            return;
        }
        char keyName[64] = { 0 };
        if (key == VK_MBUTTON) strcpy_s(keyName, "Mouse Middle");
        else if (key == VK_XBUTTON1) strcpy_s(keyName, "Mouse 4");
        else if (key == VK_XBUTTON2) strcpy_s(keyName, "Mouse 5");
        UINT scanCode = MapVirtualKeyA((UINT)key, MAPVK_VK_TO_VSC) << 16;
        if (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN ||
            key == VK_INSERT || key == VK_DELETE || key == VK_HOME || key == VK_END ||
            key == VK_PRIOR || key == VK_NEXT || key == VK_DIVIDE || key == VK_NUMLOCK) {
            scanCode |= 1u << 24;
        }
        if (!keyName[0] && !GetKeyNameTextA((LONG)scanCode, keyName, (int)sizeof(keyName))) {
            sprintf_s(keyName, "VK_%02X", key);
        }
        sprintf_s(output, outputSize, "Tremor hotkey: [%s]", keyName);
    }

    static void DrawKeyBind(LPDRAWITEMSTRUCT d) {
        RECT rc = d->rcItem;
        HBRUSH bg = CreateSolidBrush(g_tremorBindingKey ? g_theme.goldDark : g_theme.box);
        FillRect(d->hDC, &rc, bg);
        DeleteObject(bg);
        HBRUSH frame = CreateSolidBrush(g_theme.gold);
        FrameRect(d->hDC, &rc, frame);
        DeleteObject(frame);
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, g_tremorBindingKey ? g_theme.gold : g_theme.text);
        HFONT oldFont = (HFONT)SelectObject(d->hDC, hFontNorm);
        char text[128] = { 0 };
        GetTremorHotkeyName(text, sizeof(text));
        DrawTextA(d->hDC, text, -1, &rc, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SelectObject(d->hDC, oldFont);
    }

    static void InitConfigPath() {
        GetModuleFileNameA(hSelf, g_configPath, MAX_PATH);
        char* slash = strrchr(g_configPath, '\\');
        if (slash) strcpy_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - g_configPath), "YamiYami.ini");
        else strcpy_s(g_configPath, "YamiYami.ini");
    }

    static void LoadTremorHotkey() {
        if (!g_configPath[0]) InitConfigPath();
        int key = GetPrivateProfileIntA("Warlock", "TremorHotkey", 0, g_configPath);
        if (key >= 0 && key < 256 && key != VK_END && key != VK_OEM_7) g_tremorHotkey = key;
    }

    static void SaveTremorHotkey() {
        if (!g_configPath[0]) InitConfigPath();
        char value[16] = { 0 };
        sprintf_s(value, "%d", (int)g_tremorHotkey);
        WritePrivateProfileStringA("Warlock", "TremorHotkey", value, g_configPath);
    }

    static bool IsSupportedTremorBindKey(int key) {
        if (key == VK_MBUTTON || key == VK_XBUTTON1 || key == VK_XBUTTON2) return true;
        if (key < 8 || key >= 256) return false;
        if (key == VK_LBUTTON || key == VK_RBUTTON || key == VK_END || key == VK_OEM_7) return false;
        return true;
    }

    static bool IsAnyTremorBindKeyDown() {
        for (int key = 1; key < 256; ++key) {
            if (!IsSupportedTremorBindKey(key) && key != VK_ESCAPE && key != VK_BACK && key != VK_DELETE) continue;
            if (GetAsyncKeyState(key) & 0x8000) return true;
        }
        return false;
    }

    static void FinishTremorBinding(int selectedKey, bool cancel) {
        if (!cancel) {
            g_tremorHotkey = selectedKey;
            SaveTremorHotkey();
        }
        g_tremorPendingHotkey = 0;
        g_tremorBindingKey = false;
        InterlockedExchange(&g_tremorBindState, TREMOR_BIND_IDLE);
        g_tremorHotkeyWasDown = false;
        if (hHeroWarlockTremorKey) InvalidateRect(hHeroWarlockTremorKey, NULL, TRUE);
    }

    static void PollTremorHotkeyGameThread() {
        static bool previousDown[256] = { false };
        static DWORD allReleasedSince = 0;
        const DWORD now = (DWORD)GetTickCount64();
        const LONG state = InterlockedCompareExchange(&g_tremorBindState, 0, 0);

        if (state == TREMOR_BIND_WAIT_RELEASE) {
            if (IsAnyTremorBindKeyDown()) allReleasedSince = 0;
            else {
                if (!allReleasedSince) allReleasedSince = now;
                if ((now - allReleasedSince) >= 120) {
                    for (int key = 0; key < 256; ++key) previousDown[key] = false;
                    InterlockedExchange(&g_tremorBindState, TREMOR_BIND_CAPTURE);
                }
            }
            return;
        }

        if (state == TREMOR_BIND_CAPTURE) {
            for (int key = 1; key < 256; ++key) {
                const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
                if (down && !previousDown[key]) {
                    if (key == VK_ESCAPE) g_tremorPendingHotkey = -1;
                    else if (key == VK_BACK || key == VK_DELETE) g_tremorPendingHotkey = 0;
                    else if (IsSupportedTremorBindKey(key)) g_tremorPendingHotkey = key;
                    else continue;
                    InterlockedExchange(&g_tremorBindState, TREMOR_BIND_WAIT_SELECTED_RELEASE);
                    previousDown[key] = true;
                    return;
                }
                previousDown[key] = down;
            }
            return;
        }

        if (state == TREMOR_BIND_WAIT_SELECTED_RELEASE) {
            if (!IsAnyTremorBindKeyDown()) {
                const int pending = g_tremorPendingHotkey;
                FinishTremorBinding(pending < 0 ? g_tremorHotkey : pending, pending < 0);
                allReleasedSince = 0;
            }
            return;
        }

        const int key = g_tremorHotkey;
        if (!key) {
            g_tremorHotkeyWasDown = false;
            return;
        }
        const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        if (g_warlockTremorEnabled && down && !g_tremorHotkeyWasDown) {
            InterlockedExchange(&g_tremorManualActiveUntil, (LONG)(now + 2500));
            g_tbNextNativeScan = 0;
        }
        g_tremorHotkeyWasDown = down;
    }

    static void RunHeroScriptsTick() {
        PollTremorHotkeyGameThread();

        if (g_warlockEnabled) {
            if (g_warlockInitPending) {
                ExecuteLuaNow(kWarlockAutoKickInitLua);
                ExecuteLuaNow("AK_Enabled = true");
                ExecuteLuaNow(g_warlockKickAll ? "AK_KickAll = true" : "AK_KickAll = false");
                ExecuteLuaNow(g_warlockInstant ? "AK_Instant = true" : "AK_Instant = false");
                g_warlockInitPending = false;
            }
            ExecuteLuaNow(kWarlockAutoKickMainLua);
        }

        if (g_warlockTremorEnabled) {
            if (g_warlockTremorInitPending) {
                ExecuteLuaNow(kTremorBreakerInitLua);
                ExecuteLuaNow("TB_Enabled=true; TB_NativeAllowed=false");
                g_warlockTremorInitPending = false;
                ResetNativeTremorSlots(false);
                g_tbNextNativeScan = 0;
            }
            UpdateNativeTremorDetector();
        }
    }

} // namespace

bool HeroesInitialize(
    HWND menu,
    HMODULE module,
    const HeroesUiTheme& theme,
    HeroesExecuteLuaFn executeLua,
    HeroesQueueLuaFn queueLua) {
    if (!menu || !module || !executeLua || !queueLua) return false;

    hMenu = menu;
    hSelf = module;
    g_theme = theme;
    hbrBack = theme.background;
    hFontHead = theme.headingFont;
    hFontNorm = theme.normalFont;
    g_executeLua = executeLua;
    g_queueLua = queueLua;

    hLblHeroes = CreateLabel("Heroes", 24, 100, 404, hFontHead);
    hHeroWarlockRow = CreateWindowA(
        "BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        24, 134, 404, 28, hMenu, (HMENU)200, hSelf, NULL);
    hHeroWarlockCheck = CreateCheck(201, 40, 172, 388);
    hHeroWarlockTitle = CreateLabel("Warlock Spell Lock & Tremor settings", 40, 202, 388, hFontHead);
    hHeroWarlockKickAllCheck = CreateCheck(202, 40, 230, 388);
    hHeroWarlockInstantCheck = CreateCheck(203, 40, 260, 388);
    hHeroWarlockTremorCheck = CreateCheck(204, 40, 290, 388);
    hHeroWarlockTremorKey = CreateWindowA(
        "BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        40, 320, 388, 26, hMenu, (HMENU)205, hSelf, NULL);
    hHeroWarlockInfo = CreateLabel(
        "Smart Arena healer priority. Tremor works only by hotkey.",
        40, 354, 388, hFontNorm);

    InitConfigPath();
    LoadTremorHotkey();
    HeroesSetVisible(false);
    return true;
}

void HeroesTick() {
    RunHeroScriptsTick();
}

void HeroesSetVisible(bool visible) {
    g_moduleVisible = visible;
    const int rootState = visible ? SW_SHOW : SW_HIDE;
    ShowWindow(hLblHeroes, rootState);
    ShowWindow(hHeroWarlockRow, rootState);

    const int sectionState = (visible && g_warlockExpanded) ? SW_SHOW : SW_HIDE;
    ShowWindow(hHeroWarlockCheck, sectionState);
    ShowWindow(hHeroWarlockKickAllCheck, sectionState);
    ShowWindow(hHeroWarlockInstantCheck, sectionState);
    ShowWindow(hHeroWarlockTremorCheck, sectionState);
    ShowWindow(hHeroWarlockTremorKey, sectionState);
    ShowWindow(hHeroWarlockTitle, sectionState);
    ShowWindow(hHeroWarlockInfo, sectionState);

    if (hHeroWarlockRow) InvalidateRect(hHeroWarlockRow, NULL, TRUE);
}

bool HeroesDrawItem(LPDRAWITEMSTRUCT d) {
    if (!d) return false;
    switch (d->CtlID) {
    case 200: DrawHeroRow(d, "Warlock", g_warlockExpanded); return true;
    case 201: DrawCheck(d, "Enable AutoKick Spell Lock", g_warlockEnabled, false); return true;
    case 202: DrawCheck(d, "Kick All Spells (vs list only)", g_warlockKickAll, false); return true;
    case 203: DrawCheck(d, "Instant Kick (vs anti-fake)", g_warlockInstant, false); return true;
    case 204: DrawCheck(d, "Enable TremorBreaker (manual hotkey)", g_warlockTremorEnabled, false); return true;
    case 205: DrawKeyBind(d); return true;
    default: return false;
    }
}

bool HeroesHandleCommand(UINT id) {
    switch (id) {
    case 200:
        g_warlockExpanded = !g_warlockExpanded;
        HeroesSetVisible(g_moduleVisible);
        return true;
    case 201:
        g_warlockEnabled = !g_warlockEnabled;
        if (g_warlockEnabled) {
            g_warlockInitPending = true;
            QueueLua("AK_Enabled = true");
        }
        else {
            QueueLua("AK_Enabled = false");
        }
        InvalidateRect(hHeroWarlockCheck, NULL, TRUE);
        return true;
    case 202:
        g_warlockKickAll = !g_warlockKickAll;
        QueueLua(g_warlockKickAll ? "AK_KickAll = true" : "AK_KickAll = false");
        InvalidateRect(hHeroWarlockKickAllCheck, NULL, TRUE);
        return true;
    case 203:
        g_warlockInstant = !g_warlockInstant;
        QueueLua(g_warlockInstant ? "AK_Instant = true" : "AK_Instant = false");
        InvalidateRect(hHeroWarlockInstantCheck, NULL, TRUE);
        return true;
    case 204:
        g_warlockTremorEnabled = !g_warlockTremorEnabled;
        if (g_warlockTremorEnabled) {
            g_warlockTremorInitPending = true;
            QueueLua("TB_Enabled = true");
        }
        else {
            InterlockedExchange(&g_tremorManualActiveUntil, 0);
            QueueLua("TB_Enabled=false; TB_NativeAllowed=false");
        }
        InvalidateRect(hHeroWarlockTremorCheck, NULL, TRUE);
        return true;
    case 205:
        g_tremorPendingHotkey = 0;
        g_tremorBindingKey = true;
        InterlockedExchange(&g_tremorBindState, TREMOR_BIND_WAIT_RELEASE);
        InvalidateRect(hHeroWarlockTremorKey, NULL, TRUE);
        return true;
    default:
        return false;
    }
}

bool HeroesHandleStaticColor(HWND control, HDC dc, LRESULT& result) {
    if (control != hLblHeroes && control != hHeroWarlockTitle && control != hHeroWarlockInfo) return false;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, (control == hHeroWarlockInfo) ? g_theme.text : g_theme.gold);
    result = (LRESULT)hbrBack;
    return true;
}

void HeroesShutdown() {
    g_warlockEnabled = false;
    g_warlockTremorEnabled = false;
    g_tremorBindingKey = false;
    InterlockedExchange(&g_tremorBindState, TREMOR_BIND_IDLE);
    InterlockedExchange(&g_tremorManualActiveUntil, 0);
    ResetNativeTremorSlots(false);
    QueueLua("AK_Enabled=false; TB_Enabled=false; TB_NativeAllowed=false");
    HeroesSetVisible(false);
}
