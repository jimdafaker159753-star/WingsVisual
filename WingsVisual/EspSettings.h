#pragma once

#include <windows.h>
#include <stdint.h>

// Shared variables for MSVC builds that may not enable C++17 inline variables.
#define ESP_GLOBAL __declspec(selectany)

enum EspCategory
{
    ESP_CATEGORY_NPCS = 0,
    ESP_CATEGORY_PLAYERS = 1,
    ESP_CATEGORY_RESOURCES = 2,
    ESP_CATEGORY_COUNT = 3
};

ESP_GLOBAL int g_espCategory = ESP_CATEGORY_NPCS;

ESP_GLOBAL bool g_espEnabled = false;
ESP_GLOBAL bool g_drawNames = false;
ESP_GLOBAL bool g_drawHP = false;
ESP_GLOBAL bool g_drawDistance = false;
ESP_GLOBAL bool g_drawTracers = false;
ESP_GLOBAL bool g_drawRadius = false;
ESP_GLOBAL bool g_drawSelfRadius = false;
ESP_GLOBAL bool g_drawSelf = false;
ESP_GLOBAL float g_radiusYards = 12.0f;
ESP_GLOBAL float g_selfRadiusYards = 10.0f;
ESP_GLOBAL float g_nameFontSize = 16.0f;
ESP_GLOBAL float g_espDist = 200.0f;

ESP_GLOBAL bool g_espShowPlayers = true;
ESP_GLOBAL bool g_espShowNpcs = true;

ESP_GLOBAL bool g_chamsEnabled = false;
ESP_GLOBAL bool g_chamsDebug = false;
ESP_GLOBAL int g_chamsMinVerts = 20;
ESP_GLOBAL int g_chamsMaxVerts = 2500;

// ARGB colors in this order: NPCs, Players, Resources.
ESP_GLOBAL uint32_t g_chamsVisibleByCategory[ESP_CATEGORY_COUNT] =
{
    0xFFFFA040u,
    0xFF40B0FFu,
    0xFF50E070u
};

ESP_GLOBAL uint32_t g_chamsHiddenByCategory[ESP_CATEGORY_COUNT] =
{
    0xFF8A4018u,
    0xFF1E78C8u,
    0xFF167A32u
};


// Independent ARGB colors for each ESP feature and target category.
ESP_GLOBAL uint32_t g_boxColorByCategory[ESP_CATEGORY_COUNT] =
{
    0xFFFFFFFFu,
    0xFF40B0FFu,
    0xFF50E070u
};

ESP_GLOBAL uint32_t g_radiusColorByCategory[ESP_CATEGORY_COUNT] =
{
    0xFFFF9EB5u,
    0xFF66B9FFu,
    0xFF6EE58Bu
};

ESP_GLOBAL uint32_t g_selfRadiusColor = 0xFF40E8FFu;

ESP_GLOBAL uint32_t g_tracerColorByCategory[ESP_CATEGORY_COUNT] =
{
    0xFFFF9EB5u,
    0xFF66B9FFu,
    0xFF6EE58Bu
};

ESP_GLOBAL uint32_t g_nameColorByCategory[ESP_CATEGORY_COUNT] =
{
    0xFFFFFFFFu,
    0xFFFFFFFFu,
    0xFFFFFFFFu
};

ESP_GLOBAL uint32_t g_chamsColorVisible = 0xFF40B0FFu;
ESP_GLOBAL uint32_t g_chamsColorHidden = 0xFF1E78C8u;

ESP_GLOBAL float g_boxHeight = 2.0f;
ESP_GLOBAL float g_fovDegrees = 90.0f;

// Legacy fields retained for compatibility.
ESP_GLOBAL bool g_drawPlayers = true;
ESP_GLOBAL bool g_drawUnits = true;
ESP_GLOBAL bool g_drawBoxes = true;
ESP_GLOBAL float g_maxDistance = 200.0f;
ESP_GLOBAL bool g_showDebug = false;
ESP_GLOBAL bool g_matrixMode = false;
ESP_GLOBAL bool g_calibLog = false;

#undef ESP_GLOBAL
