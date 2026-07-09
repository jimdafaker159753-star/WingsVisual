// ===============================================================
//  ESP.cpp — WingsVisual. ESP (+фильтр игрок/NPC) + CHAMS.
//  CHAMS: классификация по адресу возврата (_ReturnAddress), hold-F8.
// ===============================================================
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <atomic>
#include <intrin.h>

#include "MinHook.h"
#include "imgui.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"

#include "EspSettings.h"
#include "ObjectManager.h"
#include "WorldToScreen.h"

#pragma comment(lib, "d3d9.lib")
#pragma intrinsic(_ReturnAddress)

static const char* g_targetNames[] = {
    "%%NOTARGET%%",
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
typedef HRESULT(WINAPI* DIP_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);

static EndScene_t oEndScene = nullptr;
static Reset_t oReset = nullptr;
static SetTransform_t oSetTransform = nullptr;
static DIP_t oDIP = nullptr;

static void* g_pEndScene = nullptr;
static void* g_pReset = nullptr;
static void* g_pSetTransform = nullptr;
static void* g_pDIP = nullptr;

static HWND g_hGameWnd = nullptr;
static std::atomic<bool> g_imguiReady{ false };
static std::atomic<bool> g_unloading{ false };
static std::atomic<bool> g_inImGui{ false };

// ---- классификатор по адресу возврата (какая функция игры рисует меш) ----
struct CallerStat { void* ret; unsigned total; bool isWorld; };
static CallerStat g_callTab[256];
static int  g_callN = 0;
static bool g_chamsLearning = false;     // пока true — все вызовы = "мир"
static bool g_chamsCalibrated = false;   // хоть раз обучались
static int  g_chamsPaintedThis = 0;      // покрашено draw-вызовов в текущем кадре
static int  g_chamsPaintedLast = 0;      // покрашено в прошлом кадре (для показа)

static float g_boxRadius = 0.55f;

static bool GetD3D9VTable(void** outEndScene, void** outReset, void** outSetTransform, void** outDIP) {
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
    *outDIP = vtbl[82];
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

// поиск/добавление вызывающей функции в таблицу классификатора
static int FindOrAddCaller(void* ret) {
    for (int i = 0; i < g_callN; i++)
        if (g_callTab[i].ret == ret) return i;
    if (g_callN >= 256) return -1;
    int idx = g_callN++;
    g_callTab[idx].ret = ret;
    g_callTab[idx].total = 0;
    g_callTab[idx].isWorld = false;
    return idx;
}

// ================== CHAMS ==================
static HRESULT WINAPI hkDIP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, INT baseVI,
    UINT minVI, UINT numV, UINT startIdx, UINT primCount)
{
    void* ret = _ReturnAddress();   // адрес в wow.exe, откуда вызвана отрисовка

    if (!oDIP) return D3D_OK;
    if (!g_chamsEnabled || g_inImGui.load() || g_unloading.load())
        return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

    if ((int)numV < g_chamsMinVerts || (int)numV > g_chamsMaxVerts)
        return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

    IDirect3DBaseTexture9* tex = nullptr;
    bool hasTex = (dev->GetTexture(0, &tex) == D3D_OK && tex);
    if (tex) tex->Release();
    if (!hasTex)
        return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

    int idx = FindOrAddCaller(ret);
    if (idx < 0)
        return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

    g_callTab[idx].total++;

    // РЕЖИМ ОБУЧЕНИЯ: всё, что рисуется сейчас = "мир"
    if (g_chamsLearning) {
        g_callTab[idx].isWorld = true;
        return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);
    }

    // красим только вызовы из НЕмировых функций и только после обучения
    bool paint = g_chamsCalibrated && !g_callTab[idx].isWorld;
    if (!paint)
        return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

    g_chamsPaintedThis++;

    __try {
        DWORD sZEnable, sZFunc, sZWrite, sLighting, sFog, sTFactor, sAlpha, sColorOp, sColorArg1;
        dev->GetRenderState(D3DRS_ZENABLE, &sZEnable);
        dev->GetRenderState(D3DRS_ZFUNC, &sZFunc);
        dev->GetRenderState(D3DRS_ZWRITEENABLE, &sZWrite);
        dev->GetRenderState(D3DRS_LIGHTING, &sLighting);
        dev->GetRenderState(D3DRS_FOGENABLE, &sFog);
        dev->GetRenderState(D3DRS_TEXTUREFACTOR, &sTFactor);
        dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &sAlpha);
        dev->GetTextureStageState(0, D3DTSS_COLOROP, &sColorOp);
        dev->GetTextureStageState(0, D3DTSS_COLORARG1, &sColorArg1);
        IDirect3DPixelShader9* sPS = nullptr;
        dev->GetPixelShader(&sPS);

        dev->SetPixelShader(nullptr);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);

        // проход 1: сквозь стены (без теста глубины) — тёмный цвет
        dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetRenderState(D3DRS_TEXTUREFACTOR, (D3DCOLOR)g_chamsColorHidden);
        oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

        // проход 2: видимая часть — светлый цвет
        dev->SetRenderState(D3DRS_ZENABLE, TRUE);
        dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        dev->SetRenderState(D3DRS_TEXTUREFACTOR, (D3DCOLOR)g_chamsColorVisible);
        oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

        dev->SetPixelShader(sPS);
        if (sPS) sPS->Release();
        dev->SetRenderState(D3DRS_ZENABLE, sZEnable);
        dev->SetRenderState(D3DRS_ZFUNC, sZFunc);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, sZWrite);
        dev->SetRenderState(D3DRS_LIGHTING, sLighting);
        dev->SetRenderState(D3DRS_FOGENABLE, sFog);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, sAlpha);
        dev->SetRenderState(D3DRS_TEXTUREFACTOR, sTFactor);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, sColorOp);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, sColorArg1);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);
    }
    return D3D_OK;
}

static void DrawEntity(const PlayerInfo& p, int w, int h, const CGCameraSnapshot& cam,
    bool tracerValid, float tracerX, float tracerY)
{
    const bool isPlayer = (p.type == OT_PLAYER);
    const bool isTarget = IsTargetName(p.name);

    // фильтр по типу (цели показываем всегда)
    if (!isTarget) {
        if (isPlayer && !g_espShowPlayers) return;
        if (!isPlayer && !g_espShowNpcs) return;
    }

    if (!isPlayer && !isTarget) {
        if (p.maxHealth == 0) return;
    }

    const Vector3 world{ p.x, p.y, p.z };

    const float ox0 = g_localValid ? g_localX : cam.pos.x;
    const float oy0 = g_localValid ? g_localY : cam.pos.y;
    const float oz0 = g_localValid ? g_localZ : cam.pos.z;
    const float dx = world.x - ox0, dy = world.y - oy0, dz = world.z - oz0;
    const float dist = sqrtf(dx * dx + dy * dy + dz * dz);

    if (!isTarget && g_espDist > 0.0f && dist > g_espDist) return;
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
    else if (!alive) col = IM_COL32(160, 160, 160, 255);
    else {
        Reaction rx = GetReaction(p);
        if (rx == REACT_HOSTILE) col = IM_COL32(235, 55, 45, 255);
        else if (rx == REACT_FRIENDLY) col = IM_COL32(60, 220, 90, 255);
        else col = IM_COL32(240, 240, 240, 255);
    }

    const float thick = isTarget ? 2.5f : 1.5f;

    if (g_drawTracers || isTarget) {
        float ox = tracerValid ? tracerX : (float)w * 0.5f;
        float oy = tracerValid ? tracerY : (float)h;
        dl->AddLine(ImVec2(ox, oy), ImVec2(cx, cy), col, thick);
    }

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

        io.Fonts->Clear();
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 16.0f, NULL,
            io.Fonts->GetGlyphRangesCyrillic());

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

    // ---- CHAMS: обучение "мира" по зажатому F8 + счётчик статуса ----
    if (g_chamsEnabled) {
        bool holdF8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        static bool prevHold = false;
        if (holdF8 && !prevHold) {                    // начали удержание — сброс таблицы
            for (int i = 0; i < g_callN; i++) g_callTab[i].isWorld = false;
            g_chamsCalibrated = false;
        }
        g_chamsLearning = holdF8;                     // hkDIP помечает "мир", пока зажато
        if (!holdF8 && prevHold) g_chamsCalibrated = true; // отпустили — готово
        prevHold = holdF8;

        g_chamsPaintedLast = g_chamsPaintedThis;
        g_chamsPaintedThis = 0;

        const char* stt = g_chamsLearning ? "LEARNING - pan camera (hold F8, no units on screen)"
            : (g_chamsCalibrated ? "OK" : "HOLD F8 in 1st person, pan around, release");
        int worldCnt = 0; for (int i = 0; i < g_callN; i++) if (g_callTab[i].isWorld) worldCnt++;
        char sbuf[192];
        _snprintf_s(sbuf, _TRUNCATE, "CHAMS: %s | callers:%d world:%d painted:%d",
            stt, g_callN, worldCnt, g_chamsPaintedLast);
        ImGui::GetForegroundDrawList()->AddText(ImVec2(12.0f, 12.0f), IM_COL32(255, 235, 0, 255), sbuf);
    }

    // ---- ESP ----
    CGCameraSnapshot cam;
    if (g_espEnabled && GetCameraSnapshot(cam)) {
        int n = RefreshEntities(true, true, g_drawSelf);

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

    g_inImGui.store(true);
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    g_inImGui.store(false);
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

    void* pEndScene = nullptr, * pReset = nullptr, * pSetTransform = nullptr, * pDIP = nullptr;
    if (!GetD3D9VTable(&pEndScene, &pReset, &pSetTransform, &pDIP)) return 1;
    g_pEndScene = pEndScene; g_pReset = pReset; g_pSetTransform = pSetTransform; g_pDIP = pDIP;

    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return 1;
    if (MH_CreateHook(pEndScene, &hkEndScene, (void**)&oEndScene) != MH_OK) return 1;
    if (MH_CreateHook(pReset, &hkReset, (void**)&oReset) != MH_OK) return 1;
    MH_CreateHook(pSetTransform, &hkSetTransform, (void**)&oSetTransform);
    MH_CreateHook(pDIP, &hkDIP, (void**)&oDIP);
    if (MH_EnableHook(pEndScene) != MH_OK) return 1;
    if (MH_EnableHook(pReset) != MH_OK) return 1;
    MH_EnableHook(pSetTransform);
    MH_EnableHook(pDIP);
    return 0;
}

void EspShutdown() {
    g_unloading.store(true);
    if (g_pDIP) MH_DisableHook(g_pDIP);
    if (g_pEndScene) MH_DisableHook(g_pEndScene);
    if (g_pReset) MH_DisableHook(g_pReset);
    if (g_pSetTransform) MH_DisableHook(g_pSetTransform);
    Sleep(80);
    if (g_imguiReady.exchange(false)) {
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    g_espDev = nullptr;
}