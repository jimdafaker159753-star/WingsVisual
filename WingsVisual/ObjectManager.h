#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cmath>

// Включи в 1 для дампа флагов игроков в OutputDebugString (DebugView). Файла на диске нет.
#define ESP_DEBUG_FLAGS 0

// ================= WoW 3.3.5a (build 12340) =================
namespace Off {
    constexpr uintptr_t CLIENT_CONNECTION = 0x00C79CE0;
    constexpr uintptr_t CURMGR_OFFSET = 0x2ED0;
    constexpr uintptr_t LOCAL_GUID = 0xC0;
    constexpr uintptr_t FIRST_OBJECT = 0xAC;
    constexpr uintptr_t NEXT_OBJECT = 0x3C;

    constexpr uintptr_t OBJ_TYPE = 0x14;
    constexpr uintptr_t OBJ_GUID = 0x30;
    constexpr uintptr_t OBJ_DESCRIPTORS = 0x08;

    // X @ 0x798, Y @ 0x79C, Z @ 0x7A0 — своп зашит здесь, в ESP правится не нужно
    constexpr uintptr_t POS_X = 0x798;
    constexpr uintptr_t POS_Y = 0x79C;
    constexpr uintptr_t POS_Z = 0x7A0;
    constexpr uintptr_t ROTATION = 0x7A8;

    // индексы дескрипторных полей (байт = index*4)
    constexpr uint32_t UNIT_FIELD_HEALTH = 0x18;
    constexpr uint32_t UNIT_FIELD_MAXHEALTH = 0x20;
    constexpr uint32_t UNIT_FIELD_LEVEL = 0x36;
    constexpr uint32_t UNIT_FIELD_FLAGS = 0x3B;
    constexpr uint32_t UNIT_DYNAMIC_FLAGS = 0x4F;

    constexpr uintptr_t NAME_STORE = 0x00C5D938 + 0x8;
    constexpr uintptr_t NAME_MASK_OFFSET = 0x24;
    constexpr uintptr_t NAME_BASE_OFFSET = 0x1C;
    constexpr uintptr_t NAME_STRING_OFFSET = 0x20;

    constexpr uintptr_t UNIT_NAME_PTR1 = 0x964;
    constexpr uintptr_t UNIT_NAME_PTR2 = 0x5C;
}

enum UnitFlags : uint32_t {
    UNIT_FLAG_NON_ATTACKABLE = 0x00000002,
    UNIT_FLAG_DISABLE_MOVE = 0x00000004,
    UNIT_FLAG_NOT_SELECTABLE = 0x02000000,
};

enum WowObjectType : uint8_t {
    OT_NONE = 0, OT_ITEM = 1, OT_CONTAINER = 2, OT_UNIT = 3,
    OT_PLAYER = 4, OT_GAMEOBJECT = 5, OT_DYNOBJECT = 6, OT_CORPSE = 7
};

struct PlayerInfo {
    uint64_t guid;
    char     name[48];
    float    x, y, z;
    int32_t  health;
    int32_t  maxHealth;
    uint32_t unitFlags;
    uint32_t dynFlags;
    uint8_t  type;
};

constexpr int MAX_PLAYERS = 512;
inline PlayerInfo g_players[MAX_PLAYERS];
inline int        g_playerCount = 0;

inline float g_localX = 0.0f, g_localY = 0.0f, g_localZ = 0.0f;
inline bool  g_localValid = false;

template<typename T>
static inline bool SafeRead(uintptr_t addr, T& out) {
    if (addr < 0x10000) return false;
    __try { out = *reinterpret_cast<T*>(addr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static inline bool ReadUnitField(uintptr_t obj, uint32_t fieldIndex, uint32_t& out) {
    uintptr_t desc = 0;
    if (!SafeRead(obj + Off::OBJ_DESCRIPTORS, desc) || !desc) return false;
    return SafeRead(desc + fieldIndex * 4, out);
}

static inline bool IsValidEntity(uintptr_t base) {
    if (base < 0x10000) return false;
    if (base & 0x1) return false;
    uint8_t type = 0;
    if (!SafeRead(base + Off::OBJ_TYPE, type)) return false;
    if (type == OT_NONE || type > OT_CORPSE) return false;
    uint64_t guid = 0;
    if (!SafeRead(base + Off::OBJ_GUID, guid) || guid == 0) return false;
    return true;
}

static inline bool IsActivePlayer(uintptr_t base) {
    uint8_t type = 0; uint64_t guid = 0;
    if (!SafeRead(base + Off::OBJ_TYPE, type)) return false;
    if (!SafeRead(base + Off::OBJ_GUID, guid)) return false;
    return (type == OT_PLAYER && guid != 0);
}

static inline bool CoordsSane(float x, float y, float z) {
    if (!(x == x) || !(y == y) || !(z == z)) return false;
    if (fabsf(x) < 0.01f && fabsf(y) < 0.01f && fabsf(z) < 0.01f) return false;
    if (fabsf(x) > 100000.0f || fabsf(y) > 100000.0f) return false;
    return true;
}

static inline uintptr_t GetObjectManager() {
    uintptr_t conn = 0;
    if (!SafeRead(Off::CLIENT_CONNECTION, conn) || !conn) return 0;
    uintptr_t mgr = 0;
    if (!SafeRead(conn + Off::CURMGR_OFFSET, mgr)) return 0;
    return mgr;
}

static const char* GetPlayerName(uint64_t guid) {
    __try {
        uint32_t shortGuid = static_cast<uint32_t>(guid);
        uint32_t mask = *reinterpret_cast<uint32_t*>(Off::NAME_STORE + Off::NAME_MASK_OFFSET);
        uint32_t base = *reinterpret_cast<uint32_t*>(Off::NAME_STORE + Off::NAME_BASE_OFFSET);
        if (!base) return "";
        uint32_t slot = 12 * (mask & shortGuid);
        uint32_t current = *reinterpret_cast<uint32_t*>(base + slot + 8);
        uint32_t offset = *reinterpret_cast<uint32_t*>(base + slot);
        if (current & 0x1) return "";
        uint32_t testGuid = *reinterpret_cast<uint32_t*>(current);
        while (testGuid != shortGuid) {
            current = *reinterpret_cast<uint32_t*>(current + offset + 4);
            if (current & 0x1) return "";
            testGuid = *reinterpret_cast<uint32_t*>(current);
        }
        return reinterpret_cast<const char*>(current + Off::NAME_STRING_OFFSET);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return ""; }
}

static const char* GetUnitName(uintptr_t obj) {
    __try {
        uintptr_t p1 = *reinterpret_cast<uintptr_t*>(obj + Off::UNIT_NAME_PTR1);
        if (!p1) return "";
        const char* name = *reinterpret_cast<const char**>(p1 + Off::UNIT_NAME_PTR2);
        if (!name) return "";
        return name;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return ""; }
}

static bool FillEntity(uintptr_t obj, uint8_t type, PlayerInfo& pi) {
    uint64_t guid = 0;
    if (!SafeRead(obj + Off::OBJ_GUID, guid) || !guid) return false;

    float x = 0, y = 0, z = 0;
    SafeRead(obj + Off::POS_X, x);
    SafeRead(obj + Off::POS_Y, y);
    SafeRead(obj + Off::POS_Z, z);

    int32_t hp = 0, maxHp = 0;
    uintptr_t desc = 0;
    if (SafeRead(obj + Off::OBJ_DESCRIPTORS, desc) && desc) {
        SafeRead(desc + Off::UNIT_FIELD_HEALTH * 4, hp);
        SafeRead(desc + Off::UNIT_FIELD_MAXHEALTH * 4, maxHp);
    }

    uint32_t uflags = 0, dflags = 0;
    ReadUnitField(obj, Off::UNIT_FIELD_FLAGS, uflags);
    ReadUnitField(obj, Off::UNIT_DYNAMIC_FLAGS, dflags);

    pi.guid = guid;
    pi.x = x; pi.y = y; pi.z = z;
    pi.health = hp; pi.maxHealth = maxHp;
    pi.unitFlags = uflags; pi.dynFlags = dflags;
    pi.type = type;

    const char* nm = (type == OT_PLAYER) ? GetPlayerName(guid) : GetUnitName(obj);
    strncpy_s(pi.name, sizeof(pi.name),
        (nm && *nm) ? nm : (type == OT_PLAYER ? "Unknown" : "Unit"),
        _TRUNCATE);
    return true;
}

#if ESP_DEBUG_FLAGS
static void DebugDumpUnit(uintptr_t obj) {
    uint64_t guid = 0; SafeRead(obj + Off::OBJ_GUID, guid);
    uint32_t uf = 0, df = 0;
    ReadUnitField(obj, Off::UNIT_FIELD_FLAGS, uf);
    ReadUnitField(obj, Off::UNIT_DYNAMIC_FLAGS, df);
    float x = 0, y = 0, z = 0;
    SafeRead(obj + Off::POS_X, x); SafeRead(obj + Off::POS_Y, y); SafeRead(obj + Off::POS_Z, z);
    char buf[256];
    _snprintf_s(buf, _TRUNCATE,
        "[ESP] guid=%08X FLAGS=0x%08X DYN=0x%08X pos=(%.1f,%.1f,%.1f)\n",
        (uint32_t)guid, uf, df, x, y, z);
    OutputDebugStringA(buf);
}
#endif

inline int RefreshEntities(bool includeUnits, bool includePlayers, bool includeSelf) {
    g_playerCount = 0;
    g_localValid = false;

    uintptr_t mgr = GetObjectManager();
    if (!mgr) return 0;

    uint64_t localGuid = 0;
    SafeRead(mgr + Off::LOCAL_GUID, localGuid);

    uintptr_t obj = 0;
    if (!SafeRead(mgr + Off::FIRST_OBJECT, obj)) return 0;

    while (obj && (obj & 0x1) == 0 && g_playerCount < MAX_PLAYERS) {
        if (IsValidEntity(obj)) {
            uint8_t type = 0; SafeRead(obj + Off::OBJ_TYPE, type);
            bool isPlayer = (type == OT_PLAYER);
            bool isUnit = (type == OT_UNIT);

            if (isPlayer || isUnit) {
                uint64_t guid = 0; SafeRead(obj + Off::OBJ_GUID, guid);
                bool isSelf = (guid == localGuid && localGuid != 0);

                if (isSelf) {
                    float lx = 0, ly = 0, lz = 0;
                    SafeRead(obj + Off::POS_X, lx);
                    SafeRead(obj + Off::POS_Y, ly);
                    SafeRead(obj + Off::POS_Z, lz);
                    g_localX = lx; g_localY = ly; g_localZ = lz; g_localValid = true;
                }

#if ESP_DEBUG_FLAGS
                if (isPlayer && !isSelf) DebugDumpUnit(obj);
#endif
                bool take = (isPlayer && includePlayers) || (isUnit && includeUnits);
                if (take && (includeSelf || !isSelf)) {
                    if (FillEntity(obj, type, g_players[g_playerCount]))
                        ++g_playerCount;
                }
            }
        }

        uintptr_t next = 0;
        if (!SafeRead(obj + Off::NEXT_OBJECT, next)) break;
        if (next == obj) break;
        obj = next;
    }

    return g_playerCount;
}

inline int RefreshPlayerList(bool includeSelf = true) {
    return RefreshEntities(false, true, includeSelf);
}