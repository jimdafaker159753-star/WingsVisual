// ===============================================================
//  ESP.cpp — WingsVisual. FINAL.
//  Players (Type 4) draw always. Tracers from player. Target highlight.
//  Real-lens capture (SetTransform). No log, no menu, no debug HUD.
// ===============================================================
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <atomic>

#include "MinHook.h"
#include "imgui.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"

#include "EspSettings.h"
#include "ObjectManager.h"
#include "WorldToScreen.h"

#pragma comment(lib, "d3d9.lib")

// ==== СПИСОК ЦЕЛЕЙ (редкие мобы), совпадение по подстроке ====
static const char* g_targetNames[] = {
    "%%NOTARGET%%",   // <- впиши сюда реальные имена редких мобов
};
static const int g_targetCount = (int)(sizeof(g_targetNames) / sizeof(g_targetNames[0]));

static bool IsTargetName(const char* n) {
    if (!n || !*n) return false;
    for (int i = 0; i < g_targetCount; i++)
        if (g_targetNames[i] && *g_targetNames[i] && strstr(n, g_targetNames[i]))
            return true;
    return false;
}

typedef HRESULT(WINAPI* EndScene_t)(IDirect3DDevice9*);
typedef HRESULT(WINAPI* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef HRESULT(WINAPI* SetTransform_t)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);

static EndScene_t     oEndScene = nullptr;
static Reset_t        oReset = nullptr;
static SetTransform_t oSetTransform = nullptr;
static void* g_pEndScene = nullptr;
static void* g_pReset = nullptr;
static void* g_pSetTransform = nullptr;

static HWND g_hGameWnd = nullptr;
static std::atomic<bool> g_imguiReady{ false };
static std::atomic<bool> g_unloading{ false };

static float g_boxRadius = 0.55f;

static bool GetD3D9VTable(void** outEndScene, void** outReset, void** outSetTransform) {
    HWND hDummy = CreateWindowExA(0, "STATIC", "dummy", WS_OVERLAPPED, 0, 0, 1, 1,
        NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!hDummy) return false;
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { DestroyWindow(hDummy); return false; }
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN; pp.hDeviceWindow = hDummy;
    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hDummy,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT, &pp, &dev);
    if (FAILED(hr) || !dev) { d3d->Release(); DestroyWindow(hDummy); return false; }
    void** vtbl = *reinterpret_cast<void***>(dev);
    *outEndScene = vtbl[42];
    *outReset = vtbl[16];
    *outSetTransform = vtbl[44];
    dev->Release(); d3d->Release(); DestroyWindow(hDummy);
    return true;
}

static HRESULT WINAPI hkSetTransform(IDirect3DDevice9* dev, D3DTRANSFORMSTATETYPE st, const D3DMATRIX* m) {
    if (m && st == D3DTS_PROJECTION) {
        if (fabsf(m->_34) > 0.5f && fabsf(m->_44) < 0.1f &&
            fabsf(m->_11) > 0.01f && fabsf(m->_22) > 0.01f) {
            g_projXScale = fabsf(m->_11);
            g_projYScale = fabsf(m->_22);
            g_projScaleValid = true;
        }
    }
    return oSetTransform ? oSetTransform(dev, st, m) : D3D_OK;
}

static void DrawEntity(const PlayerInfo& p, int w, int h, const CGCameraSnapshot& cam,
    bool tracerValid, float tracerX, float tracerY)
{
    const bool isPlayer = (p.type == OT_PLAYER);
    const bool isTarget = IsTargetName(p.name);

    // Игроки и цели рисуем всегда; мобы — по фильтрам HP/дистанции
    if (!isPlayer && !isTarget) {
        if (p.maxHealth == 0) return;
    }

    const Vector3 world{ p.x, p.y, p.z };
    const float dx = world.x - cam.pos.x,
        dy = world.y - cam.pos.y,
        dz = world.z - cam.pos.z;
    const float dist = sqrtf(dx * dx + dy * dy + dz * dz);

    if (!isPlayer && !isTarget) {
        if (g_maxDistance > 0.0f && dist > g_maxDistance) return;
    }

    if (!CoordsSane(p.x, p.y, p.z)) return;

    const float r = g_boxRadius, zTop = g_boxHeight;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    int okCount = 0;
    for (int ix = 0; ix < 2; ix++)
        for (int iy = 0; iy < 2; iy++)
            for (int iz = 0; iz < 2; iz++) {
                Vector3 corner{ world.x + (ix ? r : -r),
                                world.y + (iy ? r : -r),
                                world.z + (iz ? zTop : 0.0f) };
                Vector2 s;
                if (!WorldToScreen(corner, s, w, h)) continue;
                if (s.x < minX) minX = s.x; if (s.y < minY) minY = s.y;
                if (s.x > maxX) maxX = s.x; if (s.y > maxY) maxY = s.y;
                okCount++;
            }
    if (okCount < 4) return;

    float boxH = maxY - minY, boxW = maxX - minX;

    if (isTarget) {
        if (boxH < 10.0f) { float c = (minY + maxY) * 0.5f; minY = c - 5; maxY = c + 5; boxH = 10; }
        if (boxW < 10.0f) { float c = (minX + maxX) * 0.5f; minX = c - 5; maxX = c + 5; boxW = 10; }
    }
    else if (boxH < 3.0f || boxW < 1.0f) return;

    const float cx = (minX + maxX) * 0.5f;
    const float cy = (minY + maxY) * 0.5f;

    auto dl = ImGui::GetForegroundDrawList();
    const bool alive = (p.health > 0);

    ImU32 col;
    if (isTarget) col = IM_COL32(255, 215, 0, 255);
    else if (!alive)   col = IM_COL32(180, 180, 180, 255);
    else if (isPlayer) col = IM_COL32(64, 180, 255, 255);
    else               col = IM_COL32(235, 70, 60, 255);

    const float thick = isTarget ? 2.5f : 1.5f;

    if (g_drawTracers || isTarget) {
        float ox = tracerValid ? tracerX : (float)w * 0.5f;
        float oy = tracerValid ? tracerY : (float)h;
        dl->AddLine(ImVec2(ox, oy), ImVec2(cx, cy), col, thick);
    }

    if (g_drawBoxes || isTarget)
        dl->AddRect(ImVec2(minX, minY), ImVec2(maxX, maxY), col, 0.0f, 0, thick);

    if (g_drawHP && p.maxHealth > 0) {
        float frac = (float)p.health / (float)p.maxHealth;
        if (frac < 0) frac = 0; if (frac > 1) frac = 1;
        float bx = minX - 6.0f;
        dl->AddRectFilled(ImVec2(bx, minY), ImVec2(bx + 3, maxY), IM_COL32(0, 0, 0, 180));
        dl->AddRectFilled(ImVec2(bx, maxY - boxH * frac), ImVec2(bx + 3, maxY), IM_COL32(60, 220, 60, 255));
    }

    if (g_drawNames || g_drawDistance || isTarget) {
        char line[160];
        if (isTarget)
            _snprintf_s(line, _TRUNCATE, ">> %s << [%.0f]", p.name, dist);
        else if (g_drawNames && g_drawDistance)
            _snprintf_s(line, _TRUNCATE, "%s [%.0f]", p.name, dist);
        else if (g_drawNames)
            _snprintf_s(line, _TRUNCATE, "%s", p.name);
        else
            _snprintf_s(line, _TRUNCATE, "[%.0f]", dist);
        ImVec2 ts = ImGui::CalcTextSize(line);
        ImVec2 tp{ cx - ts.x * 0.5f, minY - ts.y - 2.0f };
        dl->AddRectFilled(ImVec2(tp.x - 2, tp.y - 1), ImVec2(tp.x + ts.x + 2, tp.y + ts.y + 1), IM_COL32(0, 0, 0, 160));
        dl->AddText(tp, col, line);
    }
}

static void RenderOverlay(IDirect3DDevice9* dev) {
    if (g_unloading.load()) return;
    g_espDev = dev;

    if (!g_imguiReady.load()) {
        D3DDEVICE_CREATION_PARAMETERS cp{};
        if (FAILED(dev->GetCreationParameters(&cp))) return;
        g_hGameWnd = cp.hFocusWindow;
        IMGUI_CHECKVERSION(); ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        if (!ImGui_ImplWin32_Init(g_hGameWnd)) return;
        if (!ImGui_ImplDX9_Init(dev)) {
            ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); return;
        }
        g_imguiReady.store(true);
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    D3DVIEWPORT9 vpq{}; dev->GetViewport(&vpq);
    const int w = (int)vpq.Width, h = (int)vpq.Height;

    CGCameraSnapshot cam;
    if (g_espEnabled && GetCameraSnapshot(cam)) {
        int n = RefreshEntities(g_drawUnits, g_drawPlayers, g_drawSelf);

        bool tracerValid = false;
        float tracerX = w * 0.5f, tracerY = (float)h;
        if (g_localValid) {
            Vector2 lp;
            if (WorldToScreen(Vector3{ g_localX, g_localY, g_localZ }, lp, w, h)) {
                tracerX = lp.x; tracerY = lp.y; tracerValid = true;
            }
        }

        for (int i = 0; i < n; i++)
            DrawEntity(g_players[i], w, h, cam, tracerValid, tracerX, tracerY);
    }

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

static HRESULT WINAPI hkEndScene(IDirect3DDevice9* dev) {
    __try { RenderOverlay(dev); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return oEndScene ? oEndScene(dev) : D3D_OK;
}

static HRESULT WINAPI hkReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    if (g_imguiReady.load()) ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = oReset ? oReset(dev, pp) : D3D_OK;
    if (SUCCEEDED(hr) && g_imguiReady.load()) ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}

DWORD WINAPI EspInitThread(LPVOID) {
    while (!g_unloading.load()
        && FindWindowA("GxWindowClassD3d", NULL) == NULL
        && FindWindowA("GxWindowClass", NULL) == NULL)
        Sleep(50);
    if (g_unloading.load()) return 0;

    void* pEndScene = nullptr, * pReset = nullptr, * pSetTransform = nullptr;
    if (!GetD3D9VTable(&pEndScene, &pReset, &pSetTransform)) return 1;
    g_pEndScene = pEndScene; g_pReset = pReset; g_pSetTransform = pSetTransform;

    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return 1;
    if (MH_CreateHook(pEndScene, &hkEndScene, (void**)&oEndScene) != MH_OK) return 1;
    if (MH_CreateHook(pReset, &hkReset, (void**)&oReset) != MH_OK) return 1;
    MH_CreateHook(pSetTransform, &hkSetTransform, (void**)&oSetTransform);
    if (MH_EnableHook(pEndScene) != MH_OK) return 1;
    if (MH_EnableHook(pReset) != MH_OK) return 1;
    MH_EnableHook(pSetTransform);
    return 0;
}

void EspShutdown() {
    g_unloading.store(true);
    if (g_pEndScene)     MH_DisableHook(g_pEndScene);
    if (g_pReset)        MH_DisableHook(g_pReset);
    if (g_pSetTransform) MH_DisableHook(g_pSetTransform);
    Sleep(80);
    if (g_imguiReady.exchange(false)) {
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    g_espDev = nullptr;
}