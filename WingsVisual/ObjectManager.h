#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>

namespace Off
{
    constexpr uintptr_t CLIENT_CONNECTION = 0x00C79CE0;
    constexpr uintptr_t CURMGR_OFFSET = 0x2ED0;
    constexpr uintptr_t LOCAL_GUID = 0xC0;
    constexpr uintptr_t FIRST_OBJECT = 0xAC;
    constexpr uintptr_t NEXT_OBJECT = 0x3C;

    constexpr uintptr_t OBJ_TYPE = 0x14;
    constexpr uintptr_t OBJ_GUID = 0x30;
    constexpr uintptr_t OBJ_DESCRIPTORS = 0x08;

    constexpr uintptr_t POS_X = 0x798;
    constexpr uintptr_t POS_Y = 0x79C;
    constexpr uintptr_t POS_Z = 0x7A0;
    constexpr uintptr_t ROTATION = 0x7A8;

    // Statically verified in this exact Wow.exe (SHA-256 6179106f...):
    // CGObject_C::SetModel at 0x00743730 reads/writes [this + 0xB4].
    constexpr uintptr_t MODEL_PTR = 0xB4;

    constexpr uint32_t UNIT_FIELD_HEALTH = 0x18;
    constexpr uint32_t UNIT_FIELD_MAXHEALTH = 0x20;
    constexpr uint32_t UNIT_FIELD_LEVEL = 0x36;
    constexpr uint32_t UNIT_FIELD_FACTIONTEMPLATE = 0x37;
    constexpr uint32_t UNIT_FIELD_FLAGS = 0x3B;
    constexpr uint32_t UNIT_DYNAMIC_FLAGS = 0x4F;
    constexpr uint32_t UNIT_FIELD_BYTES_0 = 0x5C;

    constexpr uintptr_t NAME_STORE = 0x00C5D938 + 0x8;
    constexpr uintptr_t NAME_MASK_OFFSET = 0x24;
    constexpr uintptr_t NAME_BASE_OFFSET = 0x1C;
    constexpr uintptr_t NAME_STRING_OFFSET = 0x20;

    constexpr uintptr_t UNIT_NAME_PTR1 = 0x964;
    constexpr uintptr_t UNIT_NAME_PTR2 = 0x5C;
}

enum UnitFlags : uint32_t
{
    UNIT_FLAG_NON_ATTACKABLE = 0x00000002,
    UNIT_FLAG_DISABLE_MOVE = 0x00000004,
    UNIT_FLAG_NOT_SELECTABLE = 0x02000000
};

enum WowObjectType : uint8_t
{
    OT_NONE = 0,
    OT_ITEM = 1,
    OT_CONTAINER = 2,
    OT_UNIT = 3,
    OT_PLAYER = 4,
    OT_GAMEOBJECT = 5,
    OT_DYNOBJECT = 6,
    OT_CORPSE = 7
};

enum Reaction : uint8_t
{
    REACT_NEUTRAL = 0,
    REACT_FRIENDLY = 1,
    REACT_HOSTILE = 2
};

struct PlayerInfo
{
    uintptr_t objectBase;
    uintptr_t model;

    uint64_t guid;
    char name[48];

    float x;
    float y;
    float z;

    int32_t health;
    int32_t maxHealth;

    uint32_t unitFlags;
    uint32_t dynFlags;
    uint32_t factionTemplate;

    uint8_t race;
    uint8_t type;
};

constexpr int MAX_PLAYERS = 512;

inline PlayerInfo g_players[MAX_PLAYERS];
inline int g_playerCount = 0;

inline float g_localX = 0.0f;
inline float g_localY = 0.0f;
inline float g_localZ = 0.0f;
inline bool g_localValid = false;

inline uint8_t g_localRace = 0;
inline int g_localGroup = 0;

template<typename T>
static inline bool SafeRead(
    uintptr_t address,
    T& output)
{
    if (address < 0x10000)
        return false;

    __try
    {
        output =
            *reinterpret_cast<T*>(address);

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static inline bool ReadUnitField(
    uintptr_t object,
    uint32_t fieldIndex,
    uint32_t& output)
{
    uintptr_t descriptors = 0;

    if (!SafeRead(
        object + Off::OBJ_DESCRIPTORS,
        descriptors) ||
        !descriptors)
    {
        return false;
    }

    return SafeRead(
        descriptors +
        static_cast<uintptr_t>(
            fieldIndex) * 4,
        output);
}

static inline bool IsValidEntity(
    uintptr_t object)
{
    if (object < 0x10000 ||
        (object & 0x1))
    {
        return false;
    }

    uint8_t type = OT_NONE;

    if (!SafeRead(
        object + Off::OBJ_TYPE,
        type))
    {
        return false;
    }

    if (type == OT_NONE ||
        type > OT_CORPSE)
    {
        return false;
    }

    uint64_t guid = 0;

    if (!SafeRead(
        object + Off::OBJ_GUID,
        guid) ||
        !guid)
    {
        return false;
    }

    return true;
}

static inline bool CoordsSane(
    float x,
    float y,
    float z)
{
    if (!(x == x) ||
        !(y == y) ||
        !(z == z))
    {
        return false;
    }

    if (fabsf(x) < 0.01f &&
        fabsf(y) < 0.01f &&
        fabsf(z) < 0.01f)
    {
        return false;
    }

    if (fabsf(x) > 100000.0f ||
        fabsf(y) > 100000.0f ||
        fabsf(z) > 100000.0f)
    {
        return false;
    }

    return true;
}

static inline uintptr_t GetObjectManager()
{
    uintptr_t connection = 0;

    if (!SafeRead(
        Off::CLIENT_CONNECTION,
        connection) ||
        !connection)
    {
        return 0;
    }

    uintptr_t manager = 0;

    if (!SafeRead(
        connection +
        Off::CURMGR_OFFSET,
        manager))
    {
        return 0;
    }

    return manager;
}

static inline int RaceGroup(uint8_t race)
{
    switch (race)
    {
    case 1:
    case 3:
    case 4:
    case 7:
    case 11:
        return 1;

    case 2:
    case 5:
    case 6:
    case 8:
    case 10:
        return 2;

    default:
        return 0;
    }
}

static const char* GetPlayerName(
    uint64_t guid)
{
    __try
    {
        const uint32_t shortGuid =
            static_cast<uint32_t>(guid);

        const uint32_t mask =
            *reinterpret_cast<uint32_t*>(
                Off::NAME_STORE +
                Off::NAME_MASK_OFFSET);

        const uint32_t base =
            *reinterpret_cast<uint32_t*>(
                Off::NAME_STORE +
                Off::NAME_BASE_OFFSET);

        if (!base)
            return "";

        const uint32_t slot =
            12 * (mask & shortGuid);

        uint32_t current =
            *reinterpret_cast<uint32_t*>(
                base + slot + 8);

        const uint32_t offset =
            *reinterpret_cast<uint32_t*>(
                base + slot);

        if (current & 0x1)
            return "";

        uint32_t testGuid =
            *reinterpret_cast<uint32_t*>(
                current);

        while (testGuid != shortGuid)
        {
            current =
                *reinterpret_cast<uint32_t*>(
                    current + offset + 4);

            if (current & 0x1)
                return "";

            testGuid =
                *reinterpret_cast<uint32_t*>(
                    current);
        }

        return reinterpret_cast<const char*>(
            current +
            Off::NAME_STRING_OFFSET);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return "";
    }
}

static const char* GetUnitName(
    uintptr_t object)
{
    __try
    {
        const uintptr_t nameObject =
            *reinterpret_cast<uintptr_t*>(
                object +
                Off::UNIT_NAME_PTR1);

        if (!nameObject)
            return "";

        const char* name =
            *reinterpret_cast<const char**>(
                nameObject +
                Off::UNIT_NAME_PTR2);

        return name ? name : "";
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return "";
    }
}

static inline Reaction GetReaction(
    const PlayerInfo& info)
{
    if (info.type == OT_PLAYER)
    {
        const int group =
            RaceGroup(info.race);

        if (group == 0 ||
            g_localGroup == 0)
        {
            return REACT_NEUTRAL;
        }

        return group == g_localGroup
            ? REACT_FRIENDLY
            : REACT_HOSTILE;
    }

    if (info.unitFlags &
        UNIT_FLAG_NON_ATTACKABLE)
    {
        return REACT_FRIENDLY;
    }

    switch (info.factionTemplate)
    {
    case 7:
    case 14:
    case 16:
    case 21:
    case 83:
    case 84:
    case 128:
    case 129:
    case 168:
    case 733:
    case 1888:
    case 1890:
        return REACT_HOSTILE;

    default:
        return REACT_NEUTRAL;
    }
}

static bool FillEntity(
    uintptr_t object,
    uint8_t type,
    PlayerInfo& info)
{
    uint64_t guid = 0;

    if (!SafeRead(
        object + Off::OBJ_GUID,
        guid) ||
        !guid)
    {
        return false;
    }

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    SafeRead(
        object + Off::POS_X,
        x);

    SafeRead(
        object + Off::POS_Y,
        y);

    SafeRead(
        object + Off::POS_Z,
        z);

    int32_t health = 0;
    int32_t maxHealth = 0;

    uintptr_t descriptors = 0;

    if (SafeRead(
        object + Off::OBJ_DESCRIPTORS,
        descriptors) &&
        descriptors)
    {
        SafeRead(
            descriptors +
            Off::UNIT_FIELD_HEALTH * 4,
            health);

        SafeRead(
            descriptors +
            Off::UNIT_FIELD_MAXHEALTH * 4,
            maxHealth);
    }

    uint32_t unitFlags = 0;
    uint32_t dynFlags = 0;
    uint32_t faction = 0;
    uint32_t bytes0 = 0;

    ReadUnitField(
        object,
        Off::UNIT_FIELD_FLAGS,
        unitFlags);

    ReadUnitField(
        object,
        Off::UNIT_DYNAMIC_FLAGS,
        dynFlags);

    ReadUnitField(
        object,
        Off::UNIT_FIELD_FACTIONTEMPLATE,
        faction);

    ReadUnitField(
        object,
        Off::UNIT_FIELD_BYTES_0,
        bytes0);

    uintptr_t model = 0;
    SafeRead(object + Off::MODEL_PTR, model);

    info.objectBase = object;
    info.model = model;
    info.guid = guid;

    info.x = x;
    info.y = y;
    info.z = z;

    info.health = health;
    info.maxHealth = maxHealth;

    info.unitFlags = unitFlags;
    info.dynFlags = dynFlags;
    info.factionTemplate = faction;

    info.race =
        static_cast<uint8_t>(
            bytes0 & 0xFF);

    info.type = type;

    const char* name =
        type == OT_PLAYER
        ? GetPlayerName(guid)
        : GetUnitName(object);

    strncpy_s(
        info.name,
        sizeof(info.name),
        (name && *name)
        ? name
        : (type == OT_PLAYER
            ? "Unknown"
            : "Unit"),
        _TRUNCATE);

    return true;
}

inline int RefreshEntities(
    bool includeUnits,
    bool includePlayers,
    bool includeSelf)
{
    g_playerCount = 0;
    g_localValid = false;

    uintptr_t manager =
        GetObjectManager();

    if (!manager)
        return 0;

    uint64_t localGuid = 0;

    SafeRead(
        manager +
        Off::LOCAL_GUID,
        localGuid);

    uintptr_t object = 0;

    if (!SafeRead(
        manager +
        Off::FIRST_OBJECT,
        object))
    {
        return 0;
    }

    int guard = 0;

    while (object &&
        !(object & 0x1) &&
        g_playerCount <
        MAX_PLAYERS &&
        guard++ < 8192)
    {
        if (IsValidEntity(object))
        {
            uint8_t type = OT_NONE;

            SafeRead(
                object +
                Off::OBJ_TYPE,
                type);

            const bool isPlayer =
                type == OT_PLAYER;

            const bool isUnit =
                type == OT_UNIT;

            if (isPlayer || isUnit)
            {
                uint64_t guid = 0;

                SafeRead(
                    object +
                    Off::OBJ_GUID,
                    guid);

                const bool isSelf =
                    guid == localGuid &&
                    localGuid != 0;

                if (isSelf)
                {
                    SafeRead(
                        object +
                        Off::POS_X,
                        g_localX);

                    SafeRead(
                        object +
                        Off::POS_Y,
                        g_localY);

                    SafeRead(
                        object +
                        Off::POS_Z,
                        g_localZ);

                    g_localValid = true;

                    uint32_t localBytes0 = 0;

                    ReadUnitField(
                        object,
                        Off::UNIT_FIELD_BYTES_0,
                        localBytes0);

                    g_localRace =
                        static_cast<uint8_t>(
                            localBytes0 &
                            0xFF);

                    g_localGroup =
                        RaceGroup(
                            g_localRace);
                }

                const bool take =
                    (isPlayer &&
                        includePlayers) ||
                    (isUnit &&
                        includeUnits);

                if (take &&
                    (includeSelf ||
                        !isSelf))
                {
                    PlayerInfo& destination =
                        g_players[
                            g_playerCount];

                    ZeroMemory(
                        &destination,
                        sizeof(destination));

                    if (FillEntity(
                        object,
                        type,
                        destination))
                    {
                        ++g_playerCount;
                    }
                }
            }
        }

        uintptr_t nextObject = 0;

        if (!SafeRead(
            object +
            Off::NEXT_OBJECT,
            nextObject))
        {
            break;
        }

        if (!nextObject ||
            nextObject == object)
        {
            break;
        }

        object = nextObject;
    }


    return g_playerCount;
}

inline int RefreshPlayerList(
    bool includeSelf = true)
{
    return RefreshEntities(
        false,
        true,
        includeSelf);
}
