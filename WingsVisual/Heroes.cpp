#include "Heroes.h"
#include "WarlockScripts.h"
#include "WarriorScripts.h"

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

    static HWND hHeroWarriorRow = NULL;
    static HWND hHeroWarriorEnableCheck = NULL;
    static HWND hHeroWarriorFuryCheck = NULL;
    static HWND hHeroWarriorArmsCheck = NULL;
    static HWND hHeroWarriorProtoCheck = NULL;
    static HWND hHeroWarriorArmsRendCheck = NULL;
    static HWND hHeroWarriorArmsHamstringCheck = NULL;
    static HWND hHeroWarriorArmsPummelCheck = NULL;
    static HWND hHeroWarriorArmsReflectCheck = NULL;
    static HWND hHeroWarriorArmsRageCheck = NULL;
    static HWND hHeroWarriorAutoRotationCheck = NULL;
    static HWND hHeroWarriorAutoRotationKey = NULL;
    static HWND hHeroWarriorDefensiveCheck = NULL;
    static HWND hHeroWarriorGearTitle = NULL;
    static HWND hHeroWarriorCaptureArms = NULL;
    static HWND hHeroWarriorCaptureReflect = NULL;
    static HWND hHeroWarriorInfo = NULL;

    static bool g_moduleVisible = false;
    static bool g_warlockExpanded = false;
    static bool g_warlockEnabled = false;
    static bool g_warlockKickAll = false;
    static bool g_warlockInstant = true;
    static bool g_warlockTremorEnabled = false;
    static bool g_warlockInitPending = false;
    static bool g_warlockTremorInitPending = false;

    static bool g_warriorExpanded = false;
    static bool g_warriorEnabled = false;
    static int g_warriorMode = 0; // 0 Fury, 1 Arms, 2 Protection
    static bool g_warriorInitPending = false;
    static bool g_warriorArmsAutoRend = true;
    static bool g_warriorArmsAutoHamstring = true;
    static bool g_warriorArmsAutoPummel = true;
    static bool g_warriorArmsAutoReflect = true;
    static bool g_warriorArmsBerserkerRage = true;
    static bool g_warriorAutoRotation = true;
    static bool g_warriorAutoDefensives = true;
    static bool g_warriorForceArmsOffense = false;
    static int g_warriorGearCaptureMode = 0; // 1 = 2H weapon, 2 = 1H + Shield set
    static bool g_warriorRuntimeReady = false;

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

    static volatile LONG g_warriorRotationBindState = TREMOR_BIND_IDLE;
    static volatile bool g_warriorRotationBindingKey = false;
    static volatile int g_warriorRotationHotkey = 0;
    static volatile int g_warriorRotationPendingHotkey = 0;
    static bool g_warriorRotationHotkeyWasDown = false;

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

    // Warlock and Tremor Lua sources were moved unchanged to
    // WarlockScripts.cpp. Runtime behavior remains in Heroes.cpp for stage 1.

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

    static void DrawGearCapture(LPDRAWITEMSTRUCT d, const char* label, bool selected) {
        RECT rc = d->rcItem;
        HBRUSH bg = CreateSolidBrush(selected ? g_theme.goldDark : g_theme.box);
        FillRect(d->hDC, &rc, bg);
        DeleteObject(bg);
        HBRUSH frame = CreateSolidBrush(g_theme.gold);
        FrameRect(d->hDC, &rc, frame);
        DeleteObject(frame);
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, selected ? g_theme.gold : g_theme.text);
        HFONT oldFont = (HFONT)SelectObject(d->hDC, hFontNorm);
        DrawTextA(d->hDC, label, -1, &rc, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SelectObject(d->hDC, oldFont);
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

    static void GetWarriorRotationHotkeyName(char* output, size_t outputSize) {
        if (!output || outputSize == 0) return;
        if (g_warriorRotationBindingKey) {
            strcpy_s(output, outputSize, "Press key... ESC cancel, DEL clear");
            return;
        }
        const int key = g_warriorRotationHotkey;
        if (!key) {
            strcpy_s(output, outputSize, "Rotation hotkey: [Not set]");
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
        sprintf_s(output, outputSize, "Rotation hotkey: [%s]", keyName);
    }

    static void DrawWarriorRotationKeyBind(LPDRAWITEMSTRUCT d) {
        RECT rc = d->rcItem;
        HBRUSH bg = CreateSolidBrush(g_warriorRotationBindingKey ? g_theme.goldDark : g_theme.box);
        FillRect(d->hDC, &rc, bg);
        DeleteObject(bg);
        HBRUSH frame = CreateSolidBrush(g_theme.gold);
        FrameRect(d->hDC, &rc, frame);
        DeleteObject(frame);
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, g_warriorRotationBindingKey ? g_theme.gold : g_theme.text);
        HFONT oldFont = (HFONT)SelectObject(d->hDC, hFontNorm);
        char text[128] = { 0 };
        GetWarriorRotationHotkeyName(text, sizeof(text));
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

    static void LoadWarriorRotationHotkey() {
        if (!g_configPath[0]) InitConfigPath();
        int key = GetPrivateProfileIntA("Warrior", "AutoRotationHotkey", 0, g_configPath);
        if (key >= 0 && key < 256 && key != VK_END && key != VK_OEM_7) g_warriorRotationHotkey = key;
    }

    static void SaveWarriorRotationHotkey() {
        if (!g_configPath[0]) InitConfigPath();
        char value[16] = { 0 };
        sprintf_s(value, "%d", (int)g_warriorRotationHotkey);
        WritePrivateProfileStringA("Warrior", "AutoRotationHotkey", value, g_configPath);
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
            if (selectedKey != 0 && selectedKey == g_warriorRotationHotkey) {
                g_warriorRotationHotkey = 0;
                SaveWarriorRotationHotkey();
                if (hHeroWarriorAutoRotationKey) InvalidateRect(hHeroWarriorAutoRotationKey, NULL, TRUE);
            }
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

        if (g_warriorRotationBindingKey) {
            g_tremorHotkeyWasDown = false;
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

    static void FinishWarriorRotationBinding(int selectedKey, bool cancel) {
        if (!cancel) {
            if (selectedKey != 0 && selectedKey == g_tremorHotkey) {
                g_tremorHotkey = 0;
                SaveTremorHotkey();
                if (hHeroWarlockTremorKey) InvalidateRect(hHeroWarlockTremorKey, NULL, TRUE);
            }
            g_warriorRotationHotkey = selectedKey;
            SaveWarriorRotationHotkey();
        }
        g_warriorRotationPendingHotkey = 0;
        g_warriorRotationBindingKey = false;
        InterlockedExchange(&g_warriorRotationBindState, TREMOR_BIND_IDLE);
        g_warriorRotationHotkeyWasDown = false;
        if (hHeroWarriorAutoRotationKey) InvalidateRect(hHeroWarriorAutoRotationKey, NULL, TRUE);
    }

    static void PollWarriorRotationHotkeyGameThread() {
        static bool previousDown[256] = { false };
        static DWORD allReleasedSince = 0;
        const DWORD now = (DWORD)GetTickCount64();
        const LONG state = InterlockedCompareExchange(&g_warriorRotationBindState, 0, 0);

        if (state == TREMOR_BIND_WAIT_RELEASE) {
            if (IsAnyTremorBindKeyDown()) allReleasedSince = 0;
            else {
                if (!allReleasedSince) allReleasedSince = now;
                if ((now - allReleasedSince) >= 120) {
                    for (int key = 0; key < 256; ++key) previousDown[key] = false;
                    InterlockedExchange(&g_warriorRotationBindState, TREMOR_BIND_CAPTURE);
                }
            }
            return;
        }

        if (state == TREMOR_BIND_CAPTURE) {
            for (int key = 1; key < 256; ++key) {
                const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
                if (down && !previousDown[key]) {
                    if (key == VK_ESCAPE) g_warriorRotationPendingHotkey = -1;
                    else if (key == VK_BACK || key == VK_DELETE) g_warriorRotationPendingHotkey = 0;
                    else if (IsSupportedTremorBindKey(key)) g_warriorRotationPendingHotkey = key;
                    else continue;
                    InterlockedExchange(&g_warriorRotationBindState, TREMOR_BIND_WAIT_SELECTED_RELEASE);
                    previousDown[key] = true;
                    return;
                }
                previousDown[key] = down;
            }
            return;
        }

        if (state == TREMOR_BIND_WAIT_SELECTED_RELEASE) {
            if (!IsAnyTremorBindKeyDown()) {
                const int pending = g_warriorRotationPendingHotkey;
                FinishWarriorRotationBinding(pending < 0 ? g_warriorRotationHotkey : pending, pending < 0);
                allReleasedSince = 0;
            }
            return;
        }

        if (g_tremorBindingKey) {
            g_warriorRotationHotkeyWasDown = false;
            return;
        }
        const int key = g_warriorRotationHotkey;
        if (!key) {
            g_warriorRotationHotkeyWasDown = false;
            return;
        }
        const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        if (down && !g_warriorRotationHotkeyWasDown) {
            g_warriorAutoRotation = !g_warriorAutoRotation;
            if (g_warriorAutoRotation) {
                ExecuteLuaNow("WR_AutoRotation=true; if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage('|cFFFFD10A[YamiYami]|r Auto Rotation: |cff33ff33ON|r') end");
            }
            else {
                ExecuteLuaNow("WR_AutoRotation=false; if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage('|cFFFFD10A[YamiYami]|r Auto Rotation: |cffff5555OFF|r') end");
            }
            if (hHeroWarriorInfo) {
                SetWindowTextA(hHeroWarriorInfo, g_warriorAutoRotation
                    ? "Auto rotation ON (hotkey)."
                    : "Auto rotation OFF (hotkey) - interrupt, reflect, rend, hamstring still run.");
            }
            if (hHeroWarriorAutoRotationCheck) InvalidateRect(hHeroWarriorAutoRotationCheck, NULL, TRUE);
        }
        g_warriorRotationHotkeyWasDown = down;
    }


    // Warrior Lua sources were moved unchanged to WarriorScripts.cpp.
    // Runtime state, UI, hotkeys and gear capture remain here for stage 1.

    static void RunHeroScriptsTick() {
        PollTremorHotkeyGameThread();
        PollWarriorRotationHotkeyGameThread();

        if (!g_warriorRuntimeReady) {
            // WR_Engine.Enabled defaults to false inside this Lua, so loading it early
            // only registers the gear-capture tooltip hook and helper functions.
            ExecuteLuaNow(WarriorScripts::RotationInit);
            ExecuteLuaNow(WarriorScripts::AutoDefensives);
            ExecuteLuaNow(WarriorScripts::KickReflectFix);
            ExecuteLuaNow(WarriorScripts::PvPCore);
            g_warriorRuntimeReady = true;
        }
        if (!g_warriorEnabled && g_warriorGearCaptureMode != 0) {
            ExecuteLuaNow("if WR_GearCaptureTick then WR_GearCaptureTick() end");
        }

        if (g_warlockEnabled) {
            if (g_warlockInitPending) {
                ExecuteLuaNow(WarlockScripts::AutoKickInit);
                ExecuteLuaNow("AK_Enabled = true");
                ExecuteLuaNow(g_warlockKickAll ? "AK_KickAll = true" : "AK_KickAll = false");
                ExecuteLuaNow(g_warlockInstant ? "AK_Instant = true" : "AK_Instant = false");
                g_warlockInitPending = false;
            }
            ExecuteLuaNow(WarlockScripts::AutoKickMain);
        }

        if (g_warriorEnabled) {
            if (g_warriorInitPending) {
                ExecuteLuaNow(WarriorScripts::RotationInit);
                const char* mode = g_warriorMode == 1 ? "Arms_DPS_DarhangeR" :
                    g_warriorMode == 2 ? "Proto_Tank_DarhangeR" :
                    "Fury_DPS_DarhangeR";
                char command[128] = { 0 };
                sprintf_s(command, "WR_Engine.CurrentRotation=%c%s%c; WR_Engine.Enabled=true", 39, mode, 39);
                ExecuteLuaNow(command);
                ExecuteLuaNow(WarriorScripts::AutoDefensives);
                ExecuteLuaNow(WarriorScripts::KickReflectFix);
                ExecuteLuaNow(WarriorScripts::PvPCore);
                ExecuteLuaNow(g_warriorArmsAutoRend ? "WR_ArmsAutoRend=true" : "WR_ArmsAutoRend=false");
                ExecuteLuaNow(g_warriorArmsAutoHamstring ? "WR_ArmsAutoHamstring=true" : "WR_ArmsAutoHamstring=false");
                ExecuteLuaNow(g_warriorArmsAutoPummel ? "WR_ArmsAutoPummel=true" : "WR_ArmsAutoPummel=false");
                ExecuteLuaNow(g_warriorArmsAutoReflect ? "WR_ArmsAutoReflect=true" : "WR_ArmsAutoReflect=false");
                ExecuteLuaNow(g_warriorArmsBerserkerRage ? "WR_ArmsBerserkerRage=true" : "WR_ArmsBerserkerRage=false");
                ExecuteLuaNow(g_warriorAutoRotation ? "WR_AutoRotation=true" : "WR_AutoRotation=false");
                ExecuteLuaNow(g_warriorAutoDefensives ? "WR_AutoDefensives=true" : "WR_AutoDefensives=false");
                if (g_warriorForceArmsOffense) {
                    ExecuteLuaNow("WR_AutoRotation=true; if WR_Engine and WR_Engine.DefensiveActive then WR_Engine.DefensiveManualOverride=true; WR_Engine.DefensiveHoldUntil=nil; WR_Engine.ArmsResponse=nil; WR_Engine.ReflectHoldUntil=nil end");
                    g_warriorForceArmsOffense = false;
                }
                if (g_warriorGearCaptureMode == 1) ExecuteLuaNow("if WR_StartArmsCapture then WR_StartArmsCapture() end");
                else if (g_warriorGearCaptureMode == 2) ExecuteLuaNow("if WR_StartReflectCapture then WR_StartReflectCapture() end");
                g_warriorInitPending = false;
            }
            ExecuteLuaNow("if WR_Engine and WR_Engine.Tick then WR_Engine.Tick() end");
        }

        if (g_warlockTremorEnabled) {
            if (g_warlockTremorInitPending) {
                ExecuteLuaNow(WarlockScripts::TremorInit);
                ExecuteLuaNow("TB_Enabled=true; TB_NativeAllowed=false");
                g_warlockTremorInitPending = false;
                ResetNativeTremorSlots(false);
                g_tbNextNativeScan = 0;
            }
            UpdateNativeTremorDetector();
        }
    }


    static void RepositionHeroControls() {
        if (!hMenu) return;

        const int rootState = g_moduleVisible ? SW_SHOW : SW_HIDE;
        ShowWindow(hHeroWarlockRow, rootState);
        ShowWindow(hHeroWarriorRow, rootState);

        int y = 134;
        MoveWindow(hHeroWarlockRow, 24, y, 404, 28, TRUE);
        y += 38;
        const int warlockState = (g_moduleVisible && g_warlockExpanded) ? SW_SHOW : SW_HIDE;
        ShowWindow(hHeroWarlockCheck, warlockState);
        ShowWindow(hHeroWarlockKickAllCheck, warlockState);
        ShowWindow(hHeroWarlockInstantCheck, warlockState);
        ShowWindow(hHeroWarlockTremorCheck, warlockState);
        ShowWindow(hHeroWarlockTremorKey, warlockState);
        ShowWindow(hHeroWarlockTitle, warlockState);
        ShowWindow(hHeroWarlockInfo, warlockState);
        if (g_warlockExpanded) {
            MoveWindow(hHeroWarlockCheck, 40, y, 388, 24, TRUE);
            MoveWindow(hHeroWarlockTitle, 40, y + 30, 388, 22, TRUE);
            MoveWindow(hHeroWarlockKickAllCheck, 40, y + 58, 388, 24, TRUE);
            MoveWindow(hHeroWarlockInstantCheck, 40, y + 88, 388, 24, TRUE);
            MoveWindow(hHeroWarlockTremorCheck, 40, y + 118, 388, 24, TRUE);
            MoveWindow(hHeroWarlockTremorKey, 40, y + 148, 388, 26, TRUE);
            MoveWindow(hHeroWarlockInfo, 40, y + 182, 388, 22, TRUE);
            y += 220;
        }

        MoveWindow(hHeroWarriorRow, 24, y, 404, 28, TRUE);
        y += 38;
        const int warriorState = (g_moduleVisible && g_warriorExpanded) ? SW_SHOW : SW_HIDE;
        ShowWindow(hHeroWarriorEnableCheck, warriorState);
        ShowWindow(hHeroWarriorFuryCheck, warriorState);
        ShowWindow(hHeroWarriorArmsCheck, warriorState);
        ShowWindow(hHeroWarriorProtoCheck, warriorState);
        ShowWindow(hHeroWarriorAutoRotationCheck, warriorState);
        ShowWindow(hHeroWarriorAutoRotationKey, warriorState);
        ShowWindow(hHeroWarriorDefensiveCheck, warriorState);
        const int armsOptionsState = (g_moduleVisible && g_warriorExpanded) ? SW_SHOW : SW_HIDE;
        ShowWindow(hHeroWarriorArmsRendCheck, armsOptionsState);
        ShowWindow(hHeroWarriorArmsHamstringCheck, armsOptionsState);
        ShowWindow(hHeroWarriorArmsPummelCheck, armsOptionsState);
        ShowWindow(hHeroWarriorArmsReflectCheck, armsOptionsState);
        ShowWindow(hHeroWarriorArmsRageCheck, armsOptionsState);
        ShowWindow(hHeroWarriorGearTitle, armsOptionsState);
        ShowWindow(hHeroWarriorCaptureArms, armsOptionsState);
        ShowWindow(hHeroWarriorCaptureReflect, armsOptionsState);
        ShowWindow(hHeroWarriorInfo, warriorState);
        if (g_warriorExpanded) {
            MoveWindow(hHeroWarriorEnableCheck, 40, y, 200, 24, TRUE);
            MoveWindow(hHeroWarriorFuryCheck, 40, y + 30, 200, 24, TRUE);
            MoveWindow(hHeroWarriorArmsCheck, 40, y + 60, 200, 24, TRUE);
            MoveWindow(hHeroWarriorProtoCheck, 40, y + 90, 200, 24, TRUE);
            MoveWindow(hHeroWarriorAutoRotationCheck, 40, y + 120, 200, 24, TRUE);
            MoveWindow(hHeroWarriorAutoRotationKey, 40, y + 150, 200, 24, TRUE);
            // Keep settings in fixed positions for every spec so controls never flicker or disappear.
            MoveWindow(hHeroWarriorArmsRendCheck, 248, y, 180, 24, TRUE);
            MoveWindow(hHeroWarriorArmsHamstringCheck, 248, y + 30, 180, 24, TRUE);
            MoveWindow(hHeroWarriorArmsPummelCheck, 248, y + 60, 180, 24, TRUE);
            MoveWindow(hHeroWarriorArmsReflectCheck, 248, y + 90, 180, 24, TRUE);
            MoveWindow(hHeroWarriorArmsRageCheck, 248, y + 120, 180, 24, TRUE);
            MoveWindow(hHeroWarriorDefensiveCheck, 248, y + 150, 180, 24, TRUE);
            MoveWindow(hHeroWarriorInfo, 40, y + 180, 470, 34, TRUE);
            MoveWindow(hHeroWarriorGearTitle, 40, y + 218, 470, 20, TRUE);
            MoveWindow(hHeroWarriorCaptureArms, 40, y + 244, 180, 24, TRUE);
            MoveWindow(hHeroWarriorCaptureReflect, 234, y + 244, 180, 24, TRUE);
        }
    }

    static void SelectWarriorMode(int mode) {
        g_warriorMode = mode;
        if (mode == 1) {
            g_warriorAutoRotation = true;
            g_warriorForceArmsOffense = true;
            QueueLua("WR_AutoRotation=true");
            SetWindowTextA(hHeroWarriorInfo, "Arms selected - leaving defense, Auto Rotation ON.");
            InvalidateRect(hHeroWarriorAutoRotationCheck, NULL, TRUE);
        }
        if (g_warriorEnabled) g_warriorInitPending = true;
        RepositionHeroControls();
        InvalidateRect(hHeroWarriorFuryCheck, NULL, TRUE);
        InvalidateRect(hHeroWarriorArmsCheck, NULL, TRUE);
        InvalidateRect(hHeroWarriorProtoCheck, NULL, TRUE);
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

    hHeroWarriorRow = CreateWindowA(
        "BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        24, 390, 404, 28, hMenu, (HMENU)300, hSelf, NULL);
    hHeroWarriorEnableCheck = CreateCheck(301, 40, 428, 388);
    hHeroWarriorFuryCheck = CreateCheck(302, 40, 458, 388);
    hHeroWarriorArmsCheck = CreateCheck(303, 40, 488, 388);
    hHeroWarriorProtoCheck = CreateCheck(304, 40, 518, 388);
    hHeroWarriorArmsRendCheck = CreateCheck(305, 40, 548, 388);
    hHeroWarriorArmsHamstringCheck = CreateCheck(306, 40, 578, 388);
    hHeroWarriorArmsPummelCheck = CreateCheck(307, 40, 608, 388);
    hHeroWarriorArmsReflectCheck = CreateCheck(308, 40, 638, 388);
    hHeroWarriorArmsRageCheck = CreateCheck(309, 40, 668, 388);
    hHeroWarriorAutoRotationCheck = CreateCheck(313, 40, 698, 388);
    hHeroWarriorAutoRotationKey = CreateWindowA(
        "BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        40, 728, 200, 24, hMenu, (HMENU)315, hSelf, NULL);
    hHeroWarriorDefensiveCheck = CreateCheck(314, 40, 728, 388);
    hHeroWarriorGearTitle = CreateLabel("Weapons: click a setup button, then hover the item. 1H and shield work in either order.", 40, 698, 470, hFontNorm);
    hHeroWarriorCaptureArms = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        40, 724, 180, 24, hMenu, (HMENU)310, hSelf, NULL);
    hHeroWarriorCaptureReflect = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        234, 724, 180, 24, hMenu, (HMENU)311, hSelf, NULL);
    hHeroWarriorInfo = CreateLabel(
        "DarhangeR profile: Fury / Arms / Protection. Warrior only.",
        40, 550, 388, hFontNorm);

    InitConfigPath();
    LoadTremorHotkey();
    LoadWarriorRotationHotkey();
    HeroesSetVisible(false);
    return true;
}

void HeroesTick() {
    RunHeroScriptsTick();
}

void HeroesSetVisible(bool visible) {
    g_moduleVisible = visible;
    ShowWindow(hLblHeroes, visible ? SW_SHOW : SW_HIDE);
    RepositionHeroControls();
    if (hHeroWarlockRow) InvalidateRect(hHeroWarlockRow, NULL, TRUE);
    if (hHeroWarriorRow) InvalidateRect(hHeroWarriorRow, NULL, TRUE);
}

bool HeroesDrawItem(LPDRAWITEMSTRUCT d) {
    if (!d) return false;
    switch (d->CtlID) {
    case 200: DrawHeroRow(d, "Warlock", g_warlockExpanded); return true;
    case 300: DrawHeroRow(d, "Warrior", g_warriorExpanded); return true;
    case 301: DrawCheck(d, "Enable Warrior rotation", g_warriorEnabled, false); return true;
    case 302: DrawCheck(d, "Fury DPS", g_warriorMode == 0, false); return true;
    case 303: DrawCheck(d, "Arms DPS", g_warriorMode == 1, false); return true;
    case 304: DrawCheck(d, "Protection Tank", g_warriorMode == 2, false); return true;
    case 305: DrawCheck(d, "Auto Rend", g_warriorArmsAutoRend, false); return true;
    case 306: DrawCheck(d, "Auto Hamstring", g_warriorArmsAutoHamstring, false); return true;
    case 307: DrawCheck(d, "Auto Interrupt", g_warriorArmsAutoPummel, false); return true;
    case 308: DrawCheck(d, "Auto Reflect", g_warriorArmsAutoReflect, false); return true;
    case 309: DrawCheck(d, "Berserker Rage", g_warriorArmsBerserkerRage, false); return true;
    case 313: DrawCheck(d, "Auto Rotation", g_warriorAutoRotation, false); return true;
    case 314: DrawCheck(d, "Auto Defensives", g_warriorAutoDefensives, false); return true;
    case 315: DrawWarriorRotationKeyBind(d); return true;
    case 310: DrawGearCapture(d, "2H weapon", g_warriorGearCaptureMode == 1); return true;
    case 311: DrawGearCapture(d, "1H + Shield", g_warriorGearCaptureMode == 2); return true;
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
        if (g_warlockExpanded) g_warriorExpanded = false;
        RepositionHeroControls();
        InvalidateRect(hHeroWarlockRow, NULL, TRUE);
        InvalidateRect(hHeroWarriorRow, NULL, TRUE);
        return true;
    case 300:
        g_warriorExpanded = !g_warriorExpanded;
        if (g_warriorExpanded) g_warlockExpanded = false;
        RepositionHeroControls();
        InvalidateRect(hHeroWarlockRow, NULL, TRUE);
        InvalidateRect(hHeroWarriorRow, NULL, TRUE);
        return true;
    case 301:
        g_warriorEnabled = !g_warriorEnabled;
        g_warriorInitPending = g_warriorEnabled;
        if (!g_warriorEnabled) QueueLua("if WR_Engine then WR_Engine.Enabled=false end");
        InvalidateRect(hHeroWarriorEnableCheck, NULL, TRUE);
        return true;
    case 302:
        SelectWarriorMode(0);
        return true;
    case 303:
        SelectWarriorMode(1);
        return true;
    case 304:
        SelectWarriorMode(2);
        return true;
    case 305:
        g_warriorArmsAutoRend = !g_warriorArmsAutoRend;
        QueueLua(g_warriorArmsAutoRend ? "WR_ArmsAutoRend=true" : "WR_ArmsAutoRend=false");
        InvalidateRect(hHeroWarriorArmsRendCheck, NULL, TRUE);
        return true;
    case 306:
        g_warriorArmsAutoHamstring = !g_warriorArmsAutoHamstring;
        QueueLua(g_warriorArmsAutoHamstring ? "WR_ArmsAutoHamstring=true" : "WR_ArmsAutoHamstring=false");
        InvalidateRect(hHeroWarriorArmsHamstringCheck, NULL, TRUE);
        return true;
    case 307:
        g_warriorArmsAutoPummel = !g_warriorArmsAutoPummel;
        QueueLua(g_warriorArmsAutoPummel ? "WR_ArmsAutoPummel=true" : "WR_ArmsAutoPummel=false");
        InvalidateRect(hHeroWarriorArmsPummelCheck, NULL, TRUE);
        return true;
    case 308:
        g_warriorArmsAutoReflect = !g_warriorArmsAutoReflect;
        QueueLua(g_warriorArmsAutoReflect ? "WR_ArmsAutoReflect=true" : "WR_ArmsAutoReflect=false");
        InvalidateRect(hHeroWarriorArmsReflectCheck, NULL, TRUE);
        return true;
    case 309:
        g_warriorArmsBerserkerRage = !g_warriorArmsBerserkerRage;
        QueueLua(g_warriorArmsBerserkerRage ? "WR_ArmsBerserkerRage=true" : "WR_ArmsBerserkerRage=false");
        InvalidateRect(hHeroWarriorArmsRageCheck, NULL, TRUE);
        return true;
    case 313:
        g_warriorAutoRotation = !g_warriorAutoRotation;
        QueueLua(g_warriorAutoRotation ? "WR_AutoRotation=true" : "WR_AutoRotation=false");
        SetWindowTextA(hHeroWarriorInfo, g_warriorAutoRotation ? "Auto rotation ON." : "Auto rotation OFF - interrupt, reflect, rend, hamstring still run.");
        InvalidateRect(hHeroWarriorAutoRotationCheck, NULL, TRUE);
        return true;
    case 315:
        g_tremorBindingKey = false;
        InterlockedExchange(&g_tremorBindState, TREMOR_BIND_IDLE);
        g_warriorRotationPendingHotkey = 0;
        g_warriorRotationBindingKey = true;
        InterlockedExchange(&g_warriorRotationBindState, TREMOR_BIND_WAIT_RELEASE);
        InvalidateRect(hHeroWarriorAutoRotationKey, NULL, TRUE);
        return true;
    case 314:
        g_warriorAutoDefensives = !g_warriorAutoDefensives;
        QueueLua(g_warriorAutoDefensives ? "WR_AutoDefensives=true" : "WR_AutoDefensives=false");
        SetWindowTextA(hHeroWarriorInfo, g_warriorAutoDefensives
            ? "Auto Defensives ON: Last Stand, Shield Wall and Shield Block at low HP."
            : "Auto Defensives OFF: returning to 2H and Battle Stance.");
        InvalidateRect(hHeroWarriorDefensiveCheck, NULL, TRUE);
        return true;
    case 310:
        if (g_warriorGearCaptureMode == 1) {
            g_warriorGearCaptureMode = 0;
            QueueLua("if WR_CancelCapture then WR_CancelCapture() end");
            SetWindowTextA(hHeroWarriorInfo, "Weapon setup closed.");
        }
        else {
            g_warriorGearCaptureMode = 1;
            QueueLua("if WR_StartArmsCapture then WR_StartArmsCapture() end");
            SetWindowTextA(hHeroWarriorInfo, "2H: hover the two-handed weapon.");
        }
        InvalidateRect(hHeroWarriorCaptureArms, NULL, TRUE);
        InvalidateRect(hHeroWarriorCaptureReflect, NULL, TRUE);
        return true;
    case 311:
        if (g_warriorGearCaptureMode == 2) {
            g_warriorGearCaptureMode = 0;
            QueueLua("if WR_CancelCapture then WR_CancelCapture() end");
            SetWindowTextA(hHeroWarriorInfo, "Weapon setup closed.");
        }
        else {
            g_warriorGearCaptureMode = 2;
            QueueLua("if WR_StartReflectCapture then WR_StartReflectCapture() end");
            SetWindowTextA(hHeroWarriorInfo, "1H + Shield: hover the 1H weapon and shield in either order.");
        }
        InvalidateRect(hHeroWarriorCaptureArms, NULL, TRUE);
        InvalidateRect(hHeroWarriorCaptureReflect, NULL, TRUE);
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
        g_warriorRotationBindingKey = false;
        InterlockedExchange(&g_warriorRotationBindState, TREMOR_BIND_IDLE);
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
    if (control != hLblHeroes && control != hHeroWarlockTitle && control != hHeroWarlockInfo && control != hHeroWarriorInfo) return false;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, (control == hHeroWarlockInfo || control == hHeroWarriorInfo) ? g_theme.text : g_theme.gold);
    result = (LRESULT)hbrBack;
    return true;
}

void HeroesShutdown() {
    g_warlockEnabled = false;
    g_warriorEnabled = false;
    g_warriorInitPending = false;
    g_warlockTremorEnabled = false;
    g_tremorBindingKey = false;
    InterlockedExchange(&g_tremorBindState, TREMOR_BIND_IDLE);
    g_warriorRotationBindingKey = false;
    InterlockedExchange(&g_warriorRotationBindState, TREMOR_BIND_IDLE);
    g_warriorRotationHotkeyWasDown = false;
    InterlockedExchange(&g_tremorManualActiveUntil, 0);
    ResetNativeTremorSlots(false);
    g_warriorGearCaptureMode = 0;
    QueueLua("WR_GearCaptureTarget=nil; WR_GearCaptureAfter=nil; if WR_GearCaptureFrame then WR_GearCaptureFrame:Hide() end");
    QueueLua("AK_Enabled=false; TB_Enabled=false; TB_NativeAllowed=false; if WR_Engine then WR_Engine.Enabled=false end");
    HeroesSetVisible(false);
}
