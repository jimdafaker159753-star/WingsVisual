#include "WarriorScripts.h"

namespace WarriorScripts
{
    const char* const RotationInit = R"WARRIOR_LUA(
-- DarhangeR Warrior profile adapter for WoW 3.3.5a / WingsVisual.
-- The original profile was written for PQR. This adapter supplies only the
-- small compatibility layer needed by the imported priority lists.
WR_Engine = WR_Engine or {}
WR_Engine.Enabled = false
WR_Engine.CurrentRotation = WR_Engine.CurrentRotation or "Fury_DPS_DarhangeR"
WR_Engine.LastErrorAt = 0
WR_Engine.LastErrorAction = nil
WR_Engine.LastErrorText = nil
WR_ArmsAutoRend = true
WR_ArmsAutoHamstring = true
WR_ArmsAutoPummel = true
WR_ArmsAutoReflect = true
WR_ArmsBerserkerRage = true
WR_AutoRotation = true
WR_Gear = WR_Gear or { arms2h = nil, reflect1h = nil, shield = nil }
WR_GearCaptureTarget = nil
WR_GearCaptureAfter = nil
WR_GearCaptureLastLink = nil
WR_GearCaptureText = ""

local WR_GEAR_ORDER  = { "arms2h", "reflect1h", "shield" }
local WR_GEAR_LABELS = { arms2h = "2H weapon", reflect1h = "1H (reflect)", shield = "Shield (reflect)" }

local function WR_GearDisplay(link)
    if link and link ~= "" then return link end
    return "|cff808080not set|r"
end

local function WR_GearPanelText()
    local armsActive = (WR_GearCaptureTarget == "arms2h")
    local reflectActive = (WR_GearCaptureTarget == "reflect1h" or WR_GearCaptureTarget == "shield")
    local oneH = WR_GearDisplay(WR_Gear.reflect1h)
    local shld = WR_GearDisplay(WR_Gear.shield)
    if WR_GearCaptureTarget == "reflect1h" then oneH = "|cffffff00hover your 1H now|r" end
    if WR_GearCaptureTarget == "shield" then shld = "|cffffff00hover your shield now|r" end
    local lines = {
        "|cffFFD10AYamiYami weapon setup|r  -  hold Shift and hover an item",
        (armsActive and "|cff33ff33>|r " or "   ") .. "2H weapon:    " .. WR_GearDisplay(WR_Gear.arms2h),
        (reflectActive and "|cff33ff33>|r " or "   ") .. "1H + Shield:  " .. oneH .. "  +  " .. shld,
        "|cff888888click the same button again to close|r",
    }
    return table.concat(lines, "\n")
end

local function WR_EnsureGearFrame()
    if WR_GearCaptureFrame then return end
    local f = CreateFrame("Frame", "YamiYamiGearCapture", UIParent)
    f:SetWidth(430)
    f:SetHeight(112)
    f:SetPoint("TOP", UIParent, "TOP", 0, -80)
    f:SetFrameStrata("DIALOG")
    f.bg = f:CreateTexture(nil, "BACKGROUND")
    f.bg:SetAllPoints(f)
    f.bg:SetTexture(0, 0, 0, 0.75)
    f.text = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    f.text:SetPoint("TOPLEFT", f, "TOPLEFT", 12, -10)
    f.text:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", -12, 10)
    f.text:SetJustifyH("LEFT")
    f.text:SetJustifyV("TOP")
    f:SetScript("OnUpdate", function(self)
        if self.expire and GetTime() >= self.expire then
            self.expire = nil
            self:Hide()
        end
    end)
    WR_GearCaptureFrame = f
end

local function WR_ShowGearPanel(autoHideSeconds)
    WR_EnsureGearFrame()
    WR_GearCaptureFrame.text:SetText(WR_GearPanelText())
    WR_GearCaptureFrame.expire = autoHideSeconds and (GetTime() + autoHideSeconds) or nil
    WR_GearCaptureFrame:Show()
end

function WR_PrintGear()
    if not DEFAULT_CHAT_FRAME then return end
    DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD10A[YamiYami]|r weapon setup:")
    for _, key in ipairs(WR_GEAR_ORDER) do
        DEFAULT_CHAT_FRAME:AddMessage("   " .. WR_GEAR_LABELS[key] .. ": " .. WR_GearDisplay(WR_Gear[key]))
    end
end

function WR_StartReflectCapture()
    WR_GearCaptureTarget = "reflect1h"
    WR_GearCaptureAfter = "shield"
    WR_GearCaptureLastLink = nil
    WR_ShowGearPanel(nil)
end

function WR_CancelCapture()
    WR_GearCaptureTarget = nil
    WR_GearCaptureAfter = nil
    WR_GearCaptureLastLink = nil
    if WR_GearCaptureFrame then
        WR_GearCaptureFrame.expire = nil
        WR_GearCaptureFrame:Hide()
    end
end

function WR_TryCaptureTooltip(tooltip)
    if not WR_GearCaptureTarget or not IsShiftKeyDown() then return false end
    local source = tooltip or GameTooltip
    if not source or not source.GetItem then return false end
    local name, link = source:GetItem()
    if not link or link == "" then return false end
    -- Require a different item for the next slot so one hover cannot fill both
    -- the 1H and the shield from the same item.
    if WR_GearCaptureLastLink and link == WR_GearCaptureLastLink then return false end
    local key = WR_GearCaptureTarget
    WR_Gear[key] = link
    if DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD10A[YamiYami]|r Saved " .. (WR_GEAR_LABELS[key] or "item") .. ": " .. (link or name or "?"))
    end
    if key == "reflect1h" and WR_GearCaptureAfter == "shield" then
        WR_GearCaptureTarget = "shield"
        WR_GearCaptureAfter = nil
        WR_GearCaptureLastLink = link
        WR_ShowGearPanel(nil)
        return true
    end
    WR_GearCaptureTarget = nil
    WR_GearCaptureAfter = nil
    WR_GearCaptureLastLink = nil
    WR_ShowGearPanel(4)
    return true
end

function WR_GearCaptureTick()
    if not WR_GearCaptureTarget then
        if WR_GearCaptureFrame and not WR_GearCaptureFrame.expire and WR_GearCaptureFrame:IsShown() then
            WR_GearCaptureFrame:Hide()
        end
        return
    end
    if WR_TryCaptureTooltip(GameTooltip) then return end
    WR_ShowGearPanel(nil)
end

if GameTooltip and GameTooltip.HookScript and not WR_GearTooltipHooked then
    WR_GearTooltipHooked = true
    GameTooltip:HookScript("OnTooltipSetItem", function(self)
        WR_TryCaptureTooltip(self)
    end)
end

local function WR_AuraHasSpell(auraGetter, unit, spellID, caster)
    local wantedName = GetSpellInfo(spellID)
    if type(auraGetter) ~= "function" or not wantedName or not UnitExists(unit) then return nil end
    for index = 1, 40 do
        local ok, name, rank, icon, count, debuffType, duration, expirationTime, auraCaster, stealable, consolidate, auraSpellID =
            pcall(auraGetter, unit, index)
        if not ok or not name then break end
        if (auraSpellID == spellID or name == wantedName) and (not caster or auraCaster == caster) then
            return name, rank, icon, count, debuffType, duration, expirationTime, auraCaster, stealable, consolidate, auraSpellID
        end
    end
    return nil
end

function UnitBuffID(unit, spellID, caster)
    return WR_AuraHasSpell(UnitBuff, unit, spellID, caster)
end

function UnitDebuffID(unit, spellID, caster)
    return WR_AuraHasSpell(UnitDebuff, unit, spellID, caster)
end

function PQR_SpellAvailable(spellID)
    local usable = IsUsableSpell(GetSpellInfo(spellID))
    if not usable then return false end
    local start, duration, enabled = GetSpellCooldown(spellID)
    return enabled ~= 0 and (not start or start == 0 or not duration or duration <= 1.5)
end

function PQR_WriteToChat(text)
    if DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD10A[YamiYami Warrior]|r " .. tostring(text))
    end
end

function PQR_Debug(_) end

function WR_Cast(spellID, unit)
    if not spellID or spellID == 0 then return false end
    if CastSpellByID then
        CastSpellByID(spellID, unit or "target")
        return true
    end
    local spellName = GetSpellInfo(spellID)
    if not spellName then return false end
    RunMacroText("/cast [@" .. (unit or "target") .. ",exists] " .. spellName)
    return true
end

if not tContains then
    function tContains(list, value)
        for _, entry in ipairs(list or {}) do
            if entry == value then return true end
        end
        return false
    end
end

PQR_InterruptDelay = PQR_InterruptDelay or 0.05
PQR_InterruptPercent = PQR_InterruptPercent or math.random(30, 60)


WR_Engine.Abilities = {
    ["-- Hotkeys --"] = {
        id = 0,
        target = "Click",
        test = function()
            if not leftkeydown then leftkeydown = 0 end
            
            if modtime == nil then
            	modtime = 0
            end
            
            if IsLeftControlKeyDown()  and GetTime() - leftkeydown > 1 then
            	leftkeydown = GetTime()
              	if StopRota then
            		StopRota = false 
            		PQR_WriteToChat("Rotation \124cFF15E615reenabled!")
              	else 
            		StopRota = true 
            		PQR_WriteToChat("Rotation \124cFFE61515stopped!")
              	end
            end
            
            if not leftkeydown then leftkeydown = 0 end
            
            if modtime == nil then
            	modtime = 0
            end
            
            if IsLeftShiftKeyDown() and not GetCurrentKeyBoardFocus() and GetTime() - modtime > 0.3 then
            	modtime = GetTime()
            	if AoE then
            		AoE = false
            		PQR_WriteToChat("AoE: \124cFFE61515Disabled")
            	else
            		AoE = true
            		PQR_WriteToChat("AoE: \124cFF15E615Enabled")
            	end
            end
        end
    },
    ["-- Offensive Spells --"] = {
        id = 0,
        target = "Target",
        test = function()
            local buff = { 642, 45438, 31224, 23920, 33786, 19263, 21892, 40733 }
            local mob = { "Training Dummy", "Raider's Training Dummy" }
            local elite = {  }
            local noaggromobs = nil
            local immunity = nil
            local eliteboss = nil
            
            for i,v in ipairs(buff) do
              if UnitBuffID("target",v) then immunity = 1 end
            end
            
            for i,v in ipairs(mob) do
              if UnitName("target") == v then noaggromobs = 1 end
            end
            
            if immunity
            or UnitCanAttack("player","target") == nil
            or (UnitAffectingCombat("target") == nil and noaggromobs == nil and UnitIsPlayer("target") == nil)
            or (UnitGUID("target") and tonumber(UnitGUID("target"):sub(5,5),16) == 4)
            then return true end
            
            for i,v in ipairs(elite) do
            	  if UnitName("target") == v then eliteboss = 1 end
            	end
            
            IsBoss = nil
            function IsBoss()
            	if UnitExists("target")
            	and ( UnitLevel("target") == -1 or eliteboss )
            	or (UnitGUID("target") and tonumber(UnitGUID("target"):sub(5,5),16) == 4)
            		then return 
            			true 
            		end
            	end
        end
    },
    ["-- Start Fight --"] = {
        id = 0,
        target = "Target",
        test = function()
            if StopRota
            or IsMounted()
            or UnitInVehicle("player")
            or UnitIsDeadOrGhost("target") 
            or UnitIsDeadOrGhost("player")
            or UnitChannelInfo("player") ~= nil
            or UnitCastingInfo("player") ~= nil
            or UnitBuff("target", 59301)
            or UnitBuff("player", GetSpellInfo(430))
            or UnitBuff("player", GetSpellInfo(433))
            then return true end
        end
    },
    ["F:Berserker Rage"] = {
        id = 18499,
        target = "Target",
        test = function()
            if IsBoss()
            and CanAttack()
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["F:Execute"] = {
        id = 47471,
        target = "Target",
        test = function()
            if WR_MSPooling() then return false end
            if valid("target", 47471)
            and CanAttack()
            and getHp("target") <=20
            and UnitPower("player") >= 30
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["F:Slam"] = {
        id = 47475,
        target = "Target",
        test = function()
            if slm
            and ragecost(47475)
            and CanAttack()
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["F:Heroic Strike"] = {
        id = 47450,
        target = "Target",
        test = function()
            if ragecost(47450)
            and CanAttack()
            and rangeCheck2(47465, "target")
            then return true end
        end
    },
    ["F:Heroic Throw"] = {
        id = 57755,
        target = "Target",
        test = function()
            if rangeCheck(57755, "target")
            and CanAttack()
            and not CooldownRemains(12328) 
            then return true end
        end
    },
    ["F:Recklessness"] = {
        id = 1719,
        target = "Target",
        test = function()
            if rangeCheck(47465, "target")
            and IsBoss()
            and CanAttack()
            and not CooldownRemains(1719) 
            then return true end
        end
    },
    ["F:Bloodrage"] = {
        id = 2687,
        target = "Player",
        test = function()
            if IsBoss()
            and UnitPower("player") < 65
            and CanAttack()
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["F:Cleave"] = {
        id = 47520,
        target = "Target",
        test = function()
            if ragecost(47520)
            and CanAttack()
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["F:Bloodthirst"] = {
        id = 23881,
        target = "Target",
        test = function()
            if ragecost(23881)
            and CanAttack()
            and not CooldownRemains(23881) 
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["Use:Trinkets/Hands"] = {
        id = 0,
        target = "Target",
        test = function()
            local _,cd,havecd = GetInventoryItemCooldown("player",13)
            
            if cd == 0 and havecd == 1
            and IsBoss()
            and CanAttack()
            and rangeCheck(47465, "target")
            then 
              UseInventoryItem(13)
              return true
            end
            
            local _,cd,havecd = GetInventoryItemCooldown("player",14)
            
            if cd == 0 and havecd == 1
            and IsBoss()
            and CanAttack()
            and rangeCheck(47465, "target")
            then 
              UseInventoryItem(14)
              return true
            end
            
            local _,cd,havecd = GetInventoryItemCooldown("player",10)
            
            if cd == 0 and havecd == 1
            and IsBoss()
            and CanAttack()
            and rangeCheck(47465, "target")
            then 
              UseInventoryItem(10)
              return true
            end
        end
    },
    ["A:Battle Stance"] = {
        id = 2457,
        target = "Player",
        test = function()
            local DS = GetShapeshiftForm()
            
            if DS ~= 1
            then return true end
        end
    },
    ["A:Execute"] = {
        id = 47471,
        target = "Target",
        test = function()
            if WR_MSPooling() then return false end
            if valid("target", 47471)
            and CanAttack()
            and getHp("target") <= 20
            and UnitPower("player") >= 30
            and rangeCheck(47465, "target")
            or sdeath
            then return true end
        end
    },
    ["A:Charge"] = {
        id = 11578,
        target = "Mouseover",
        test = function()
            if IsShiftKeyDown()  and GetCurrentKeyBoardFocus() == nil then
                RunMacroText("/target [@mouseover]")
            return true end
        end
    },
    ["A:Slam"] = {
        id = 47475,
        target = "Target",
        test = function()
            if ragecost(47475)
            and CanAttack()
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["A:Rend"] = {
        id = 47465,
        target = "Target",
        test = function()
            if WR_ArmsAutoRend == false then return false end
            local rend, _, _, _, _, _, rtime = UnitDebuffID("target", 47465, "player")
            local refreshSoon = rend and rtime and rtime > 0 and (rtime - GetTime() < 3)

            if ragecost(47465)
            and CanAttack()
            and (rend == nil or refreshSoon)
            and ( calc_time == nil
            or ( calc_time ~= nil and calc_time > 13 ) )
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["A:Overpower"] = {
        id = 7384,
        target = "Target",
        test = function()
            if WR_MSPooling() then return false end
            if valid("target", 7384)
            and ragecost(7384)
            and CanAttack()
            and rangeCheck(7384, "target")
            then return true end
        end
    },
    ["F:Berserker Stance"] = {
        id = 2458,
        target = "Player",
        test = function()
            local DS = GetShapeshiftForm()
            
            if DS ~= 3
            then return true end
        end
    },
    ["A:Heroic Strike"] = {
        id = 47450,
        target = "Target",
        test = function()
            if ragecost(47450)
            and CanAttack()
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["A:Bloodrage"] = {
        id = 2687,
        target = "Target",
        test = function()
            if IsBoss()
            and UnitPower("player") < 65
            and CanAttack()
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["F:Whirlwind"] = {
        id = 1680,
        target = "Target",
        test = function()
            if ragecost(1680)
            and CanAttack()
            and not CooldownRemains(1680) 
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["A:Cleave"] = {
        id = 47520,
        target = "Target",
        test = function()
            if ragecost(47520)
            and CanAttack()
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["A:Mortal Strike"] = {
        id = 47486,
        target = "Target",
        test = function()
            if ragecost(47486)
            and CanAttack()
            and not CooldownRemains(47486) 
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["A:Bladestorm"] = {
        id = 46924,
        target = "Target",
        test = function()
            local sa, _, _, _, _, _, satime = UnitDebuffID("target", 7386) 
            local _,_,_,count = UnitDebuffID("target", 7386)
            
            if (count == 5 and (sa and satime  - GetTime() >= 8 ))
            and IsBoss()
            and ragecost(46924)
            and CanAttack()
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["F:Death Wish"] = {
        id = 12292,
        target = "Target",
        test = function()
            if IsBoss()
            and CanAttack()
            and ragecost(12292)
            and not CooldownRemains(12292)
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["P:Shockwave"] = {
        id = 46968,
        target = "Target",
        test = function()
            if ragecost(46968)
            and CanAttack()
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["P:Concussion Blow"] = {
        id = 12809,
        target = "Target",
        test = function()
            if ragecost(12809)
            and CanAttack()
            and not CooldownRemains(12809) 
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["A:Sweeping Strikes"] = {
        id = 12328,
        target = "Target",
        test = function()
            if ragecost(12328)
            and CanAttack()
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["F:Battle Shout"] = {
        id = 47436,
        target = "Player",
        test = function()
            if not GBoM
            and not bshout
            and not UnitIsDeadOrGhost("player")
            and not IsMounted()
            and ragecost(47436)
            then return true end
        end
    },
    ["P:Defensive Stance"] = {
        id = 71,
        target = "Player",
        test = function()
            local DS = GetShapeshiftForm()
            
            if DS ~= 3
            then return true end
        end
    },
    ["F:Commanding Shout"] = {
        id = 0,
        target = "Custom",
        test = function()
            -- Check Warlock in Party / Raid --   
            local group = "party"
            local members = GetNumPartyMembers()
            
            if GetNumRaidMembers() > 0 then
              group = "raid"
              members = GetNumRaidMembers()
            end
            
            CheckWlock = nil
            function CheckWlock()
            for i = 1, members, 1 do
            local member = group..tostring(i)
            local _, playerClass = UnitClass(member)
            	if playerClass == "Warlock"
            	and UnitIsFriend("player", member)
            		then
            			return true
            		end
            	end
            end
            
            if not CheckWlock()
            and not cshout
            and not UnitIsDeadOrGhost("player")
            and not IsMounted()
            and ragecost(47440)
            then
            	CastSpellByID(47440)
            return true end
        end
    },
    ["P:Last Stand"] = {
        id = 12975,
        target = "Player",
        test = function()
            if getHp("player") < 35
            and not CooldownRemains(12975) 
            then return true end
        end
    },
    ["P:Shield Wall"] = {
        id = 871,
        target = "Target",
        test = function()
            if getHp("player") < 50
            and not CooldownRemains(871)
            then return true end
        end
    },
    ["F:Demoralizing Shout"] = {
        id = 47437,
        target = "Target",
        test = function()
            if ragecost(47437)
            and CanAttack()
            and UnitDebuffID("target",47437,"player") == nil
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["A:Bladestorm (Sh)"] = {
        id = 46924,
        target = "Target",
        test = function()
            if IsLeftShiftKeyDown() and GetCurrentKeyBoardFocus() == nil
            then return true end
        end
    },
    ["A:Thunder Clap"] = {
        id = 47502,
        target = "Target",
        test = function()
            if AoE == true
            and ragecost(47502)
            and CanAttack()
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["P:Shield Block"] = {
        id = 2565,
        target = "Target",
        test = function()
            if getHp("player") < 90
            and not CooldownRemains(2565) 
            then return true end
        end
    },
    ["F:Enraged Regeneration"] = {
        id = 0,
        target = "Custom",
        test = function()
            local enrage = { 18499, 12292, 29131, 14204, 57522 }
            
            for i = 1, #enrage do
            	if UnitBuffID("player", enrage[i])
            	and getHp("player") < 25
            	and not CooldownRemains(55694) then 
            		CastSpellByID(55694)
            	else
            		if not UnitBuffID("player", enrage[i])
            		and not CooldownRemains(2687) 
            		and getHp("player") < 25 then
            		CastSpellByID(2687)
            	return true
            		end
            	end
            end
        end
    },
    ["P:Devastate"] = {
        id = 47498,
        target = "Target",
        test = function()
            if ragecost(47498)
            and CanAttack()
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["P:Shield Slam"] = {
        id = 47488,
        target = "Target",
        test = function()
            if ragecost(47488)
            and CanAttack()
            and not CooldownRemains(47488) 
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["P:Revenge"] = {
        id = 57823,
        target = "Target",
        test = function()
            if ragecost(57823)
            and CanAttack()
            and valid("target", 57823)
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["P:Bloodthirst"] = {
        id = 23881,
        target = "Target",
        test = function()
            if ragecost(23881)
            and getHp("player") < 85
            and CanAttack()
            and not CooldownRemains(23881)
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["P:Shockwave (Tank)"] = {
        id = 46968,
        target = "Target",
        test = function()
            if getHp("player") < 20
            and ragecost(46968)
            and CanAttack()
            and not CooldownRemains(46968) 
            and rangeCheck(47465, "target")
            then return true end
        end
    },
    ["-- DarhangeR --"] = {
        id = 0,
        target = "Target",
        test = function()
            -- I'm the original developer of dat profiles. Any changes, copying or sharing without my knowledge direct copyright infringement. For more info: 
            -- Discord: https://discord.com/users/250267265285488641
        end
    },
    ["A:Hamstring"] = {
        id = 1715,
        target = "Target",
        test = function()
            if WR_ArmsAutoHamstring == false then return false end
            if ragecost(1715)
            and CanAttack()
            and not ham
            and rangeCheck(47465, "target")
            and not IsBoss()
            then return true end
        end
    },
    ["P:Vigilance"] = {
        id = 0,
        target = "Custom",
        test = function()
            if UnitAffectingCombat("player") == nil then
            
            local mytarget = nil
            local doneVG = nil
            local tophp = 0
            local allinrange = 1
            local group = "party"
            local members = GetNumPartyMembers()
            
            if GetNumRaidMembers() > 0 then
              group = "raid"
              members = GetNumRaidMembers()
            end
            
            for i = 1, members, 1 do
              local member = group..tostring(i)
              local memberhp = UnitHealthMax(member)
              local _,_,_,_,_,_,_,source = UnitBuffID(member, 50720)
              if source == "player" then doneVG = 1 end
              if UnitInRange(member) == false then allinrange = nil end
              if memberhp > tophp and UnitInRange(member) and UnitIsUnit("player",member) == nil and source == nil and UnitGroupRolesAssigned("target") ~= "HEALER" then
                mytarget = member
                tophp = memberhp
              end
            end
            
            
            if mytarget and doneVG == nil and allinrange and UnitIsDead(mytarget) == nil then CastSpellByID(50720, mytarget) end
            
            end
        end
    },
    ["F:Heroic Strike + AOE"] = {
        id = 0,
        target = "Target",
        test = function()
            if ragecost(47450)
            and CanAttack()
            and rangeCheck2(47465, "target")
            then
            	if AoE == true then
            		if not CooldownRemains(12328) then
            			CastSpellByID(12328)
            		end	
            		CastSpellByID(47520)
            	else
            		CastSpellByID(47450)
            	end
            end
        end
    },
    ["A:-- Hotkeys --"] = {
        id = 0,
        target = "Click",
        test = function()
            if not leftkeydown then leftkeydown = 0 end
            
            if modtime == nil then
            	modtime = 0
            end
            
            if IsLeftControlKeyDown()  and GetTime() - leftkeydown > 1 then
            	leftkeydown = GetTime()
              	if StopRota  then
            		StopRota  = false 
            		PQR_WriteToChat("Rotation \124cFF15E615reenabled!")
              	else 
            		StopRota  = true 
            		PQR_WriteToChat("Rotation \124cFFE61515stopped!")
              	end
            end
        end
    },
    ["A:Berserker Rage (PvP)"] = {
        id = 18499,
        target = "Player",
        test = function()
            if WR_ArmsBerserkerRage == false then return false end
            local debuff = { 6215, 8122, 5484, 2637, 5246, 6358, 20066 }
            
            for i,v in ipairs(debuff) do
              if UnitDebuffID("player",v) 
              then return true end 
            end
        end
    },
    ["A:Shattering Throw (PvP)"] = {
        id = 64382,
        target = "Target",
        test = function()
            if bDS ~= nil or bBOP ~= nil or bIB ~= nil then
            	return true
            end
        end
    },
    ["A:Rend (DPS)"] = {
        id = 0,
        target = "Target",
        test = function()
            if ragecost(47465)
            and CanAttack()
            and rangeCheck(47465, "target")
            then
            
            	if GetShapeshiftForm() ~= 3  and UnitDebuffID("target", 47465, "player") == 1 then
            		CastShapeshiftForm(3)
            	end
            
            	if GetShapeshiftForm() ~= 1 and UnitDebuffID("target", 47465, "player") == nil then
            		CastShapeshiftForm(1)
            	elseif GetShapeshiftForm() == 1 and UnitDebuffID("target", 47465, "player") == nil then
            		CastSpellByID(47465)
            	end
            end
        end
    },
    ["P:Cleave"] = {
        id = 47520,
        target = "Target",
        test = function()
            if AoE == true
            and CanAttack()
            and ragecost(47520)
            and rangeCheck2(47465, "target")
            then return true end
        end
    },
    ["F:Piercing Howl"] = {
        id = 12323,
        target = "Target",
        test = function()
            if ragecost(12323)
            and CanAttack()
            and not phowl
            and rangeCheck(47465, "target")
            and not IsBoss()
            then return true end
        end
    },
    ["-- Interrupt Engine:D --"] = {
        id = 0,
        target = "Target",
        test = function()
            -- New Interrupt Engine // Новый движок--
            if xelperInterruptInit == nil then
            		xelperInterruptInit = true
            		function PQR_InterruptSpell()
            			local _, playerClass = UnitClass("player")
            		
            			if playerClass == "DEATHKNIGHT" then
            				return 47528
            			elseif playerClass == "HUNTER" then
            				return 34490
            			elseif playerClass == "MAGE" then
            				return 2139
            			elseif playerClass == "PRIEST" then
            				return 15487
            			elseif playerClass == "ROGUE" then
            				return 1766
            			elseif playerClass == "SHAMAN" then
            				return 57994
            			elseif playerClass == "WARRIOR" then
            				return 72
            			else
            				return 0
            		end	
            	end
            end
            
            -- Сore / Ядро -- 
            
            local customTarget = "target"
            local castName, _, _, _, castStartTime, castEndTime, _, _, castInterruptable = UnitCastingInfo(customTarget)
            local channelName, _, _, _, channelStartTime, channelEndTime, _, channelInterruptable = UnitChannelInfo(customTarget)
            
            if channelName ~= nil then
            	--target is channeling a spell that is interruptable
            	--load the channel variables into the cast variables to make logic a little easier.
            	castName = channelName
            	castStartTime = channelStartTime
            	castEndTime = channelEndTime
            	castInterruptable = channelInterruptable
            end
            
            --This is actually "Not Interruptable"... so lets swap it around to use in the positive.
            if castInterruptable == false then
            	castInterruptable = true
            else
            	castInterruptable = false
            end
            
            --we can't attack the target.
            if UnitCanAttack("player", customTarget) == nil then
            	return false
            end
            
            if castInterruptable then
            	--target is casting something that is interruptable.
            	--the following 2 variables are named logically... value is in seconds.
            	local timeSinceStart = (GetTime() * 1000 - castStartTime) / 1000
            	local timeLeft = ((GetTime() * 1000 - castEndTime) * -1) / 1000
            	local castTime = castEndTime - castStartTime
            	local currentPercent = timeSinceStart / castTime * 100000
            	
            	--interrupt percentage check
            	if currentPercent < PQR_InterruptPercent then
            		return false
            	end
            
            	--minimum interrupt time.
            	if timeSinceStart - PQR_InterruptDelay < 0 then
            		return false
            	end
            
            	--make sure the interrupt spell is available
            	local interruptSpell = 72
            	if interruptSpell ~= 0 then
            		
            		local interruptName = GetSpellInfo(interruptSpell)
            		if not PQR_SpellAvailable(interruptSpell) or not IsSpellInRange(interruptName, customTarget) then
            			return false
            		end
            	else
            		return false
            	end
            
            	--Finally, make sure the spell they are casting is on the interrupt list or interrupt all is checked
            	--if PQR_IsInterruptAll() then
            		PQR_Debug("Casting interrupt on "..customTarget)
            		CastSpellByID(interruptSpell, customTarget)
            		return true
            	--end
            	
            end
            
            --PQR_InterruptStarted is a global flag to indicate that a new interrupt rotation has started.
            
            if not PQR_InterruptStart then
            	PQR_InterruptStart = true
            
            	--Only interrupt these abilities:
            	--PQR_AddInterrupt("Holy Light")
            	--PQR_AddInterrupt("Flash of Light")
            	--PQR_AddInterrupt(GetSpellInfo(16914)) --Hurricane
            
            	--This is the delay before interrupting in seconds.
            	PQR_InterruptDelay = 0.05
            	
            	--This is the percentage of the cast to wait before interrupting
            	PQR_InterruptPercent = math.random(30,60)
            end
            
            if not spells then
            	spells = {
            		"Greater Heal", --Priest Heal
            		"Penance", --Priest Direct channel heal
            		"Flash Heal", --Priest quick big heal"
            		"Heal", --Priest normal heal
            		"Binding Heal", --Priest heal for themself and another
            		"Lesser Heal", --Priest small heal
            		"Prayer of Healing", --Priest AoE heal
            		"Chain Heal", --Shaman AoE heal
            		"Healing Wave", --Shaman heal
            		"Lesser Healing Wave", --Shaman minor heal
            		"Flash of Light", --Paladin quick heal
            		"Holy Light", --Paladin small heal
            		"Nourish", --Druid heals
            		"Healing Touch", --Druid heal
            		"Regrowth", --Druid AoE
            		"Rebirth", --Druid brez
            		"Tranquility", --Druid AoE heal
            		"Hex", --Shaman CC
            		"Cyclone",
            		"Polymorph"
            		}
            end
        end
    },
    ["A:Last Stand + Regen"] = {
        id = 0,
        target = "Custom",
        test = function()
            local enrage = { 18499, 12292, 29131, 14204, 57522 }
            
            for i = 1, #enrage do
            	if getHp("player") < 25
            	and not CooldownRemains(12975) then 
            		CastSpellByID(12975)
            	else
            		if UnitBuffID("player", enrage[i])
            		and laststand
            		and not CooldownRemains(55694) then 
            			CastSpellByID(55694)
            	else
            		if not UnitBuffID("player", enrage[i])
            		and ( getHp("player") < 25 or laststand)
            		and not CooldownRemains(2687) then
            			CastSpellByID(2687)
            	return true
            			end
            		end
            	end
            end
        end
    },
    ["-- Interrupt Engine:F --"] = {
        id = 0,
        target = "Target",
        test = function()
            -- New Interrupt Engine // Новый движок--
            if xelperInterruptInit == nil then
            		xelperInterruptInit = true
            		function PQR_InterruptSpell()
            			local _, playerClass = UnitClass("player")
            		
            			if playerClass == "DEATHKNIGHT" then
            				return 47528
            			elseif playerClass == "HUNTER" then
            				return 34490
            			elseif playerClass == "MAGE" then
            				return 2139
            			elseif playerClass == "PRIEST" then
            				return 15487
            			elseif playerClass == "ROGUE" then
            				return 1766
            			elseif playerClass == "SHAMAN" then
            				return 57994
            			elseif playerClass == "WARRIOR" then
            				return 6552
            			else
            				return 0
            		end	
            	end
            end
            
            -- Сore / Ядро -- 
            
            local customTarget = "target"
            local castName, _, _, _, castStartTime, castEndTime, _, _, castInterruptable = UnitCastingInfo(customTarget)
            local channelName, _, _, _, channelStartTime, channelEndTime, _, channelInterruptable = UnitChannelInfo(customTarget)
            
            if channelName ~= nil then
            	--target is channeling a spell that is interruptable
            	--load the channel variables into the cast variables to make logic a little easier.
            	castName = channelName
            	castStartTime = channelStartTime
            	castEndTime = channelEndTime
            	castInterruptable = channelInterruptable
            end
            
            --This is actually "Not Interruptable"... so lets swap it around to use in the positive.
            if castInterruptable == false then
            	castInterruptable = true
            else
            	castInterruptable = false
            end
            
            --we can't attack the target.
            if UnitCanAttack("player", customTarget) == nil then
            	return false
            end
            
            if castInterruptable then
            	--target is casting something that is interruptable.
            	--the following 2 variables are named logically... value is in seconds.
            	local timeSinceStart = (GetTime() * 1000 - castStartTime) / 1000
            	local timeLeft = ((GetTime() * 1000 - castEndTime) * -1) / 1000
            	local castTime = castEndTime - castStartTime
            	local currentPercent = timeSinceStart / castTime * 100000
            	
            	--interrupt percentage check
            	if currentPercent < PQR_InterruptPercent then
            		return false
            	end
            
            	--minimum interrupt time.
            	if timeSinceStart - PQR_InterruptDelay < 0 then
            		return false
            	end
            
            	--make sure the interrupt spell is available
            	local interruptSpell = PQR_InterruptSpell()
            	if interruptSpell ~= 0 then
            		
            		local interruptName = GetSpellInfo(interruptSpell)
            		if not PQR_SpellAvailable(interruptSpell) or not IsSpellInRange(interruptName, customTarget) then
            			return false
            		end
            	else
            		return false
            	end
            
            	--Finally, make sure the spell they are casting is on the interrupt list or interrupt all is checked
            	--if PQR_IsInterruptAll() then
            		PQR_Debug("Casting interrupt on "..customTarget)
            		CastSpellByID(interruptSpell, customTarget)
            		return true
            	--end
            	
            end
            
            --PQR_InterruptStarted is a global flag to indicate that a new interrupt rotation has started.
            
            if not PQR_InterruptStart then
            	PQR_InterruptStart = true
            
            	--Only interrupt these abilities:
            	--PQR_AddInterrupt("Holy Light")
            	--PQR_AddInterrupt("Flash of Light")
            	--PQR_AddInterrupt(GetSpellInfo(16914)) --Hurricane
            
            	--This is the delay before interrupting in seconds.
            	PQR_InterruptDelay = 0.05
            	
            	--This is the percentage of the cast to wait before interrupting
            	PQR_InterruptPercent = math.random(30,60)
            end
            
            if not spells then
            	spells = {
            		"Greater Heal", --Priest Heal
            		"Penance", --Priest Direct channel heal
            		"Flash Heal", --Priest quick big heal"
            		"Heal", --Priest normal heal
            		"Binding Heal", --Priest heal for themself and another
            		"Lesser Heal", --Priest small heal
            		"Prayer of Healing", --Priest AoE heal
            		"Chain Heal", --Shaman AoE heal
            		"Healing Wave", --Shaman heal
            		"Lesser Healing Wave", --Shaman minor heal
            		"Flash of Light", --Paladin quick heal
            		"Holy Light", --Paladin small heal
            		"Nourish", --Druid heals
            		"Healing Touch", --Druid heal
            		"Regrowth", --Druid AoE
            		"Rebirth", --Druid brez
            		"Tranquility", --Druid AoE heal
            		"Hex", --Shaman CC
            		"Cyclone",
            		"Polymorph"
            		}
            end
        end
    },
    ["-- Interrupt Engine:A --"] = {
        id = 0,
        target = "Target",
        test = function()
            if WR_ArmsAutoPummel == false then return false end
            -- New Interrupt Engine // Новый движок--
            if xelperInterruptInit == nil then
            		xelperInterruptInit = true
            		function PQR_InterruptSpell()
            			local _, playerClass = UnitClass("player")
            		
            			if playerClass == "DEATHKNIGHT" then
            				return 47528
            			elseif playerClass == "HUNTER" then
            				return 34490
            			elseif playerClass == "MAGE" then
            				return 2139
            			elseif playerClass == "PRIEST" then
            				return 15487
            			elseif playerClass == "ROGUE" then
            				return 1766
            			elseif playerClass == "SHAMAN" then
            				return 57994
            			elseif playerClass == "WARRIOR" then
            				return 57755
            			else
            				return 0
            		end	
            	end
            end
            
            -- Сore / Ядро -- 
            
            local customTarget = "target"
            local castName, _, _, _, castStartTime, castEndTime, _, _, castInterruptable = UnitCastingInfo(customTarget)
            local channelName, _, _, _, channelStartTime, channelEndTime, _, channelInterruptable = UnitChannelInfo(customTarget)
            
            if channelName ~= nil then
            	--target is channeling a spell that is interruptable
            	--load the channel variables into the cast variables to make logic a little easier.
            	castName = channelName
            	castStartTime = channelStartTime
            	castEndTime = channelEndTime
            	castInterruptable = channelInterruptable
            end
            
            --This is actually "Not Interruptable"... so lets swap it around to use in the positive.
            if castInterruptable == false then
            	castInterruptable = true
            else
            	castInterruptable = false
            end
            
            --we can't attack the target.
            if UnitCanAttack("player", customTarget) == nil then
            	return false
            end
            
            if castInterruptable then
            	--target is casting something that is interruptable.
            	--the following 2 variables are named logically... value is in seconds.
            	local timeSinceStart = (GetTime() * 1000 - castStartTime) / 1000
            	local timeLeft = ((GetTime() * 1000 - castEndTime) * -1) / 1000
            	local castTime = castEndTime - castStartTime
            	local currentPercent = timeSinceStart / castTime * 100000
            	
            	--interrupt percentage check
            	if currentPercent < PQR_InterruptPercent then
            		return false
            	end
            
            	--minimum interrupt time.
            	if timeSinceStart - PQR_InterruptDelay < 0 then
            		return false
            	end
            
            	--make sure the interrupt spell is available
            	local interruptSpell = 57755 
            	if interruptSpell ~= 0 then
            		
            		local interruptName = GetSpellInfo(interruptSpell)
            		if not PQR_SpellAvailable(interruptSpell) or not IsSpellInRange(interruptName, customTarget) then
            			return false
            		end
            	else
            		return false
            	end
            
            	--Finally, make sure the spell they are casting is on the interrupt list or interrupt all is checked
            	--if PQR_IsInterruptAll() then
            		PQR_Debug("Casting interrupt on "..customTarget)
            		CastSpellByID(interruptSpell, customTarget)
            		return true
            	--end
            	
            end
            
            --PQR_InterruptStarted is a global flag to indicate that a new interrupt rotation has started.
            
            if not PQR_InterruptStart then
            	PQR_InterruptStart = true
            
            	--Only interrupt these abilities:
            	--PQR_AddInterrupt("Holy Light")
            	--PQR_AddInterrupt("Flash of Light")
            	--PQR_AddInterrupt(GetSpellInfo(16914)) --Hurricane
            
            	--This is the delay before interrupting in seconds.
            	PQR_InterruptDelay = 0.05
            	
            	--This is the percentage of the cast to wait before interrupting
            	PQR_InterruptPercent = math.random(30,60)
            end
            
            if not spells then
            	spells = {
            		"Greater Heal", --Priest Heal
            		"Penance", --Priest Direct channel heal
            		"Flash Heal", --Priest quick big heal"
            		"Heal", --Priest normal heal
            		"Binding Heal", --Priest heal for themself and another
            		"Lesser Heal", --Priest small heal
            		"Prayer of Healing", --Priest AoE heal
            		"Chain Heal", --Shaman AoE heal
            		"Healing Wave", --Shaman heal
            		"Lesser Healing Wave", --Shaman minor heal
            		"Flash of Light", --Paladin quick heal
            		"Holy Light", --Paladin small heal
            		"Nourish", --Druid heals
            		"Healing Touch", --Druid heal
            		"Regrowth", --Druid AoE
            		"Rebirth", --Druid brez
            		"Tranquility", --Druid AoE heal
            		"Hex", --Shaman CC
            		"Cyclone",
            		"Polymorph"
            		}
            end
        end
    },
    ["-- Function --"] = {
        id = 0,
        target = "Target",
        test = function()
            if not functionsloaded then
            	
            		-- Glyph Check -- 
            	glyph = function(glyphID)
                    local hasglyph = false;
                    local i = 1;
                    local glyph = GetGlyphSocketInfo(i);
                    while glyph do
                        local id = select(3, GetGlyphSocketInfo(i));
                        if id == glyphID then
                            hasglyph = true;
                            break;
                        end
                        i = i + 1;
                        glyph = GetGlyphSocketInfo(i);
                    end
                    return hasglyph;
                end
            	
            		-- Check target --
            	function CanAttack()
            	if UnitExists("target")
            	and UnitCanAttack("player", "target") == 1
            		then
            			return true
            		end
            	end
            	
            	function UnX()
            	if UnitExists("target")
            		then
            			return true
            		end
            	end
            	
            		-- Check if player move --
            	function pMove()
            	if GetUnitSpeed("player") >= 3
            		then
            			return true
            		end
            	end
            	
            		-- Check if player stay --
            	function pStay()
            	if GetUnitSpeed("player") == 0
            		then
            			return true
            		end
            	end
            	
            		-- Combat Check --
            	function inCombat()
            	if UnitAffectingCombat("player") ~= nil
            		then
            			return true
            		end
            	end
            	
            		-- Get HP simple --
            	function getHp(unit)
            	if UnitExists(unit) ~= nil
            		then
            			return 100 * UnitHealth(unit) / UnitHealthMax(unit)
            		end
            	end
            	
            		-- Calculate HP simple --
            	function CalculateHP(t)
            	local ActualWithIncoming = 100 * UnitHealth(t) / UnitHealthMax(t)
            		if ActualWithIncoming then
            			return ActualWithIncoming
            		else
            			return 100
            		end
            	end
            		
            		-- Spell Cooldown remaining check --
            	CooldownRemains = nil
            	function CooldownRemains(spell)
            		local start, duration, enable = GetSpellCooldown(spell)
            		if ( start > 0 and duration > 1.5 ) 
            		and not gcd()then
            			return start + duration - GetTime()
            		else
            			return false
            		end
            	end
            
            		-- Item Cooldown remaining check --
            	ItemCooldownRemains = nil
            	function ItemCooldownRemains(item)
            		local start, duration, enable = GetItemCooldown(item)
            		if ( start > 0 and duration > 1.5 ) then
            			return start + duration - GetTime()
            		else
            			return false
            		end
            	end
            	
            		-- GCD Check --
            	function gcd()
            	local _, d = GetSpellCooldown(61304)
            		if (d ~= 0) then
            			return true
            		else
            			return false
            		end
            	end
            		-- Range Check Simple --
            	function rangeCheck(spellid,unit)
            	if IsSpellInRange(GetSpellInfo(spellid),unit) == 1
            	and not gcd()
            	then
            		return true
            		end
            	end
            	
            		-- Range Check Simple --
            	function rangeCheck2(spellid,unit)
            	if IsSpellInRange(GetSpellInfo(spellid),unit) == 1 then
            		return true
            		end
            	end
            	
            		-- Threat --
            	function threat(t)
            	if UnitExists(t)
            		and UnitThreatSituation(t) ~= nil then
            			return UnitThreatSituation(t)
            		else
            			return 0
            		end
            	end
            	
            		-- Validator --
            	valid = nil
            	function valid(t, spellid)
            		if UnitExists(t)
            		 and not UnitIsDeadOrGhost(t)
            		 and UnitCanAttack("player", t) == 1
            		 and UnitDebuffID("target", 33786) == nil
            		 and IsUsableSpell(GetSpellInfo(spellid)) == 1
            		 and IsSpellInRange(GetSpellInfo(spellid), t) == 1
            		 then
            			return true
            		else
            			return false
            		end
            	end
            
            		-- Rage Check --
            	ragecost = nil
            	function ragecost(spellid)
            		local rage = UnitPower("player")
            		if rage >= select(4, GetSpellInfo(spellid)) then
            			return true
            		else
            			return false
            		end 
            	end
            
            	functionsloaded = true
            end
        end
    },
    ["-- All:Racial --"] = {
        id = 0,
        target = "Target",
        test = function()
            local debuff = { 6215, 8122, 5484, 2637, 5246, 6358 }
            local hracial = { 33697, 20572, 33702, 26297 }
            local alracial = { 20594, 28880 }
            local hrace = { "Troll", "Orc" }
            local alrace = { "Dwarf", "Draenei" }
            local myrace = UnitRace("player")
            
            	-- Undead --
            for i = 1, #debuff do
              if UnitDebuffID("player",debuff[i])
               and UnitRace("player") == "Undead" 
               and IsSpellKnown(7744) 
               and not CooldownRemains(7744)
               and CanAttack() then
                CastSpellByID(7744)
                return true
              end
            end
            
            	-- Horde race --
            if IsBoss()
             and CanAttack()
             and tContains(hrace, UnitRace("player")) then
              for i = 1, #hracial do
                if IsSpellKnown(hracial[i]) 
                 and not CooldownRemains(hracial[i]) then 
                  CastSpellByID(hracial[i])
                  return true
                end
              end
            end
            
            	-- Ally race -- 
            if CanAttack() 
            and getHp("player") <= 35 
            and tContains(alrace, UnitRace("player")) then
              for i = 1, #alracial do
                if IsSpellKnown(alracial[i]) 
                 and not CooldownRemains(alracial[i]) then
                  CastSpellByID(alracial[i])
                  return true
                end
              end
            end
        end
    },
    ["P:Sunder Armor"] = {
        id = 0,
        target = "Target",
        test = function()
            if IsBoss()
            and CanAttack()
            and not sa
            or (UnitDebuffID("target", 7386) and ( select(4, UnitDebuffID("target", 7386)) < 5 or select(7, UnitDebuffID("target", 7386)) - GetTime() < 5 )) 
            and rangeCheck(47465, "target")
            then
            	if UnitPower("player") > 30 then
            		CastSpellByID(47450)
            	end	
            		CastSpellByID(7386)
            	return true
            end
        end
    },
    ["-- All:Mouseover --"] = {
        id = 0,
        target = "Target",
        test = function()
            return false
        end
    },
    ["-- Autotarget script --"] = {
        id = 0,
        target = "Target",
        test = function()
            if UnitAffectingCombat("player")
            and (not UnitExists("target")
            or (UnitExists("target") and not UnitCanAttack("player", "target"))) then
                RunMacroText("/targetenemy")
                return true
            end
        end
    },
    ["-- Warrior Stuff --"] = {
        id = 0,
        target = "Target",
        test = function()
            -- Warrior Stuff --
            -- Dots --
            sa = UnitDebuffID("target", 7386) 
            ham = UnitDebuffID("target", 1715, "player")
            phowl = UnitDebuffID("target", 12323, "player")
            -- Procs --
            sdeath = UnitBuffID("player", 52437)
            slm = UnitBuffID("player", 46916)
            -- Buffs -- 
            bshout = UnitBuffID("player", 47436)
            cshout = UnitBuffID("player", 47440)
            GBoM = UnitBuffID("player", 48934)
            bpact = UnitBuffID("player", 47982)
            -- Other --
            rage = UnitPower("player")
            bDS = UnitBuffID("target", 642)
            bBOP = UnitBuffID("target", 10278)
            bIB = UnitBuffID("target", 45438)
        end
    },
    ["-- All:Healthstone --"] = {
        id = 0,
        target = "Custom",
        test = function()
            local stones = { 36892, 36893, 36894 }
            
            for i = 1, #stones do
               	 if getHp("player") <= 25
            	and GetItemCount(stones[i]) >= 1 
            	and not ItemCooldownRemains(stones[i]) then
                   		UseItemByName(stones[i])
            	end
            end
        end
    },
    ["-- All:Potions --"] = {
        id = 0,
        target = "Custom",
        test = function()
            hpot = { 33447, 43569, 40087, 41166, 40067 }
            
            for i = 1, #hpot do
               	if getHp("player") <= 12
            	and GetItemCount(hpot[i]) >= 1 
            	and not ItemCooldownRemains(hpot[i]) then
                   		UseItemByName(hpot[i])
            	end
            end
        end
    },
}

WR_Engine.Rotations = {
    ["Fury_DPS_DarhangeR"] = {
        "-- Function --",
        "-- Warrior Stuff --",
        "-- Hotkeys --",
        "-- Start Fight --",
        "-- Autotarget script --",
        "-- Interrupt Engine:F --",
        "-- All:Mouseover --",
        "F:Berserker Stance",
        "F:Battle Shout",
        "-- Offensive Spells --",
        "-- All:Healthstone --",
        "-- All:Potions --",
        "F:Enraged Regeneration",
        "Use:Trinkets/Hands",
        "F:Death Wish",
        "F:Recklessness",
        "-- All:Racial --",
        "F:Bloodrage",
        "F:Heroic Strike + AOE",
        "F:Whirlwind",
        "A:Execute",
        "F:Bloodthirst",
        "F:Slam",
    },
    ["Arms_DPS_DarhangeR"] = {
        "-- Function --",
        "-- Warrior Stuff --",
        "A:-- Hotkeys --",
        "-- Start Fight --",
        "-- Autotarget script --",
        "-- Interrupt Engine:A --",
        "-- All:Mouseover --",
        "A:Battle Stance",
        "F:Battle Shout",
        "-- Offensive Spells --",
        "-- All:Healthstone --",
        "-- All:Potions --",
        "A:Berserker Rage (PvP)",
        "F:Enraged Regeneration",
        "Use:Trinkets/Hands",
        "-- All:Racial --",
        "A:Shattering Throw (PvP)",
        "A:Bladestorm (Sh)",
        "F:Bloodrage",
        "A:Mortal Strike",
        "A:Overpower",
        "A:Execute",
        "A:Rend",
        "A:Hamstring",
        "F:Heroic Strike",
    },
    ["Proto_Tank_DarhangeR"] = {
        "-- Function --",
        "-- Warrior Stuff --",
        "-- Hotkeys --",
        "-- Start Fight --",
        "-- Autotarget script --",
        "-- Interrupt Engine:D --",
        "-- All:Mouseover --",
        "P:Defensive Stance",
        "P:Vigilance",
        "F:Commanding Shout",
        "-- Offensive Spells --",
        "-- All:Healthstone --",
        "-- All:Potions --",
        "-- All:Racial --",
        "P:Shield Wall",
        "A:Last Stand + Regen",
        "P:Shield Block",
        "P:Bloodthirst",
        "P:Shockwave (Tank)",
        "A:Rend",
        "P:Shield Slam",
        "A:Thunder Clap",
        "P:Cleave",
        "P:Devastate",
        "P:Revenge",
    },
}

local function WR_ItemID(link)
    return link and string.match(link, "item:(%d+)") or nil
end

local function WR_IsEquipped(slot, itemLink)
    local wantedID = WR_ItemID(itemLink)
    local equippedID = WR_ItemID(GetInventoryItemLink("player", slot))
    return wantedID and equippedID and wantedID == equippedID
end

local function WR_SpellReadyIgnoringStance(spellID)
    if not IsSpellKnown(spellID) then return false end
    local start, duration, enabled = GetSpellCooldown(spellID)
    return enabled ~= 0 and (not start or start == 0 or not duration or duration <= 1.5)
end

local function WR_EquipReflectGear()
    if not WR_Gear.reflect1h or not WR_Gear.shield then return false end
    if not WR_IsEquipped(16, WR_Gear.reflect1h) then
        EquipItemByName(WR_Gear.reflect1h, 16)
        return false
    end
    if not WR_IsEquipped(17, WR_Gear.shield) then
        EquipItemByName(WR_Gear.shield, 17)
        return false
    end
    return true
end

local function WR_RestoreArms()
    if WR_Gear.arms2h and not WR_IsEquipped(16, WR_Gear.arms2h) then
        EquipItemByName(WR_Gear.arms2h, 16)
        return true
    end
    if GetShapeshiftForm() ~= 1 then
        WR_Cast(2457, "player") -- Battle Stance is the Arms baseline.
        return true
    end
    WR_Engine.ArmsResponse = nil
    WR_Engine.ReflectHoldUntil = nil
    WR_Engine.ArmsUnit = nil
    return false
end

-- Highest known rank of Shield Bash (interrupt is rank-independent). IsSpellKnown
-- is rank-specific and already used elsewhere in this profile, so this is safe.
local WR_SHIELD_BASH_RANKS = { 1672, 1671, 72 }
local function WR_ShieldBashID()
    for _, id in ipairs(WR_SHIELD_BASH_RANKS) do
        if IsSpellKnown(id) then return id end
    end
    return nil
end

-- Rage costs. Arms always specs Tactical Mastery, which keeps up to 25 rage on a
-- stance change, so "current rage >= cost" also guarantees enough rage after a swap.
local WR_PUMMEL_RAGE = 10
local WR_SHIELDBASH_RAGE = 10
local WR_REFLECT_RAGE = 15

-- Priority spell lists for auto interrupt / reflect. Enemy casts are matched by
-- localized NAME (UnitCastingInfo carries no spellID in 3.3.5a); names are
-- resolved from spellIDs via GetSpellInfo so this works on any client locale.
-- REFLECT list = high-value casts best sent back (CC + big nukes). Reflection
-- ignores the "not interruptible" flag, so these get reflected even when a kick
-- would be blocked. KICK list = casts best interrupted outright (mainly heals);
-- reflectable spells are also kicked when reflect is unavailable.
local WR_REFLECT_IDS = {
    51514, -- Hex (Shaman)
    118,   -- Polymorph (Mage), all variants share this name
    5782,  -- Fear (Warlock)
    6358,  -- Seduction (Succubus)
    605,   -- Mind Control (Priest)
    710,   -- Banish (Warlock)
    50796, -- Chaos Bolt (Warlock)
    11366, -- Pyroblast (Mage)
    133,   -- Fireball (Mage)
    116,   -- Frostbolt (Mage)
    44614, -- Frostfire Bolt (Mage)
    686,   -- Shadow Bolt (Warlock)
    29722, -- Incinerate (Warlock)
    6353,  -- Soul Fire (Warlock)
    51505, -- Lava Burst (Shaman)
    403,   -- Lightning Bolt (Shaman)
    8092,  -- Mind Blast (Priest)
    585,   -- Smite (Priest)
    2912,  -- Starfire (Druid)
    5176,  -- Wrath (Druid)
}
local WR_KICK_IDS = {
    2060,  -- Greater Heal (Priest)
    2061,  -- Flash Heal (Priest)
    2054,  -- Heal (Priest)
    596,   -- Prayer of Healing (Priest)
    32546, -- Binding Heal (Priest)
    47540, -- Penance (Priest)
    64843, -- Divine Hymn (Priest)
    5185,  -- Healing Touch (Druid)
    50464, -- Nourish (Druid)
    8936,  -- Regrowth (Druid)
    20484, -- Rebirth (Druid battle rez)
    331,   -- Healing Wave (Shaman)
    8004,  -- Lesser Healing Wave (Shaman)
    635,   -- Holy Light (Paladin)
    19750, -- Flash of Light (Paladin)
}

local WR_ReflectNames, WR_KickNames
local function WR_BuildSpellSets()
    if WR_ReflectNames then return end
    WR_ReflectNames, WR_KickNames = {}, {}
    for _, id in ipairs(WR_REFLECT_IDS) do
        local n = GetSpellInfo(id)
        if n then WR_ReflectNames[n] = true; WR_KickNames[n] = true end
    end
    for _, id in ipairs(WR_KICK_IDS) do
        local n = GetSpellInfo(id)
        if n then WR_KickNames[n] = true end
    end
end

-- Returns castName, remainingMS, notInterruptible for a unit cast or channel.
local function WR_UnitCast(unit)
    if not UnitExists(unit) or not UnitCanAttack("player", unit) or UnitIsDeadOrGhost(unit) then
        return nil
    end
    local name, _, _, _, _, endMS, _, _, notInt = UnitCastingInfo(unit)
    if not name then
        name, _, _, _, _, endMS, _, notInt = UnitChannelInfo(unit)
    end
    if not name or not endMS then return nil end
    return name, endMS - (GetTime() * 1000), notInt
end

local WR_SCAN_UNITS = { "target", "focus" }

-- Melee range probe that ignores stance. Pummel is Berserker-only, so
-- IsSpellInRange(Pummel, unit) returns nil while in Battle stance and would
-- wrongly fail the range test, handing the kick to Shield Bash. Rend (47465)
-- is castable in every stance and shares melee range, so it is a reliable probe.
local function WR_InMelee(unit, spellName)
    local probe = GetSpellInfo(47465)
    if probe then
        local r = IsSpellInRange(probe, unit)
        if r == 1 then return true end
        if r == 0 then return false end
    end
    if spellName then
        return IsSpellInRange(spellName, unit) == 1
    end
    return false
end

function WR_ArmsInterruptOrReflect()
    if WR_Engine.CurrentRotation ~= "Arms_DPS_DarhangeR" then return false end

    -- Restoring 2H + Battle stance has priority over everything else.
    if WR_Engine.ArmsResponse == "restore" then
        return WR_RestoreArms()
    end

    -- Reflection was cast: hold shield + stance until the enemy spell lands.
    if WR_Engine.ReflectHoldUntil then
        if GetTime() >= WR_Engine.ReflectHoldUntil then
            WR_Engine.ReflectHoldUntil = nil
            WR_Engine.ArmsResponse = "restore"
            return WR_RestoreArms()
        end
        return true
    end

    WR_BuildSpellSets()

    local stance = GetShapeshiftForm()
    local rage = UnitPower("player")
    local hasReflectGear = WR_Gear.reflect1h and WR_Gear.reflect1h ~= "" and WR_Gear.shield and WR_Gear.shield ~= ""
    local pummelName = GetSpellInfo(6552)
    local shieldBashID = WR_ShieldBashID()
    local shieldBashName = shieldBashID and GetSpellInfo(shieldBashID) or nil

    local reflectReady = WR_ArmsAutoReflect and hasReflectGear and rage >= WR_REFLECT_RAGE
        and WR_SpellReadyIgnoringStance(23920)
    local pummelReady = WR_ArmsAutoPummel and pummelName and rage >= WR_PUMMEL_RAGE
        and WR_SpellReadyIgnoringStance(6552)
    local shieldBashReady = WR_ArmsAutoPummel and hasReflectGear and shieldBashID
        and rage >= WR_SHIELDBASH_RAGE and WR_SpellReadyIgnoringStance(shieldBashID)

    -- Choose a reaction only when idle; once committed we stick to the same unit
    -- so the bot never dances between stances.
    if not WR_Engine.ArmsResponse then
        local reflectUnit, reflectMS
        local kickUnit, kickMS
        for _, unit in ipairs(WR_SCAN_UNITS) do
            local name, remMS, notInt = WR_UnitCast(unit)
            if name and remMS and remMS >= 120 then
                -- Reflect: reflectable cast from target or focus (ignores notInterruptible).
                if not reflectUnit and WR_ReflectNames[name] then
                    reflectUnit, reflectMS = unit, remMS
                end
                -- Kick: important, actually interruptible, and a melee interrupt is in range.
                if not kickUnit and WR_KickNames[name] and not notInt then
                    local pInRange = pummelReady and WR_InMelee(unit, pummelName)
                    local sInRange = shieldBashReady and shieldBashName and WR_InMelee(unit, shieldBashName)
                    if pInRange or sInRange then
                        kickUnit, kickMS = unit, remMS
                    end
                end
            end
        end

        -- Priority: reflect a high-value cast if reflect is up, otherwise interrupt.
        if reflectReady and reflectUnit and reflectMS <= 1300 then
            WR_Engine.ArmsResponse, WR_Engine.ArmsUnit = "reflect", reflectUnit
        elseif kickUnit then
            if pummelReady and WR_InMelee(kickUnit, pummelName) and kickMS <= 1300 then
                WR_Engine.ArmsResponse, WR_Engine.ArmsUnit = "pummel", kickUnit
            elseif shieldBashReady and shieldBashName and WR_InMelee(kickUnit, shieldBashName) and kickMS <= 1300 then
                WR_Engine.ArmsResponse, WR_Engine.ArmsUnit = "shieldbash", kickUnit
            else
                return false
            end
        else
            return false
        end
    end

    local unit = WR_Engine.ArmsUnit or "target"
    local castName, remainingMS = WR_UnitCast(unit)
    if not castName or not remainingMS or remainingMS < 120 then
        WR_Engine.ArmsResponse = "restore"
        return WR_RestoreArms()
    end

    -- PUMMEL: Berserker stance, no gear swap.
    if WR_Engine.ArmsResponse == "pummel" then
        if rage < WR_PUMMEL_RAGE then WR_Engine.ArmsResponse = "restore"; return WR_RestoreArms() end
        if stance ~= 3 then
            if remainingMS > 300 then return WR_Cast(2458, "player") end
            WR_Engine.ArmsResponse = "restore"; return WR_RestoreArms()
        end
        if remainingMS <= 300 and remainingMS >= 150 and PQR_SpellAvailable(6552) then
            WR_Engine.ArmsResponse = "restore"
            return WR_Cast(6552, unit)
        end
        return true
    end

    -- SHIELD BASH: 1H + shield, Battle/Defensive stance, interrupt without Berserker.
    if WR_Engine.ArmsResponse == "shieldbash" then
        if rage < WR_SHIELDBASH_RAGE or not shieldBashID then WR_Engine.ArmsResponse = "restore"; return WR_RestoreArms() end
        if stance ~= 1 and stance ~= 2 then
            if remainingMS > 300 then return WR_Cast(2457, "player") end
            WR_Engine.ArmsResponse = "restore"; return WR_RestoreArms()
        end
        if not WR_EquipReflectGear() then return true end
        if remainingMS <= 350 and remainingMS >= 120 and PQR_SpellAvailable(shieldBashID) then
            WR_Engine.ArmsResponse = "restore"
            return WR_Cast(shieldBashID, unit)
        end
        return true
    end

    -- REFLECT: 1H + shield, Battle/Defensive stance, send the spell back.
    if WR_Engine.ArmsResponse == "reflect" then
        if rage < WR_REFLECT_RAGE then WR_Engine.ArmsResponse = "restore"; return WR_RestoreArms() end
        if stance ~= 1 and stance ~= 2 then
            if remainingMS > 300 then return WR_Cast(2457, "player") end
            WR_Engine.ArmsResponse = "restore"; return WR_RestoreArms()
        end
        if not WR_EquipReflectGear() then return true end
        if remainingMS <= 300 and remainingMS >= 120 and PQR_SpellAvailable(23920) then
            WR_Engine.ReflectHoldUntil = GetTime() + (remainingMS / 1000) + 0.4
            return WR_Cast(23920, "player")
        end
        return true
    end

    return false
end

WR_PAUSE_ALLOW = {
    ["A:Rend"] = true,
    ["A:Hamstring"] = true,
    ["A:Berserker Rage (PvP)"] = true,
}

function WR_MSPooling()
    -- Hold damage fillers (Overpower/Execute) during the final GCD before Mortal
    -- Strike is ready, so MS lands the instant its cooldown ends with no gap in
    -- the Mortal Wounds (-50% healing) debuff.
    if WR_Engine.CurrentRotation ~= "Arms_DPS_DarhangeR" then return false end
    local start, dur = GetSpellCooldown(47486)
    if not start or start == 0 or not dur or dur == 0 then return false end
    local remain = (start + dur) - GetTime()
    if remain <= 0.05 then return false end
    if remain <= 1.5 and IsUsableSpell(47486) then return true end
    return false
end

function WR_Engine.Tick()
    WR_GearCaptureTick()
    if not WR_Engine.Enabled then return false end
    local _, playerClass = UnitClass("player")
    if playerClass ~= "WARRIOR" or UnitIsDeadOrGhost("player") or IsMounted() or UnitInVehicle("player") then
        return false
    end
    local rotation = WR_Engine.Rotations[WR_Engine.CurrentRotation]
    if not rotation then return false end
    if WR_ArmsInterruptOrReflect() then return true end

    for _, abilityName in ipairs(rotation) do
        local paused = (WR_AutoRotation == false) and not WR_PAUSE_ALLOW[abilityName]
        local ability = (paused or abilityName == "-- Interrupt Engine:A --" or abilityName == "A:Battle Stance") and nil or WR_Engine.Abilities[abilityName]
        if ability then
            local ok, shouldCast = pcall(ability.test)
            if not ok then
                WR_Engine.LastErrorAction = abilityName
                WR_Engine.LastErrorText = tostring(shouldCast)
                WR_Engine.LastErrorAt = GetTime()
            elseif shouldCast then
                if ability.id > 0 then
                    local unit = ability.target
                    if unit == "Player" then unit = "player"
                    elseif unit == "Target" or unit == "Click" or unit == "Custom" then unit = "target" end
                    WR_Cast(ability.id, unit)
                end
                return true
            end
        end
    end
    return false
end

)WARRIOR_LUA";

    const char* const AutoDefensives = R"WR_DEF_LUA(
WR_AutoDefensives = (WR_AutoDefensives ~= false)

local function WRD_ItemID(value)
    if type(value) == "number" then return value end
    if type(value) ~= "string" then return nil end
    return tonumber(string.match(value, "item:(%d+)")) or tonumber(value)
end

local function WRD_IsEquipped(slot, value)
    local wanted = WRD_ItemID(value)
    local equipped = WRD_ItemID(GetInventoryItemLink("player", slot))
    return wanted and equipped and wanted == equipped
end

local function WRD_Ready(spellID)
    if not IsSpellKnown(spellID) then return false end
    local start, duration, enabled = GetSpellCooldown(spellID)
    return enabled ~= 0 and (not start or start == 0 or not duration or duration <= 1.5)
end

local function WRD_EquipShieldSet()
    if not WR_Gear or not WR_Gear.reflect1h or WR_Gear.reflect1h == ""
        or not WR_Gear.shield or WR_Gear.shield == "" then
        return false
    end
    if not WRD_IsEquipped(16, WR_Gear.reflect1h) then
        EquipItemByName(WR_Gear.reflect1h, 16)
        return false
    end
    if not WRD_IsEquipped(17, WR_Gear.shield) then
        EquipItemByName(WR_Gear.shield, 17)
        return false
    end
    return true
end

local function WRD_RestoreArms()
    if WR_Gear and WR_Gear.arms2h and WR_Gear.arms2h ~= ""
        and not WRD_IsEquipped(16, WR_Gear.arms2h) then
        EquipItemByName(WR_Gear.arms2h, 16)
        return true
    end
    if GetShapeshiftForm() ~= 1 then
        WR_Cast(2457, "player")
        return true
    end
    WR_Engine.DefensiveActive = nil
    WR_Engine.DefensiveHoldUntil = nil
    WR_Engine.DefensiveReachedStance = nil
    WR_Engine.ArmsResponse = nil
    WR_Engine.ReflectHoldUntil = nil
    WR_Engine.ArmsUnit = nil
    return false
end

function WR_DefensiveCooldowns()
    if not WR_Engine or WR_Engine.CurrentRotation ~= "Arms_DPS_DarhangeR" then
        return false
    end

    -- Interrupt/reflect sequence has priority; never fight it for stance or gear.
    if WR_Engine.ArmsResponse or WR_Engine.ReflectHoldUntil then
        return false
    end

    -- If disabled while turtling, safely return to 2H + Battle Stance.
    if not WR_AutoDefensives then
        if WR_Engine.DefensiveActive then
            return WRD_RestoreArms()
        end
        return false
    end

    local maxHp = UnitHealthMax("player") or 0
    if maxHp <= 0 then return false end
    local hp = 100 * (UnitHealth("player") or 0) / maxHp

    -- If the player manually leaves a fully-entered Defensive Stance for Battle,
    -- treat it as an explicit request to resume Arms Auto Rotation.
    if WR_Engine.DefensiveActive and WR_Engine.DefensiveReachedStance
        and GetShapeshiftForm() == 1 then
        WR_AutoRotation = true
        WR_Engine.DefensiveManualOverride = true
        WR_Engine.DefensiveHoldUntil = nil
    end

    -- Manual Arms selection is an explicit request to leave turtle mode.
    -- Keep Auto Defensives armed, but do not take control again until HP > 45%.
    if WR_Engine.DefensiveManualOverride then
        if hp > 45 then
            WR_Engine.DefensiveManualOverride = nil
        else
            WR_Engine.DefensiveHoldUntil = nil
            if WR_Engine.DefensiveActive then
                return WRD_RestoreArms()
            end
            return false
        end
    end

    -- Last Stand at critical HP; works only if the character knows the talent.
    if hp <= 30 and WRD_Ready(12975) and PQR_SpellAvailable(12975) then
        return WR_Cast(12975, "player")
    end

    local hasShieldSet = WR_Gear and WR_Gear.reflect1h and WR_Gear.reflect1h ~= ""
        and WR_Gear.shield and WR_Gear.shield ~= ""
    local holding = WR_Engine.DefensiveHoldUntil
        and GetTime() < WR_Engine.DefensiveHoldUntil
    local shouldEnter = hp <= 35
    -- Once entered, stay in Defensive until an explicit manual override.
    local shouldStay = WR_Engine.DefensiveActive

    if not hasShieldSet then
        -- Never change stance automatically after defense was entered.
        if WR_Engine.DefensiveActive then return true end
        return false
    end
    if not (holding or shouldEnter or shouldStay) then return false end

    WR_Engine.DefensiveActive = true

    -- Equip captured 1H + shield, then enter Defensive Stance.
    if not WRD_EquipShieldSet() then return true end
    if GetShapeshiftForm() ~= 2 then
        return WR_Cast(71, "player")
    end
    WR_Engine.DefensiveReachedStance = true

    -- Shield Wall at critical HP; keep the shield equipped for its duration.
    if hp <= 30 and WRD_Ready(871) and PQR_SpellAvailable(871) then
        WR_Engine.DefensiveHoldUntil = GetTime() + 12
        return WR_Cast(871, "player")
    end

    -- Shield Block whenever available while the defensive state is active.
    if WRD_Ready(2565) and PQR_SpellAvailable(2565) then
        return WR_Cast(2565, "player")
    end

    return true
end

-- Preserve the exact original rotation Tick. Reinstall safely after every base reload.
if WR_Engine and WR_Engine.Tick ~= WR_DefensiveWrappedTick then
    WR_DefensiveBaseTick = WR_Engine.Tick
end

function WR_DefensiveWrappedTick()
    if not WR_DefensiveBaseTick then return false end
    local ok, handled = pcall(WR_DefensiveCooldowns)
    if ok and handled then return true end
    if not ok and WR_Engine then
        WR_Engine.LastErrorAction = "Auto Defensives"
        WR_Engine.LastErrorText = tostring(handled)
        WR_Engine.LastErrorAt = GetTime()
    end
    return WR_DefensiveBaseTick()
end

if WR_Engine and WR_DefensiveBaseTick then
    WR_Engine.Tick = WR_DefensiveWrappedTick
end
)WR_DEF_LUA";

    const char* const KickReflectFix = R"WR_KR_LUA(
local WRK_REFLECT_IDS = {
    51514, 118, 5782, 6358, 605, 710, 50796, 11366, 133, 116,
    44614, 686, 29722, 6353, 51505, 403, 8092, 585, 2912, 5176,
    33786, -- Cyclone
    339,   -- Entangling Roots (all ranks share the name)
    2637,  -- Hibernate
    9484,  -- Shackle Undead
    8129,  -- Mana Burn
    30108, -- Unstable Affliction
    48181, -- Haunt
}
local WRK_KICK_IDS = {
    2060, 2061, 2054, 596, 32546, 47540, 64843, 5185, 50464,
    8936, 20484, 331, 8004, 635, 19750,
    32375, -- Mass Dispel
    55459, -- Chain Heal
    48447, -- Tranquility
    50769, -- Revive
    48171, -- Resurrection
    48950, -- Redemption
    49277, -- Ancestral Spirit
    47857, -- Drain Life
    47855, -- Drain Soul
    47836, -- Seed of Corruption
}
local WRK_ReflectNames = {}
local WRK_KickNames = {}
for _, id in ipairs(WRK_REFLECT_IDS) do
    local name = GetSpellInfo(id)
    if name then
        WRK_ReflectNames[name] = true
        WRK_KickNames[name] = true
    end
end
for _, id in ipairs(WRK_KICK_IDS) do
    local name = GetSpellInfo(id)
    if name then WRK_KickNames[name] = true end
end

local function WRK_ItemID(value)
    if type(value) == "number" then return value end
    if type(value) ~= "string" then return nil end
    return tonumber(string.match(value, "item:(%d+)")) or tonumber(value)
end

local function WRK_IsEquipped(slot, value)
    local wanted = WRK_ItemID(value)
    local equipped = WRK_ItemID(GetInventoryItemLink("player", slot))
    return wanted and equipped and wanted == equipped
end

local function WRK_HasShieldEquipped()
    if not WR_Gear or not WR_Gear.shield or WR_Gear.shield == "" then return false end
    return WRK_IsEquipped(17, WR_Gear.shield)
end

local function WRK_Ready(spellID)
    if not spellID or not IsSpellKnown(spellID) then return false end
    local start, duration, enabled = GetSpellCooldown(spellID)
    return enabled ~= 0 and (not start or start == 0 or not duration or duration <= 1.5)
end

local function WRK_ShieldBashID()
    for _, id in ipairs({ 1672, 1671, 72 }) do
        if IsSpellKnown(id) then return id end
    end
    return nil
end

local function WRK_InMelee(unit, spellName)
    local probe = GetSpellInfo(47465) -- Rend: melee range and usable in every stance.
    if probe then
        local range = IsSpellInRange(probe, unit)
        if range == 1 then return true end
        if range == 0 then return false end
    end
    return spellName and IsSpellInRange(spellName, unit) == 1 or false
end

-- name, remainingMS, notInterruptible, isChannel, totalMS
local function WRK_UnitCast(unit)
    if not UnitExists(unit) or not UnitCanAttack("player", unit) or UnitIsDeadOrGhost(unit) then
        return nil
    end
    local name, _, _, _, startMS, endMS, _, _, notInt = UnitCastingInfo(unit)
    if name and startMS and endMS then
        return name, endMS - GetTime() * 1000, notInt, false, endMS - startMS
    end
    name, _, _, _, startMS, endMS, _, notInt = UnitChannelInfo(unit)
    if name and startMS and endMS then
        return name, endMS - GetTime() * 1000, notInt, true, endMS - startMS
    end
    return nil
end

local function WRK_AimedAtPlayer(unit)
    local unitTarget = unit .. "target"
    return UnitExists(unitTarget) and UnitIsUnit(unitTarget, "player")
end

local function WRK_LatencyLeadMS()
    local _, _, home, world = GetNetStats()
    local latency = math.max(tonumber(home) or 0, tonumber(world) or 0)
    -- Late enough to punish fake casts, early enough to survive server tick + round trip.
    return math.max(650, math.min(1000, 550 + latency * 2))
end

local function WRK_ReflectLeadMS()
    local _, _, home, world = GetNetStats()
    local latency = math.max(tonumber(home) or 0, tonumber(world) or 0)
    -- Trigger at 220-300 ms client-side. Half RTT compensation makes the server
    -- receive Spell Reflection close to 200 ms before the enemy cast finishes.
    return math.max(220, math.min(300, 220 + latency * 0.5))
end

local function WRK_ReflectPrepareMS()
    local _, _, home, world = GetNetStats()
    local latency = math.max(tonumber(home) or 0, tonumber(world) or 0)
    -- Reserve the reflect response early enough to stop starting a new GCD.
    -- The actual 1H+Shield swap is delayed by WRK_ReflectEquipMS().
    return math.max(1700, math.min(2300, 1700 + latency * 2))
end

local function WRK_ReflectEquipMS()
    local _, _, home, world = GetNetStats()
    local latency = math.max(tonumber(home) or 0, tonumber(world) or 0)
    -- Keep the 2H equipped a little longer, but preserve approximately one
    -- weapon-swap GCD plus the late 220-300 ms reflection window.
    return math.max(1750, math.min(2000, 1650 + latency * 1.25))
end

local function WRK_KickWindowMS(totalMS, isChannel)
    if isChannel then return 5000 end
    return math.max(WRK_LatencyLeadMS(), math.min(1000, (totalMS or 0) * 0.35))
end

local function WRK_EquipShieldSet()
    if not WR_Gear or not WR_Gear.reflect1h or WR_Gear.reflect1h == ""
        or not WR_Gear.shield or WR_Gear.shield == "" then
        return false
    end
    -- Request both slots in one tick; the old code spent two ticks doing this.
    if not WRK_IsEquipped(16, WR_Gear.reflect1h) then
        EquipItemByName(WR_Gear.reflect1h, 16)
    end
    if not WRK_IsEquipped(17, WR_Gear.shield) then
        EquipItemByName(WR_Gear.shield, 17)
    end
    return WRK_IsEquipped(16, WR_Gear.reflect1h) and WRK_IsEquipped(17, WR_Gear.shield)
end

local function WRK_ClearResponse()
    WR_Engine.ArmsResponse = nil
    WR_Engine.ArmsUnit = nil
    WR_Engine.ArmsSpell = nil
    WR_Engine.ArmsChannel = nil
    WR_Engine.ArmsCastTotal = nil
end

local function WRK_DefensiveLocked()
    return WR_Engine and WR_Engine.DefensiveActive
        and not WR_Engine.DefensiveManualOverride
end

local function WRK_ClearCombatResponse()
    WR_Engine.ReflectHoldUntil = nil
    WRK_ClearResponse()
end

local function WRK_RestoreArms()
    if WR_Gear and WR_Gear.arms2h and WR_Gear.arms2h ~= ""
        and not WRK_IsEquipped(16, WR_Gear.arms2h) then
        EquipItemByName(WR_Gear.arms2h, 16)
        return true
    end
    if GetShapeshiftForm() ~= 1 then
        WR_Cast(2457, "player")
        return true
    end
    WR_Engine.ReflectHoldUntil = nil
    WRK_ClearResponse()
    return false
end

local function WRK_ChooseSooner(current, candidate)
    if not current or candidate.remaining < current.remaining then return candidate end
    return current
end

function WR_ArmsInterruptOrReflect()
    if not WR_Engine or WR_Engine.CurrentRotation ~= "Arms_DPS_DarhangeR" then
        return false
    end

    if WR_Engine.ArmsResponse == "restore" then
        if WRK_DefensiveLocked() then
            WRK_ClearCombatResponse()
            return false
        end
        return WRK_RestoreArms()
    end

    -- Keep the shield only until the reflected cast should have landed.
    if WR_Engine.ReflectHoldUntil then
        if GetTime() >= WR_Engine.ReflectHoldUntil then
            if WRK_DefensiveLocked() then
                WRK_ClearCombatResponse()
                return false
            end
            WR_Engine.ArmsResponse = "restore"
            return WRK_RestoreArms()
        end
        return true
    end

    local stance = GetShapeshiftForm()
    local rage = UnitPower("player") or 0
    local pummelName = GetSpellInfo(6552)
    local bashID = WRK_ShieldBashID()
    local bashName = bashID and GetSpellInfo(bashID) or nil
    local shieldOn = WRK_HasShieldEquipped()
    local hasReflectSet = WR_Gear and WR_Gear.reflect1h and WR_Gear.reflect1h ~= ""
        and WR_Gear.shield and WR_Gear.shield ~= ""
    -- In Defensive Stance never leave for Pummel: kick only with Shield Bash.
    local pummelReady = WR_ArmsAutoPummel and stance ~= 2 and rage >= 10 and WRK_Ready(6552)
    local bashReady = WR_ArmsAutoPummel and shieldOn and stance ~= 3 and rage >= 10
        and bashID and WRK_Ready(bashID)
    local bashCanPrepare = WR_ArmsAutoPummel and stance == 2 and hasReflectSet and rage >= 10
        and bashID and WRK_Ready(bashID)
    local reflectReady = WR_ArmsAutoReflect and hasReflectSet and rage >= 15
        and WRK_Ready(23920)

    -- Revalidate the exact spell. A fake-cast followed by another spell must not be kicked.
    if WR_Engine.ArmsResponse then
        local unit = WR_Engine.ArmsUnit or "target"
        local name, remaining, notInt, isChannel, totalMS = WRK_UnitCast(unit)
        if not name or name ~= WR_Engine.ArmsSpell or not remaining or remaining < 100 then
            if WRK_DefensiveLocked() then
                WRK_ClearCombatResponse()
                return false
            end
            WR_Engine.ArmsResponse = "restore"
            return WRK_RestoreArms()
        end
        WR_Engine.ArmsChannel = isChannel
        WR_Engine.ArmsCastTotal = totalMS

        if WR_Engine.ArmsResponse == "pummel" then
            if stance == 2 then
                WRK_ClearResponse()
                return false
            end
            if not pummelReady or not WRK_InMelee(unit, pummelName) then
                WR_Engine.ArmsResponse = "restore"
                return WRK_RestoreArms()
            end
            if stance ~= 3 then
                if remaining > 350 then return WR_Cast(2458, "player") end
                WR_Engine.ArmsResponse = "restore"
                return WRK_RestoreArms()
            end
            if (isChannel or remaining <= WRK_KickWindowMS(totalMS, false))
                and not notInt and PQR_SpellAvailable(6552) then
                WR_Engine.ArmsResponse = "restore"
                return WR_Cast(6552, unit)
            end
            return true
        end

        if WR_Engine.ArmsResponse == "shieldbash" then
            -- Defensive Stance is dedicated to Shield Bash. If the shield is still
            -- being equipped by Auto Defensives, keep the cast reserved and finish it.
            if stance ~= 2 or not bashID or rage < 10 or not WRK_Ready(bashID)
                or not WRK_InMelee(unit, bashName) or notInt then
                WRK_ClearResponse()
                return false
            end
            if not shieldOn then
                if not hasReflectSet then
                    WRK_ClearResponse()
                    return false
                end
                WRK_EquipShieldSet()
                return true
            end
            if (isChannel or remaining <= WRK_KickWindowMS(totalMS, false))
                and PQR_SpellAvailable(bashID) then
                WRK_ClearResponse()
                return WR_Cast(bashID, unit)
            end
            return true
        end

        if WR_Engine.ArmsResponse == "reflect" then
            if not reflectReady or not WRK_AimedAtPlayer(unit) then
                if WRK_DefensiveLocked() then
                    WRK_ClearCombatResponse()
                    return false
                end
                WR_Engine.ArmsResponse = "restore"
                return WRK_RestoreArms()
            end
            -- Spell Reflection works in Battle/Defensive stance; never switch Battle -> Defensive.
            if stance == 3 then return WR_Cast(2457, "player") end

            -- The response is already reserved, so no new GCD ability starts,
            -- but keep the 2H equipped until the safe late-swap window. Auto
            -- attacks can continue with the 2H during this short waiting phase.
            if not shieldOn and remaining > WRK_ReflectEquipMS() then
                return true
            end

            if not WRK_EquipShieldSet() then return true end
            -- Spell Reflection itself remains late: 220-300 ms before completion,
            -- adjusted by current network latency.
            local reflectLead = WRK_ReflectLeadMS()
            if remaining <= reflectLead and remaining >= 80 and PQR_SpellAvailable(23920) then
                WR_Engine.ReflectHoldUntil = GetTime() + math.min(5.0, remaining / 1000 + 0.5)
                return WR_Cast(23920, "player")
            end
            return true
        end

        WRK_ClearResponse()
        return false
    end

    local bestReflect, bestKick
    for _, unit in ipairs({ "target", "focus" }) do
        local name, remaining, notInt, isChannel, totalMS = WRK_UnitCast(unit)
        if name and remaining and remaining >= 100 then
            local candidate = {
                unit = unit, name = name, remaining = remaining,
                notInt = notInt, channel = isChannel, total = totalMS,
            }
            -- Reflect only a cast definitely aimed at the player. This avoids wasting it on teammates.
            if reflectReady and WRK_ReflectNames[name] and WRK_AimedAtPlayer(unit)
                and remaining <= WRK_ReflectPrepareMS() then
                bestReflect = WRK_ChooseSooner(bestReflect, candidate)
            end
            if not notInt and WRK_KickNames[name] then
                local pummelInRange = pummelReady and WRK_InMelee(unit, pummelName)
                local bashInRange = (bashReady or bashCanPrepare) and WRK_InMelee(unit, bashName)
                if pummelInRange or bashInRange then
                    bestKick = WRK_ChooseSooner(bestKick, candidate)
                end
            end
        end
    end

    if bestKick and stance == 2 then
        local kickWindow = WRK_KickWindowMS(bestKick.total, bestKick.channel)
        local needsShieldTime = not shieldOn
        if bestKick.channel or bestKick.remaining <= kickWindow
            or (needsShieldTime and bestKick.remaining <= 2200) then
            WR_Engine.ArmsResponse = "shieldbash"
            WR_Engine.ArmsUnit = bestKick.unit
            WR_Engine.ArmsSpell = bestKick.name
            WR_Engine.ArmsChannel = bestKick.channel
            WR_Engine.ArmsCastTotal = bestKick.total
            return true
        end
    end

    if bestReflect then
        WR_Engine.ArmsResponse = "reflect"
        WR_Engine.ArmsUnit = bestReflect.unit
        WR_Engine.ArmsSpell = bestReflect.name
        WR_Engine.ArmsChannel = bestReflect.channel
        WR_Engine.ArmsCastTotal = bestReflect.total
        return true
    end

    if bestKick and stance ~= 2 then
        local kickWindow = WRK_KickWindowMS(bestKick.total, bestKick.channel)
        local needsStanceTime = pummelReady and stance ~= 3
        if bestKick.channel or bestKick.remaining <= kickWindow or (needsStanceTime and bestKick.remaining <= 1800) then
            -- Outside Defensive Stance use Pummel; Shield Bash is reserved for Defensive.
            WR_Engine.ArmsResponse = "pummel"
            WR_Engine.ArmsUnit = bestKick.unit
            WR_Engine.ArmsSpell = bestKick.name
            WR_Engine.ArmsChannel = bestKick.channel
            WR_Engine.ArmsCastTotal = bestKick.total
            return true
        end
    end

    return false
end

-- Final execution order for Arms:
--   1) interrupt / Shield Bash / reflection
--   2) Auto Defensives
--   3) original DarhangeR rotation
-- This replaces the earlier defensive wrapper whose early return hid Shield Bash.
if WR_Engine then
    WR_IntegratedBaseTick = WR_DefensiveBaseTick or WR_Engine.Tick
end

function WR_IntegratedWarriorTick()
    if not WR_IntegratedBaseTick then return false end

    -- A real manual Battle Stance switch is allowed to unlock defense.
    if WR_Engine and WR_Engine.DefensiveActive and WR_Engine.DefensiveReachedStance
        and not WR_Engine.DefensiveManualOverride and GetShapeshiftForm() == 1 then
        WR_AutoRotation = true
        WR_Engine.DefensiveManualOverride = true
        WR_Engine.DefensiveHoldUntil = nil
        WR_Engine.ArmsResponse = nil
        WR_Engine.ReflectHoldUntil = nil
    end

    local interruptOK, interruptHandled = pcall(WR_ArmsInterruptOrReflect)
    if interruptOK and interruptHandled then return true end
    if not interruptOK and WR_Engine then
        WR_Engine.LastErrorAction = "Interrupt / Shield Bash"
        WR_Engine.LastErrorText = tostring(interruptHandled)
        WR_Engine.LastErrorAt = GetTime()
    end

    local defensiveOK, defensiveHandled = pcall(WR_DefensiveCooldowns)
    if defensiveOK and defensiveHandled then return true end
    if not defensiveOK and WR_Engine then
        WR_Engine.LastErrorAction = "Auto Defensives"
        WR_Engine.LastErrorText = tostring(defensiveHandled)
        WR_Engine.LastErrorAt = GetTime()
    end

    return WR_IntegratedBaseTick()
end

if WR_Engine and WR_IntegratedBaseTick then
    WR_Engine.Tick = WR_IntegratedWarriorTick
end
)WR_KR_LUA";

    const char* const PvPCore = R"WR_PVP_LUA(
WR_PvP = WR_PvP or {}
WR_PvP.HamstringRefresh = 3.0
WR_PvP.RendRefresh = 3.0
WR_PvP.MortalWoundsRefresh = 2.5
WR_PvP.HeroicStrikeRage = 70
WR_PvP.MortalStrikeReserve = 30

-- Reliable Shift+hover weapon capture. This overrides the imported capture
-- functions without modifying the stable DarhangeR block.
if WR_StartReflectCapture ~= WR_StartReflectCaptureSafe then
    WRP_OriginalStartReflectCapture = WR_StartReflectCapture
end

local WRP_GEAR_LABELS = {
    arms2h = "2H weapon",
    reflect1h = "1H weapon",
    shield = "Shield",
}

local function WRP_GetTooltipItem(tooltip)
    local source = tooltip or GameTooltip
    if not source or not source.GetItem then return nil, nil end
    if source.IsShown and not source:IsShown() then return nil, nil end
    local name, link = source:GetItem()
    if not link or link == "" then return nil, nil end
    return name, link
end

local function WRP_CurrentTooltipLink()
    local _, link = WRP_GetTooltipItem(GameTooltip)
    return link
end

local function WRP_CaptureLink(link)
    return (link and link ~= "") and link or "|cff808080not set|r"
end

local function WRP_CapturePanelText()
    local target = WR_GearCaptureTarget
    local oneH = WRP_CaptureLink(WR_Gear.reflect1h)
    local shield = WRP_CaptureLink(WR_Gear.shield)

    if target == "reflect1h" and not WR_GearCaptureSession1H then
        oneH = "|cffffff00waiting for 1H weapon|r"
    end
    if target == "shield" and not WR_GearCaptureSessionShield then
        shield = "|cffffff00waiting for shield|r"
    end

    local status = WR_GearCaptureText or ""
    if status == "" then
        status = "Hover the wanted item. Shift may be held, but is not required."
    end

    return table.concat({
        "|cffFFD10AYamiYami weapon setup|r",
        "2H weapon:    " .. WRP_CaptureLink(WR_Gear.arms2h),
        "1H + Shield:  " .. oneH .. "  +  " .. shield,
        status,
        "|cff888888For 1H + Shield, items may be selected in either order.|r",
    }, "\n")
end

local function WRP_ShowCapturePanel(autoHideSeconds)
    if not WR_GearCaptureFrame and WRP_OriginalStartReflectCapture then
        -- The imported starter also changes the capture target. Preserve the
        -- active setup while using it only to create the original panel frame.
        local savedTarget = WR_GearCaptureTarget
        local savedAfter = WR_GearCaptureAfter
        local savedLastLink = WR_GearCaptureLastLink
        local savedIgnoreLink = WR_GearCaptureIgnoreLink
        local savedReadyAt = WR_GearCaptureReadyAt
        local savedText = WR_GearCaptureText
        local savedSession1H = WR_GearCaptureSession1H
        local savedSessionShield = WR_GearCaptureSessionShield
        WRP_OriginalStartReflectCapture()
        WR_GearCaptureTarget = savedTarget
        WR_GearCaptureAfter = savedAfter
        WR_GearCaptureLastLink = savedLastLink
        WR_GearCaptureIgnoreLink = savedIgnoreLink
        WR_GearCaptureReadyAt = savedReadyAt
        WR_GearCaptureText = savedText
        WR_GearCaptureSession1H = savedSession1H
        WR_GearCaptureSessionShield = savedSessionShield
    end
    if not WR_GearCaptureFrame then return end
    WR_GearCaptureFrame:SetHeight(140)
    if WR_GearCaptureFrame.text then
        WR_GearCaptureFrame.text:SetText(WRP_CapturePanelText())
    end
    WR_GearCaptureFrame.expire = autoHideSeconds and (GetTime() + autoHideSeconds) or nil
    WR_GearCaptureFrame:Show()
end

local function WRP_ResetCaptureState(hideFrame)
    WR_GearCaptureTarget = nil
    WR_GearCaptureAfter = nil
    WR_GearCaptureLastLink = nil
    WR_GearCaptureIgnoreLink = nil
    WR_GearCaptureReadyAt = nil
    WR_GearCaptureNeedShiftRelease = nil
    WR_GearCaptureSession1H = nil
    WR_GearCaptureSessionShield = nil
    if hideFrame and WR_GearCaptureFrame then
        WR_GearCaptureFrame.expire = nil
        WR_GearCaptureFrame:Hide()
    end
end

local function WRP_BeginCapture(target)
    local staleLink = WRP_CurrentTooltipLink()
    WR_GearCaptureTarget = target
    WR_GearCaptureAfter = nil
    WR_GearCaptureLastLink = nil
    WR_GearCaptureIgnoreLink = staleLink
    WR_GearCaptureReadyAt = GetTime() + 0.15
    WR_GearCaptureNeedShiftRelease = nil
    WR_GearCaptureText = target == "arms2h"
        and "|cffffff00Hover the TWO-HANDED weapon.|r"
        or "|cffffff00Hover the 1H weapon or shield; either order works.|r"

    if target == "reflect1h" then
        WR_GearCaptureSession1H = false
        WR_GearCaptureSessionShield = false
    else
        WR_GearCaptureSession1H = nil
        WR_GearCaptureSessionShield = nil
    end
    WRP_ShowCapturePanel(nil)
end

function WR_StartArmsCapture()
    WRP_BeginCapture("arms2h")
end

function WR_StartReflectCaptureSafe()
    WRP_BeginCapture("reflect1h")
end

function WR_CancelCaptureSafe()
    WRP_ResetCaptureState(true)
    WR_GearCaptureText = ""
end

local function WRP_ClassifyCaptureItem(link)
    local itemName, _, _, _, _, _, _, _, equipLoc = GetItemInfo(link)
    if not itemName then
        return nil, nil, "|cffffff00Waiting for item data... keep the tooltip open.|r"
    end

    if equipLoc == "INVTYPE_2HWEAPON" then
        return "arms2h", itemName, nil
    end
    if equipLoc == "INVTYPE_WEAPON" or equipLoc == "INVTYPE_WEAPONMAINHAND" then
        return "reflect1h", itemName, nil
    end
    if equipLoc == "INVTYPE_SHIELD" then
        return "shield", itemName, nil
    end
    if equipLoc == "INVTYPE_WEAPONOFFHAND" then
        return nil, itemName, "|cffff5555This weapon is OFF-HAND only; select a weapon usable in the main hand.|r"
    end

    return nil, itemName,
        "|cffff5555Wrong item type (" .. tostring(equipLoc or "unknown") .. ").|r"
end

local function WRP_FinishCapture()
    WR_GearCaptureText = "|cff33ff33Weapon setup saved successfully.|r"
    WRP_ResetCaptureState(false)
    WRP_ShowCapturePanel(4)
end

function WR_TryCaptureTooltipSafe(tooltip)
    if not WR_GearCaptureTarget then return false end
    if WR_GearCaptureReadyAt and GetTime() < WR_GearCaptureReadyAt then return false end

    local name, link = WRP_GetTooltipItem(tooltip)
    if not link then return false end

    -- Never consume the tooltip that was already open when setup began.
    if WR_GearCaptureIgnoreLink and link == WR_GearCaptureIgnoreLink then
        WR_GearCaptureText = "|cffffff00Move to another item, then return if this is the item you want.|r"
        WRP_ShowCapturePanel(nil)
        return false
    end
    if WR_GearCaptureIgnoreLink and link ~= WR_GearCaptureIgnoreLink then
        WR_GearCaptureIgnoreLink = nil
    end
    if WR_GearCaptureLastLink and link == WR_GearCaptureLastLink then return false end

    local kind, itemName, reason = WRP_ClassifyCaptureItem(link)
    if not kind then
        WR_GearCaptureText = reason or "|cffff5555Unsupported item.|r"
        WRP_ShowCapturePanel(nil)
        return false
    end

    local target = WR_GearCaptureTarget
    if target == "arms2h" then
        if kind ~= "arms2h" then
            WR_GearCaptureText = "|cffff5555Wrong item: select a TWO-HANDED weapon.|r"
            WRP_ShowCapturePanel(nil)
            return false
        end

        WR_Gear.arms2h = link
        if DEFAULT_CHAT_FRAME then
            DEFAULT_CHAT_FRAME:AddMessage(
                "|cFFFFD10A[YamiYami]|r Saved 2H weapon: " .. (link or itemName or name or "?"))
        end
        WRP_FinishCapture()
        return true
    end

    -- Reflect setup accepts the 1H weapon and shield in either order.
    if kind ~= "reflect1h" and kind ~= "shield" then
        WR_GearCaptureText = "|cffff5555Select a ONE-HANDED main-hand weapon or a SHIELD.|r"
        WRP_ShowCapturePanel(nil)
        return false
    end

    WR_Gear[kind] = link
    WR_GearCaptureLastLink = link
    WR_GearCaptureIgnoreLink = link
    WR_GearCaptureReadyAt = GetTime() + 0.10

    if kind == "reflect1h" then
        WR_GearCaptureSession1H = true
        WR_GearCaptureText = "|cff33ff331H weapon saved.|r Now hover the shield."
    else
        WR_GearCaptureSessionShield = true
        WR_GearCaptureText = "|cff33ff33Shield saved.|r Now hover the 1H weapon."
    end

    if DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage(
            "|cFFFFD10A[YamiYami]|r Saved " ..
            (WRP_GEAR_LABELS[kind] or "item") .. ": " .. (link or itemName or name or "?"))
    end

    if WR_GearCaptureSession1H and WR_GearCaptureSessionShield then
        WRP_FinishCapture()
        return true
    end

    WR_GearCaptureTarget = WR_GearCaptureSession1H and "shield" or "reflect1h"
    WRP_ShowCapturePanel(nil)
    return true
end

function WR_GearCaptureTickSafe()
    if not WR_GearCaptureTarget then
        if WR_GearCaptureFrame and not WR_GearCaptureFrame.expire and WR_GearCaptureFrame:IsShown() then
            WR_GearCaptureFrame:Hide()
        end
        return
    end

    local currentLink = WRP_CurrentTooltipLink()
    if not currentLink then
        WR_GearCaptureIgnoreLink = nil
    elseif WR_GearCaptureIgnoreLink and currentLink ~= WR_GearCaptureIgnoreLink then
        WR_GearCaptureIgnoreLink = nil
    end

    if WR_TryCaptureTooltipSafe(GameTooltip) then return end
    WRP_ShowCapturePanel(nil)
end

WR_StartReflectCapture = WR_StartReflectCaptureSafe
WR_CancelCapture = WR_CancelCaptureSafe
WR_TryCaptureTooltip = WR_TryCaptureTooltipSafe
WR_GearCaptureTick = WR_GearCaptureTickSafe

local function WRP_DebuffRemaining(unit, spellID, caster)
    local name, _, _, _, _, _, expiration = UnitDebuffID(unit, spellID, caster)
    if not name then return 0 end
    if not expiration or expiration == 0 then return 999 end
    return math.max(0, expiration - GetTime())
end

local function WRP_RageCost(spellID)
    local cost = select(4, GetSpellInfo(spellID))
    return tonumber(cost) or 0
end

local function WRP_Rage()
    return UnitPower("player") or 0
end

local function WRP_CooldownRemaining(spellID)
    local start, duration = GetSpellCooldown(spellID)
    if not start or start == 0 or not duration or duration == 0 then return 0 end
    return math.max(0, start + duration - GetTime())
end

local function WRP_Ready(spellID)
    if not spellID or not IsSpellKnown(spellID) then return false end
    local start, duration, enabled = GetSpellCooldown(spellID)
    if enabled == 0 then return false end
    if start and start > 0 and duration and duration > 1.5 then return false end
    local usable = IsUsableSpell(GetSpellInfo(spellID))
    return usable == 1
end

local function WRP_InMelee(unit)
    local rend = GetSpellInfo(47465)
    return rend and IsSpellInRange(rend, unit) == 1
end

local function WRP_ValidEnemy(unit)
    return UnitExists(unit) and UnitCanAttack("player", unit)
        and not UnitIsDeadOrGhost(unit)
end

local function WRP_TargetIsCasting()
    local name = UnitCastingInfo("target")
    if name then return true end
    return UnitChannelInfo("target") ~= nil
end

local WRP_CASTER_CLASS = {
    MAGE = true, WARLOCK = true, PRIEST = true, SHAMAN = true,
    DRUID = true, PALADIN = true,
}

local function WRP_TasteForBloodRemaining()
    local name, _, _, _, _, _, expiration = UnitBuffID("player", 60503)
    if not name then return 0 end
    if not expiration or expiration == 0 then return 9 end
    return math.max(0, expiration - GetTime())
end

local function WRP_OverpowerUrgent()
    if not WRP_Ready(7384) then return false end
    if WRP_TargetIsCasting() then return true end
    local remaining = WRP_TasteForBloodRemaining()
    return remaining > 0 and remaining <= 3.0
end

local function WRP_ShouldUseOverpower()
    if not WRP_Ready(7384) then return false end
    if WRP_OverpowerUrgent() then return true end
    local remaining = WRP_TasteForBloodRemaining()
    if remaining <= 0 then return true end
    local _, class = UnitClass("target")
    if class and WRP_CASTER_CLASS[class] then return false end
    return true
end

local function WRP_TargetHasImmunity()
    return UnitBuffID("target", 642) ~= nil
        or UnitBuffID("target", 10278) ~= nil
        or UnitBuffID("target", 45438) ~= nil
end

local function WRP_BloodrageFor(requiredRage)
    if WRP_Rage() >= requiredRage then return false end
    if WRP_Ready(2687) then
        WR_Cast(2687, "player")
        return true
    end
    return false
end

local function WRP_SlowRemaining()
    local ham = WRP_DebuffRemaining("target", 1715, "player")
    local howl = WRP_DebuffRemaining("target", 12323, "player")
    return math.max(ham, howl)
end

local function WRP_MortalStrikeNeeded()
    return WRP_DebuffRemaining("target", 47486, "player") <= WR_PvP.MortalWoundsRefresh
end

local function WRP_MandatoryDebuffSoon()
    if WRP_SlowRemaining() <= WR_PvP.HamstringRefresh then return true end
    if WRP_DebuffRemaining("target", 47465, "player") <= WR_PvP.RendRefresh then return true end
    if WRP_MortalStrikeNeeded() then return true end
    return false
end

local function WRP_CanSpendExecute()
    local executeCost = math.max(WRP_RageCost(47471), 10)
    local msSoon = WRP_CooldownRemaining(47486) <= 1.5
    if msSoon and WRP_Rage() < executeCost + WR_PvP.MortalStrikeReserve then
        return false
    end
    return WRP_Rage() >= executeCost
end

function WR_PvPCoreTick()
    if not WR_Engine or WR_Engine.CurrentRotation ~= "Arms_DPS_DarhangeR" then return false end
    if WR_AutoRotation == false then return false end
    if WR_Engine.DefensiveActive and not WR_Engine.DefensiveManualOverride then return false end
    if not WRP_ValidEnemy("target") or not UnitIsPlayer("target") then return false end
    -- Do not attack a Cycloned target. Immunities fall through to Shattering Throw.
    if UnitDebuffID("target", 33786) then return true end
    if WRP_TargetHasImmunity() then return false end

    -- Offensive Arms always returns to Battle Stance. Defensive lock is handled before this layer.
    if GetShapeshiftForm() ~= 1 then
        return WR_Cast(2457, "player")
    end
    if not WRP_InMelee("target") then return false end

    local rage = WRP_Rage()
    local msCost = math.max(WRP_RageCost(47486), 30)
    local hamCost = math.max(WRP_RageCost(1715), 10)
    local rendCost = math.max(WRP_RageCost(47465), 10)
    local msRemain = WRP_DebuffRemaining("target", 47486, "player")
    local slowRemain = WRP_SlowRemaining()
    local rendRemain = WRP_DebuffRemaining("target", 47465, "player")

    -- First close true gaps on a fresh, swapped or dispelled target.
    if msRemain <= 0 and WRP_Ready(47486) then
        if WRP_BloodrageFor(msCost) then return true end
        if WRP_Rage() >= msCost then return WR_Cast(47486, "target") end
    end
    if WR_ArmsAutoHamstring ~= false and slowRemain <= 0 and WRP_Ready(1715) then
        if WRP_BloodrageFor(hamCost) then return true end
        if WRP_Rage() >= hamCost then return WR_Cast(1715, "target") end
    end
    if WR_ArmsAutoRend ~= false and rendRemain <= 0 and WRP_Ready(47465) then
        if WRP_BloodrageFor(rendCost) then return true end
        if WRP_Rage() >= rendCost then return WR_Cast(47465, "target") end
    end

    -- Unrelenting Assault is time-critical once all mandatory debuffs are present.
    if WRP_TargetIsCasting() and WRP_Ready(7384) then
        return WR_Cast(7384, "target")
    end

    -- Refresh early enough to cover one full GCD plus private-server latency.
    if msRemain <= WR_PvP.MortalWoundsRefresh and WRP_Ready(47486) then
        if WRP_BloodrageFor(msCost) then return true end
        if WRP_Rage() >= msCost then return WR_Cast(47486, "target") end
    end
    if WR_ArmsAutoHamstring ~= false and slowRemain <= WR_PvP.HamstringRefresh and WRP_Ready(1715) then
        if WRP_BloodrageFor(hamCost) then return true end
        if WRP_Rage() >= hamCost then return WR_Cast(1715, "target") end
    end
    if WR_ArmsAutoRend ~= false and rendRemain <= WR_PvP.RendRefresh and WRP_Ready(47465) then
        if WRP_BloodrageFor(rendCost) then return true end
        if WRP_Rage() >= rendCost then return WR_Cast(47465, "target") end
    end

    -- With all mandatory debuffs safe, Mortal Strike remains the main pressure hit.
    if WRP_Ready(47486) and WRP_Rage() >= msCost then
        return WR_Cast(47486, "target")
    end

    -- Spend an expiring Taste for Blood proc before Execute so it is never lost.
    if WRP_OverpowerUrgent() then
        return WR_Cast(7384, "target")
    end

    -- Sudden Death/execute range with full target, range and rage validation.
    local suddenDeath = UnitBuffID("player", 52437) ~= nil
    local targetHP = UnitHealthMax("target") > 0
        and (100 * UnitHealth("target") / UnitHealthMax("target")) or 100
    if (suddenDeath or targetHP <= 20) and WRP_Ready(47471) and WRP_CanSpendExecute() then
        return WR_Cast(47471, "target")
    end

    -- Ordinary Overpower is the final core filler; caster procs may be held for UA.
    if WRP_ShouldUseOverpower() then
        return WR_Cast(7384, "target")
    end

    return false
end

-- Safe replacements for the imported tests. These also protect fallback execution.
if WR_Engine and WR_Engine.Abilities then
    local execute = WR_Engine.Abilities["A:Execute"]
    if execute then
        execute.test = function()
            if WR_MSPooling() then return false end
            if not WRP_ValidEnemy("target") or not WRP_InMelee("target") then return false end
            local suddenDeath = UnitBuffID("player", 52437) ~= nil
            local maxHP = UnitHealthMax("target") or 0
            local hp = maxHP > 0 and (100 * UnitHealth("target") / maxHP) or 100
            return (suddenDeath or hp <= 20) and WRP_Ready(47471) and WRP_CanSpendExecute()
        end
    end

    local overpower = WR_Engine.Abilities["A:Overpower"]
    if overpower then
        overpower.test = function()
            if WR_MSPooling() then return false end
            return WRP_ValidEnemy("target") and WRP_InMelee("target")
                and WRP_ShouldUseOverpower()
        end
    end

    local function safeHeroicStrike()
        if not WRP_ValidEnemy("target") or not WRP_InMelee("target") then return false end
        if WRP_Rage() < WR_PvP.HeroicStrikeRage then return false end
        if WRP_CooldownRemaining(47486) <= 2.0 then return false end
        if WRP_MandatoryDebuffSoon() then return false end
        return WRP_Ready(47450)
    end
    if WR_Engine.Abilities["F:Heroic Strike"] then
        WR_Engine.Abilities["F:Heroic Strike"].test = safeHeroicStrike
    end
    if WR_Engine.Abilities["A:Heroic Strike"] then
        WR_Engine.Abilities["A:Heroic Strike"].test = safeHeroicStrike
    end

    local rend = WR_Engine.Abilities["A:Rend"]
    if rend then
        rend.test = function()
            if WR_ArmsAutoRend == false then return false end
            return WRP_ValidEnemy("target") and WRP_InMelee("target")
                and WRP_DebuffRemaining("target", 47465, "player") <= WR_PvP.RendRefresh
                and WRP_Ready(47465) and WRP_Rage() >= WRP_RageCost(47465)
        end
    end

    local hamstring = WR_Engine.Abilities["A:Hamstring"]
    if hamstring then
        hamstring.test = function()
            if WR_ArmsAutoHamstring == false then return false end
            return WRP_ValidEnemy("target") and UnitIsPlayer("target") and WRP_InMelee("target")
                and WRP_SlowRemaining() <= WR_PvP.HamstringRefresh
                and WRP_Ready(1715) and WRP_Rage() >= WRP_RageCost(1715)
        end
    end
end

-- Corrected base loop: no `condition and nil or ability` nil-ternary bug.
function WR_PvPBaseTick()
    WR_GearCaptureTick()
    if not WR_Engine or not WR_Engine.Enabled then return false end
    local _, playerClass = UnitClass("player")
    if playerClass ~= "WARRIOR" or UnitIsDeadOrGhost("player") or IsMounted() or UnitInVehicle("player") then
        return false
    end
    local rotation = WR_Engine.Rotations[WR_Engine.CurrentRotation]
    if not rotation then return false end

    if WR_Engine.CurrentRotation == "Arms_DPS_DarhangeR" then
        local ok, handled = pcall(WR_PvPCoreTick)
        if ok and handled then return true end
        if not ok then
            WR_Engine.LastErrorAction = "Arms PvP Core"
            WR_Engine.LastErrorText = tostring(handled)
            WR_Engine.LastErrorAt = GetTime()
        end
    end

    for _, abilityName in ipairs(rotation) do
        local paused = (WR_AutoRotation == false) and not WR_PAUSE_ALLOW[abilityName]
        local marker = abilityName == "-- Interrupt Engine:A --" or abilityName == "A:Battle Stance"
        if not paused and not marker then
            local ability = WR_Engine.Abilities[abilityName]
            if ability then
                local ok, shouldCast = pcall(ability.test)
                if not ok then
                    WR_Engine.LastErrorAction = abilityName
                    WR_Engine.LastErrorText = tostring(shouldCast)
                    WR_Engine.LastErrorAt = GetTime()
                elseif shouldCast then
                    if ability.id and ability.id > 0 then
                        local unit = ability.target
                        if unit == "Player" then unit = "player"
                        elseif unit == "Target" or unit == "Click" or unit == "Custom" then unit = "target" end
                        WR_Cast(ability.id, unit)
                    end
                    return true
                end
            end
        end
    end
    return false
end

-- Keep the already-verified integrated order: interrupt -> defense -> corrected PvP/base loop.
WR_IntegratedBaseTick = WR_PvPBaseTick
if WR_Engine and WR_IntegratedWarriorTick then
    WR_Engine.Tick = WR_IntegratedWarriorTick
end
)WR_PVP_LUA";

}
