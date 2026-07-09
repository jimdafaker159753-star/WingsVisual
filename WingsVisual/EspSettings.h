#pragma once
#include <windows.h>
#include <cstdint>

// ---- ESP-переключатели ----
inline bool  g_espEnabled = false;
inline bool  g_drawNames = false;
inline bool  g_drawHP = false;
inline bool  g_drawDistance = false;
inline bool  g_drawTracers = false;
inline bool  g_drawSelf = false;
inline float g_espDist = 200.0f;

// ---- ESP-фильтры по типу ----
inline bool  g_espShowPlayers = true;   // показывать игроков
inline bool  g_espShowNpcs = true;       // показывать мобов/NPC

// ---- CHAMS ----
inline bool     g_chamsEnabled = false;
inline uint32_t g_chamsColorVisible = 0xFF35C8FF; // ARGB, светло-голубой (в прямой видимости)
inline uint32_t g_chamsColorHidden = 0xFF1E78C8;  // ARGB, темнее (сквозь стены)
inline int      g_chamsMinVerts = 20;
inline int      g_chamsMaxVerts = 2500;
inline bool     g_chamsDebug = false;

// ---- прочее ----
inline float g_boxHeight = 2.0f;
inline float g_fovDegrees = 90.0f;

// ---- устаревшее (для совместимости) ----
inline bool  g_drawPlayers = true;
inline bool  g_drawUnits = true;
inline bool  g_drawBoxes = true;
inline float g_maxDistance = 200.0f;
inline bool  g_showDebug = false;
inline bool  g_matrixMode = false;
inline bool  g_calibLog = false;