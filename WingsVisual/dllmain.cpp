#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <commctrl.h>
#include "MinHook.h"
#pragma comment(lib, "comctl32.lib")

// ==== ESP module (ESP.cpp) ====
extern DWORD WINAPI EspInitThread(LPVOID);
extern void        EspShutdown();

// ============ WoW 3.3.5a (build 12340) ============
typedef int(__cdecl* ExecBuf_t)(const char*, const char*, int);
ExecBuf_t ExecuteBuffer = (ExecBuf_t)0x00819210;

#define CAM_LIMIT_ADDR     0x00A1E2FC
#define CAM_MGR_PTR        0x00B7436C
#define CAM_OBJ_OFFSET     0x7E20
#define CAM_DIST_OFFSET    0x118
#define CAM_STATE_OFFSET   0x1E8
#define CAM_VEL_OFFSET     0x1EC
#define CLAMP_JE_ADDR      0x00600133
#define CAM_UPDATE_ADDR    0x00606F90

#define WM_TOGGLEMENU      (WM_APP+1)
#define WM_REFRESHLBL      (WM_APP+2)
#define WM_SETDIST         (WM_APP+3)

#define WHEEL_STEP         40.0f
#define SMOOTH_SPEED       10.0f
#define DIST_MIN           15.0f
#define DIST_MAX            1000.0f

#define CLR_BG     RGB(18,16,12)
#define CLR_BOX    RGB(30,26,20)
#define CLR_GOLD   RGB(255,209,10)
#define CLR_GOLDDK RGB(120,96,20)
#define CLR_TEXT   RGB(240,225,180)
#define CLR_DIVDK  RGB(70,58,30)

HWND hMenu = NULL, hStatus = NULL, hCheck = NULL, hGame = NULL;
HWND hSlDist = NULL, hSlSpeed = NULL, hSlRot = NULL;
HWND hLblDist = NULL, hLblSpeed = NULL, hLblRot = NULL, hLblFoot = NULL;
HFONT hFontTitle = NULL, hFontHead = NULL, hFontNorm = NULL;
HBRUSH hbrBack = NULL;
HMODULE hSelf = NULL; bool menuVisible = false;
bool g_unlockChecked = false;

volatile bool  unlocked = false;
volatile float g_targetDist = 15.0f;
volatile float g_curDist = 15.0f;

static LARGE_INTEGER g_qpcFreq = { 0 };
static LARGE_INTEGER g_qpcLast = { 0 };

CRITICAL_SECTION g_cs;
bool  g_csReady = false;
volatile LONG g_pending = 0;
int   g_reqState = 0;
char  g_luaQ[16][128];
int   g_luaCount = 0;

void Log(const char* t) {
    FILE* f; if (fopen_s(&f, "CameraMod_Log.txt", "a") == 0) {
        SYSTEMTIME s; GetLocalTime(&s);
        fprintf(f, "[%02d:%02d:%02d] %s\n", s.wHour, s.wMinute, s.wSecond, t); fclose(f);
    } printf("%s\n", t);
}

void QueueLua(const char* code) {
    if (!g_csReady) return;
    EnterCriticalSection(&g_cs);
    if (g_luaCount < 16) { strcpy_s(g_luaQ[g_luaCount], sizeof(g_luaQ[0]), code); g_luaCount++; }
    g_pending = 1;
    LeaveCriticalSection(&g_cs);
}
void QueueCVarF(const char* name, float v) { char b[96]; sprintf_s(b, "SetCVar('%s', %.2f)", name, v); QueueLua(b); }
void RequestState(int st) {
    if (!g_csReady) return;
    EnterCriticalSection(&g_cs);
    g_reqState = st; g_pending = 1;
    LeaveCriticalSection(&g_cs);
}

void SetMaxDistanceMemory(float meters) {
    DWORD op; if (VirtualProtect((void*)CAM_LIMIT_ADDR, 4, PAGE_EXECUTE_READWRITE, &op)) {
        *(float*)CAM_LIMIT_ADDR = meters;
        VirtualProtect((void*)CAM_LIMIT_ADDR, 4, op, &op);
    }
}
void PatchClamp(bool unlock) {
    DWORD op; if (VirtualProtect((void*)CLAMP_JE_ADDR, 2, PAGE_EXECUTE_READWRITE, &op)) {
        if (unlock) { *(BYTE*)CLAMP_JE_ADDR = 0x90; *(BYTE*)(CLAMP_JE_ADDR + 1) = 0x90; }
        else { *(BYTE*)CLAMP_JE_ADDR = 0x74; *(BYTE*)(CLAMP_JE_ADDR + 1) = 0x08; }
        VirtualProtect((void*)CLAMP_JE_ADDR, 2, op, &op);
    }
}

DWORD GetActiveCamera() {
    __try {
        DWORD mgr = *(DWORD*)CAM_MGR_PTR; if (!mgr) return 0;
        return *(DWORD*)(mgr + CAM_OBJ_OFFSET);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
float GetCameraDistance() {
    DWORD cam = GetActiveCamera(); if (!cam) return -1.0f;
    __try { return *(float*)(cam + CAM_DIST_OFFSET); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1.0f; }
}
inline void PinDistance(DWORD cam, float v) {
    *(float*)(cam + CAM_DIST_OFFSET) = v;
    *(float*)(cam + CAM_STATE_OFFSET) = v;
    *(float*)(cam + CAM_VEL_OFFSET) = 0.0f;
}
void SetBothDistance(float v) {
    DWORD cam = GetActiveCamera(); if (!cam) return;
    __try { PinDistance(cam, v); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void DrainPending() {
    if (!g_pending) return;
    int st = 0; char lua[16][128]; int n = 0;
    EnterCriticalSection(&g_cs);
    st = g_reqState; g_reqState = 0;
    n = g_luaCount; for (int i = 0;i < n;i++) strcpy_s(lua[i], sizeof(lua[0]), g_luaQ[i]); g_luaCount = 0;
    g_pending = 0;
    LeaveCriticalSection(&g_cs);

    if (st == 1) {
        PatchClamp(true);
        SetMaxDistanceMemory(DIST_MAX);
        float cur = GetCameraDistance();
        if (cur < DIST_MIN) cur = DIST_MIN; if (cur > DIST_MAX) cur = DIST_MAX;
        g_curDist = cur; g_targetDist = cur;
        g_qpcFreq.QuadPart = 0;
        unlocked = true;
        if (hMenu) PostMessageA(hMenu, WM_SETDIST, 0, (LPARAM)(int)cur);
    }
    else if (st == 2) {
        unlocked = false;
        PatchClamp(false);
        SetMaxDistanceMemory(50.0f);
        g_curDist = 50.0f; g_targetDist = 50.0f;
        DWORD cam = GetActiveCamera(); if (cam) { __try { PinDistance(cam, 50.0f); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    }
    for (int i = 0;i < n;i++) { __try { ExecuteBuffer(lua[i], "CameraMod", 0); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
}

typedef void(__fastcall* CamUpd_t)(void* ecx, void* edx, int a1, int a2);
CamUpd_t oCamUpd = nullptr;

void __fastcall hkCamUpd(void* ecx, void* edx, int a1, int a2) {
    DrainPending();

    if (unlocked) {
        if (g_qpcFreq.QuadPart == 0) { QueryPerformanceFrequency(&g_qpcFreq); QueryPerformanceCounter(&g_qpcLast); }
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - g_qpcLast.QuadPart) / (float)g_qpcFreq.QuadPart;
        g_qpcLast = now;
        if (dt > 0.1f) dt = 0.1f; if (dt < 0.0f) dt = 0.0f;

        float t = 1.0f - expf(-SMOOTH_SPEED * dt);
        g_curDist += (g_targetDist - g_curDist) * t;
        if (fabsf(g_targetDist - g_curDist) < 0.01f) g_curDist = g_targetDist;

        __try { if (ecx) PinDistance((DWORD)ecx, g_curDist); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    if (oCamUpd) oCamUpd(ecx, edx, a1, a2);

    if (unlocked) {
        __try { if (ecx) PinDistance((DWORD)ecx, g_curDist); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

int SliderPos(HWND s) { return (int)SendMessage(s, TBM_GETPOS, 0, 0); }

void UpdateLabels() {
    char b[96];
    sprintf_s(b, "Max Camera Distance:   %d yd", SliderPos(hSlDist)); SetWindowTextA(hLblDist, b);
    sprintf_s(b, "Zoom Speed:   %d", SliderPos(hSlSpeed)); SetWindowTextA(hLblSpeed, b);
    sprintf_s(b, "Rotation Speed:   %d", SliderPos(hSlRot));   SetWindowTextA(hLblRot, b);
}

WNDPROC oGameWndProc = NULL;
LRESULT CALLBACK GameWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_MOUSEWHEEL && unlocked) {
        short delta = (short)HIWORD(w);
        float nt = g_targetDist - ((float)delta / 120.0f) * WHEEL_STEP;
        if (nt < DIST_MIN) nt = DIST_MIN; if (nt > DIST_MAX) nt = DIST_MAX;
        g_targetDist = nt;
        if (hSlDist) { PostMessageA(hSlDist, TBM_SETPOS, TRUE, (int)nt); PostMessageA(hMenu, WM_REFRESHLBL, 0, 0); }
        return 0;
    }
    return CallWindowProcA(oGameWndProc, hwnd, msg, w, l);
}
void InstallWheelHook() {
    if (hGame && !oGameWndProc)
        oGameWndProc = (WNDPROC)SetWindowLongPtrA(hGame, GWLP_WNDPROC, (LONG_PTR)GameWndProc);
}
void RemoveWheelHook() {
    if (hGame && oGameWndProc) {
        SetWindowLongPtrA(hGame, GWLP_WNDPROC, (LONG_PTR)oGameWndProc);
        oGameWndProc = NULL;
    }
}

void ApplyUnlock(bool on) {
    if (on) {
        QueueLua("SetCVar('cameraDistanceMaxFactor', 3.4)");
        QueueLua("SetCVar('cameraDistanceMax', 1000)");
        RequestState(1);
        EnableWindow(hSlDist, TRUE);
        SetWindowTextA(hStatus, "Status:  UNLOCKED  -  wheel / slider zoom to 1000");
    }
    else {
        QueueLua("SetCVar('cameraDistanceMax', 50)");
        QueueLua("SetCVar('cameraDistanceMaxFactor', 1.0)");
        RequestState(2);
        EnableWindow(hSlDist, FALSE);
        SetWindowTextA(hStatus, "Status:  LOCKED (50 yd)");
    }
    UpdateLabels();
}

void BringMenuToFront(HWND h) {
    ShowWindow(h, SW_SHOW);
    SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    DWORD me = GetCurrentThreadId();
    DWORD other = hGame ? GetWindowThreadProcessId(hGame, NULL) : 0;
    if (other && other != me) AttachThreadInput(me, other, TRUE);
    BringWindowToTop(h);
    SetForegroundWindow(h);
    SetActiveWindow(h);
    if (other && other != me) AttachThreadInput(me, other, FALSE);
    ClipCursor(NULL);
    while (ShowCursor(TRUE) < 0) {}
    RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static void PaintFrame(HWND h, HDC dc) {
    RECT rc; GetClientRect(h, &rc);
    FillRect(dc, &rc, hbrBack);

    HPEN goldPen = CreatePen(PS_SOLID, 2, CLR_GOLDDK);
    HPEN goldPen2 = CreatePen(PS_SOLID, 1, CLR_GOLD);
    HPEN divPen = CreatePen(PS_SOLID, 1, CLR_DIVDK);
    HBRUSH oldB = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    HPEN oldP = (HPEN)SelectObject(dc, goldPen);

    Rectangle(dc, rc.left + 3, rc.top + 3, rc.right - 3, rc.bottom - 3);
    SelectObject(dc, goldPen2);
    Rectangle(dc, rc.left + 6, rc.top + 6, rc.right - 6, rc.bottom - 6);

    SelectObject(dc, divPen);
    MoveToEx(dc, 16, 50, NULL);            LineTo(dc, rc.right - 16, 50);
    MoveToEx(dc, 16, rc.bottom - 40, NULL);  LineTo(dc, rc.right - 16, rc.bottom - 40);

    HFONT oldF = (HFONT)SelectObject(dc, hFontTitle);
    SetBkMode(dc, TRANSPARENT);
    RECT tr = { 0,12,rc.right,44 };
    SetTextColor(dc, RGB(0, 0, 0));  OffsetRect(&tr, 1, 2);
    DrawTextA(dc, "YamiYami", -1, &tr, DT_CENTER | DT_SINGLELINE);
    OffsetRect(&tr, -1, -2);
    SetTextColor(dc, CLR_GOLD);
    DrawTextA(dc, "YamiYami", -1, &tr, DT_CENTER | DT_SINGLELINE);

    SelectObject(dc, oldF); SelectObject(dc, oldP); SelectObject(dc, oldB);
    DeleteObject(goldPen); DeleteObject(goldPen2); DeleteObject(divPen);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_TOGGLEMENU:
        if (w) BringMenuToFront(h);
        else  ShowWindow(h, SW_HIDE);
        return 0;
    case WM_REFRESHLBL:
        UpdateLabels();
        return 0;
    case WM_SETDIST:
        if (hSlDist) { SendMessage(hSlDist, TBM_SETPOS, TRUE, (int)l); UpdateLabels(); }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        PaintFrame(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_NCHITTEST: {
        int sx = (int)(short)LOWORD(l), sy = (int)(short)HIWORD(l);
        POINT p = { sx,sy }; ScreenToClient(h, &p);
        RECT rc; GetClientRect(h, &rc);
        if (p.y >= 0 && p.y < 50 && p.x >= 0 && p.x < rc.right) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_ACTIVATE:
        if (LOWORD(w) != WA_INACTIVE) { ClipCursor(NULL); }
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w; HWND c = (HWND)l; SetBkMode(dc, TRANSPARENT);
        bool gold = (c == hStatus || c == hLblDist || c == hLblSpeed || c == hLblRot);
        SetTextColor(dc, gold ? CLR_GOLD : CLR_TEXT);
        return (LRESULT)hbrBack;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)l;
        if (d->CtlID == 1) {
            RECT rc = d->rcItem;
            FillRect(d->hDC, &rc, hbrBack);
            int bs = 18, by = rc.top + ((rc.bottom - rc.top) - bs) / 2;
            RECT box = { rc.left,by,rc.left + bs,by + bs };
            HBRUSH inr = CreateSolidBrush(CLR_BOX); FillRect(d->hDC, &box, inr); DeleteObject(inr);
            HBRUSH gld = CreateSolidBrush(CLR_GOLD); FrameRect(d->hDC, &box, gld); DeleteObject(gld);
            if (g_unlockChecked) {
                RECT ck = { box.left + 4,box.top + 4,box.right - 4,box.bottom - 4 };
                HBRUSH f = CreateSolidBrush(CLR_GOLD); FillRect(d->hDC, &ck, f); DeleteObject(f);
            }
            SetBkMode(d->hDC, TRANSPARENT);
            SetTextColor(d->hDC, g_unlockChecked ? CLR_GOLD : CLR_TEXT);
            HFONT of = (HFONT)SelectObject(d->hDC, hFontHead);
            RECT tr = { box.right + 10,rc.top,rc.right,rc.bottom };
            DrawTextA(d->hDC, "Unlock Camera Distance", -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            SelectObject(d->hDC, of);
            return TRUE;
        }
        break;
    }
    case WM_HSCROLL: {
        HWND s = (HWND)l; int code = LOWORD(w);
        bool released = (code == TB_ENDTRACK);
        if (s == hSlDist) {
            if (unlocked) {
                int pos = (code == TB_THUMBTRACK || code == TB_THUMBPOSITION) ? (int)(short)HIWORD(w) : SliderPos(s);
                if (pos < (int)DIST_MIN) pos = (int)DIST_MIN;
                if (pos > (int)DIST_MAX) pos = (int)DIST_MAX;
                g_targetDist = (float)pos;
            }
        }
        else if (released && s == hSlSpeed) { QueueCVarF("cameraDistanceMoveSpeed", (float)SliderPos(s)); }
        else if (released && s == hSlRot) { QueueCVarF("cameraYawMoveSpeed", (float)SliderPos(s)); }
        UpdateLabels();
        break;
    }
    case WM_COMMAND:
        if (LOWORD(w) == 1 && HIWORD(w) == BN_CLICKED) {
            g_unlockChecked = !g_unlockChecked;
            InvalidateRect(hCheck, NULL, TRUE);
            ApplyUnlock(g_unlockChecked);
        }
        break;
    case WM_CLOSE: ShowWindow(h, SW_HIDE); menuVisible = false; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProc(h, m, w, l);
    }
    return 0;
}

static HWND mkLabel(const char* t, int x, int y, int wd, HFONT f) {
    HWND o = CreateWindowA("STATIC", t, WS_VISIBLE | WS_CHILD, x, y, wd, 22, hMenu, NULL, hSelf, NULL);
    SendMessage(o, WM_SETFONT, (WPARAM)f, TRUE); return o;
}
static HWND mkSlider(int x, int y, int wd, int lo, int hi, int pos) {
    HWND o = CreateWindowA(TRACKBAR_CLASSA, "", WS_VISIBLE | WS_CHILD | TBS_HORZ | TBS_NOTICKS, x, y, wd, 28, hMenu, NULL, hSelf, NULL);
    SendMessage(o, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    SendMessage(o, TBM_SETPOS, TRUE, pos);
    return o;
}

HWND FindGameWindow() {
    HWND h = FindWindowA("GxWindowClassD3d", NULL);
    if (!h) h = FindWindowA("GxWindowClass", NULL);
    if (!h) h = FindWindowA(NULL, "World of Warcraft");
    return h;
}

DWORD WINAPI MenuThread(LPVOID) {
    INITCOMMONCONTROLSEX ic = { sizeof(ic),ICC_BAR_CLASSES }; InitCommonControlsEx(&ic);
    hGame = FindGameWindow();
    hbrBack = CreateSolidBrush(CLR_BG);
    hFontTitle = CreateFontA(28, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Palatino Linotype");
    hFontHead = CreateFontA(17, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Palatino Linotype");
    hFontNorm = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Segoe UI");

    WNDCLASSA wc = { 0 }; wc.lpfnWndProc = WndProc; wc.hInstance = hSelf;
    wc.hbrBackground = hbrBack; wc.lpszClassName = "CamMod"; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    const int W = 452, H = 404;
    int x = 200, y = 200; RECT gr;
    if (hGame && GetWindowRect(hGame, &gr)) { x = gr.left + ((gr.right - gr.left) - W) / 2; y = gr.top + ((gr.bottom - gr.top) - H) / 2; }

    hMenu = CreateWindowExA(WS_EX_TOPMOST, "CamMod", "YamiYami",
        WS_POPUP | WS_CLIPCHILDREN, x, y, W, H, hGame, NULL, hSelf, NULL);

    hStatus = mkLabel("Status:  LOCKED (50 yd)", 24, 60, 404, hFontNorm);

    hCheck = CreateWindowA("BUTTON", "Unlock Camera Distance",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 24, 90, 404, 26, hMenu, (HMENU)1, hSelf, NULL);
    SendMessage(hCheck, WM_SETFONT, (WPARAM)hFontHead, TRUE);

    hLblDist = mkLabel("Max Camera Distance:   15 yd", 24, 126, 404, hFontHead); hSlDist = mkSlider(24, 152, 404, 15, 1000, 15);
    hLblSpeed = mkLabel("Zoom Speed:   20", 24, 192, 404, hFontHead); hSlSpeed = mkSlider(24, 218, 404, 5, 50, 20);
    hLblRot = mkLabel("Rotation Speed:   180", 24, 258, 404, hFontHead); hSlRot = mkSlider(24, 284, 404, 90, 360, 180);

    hLblFoot = mkLabel("Menu:  '      Unload:  END      Wheel zooms to 1000 when ON", 24, H - 34, 404, hFontNorm);

    EnableWindow(hSlDist, FALSE);

    InstallWheelHook();
    if (!oGameWndProc) Log("WARNING: could not subclass game window for wheel zoom (slider still works)");

    MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}

DWORD WINAPI MainThread(LPVOID p) {
    AllocConsole(); FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);
    FILE* lf; if (fopen_s(&lf, "CameraMod_Log.txt", "w") == 0) fclose(lf);
    Log("=== YamiYami loaded ===");

    InitializeCriticalSection(&g_cs); g_csReady = true;

    if (MH_Initialize() == MH_OK &&
        MH_CreateHook((LPVOID)CAM_UPDATE_ADDR, &hkCamUpd, (LPVOID*)&oCamUpd) == MH_OK &&
        MH_EnableHook((LPVOID)CAM_UPDATE_ADDR) == MH_OK) {
        Log("camera update hook installed (0x606F90); distance state=0x1E8");
    }
    else {
        Log("ERROR: failed to install camera hook");
    }

    CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);

    // ==== запуск ESP (D3D9-хуки ставит EspInitThread, MinHook уже инициализирован) ====
    CreateThread(NULL, 0, EspInitThread, NULL, 0, NULL);
    Log("ESP init thread started");

    while (true) {
        if (GetAsyncKeyState(VK_END) & 0x8000) break;
        if (GetAsyncKeyState(VK_OEM_7) & 0x8000) {
            if (hMenu) { menuVisible = !menuVisible; PostMessageA(hMenu, WM_TOGGLEMENU, menuVisible ? 1 : 0, 0); }
            Sleep(300);
        }
        Sleep(20);
    }

    // 1) вернуть состояние камеры на игровом потоке
    RequestState(2);
    Sleep(150);
    unlocked = false;

    // 2) снять in-process хук колеса
    RemoveWheelHook();

    // 3) выключить ESP (снимает свои D3D9-хуки + ImGui) ДО MH_Uninitialize
    EspShutdown();

    // 4) снять хук камеры и деинициализировать MinHook (единый владелец)
    MH_DisableHook((LPVOID)CAM_UPDATE_ADDR);
    Sleep(30);
    MH_Uninitialize();

    // 5) идемпотентный fallback
    PatchClamp(false);
    SetMaxDistanceMemory(50.0f);
    SetBothDistance(50.0f);

    if (g_csReady) { g_csReady = false; DeleteCriticalSection(&g_cs); }

    Sleep(50);
    if (hMenu) DestroyWindow(hMenu);
    if (f) fclose(f); FreeConsole();
    FreeLibraryAndExitThread((HMODULE)p, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) { hSelf = h; DisableThreadLibraryCalls(h); CreateThread(NULL, 0, MainThread, h, 0, NULL); }
    return TRUE;
}