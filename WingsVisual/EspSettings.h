#pragma once

// ---- что показывать (значения по умолчанию, финальная сборка) ----
inline bool  g_espEnabled = true;
inline bool  g_drawBoxes = true;
inline bool  g_drawNames = true;
inline bool  g_drawHP = true;
inline bool  g_drawDistance = false;
inline bool  g_drawTracers = true;   // трейсеры от игрока
inline bool  g_drawPlayers = true;
inline bool  g_drawUnits = true;
inline bool  g_drawSelf = false;

// ---- прочее ----
inline float g_maxDistance = 200.0f; // лимит дистанции для МОБов (игроки/цели игнорируют)
inline float g_boxHeight = 2.0f;   // высота бокса (ярды)
inline float g_fovDegrees = 90.0f;  // фолбэк FOV, если линза игры не захвачена

// ---- устаревшие (не используются в финале, оставлены для совместимости) ----
inline bool  g_showDebug = false;
inline int   g_matrixMode = 1;
inline bool  g_calibLog = false;