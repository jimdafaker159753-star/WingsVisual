#include "WarlockScripts.h"

namespace WarlockScripts
{
    const char* const AutoKickInit = R"WARLOCK_INIT(-- AutoKick v9 - arena healer intelligence
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

    const char* const AutoKickMain = R"WARLOCK_MAIN(if AK_Tick then AK_Tick() end
return false)WARLOCK_MAIN";

    const char* const TremorInit = R"TREMOR_INIT(-- TremorBreaker v4: persistent per-GUID state
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

    const char* const TremorMain = R"TREMOR_MAIN(-- TremorBreaker v5: permission check before native pet command
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

    const char* const TremorNativeAttack = R"TREMOR_ATTACK(-- Native code temporarily bridges target GUID without target events.
if TB_NativeAllowed
and UnitExists("target")
and not UnitIsDead("target")
and UnitCanAttack("player", "target") == 1
and TB_IsTremor("target") then
PetAttack()
end
TB_NativeAllowed = false
return false)TREMOR_ATTACK";

}
