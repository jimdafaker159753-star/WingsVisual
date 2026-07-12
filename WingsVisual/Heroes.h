#pragma once

#include <windows.h>

struct HeroesUiTheme {
    HBRUSH background;
    HFONT headingFont;
    HFONT normalFont;
    COLORREF box;
    COLORREF gold;
    COLORREF goldDark;
    COLORREF text;
};

using HeroesExecuteLuaFn = void (*)(const char* code);
using HeroesQueueLuaFn = void (*)(const char* code);

bool HeroesInitialize(
    HWND menu,
    HMODULE module,
    const HeroesUiTheme& theme,
    HeroesExecuteLuaFn executeLua,
    HeroesQueueLuaFn queueLua);

void HeroesTick();
void HeroesSetVisible(bool visible);
bool HeroesDrawItem(LPDRAWITEMSTRUCT drawItem);
bool HeroesHandleCommand(UINT controlId);
bool HeroesHandleStaticColor(HWND control, HDC dc, LRESULT& result);
void HeroesShutdown();
