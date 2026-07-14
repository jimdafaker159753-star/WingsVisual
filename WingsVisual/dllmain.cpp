#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <commctrl.h>
#include <stdint.h>
#include <string.h>
#include "MinHook.h"
#include "EspSettings.h"
#include "Heroes.h"
#pragma comment(lib, "comctl32.lib")

extern DWORD WINAPI EspInitThread(LPVOID);

extern void EspShutdown();

typedef int(__cdecl* ExecBuf_t)(const char*, const char*, int);

ExecBuf_t ExecuteBuffer = (ExecBuf_t)0x00819210;

#define CAM_LIMIT_ADDR 0x00A1E2FC
#define CAM_MGR_PTR 0x00B7436C
#define CAM_OBJ_OFFSET 0x7E20
#define CAM_DIST_OFFSET 0x118
#define CAM_STATE_OFFSET 0x1E8
#define CAM_VEL_OFFSET 0x1EC
#define CAM_FOV_OFFSET 0x40
#define CLAMP_JE_ADDR 0x00600133
#define CAM_UPDATE_ADDR 0x00606F90
#define WM_TOGGLEMENU (WM_APP+1)
#define WM_REFRESHLBL (WM_APP+2)
#define WM_SETDIST (WM_APP+3)
#define WHEEL_STEP 40.0f
#define SMOOTH_SPEED 10.0f
#define DIST_MIN 15.0f
#define DIST_MAX 1000.0f
#define DEG2RAD 0.01745329f
#define CLR_BG RGB(18,13,10)
#define CLR_BOX RGB(42,27,17)
#define CLR_GOLD RGB(255,209,10)
#define CLR_ACCENT RGB(242,112,18)
#define CLR_GOLDDK RGB(112,43,18)
#define CLR_TEXT RGB(255,232,188)
#define CLR_MUTED RGB(184,151,111)
#define CLR_DIVDK RGB(91,35,20)
#define CLR_SLIDER_BG RGB(24,17,13)
#define CLR_SLIDER_RAIL RGB(76,42,25)
#define CLR_SLIDER_FRAME RGB(125,60,23)
#define CLR_SLIDER_TEXT RGB(255,238,205)

HWND hMenu = NULL, hStatus = NULL, hCheck = NULL, hGame = NULL;

HWND hSlDist = NULL, hSlSpeed = NULL, hSlRot = NULL;

HWND hLblDist = NULL, hLblSpeed = NULL, hLblRot = NULL, hLblFoot = NULL;

HWND hLblEsp = NULL, hTabCam = NULL, hTabEsp = NULL, hTabHeroes = NULL;
HWND hEspTabs[5] = { 0 };

HWND hLblEspDist = NULL, hSlEspDist = NULL;
HWND hLblRadius = NULL, hSlRadius = NULL;
HWND hLblSelfRadius = NULL, hSlSelfRadius = NULL;
HWND hLblNameSize = NULL, hSlNameSize = NULL;

HWND hFovCheck = NULL, hLblFov = NULL, hSlFov = NULL;

HWND hEsp[11] = {
    0
};




int g_tab = 0;
int g_espSection = 0;

HFONT hFontTitle = NULL, hFontHead = NULL, hFontNorm = NULL;

HBRUSH hbrBack = NULL;

HMODULE hSelf = NULL;

bool menuVisible = false, g_unlockChecked = false;


volatile bool unlocked = false;

volatile float g_targetDist = 15.0f, g_curDist = 15.0f;

volatile bool g_fovEnabled = false;

volatile float g_fovTarget = 90.0f, g_fovSaved = 0.0f;

volatile bool g_fovHasSaved = false;

static LARGE_INTEGER g_qpcFreq = {
    0
}
, g_qpcLast = {
    0
};

CRITICAL_SECTION g_cs;

bool g_csReady = false;

volatile LONG g_pending = 0;

int g_reqState = 0;

char g_luaQ[16][128];

int g_luaCount = 0;

void Log(const char* t) {

    FILE* f;
    if (fopen_s(&f, "CameraMod_Log.txt", "a") == 0) {
        SYSTEMTIME s;
        GetLocalTime(&s);
        fprintf(f, "[%02d:%02d:%02d] %s\n", s.wHour, s.wMinute, s.wSecond, t);
        fclose(f);
    }
    printf("%s\n", t);

}

void QueueLua(const char* code) {

    if (!g_csReady)return;
    EnterCriticalSection(&g_cs);
    if (g_luaCount < 16) {
        strcpy_s(g_luaQ[g_luaCount], sizeof(g_luaQ[0]), code);
        g_luaCount++;
    }
    g_pending = 1;
    LeaveCriticalSection(&g_cs);

}

void QueueCVarF(const char* name, float v) {
    char b[96];
    sprintf_s(b, "SetCVar('%s', %.2f)", name, v);
    QueueLua(b);
}

void RequestState(int st) {
    if (!g_csReady)return;
    EnterCriticalSection(&g_cs);
    g_reqState = st;
    g_pending = 1;
    LeaveCriticalSection(&g_cs);
}

void ExecuteLuaNow(const char* code) {
    if (!code || !*code)return;
    __try {
        ExecuteBuffer(code, "Heroes", 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void SetMaxDistanceMemory(float meters) {
    DWORD op;
    if (VirtualProtect((void*)CAM_LIMIT_ADDR, 4, PAGE_EXECUTE_READWRITE, &op)) {
        *(float*)CAM_LIMIT_ADDR = meters;
        VirtualProtect((void*)CAM_LIMIT_ADDR, 4, op, &op);
    }
}

void PatchClamp(bool on) {
    DWORD op;
    if (VirtualProtect((void*)CLAMP_JE_ADDR, 2, PAGE_EXECUTE_READWRITE, &op)) {
        if (on) {
            *(BYTE*)CLAMP_JE_ADDR = 0x90;
            *(BYTE*)(CLAMP_JE_ADDR + 1) = 0x90;
        }
        else {
            *(BYTE*)CLAMP_JE_ADDR = 0x74;
            *(BYTE*)(CLAMP_JE_ADDR + 1) = 0x08;
        }
        VirtualProtect((void*)CLAMP_JE_ADDR, 2, op, &op);
    }
}

DWORD GetActiveCamera() {
    __try {
        DWORD mgr = *(DWORD*)CAM_MGR_PTR;
        if (!mgr)return 0;
        return *(DWORD*)(mgr + CAM_OBJ_OFFSET);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

float GetCameraDistance() {
    DWORD cam = GetActiveCamera();
    if (!cam)return -1.0f;
    __try {
        return *(float*)(cam + CAM_DIST_OFFSET);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1.0f;
    }
}

inline void PinDistance(DWORD cam, float v) {
    *(float*)(cam + CAM_DIST_OFFSET) = v;
    *(float*)(cam + CAM_STATE_OFFSET) = v;
    *(float*)(cam + CAM_VEL_OFFSET) = 0.0f;
}

void SetBothDistance(float v) {
    DWORD cam = GetActiveCamera();
    if (!cam)return;
    __try {
        PinDistance(cam, v);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void DrainPending() {

    if (!g_pending)return;
    int st = 0;
    char lua[16][128];
    int n = 0;
    EnterCriticalSection(&g_cs);
    st = g_reqState;
    g_reqState = 0;
    n = g_luaCount;
    for (int i = 0;i < n;i++)strcpy_s(lua[i], sizeof(lua[0]), g_luaQ[i]);
    g_luaCount = 0;
    g_pending = 0;
    LeaveCriticalSection(&g_cs);

    if (st == 1) {
        PatchClamp(true);
        SetMaxDistanceMemory(DIST_MAX);
        float cur = GetCameraDistance();
        if (cur < DIST_MIN)cur = DIST_MIN;
        if (cur > DIST_MAX)cur = DIST_MAX;
        g_curDist = cur;
        g_targetDist = cur;
        g_qpcFreq.QuadPart = 0;
        unlocked = true;
        if (hMenu)PostMessageA(hMenu, WM_SETDIST, 0, (LPARAM)(int)cur);
    }

    else if (st == 2) {
        unlocked = false;
        PatchClamp(false);
        SetMaxDistanceMemory(50.0f);
        g_curDist = g_targetDist = 50.0f;
        DWORD cam = GetActiveCamera();
        if (cam) {
            __try {
                PinDistance(cam, 50.0f);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }

    for (int i = 0;i < n;i++) {
        __try {
            ExecuteBuffer(lua[i], "CameraMod", 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

}

typedef void(__fastcall* CamUpd_t)(void*, void*, int, int);

CamUpd_t oCamUpd = nullptr;

void __fastcall hkCamUpd(void* ecx, void* edx, int a1, int a2) {

    DrainPending();
    HeroesTick();

    if (unlocked) {
        if (g_qpcFreq.QuadPart == 0) {
            QueryPerformanceFrequency(&g_qpcFreq);
            QueryPerformanceCounter(&g_qpcLast);
        }
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - g_qpcLast.QuadPart) / (float)g_qpcFreq.QuadPart;
        g_qpcLast = now;
        if (dt > 0.1f)dt = 0.1f;
        if (dt < 0)dt = 0;
        float t = 1.0f - expf(-SMOOTH_SPEED * dt);
        g_curDist += (g_targetDist - g_curDist) * t;
        if (fabsf(g_targetDist - g_curDist) < 0.01f)g_curDist = g_targetDist;
        __try {
            if (ecx)PinDistance((DWORD)ecx, g_curDist);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    if (oCamUpd)oCamUpd(ecx, edx, a1, a2);

    if (unlocked) {
        __try {
            if (ecx)PinDistance((DWORD)ecx, g_curDist);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    if (ecx) {
        __try {
            float* pf = (float*)((DWORD)ecx + CAM_FOV_OFFSET);
            if (g_fovEnabled) {
                if (!g_fovHasSaved) {
                    g_fovSaved = *pf;
                    g_fovHasSaved = true;
                }
                *pf = g_fovTarget * DEG2RAD;
            }
            else if (g_fovHasSaved) {
                *pf = g_fovSaved;
                g_fovHasSaved = false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

}

int SliderPos(HWND s) {
    return(int)SendMessage(s, TBM_GETPOS, 0, 0);
}

void UpdateLabels() {
    char b[96];
    sprintf_s(b, "Max Camera Distance:   %d yd", SliderPos(hSlDist));
    SetWindowTextA(hLblDist, b);
    sprintf_s(b, "Zoom Speed:   %d", SliderPos(hSlSpeed));
    SetWindowTextA(hLblSpeed, b);
    sprintf_s(b, "Rotation Speed:   %d", SliderPos(hSlRot));
    SetWindowTextA(hLblRot, b);
}

void UpdateFovLabel() {
    if (!hLblFov)return;
    char b[96];
    sprintf_s(b, "FOV (zoom out):   %d deg", SliderPos(hSlFov));
    SetWindowTextA(hLblFov, b);
}

void UpdateEspLabels() {
    char b[96];
    if (hLblRadius && hSlRadius) {
        sprintf_s(b, "Target radius:   %.1f yd", SliderPos(hSlRadius) / 10.0f);
        SetWindowTextA(hLblRadius, b);
    }
    if (hLblSelfRadius && hSlSelfRadius) {
        sprintf_s(b, "Self radius:   %.1f yd", SliderPos(hSlSelfRadius) / 10.0f);
        SetWindowTextA(hLblSelfRadius, b);
    }
    if (hLblEspDist && hSlEspDist) {
        sprintf_s(b, "ESP distance:   %d yd", SliderPos(hSlEspDist));
        SetWindowTextA(hLblEspDist, b);
    }
    if (hLblNameSize && hSlNameSize) {
        sprintf_s(b, "Name size:   %d px", SliderPos(hSlNameSize));
        SetWindowTextA(hLblNameSize, b);
    }
}

WNDPROC oGameWndProc = NULL;

LRESULT CALLBACK GameWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_MOUSEWHEEL && unlocked) {
        short delta = (short)HIWORD(w);
        float nt = g_targetDist - ((float)delta / 120.0f) * WHEEL_STEP;
        if (nt < DIST_MIN)nt = DIST_MIN;
        if (nt > DIST_MAX)nt = DIST_MAX;
        g_targetDist = nt;
        if (hSlDist) {
            PostMessageA(hSlDist, TBM_SETPOS, TRUE, (int)nt);
            PostMessageA(hMenu, WM_REFRESHLBL, 0, 0);
        }
        return 0;
    }
    return CallWindowProcA(oGameWndProc, hwnd, msg, w, l);
}

void InstallWheelHook() {
    if (hGame && !oGameWndProc)oGameWndProc = (WNDPROC)SetWindowLongPtrA(hGame, GWLP_WNDPROC, (LONG_PTR)GameWndProc);
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
    if (hSlDist) InvalidateRect(hSlDist, NULL, TRUE);
}

void BringMenuToFront(HWND h) {
    ShowWindow(h, SW_SHOW);
    SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    DWORD me = GetCurrentThreadId();
    DWORD other = hGame ? GetWindowThreadProcessId(hGame, NULL) : 0;
    if (other && other != me)AttachThreadInput(me, other, TRUE);
    BringWindowToTop(h);
    SetForegroundWindow(h);
    SetActiveWindow(h);
    if (other && other != me)AttachThreadInput(me, other, FALSE);
    ClipCursor(NULL);
    while (ShowCursor(TRUE) < 0) {
    }
    RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static void PaintFrame(HWND h, HDC dc) {
    RECT rc;
    GetClientRect(h, &rc);
    FillRect(dc, &rc, hbrBack);

    HPEN outer = CreatePen(PS_SOLID, 2, CLR_GOLDDK);
    HPEN inner = CreatePen(PS_SOLID, 1, CLR_ACCENT);
    HPEN divider = CreatePen(PS_SOLID, 1, CLR_DIVDK);
    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    HPEN oldPen = (HPEN)SelectObject(dc, outer);

    Rectangle(dc, rc.left + 3, rc.top + 3, rc.right - 3, rc.bottom - 3);
    SelectObject(dc, inner);
    Rectangle(dc, rc.left + 6, rc.top + 6, rc.right - 6, rc.bottom - 6);

    SelectObject(dc, divider);
    MoveToEx(dc, 16, 92, NULL);
    LineTo(dc, rc.right - 16, 92);
    if (g_tab == 1) {
        MoveToEx(dc, 16, 132, NULL);
        LineTo(dc, rc.right - 16, 132);
    }
    MoveToEx(dc, 16, rc.bottom - 40, NULL);
    LineTo(dc, rc.right - 16, rc.bottom - 40);

    // Keep the original YamiYami wordmark and serif styling unchanged.
    HFONT oldFont = (HFONT)SelectObject(dc, hFontTitle);
    SetBkMode(dc, TRANSPARENT);
    RECT titleRect = { 0, 12, rc.right, 44 };
    SetTextColor(dc, RGB(0, 0, 0));
    OffsetRect(&titleRect, 1, 2);
    DrawTextA(dc, "YamiYami", -1, &titleRect, DT_CENTER | DT_SINGLELINE);
    OffsetRect(&titleRect, -1, -2);
    SetTextColor(dc, CLR_GOLD);
    DrawTextA(dc, "YamiYami", -1, &titleRect, DT_CENTER | DT_SINGLELINE);

    SelectObject(dc, oldFont);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(outer);
    DeleteObject(inner);
    DeleteObject(divider);
}

static void DrawCheck(LPDRAWITEMSTRUCT d, const char* label, bool checked, bool bigFont) {
    RECT rc = d->rcItem;
    FillRect(d->hDC, &rc, hbrBack);
    int bs = 18, by = rc.top + ((rc.bottom - rc.top) - bs) / 2;
    RECT box = {
        rc.left,by,rc.left + bs,by + bs
    };
    HBRUSH inr = CreateSolidBrush(CLR_BOX);
    FillRect(d->hDC, &box, inr);
    DeleteObject(inr);
    HBRUSH frame = CreateSolidBrush(checked ? CLR_ACCENT : CLR_GOLDDK);
    FrameRect(d->hDC, &box, frame);
    DeleteObject(frame);
    if (checked) {
        RECT ck = {
            box.left + 4,box.top + 4,box.right - 4,box.bottom - 4
        };
        HBRUSH f = CreateSolidBrush(CLR_ACCENT);
        FillRect(d->hDC, &ck, f);
        DeleteObject(f);
    }
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, checked ? CLR_GOLD : CLR_TEXT);
    HFONT of = (HFONT)SelectObject(d->hDC, bigFont ? hFontHead : hFontNorm);
    RECT tr = {
        box.right + 10,rc.top,rc.right,rc.bottom
    };
    DrawTextA(d->hDC, label, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(d->hDC, of);
}

static void DrawTab(LPDRAWITEMSTRUCT d, const char* label, bool active) {
    RECT rc = d->rcItem;
    HBRUSH bg = CreateSolidBrush(active ? CLR_GOLDDK : CLR_BOX);
    FillRect(d->hDC, &rc, bg);
    DeleteObject(bg);
    HBRUSH fr = CreateSolidBrush(active ? CLR_ACCENT : CLR_GOLDDK);
    FrameRect(d->hDC, &rc, fr);
    DeleteObject(fr);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, active ? CLR_GOLD : CLR_TEXT);
    HFONT of = (HFONT)SelectObject(d->hDC, hFontHead);
    DrawTextA(d->hDC, label, -1, &rc, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(d->hDC, of);
}

static void DrawSubTab(LPDRAWITEMSTRUCT d, const char* label, bool active) {
    RECT rc = d->rcItem;
    HBRUSH background = CreateSolidBrush(active ? CLR_GOLDDK : CLR_BOX);
    FillRect(d->hDC, &rc, background);
    DeleteObject(background);

    HBRUSH frame = CreateSolidBrush(active ? CLR_ACCENT : CLR_DIVDK);
    FrameRect(d->hDC, &rc, frame);
    DeleteObject(frame);

    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, active ? CLR_GOLD : CLR_TEXT);
    HFONT oldFont = (HFONT)SelectObject(d->hDC, hFontNorm);
    DrawTextA(d->hDC, label, -1, &rc, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(d->hDC, oldFont);
}

static void ShowEspSection(int section) {
    if (section < 0 || section > 4)
        section = 0;
    g_espSection = section;

    for (int i = 0; i < 11; ++i)
        ShowWindow(hEsp[i], SW_HIDE);
    ShowWindow(hLblRadius, SW_HIDE);
    ShowWindow(hSlRadius, SW_HIDE);
    ShowWindow(hLblSelfRadius, SW_HIDE);
    ShowWindow(hSlSelfRadius, SW_HIDE);
    ShowWindow(hLblEspDist, SW_HIDE);
    ShowWindow(hSlEspDist, SW_HIDE);
    ShowWindow(hLblNameSize, SW_HIDE);
    ShowWindow(hSlNameSize, SW_HIDE);

    if (g_tab == 1) {
        switch (g_espSection) {
        case 0: // Chams
            ShowWindow(hEsp[5], SW_SHOW);
            break;
        case 1: // Boxes
            ShowWindow(hEsp[0], SW_SHOW);
            ShowWindow(hEsp[2], SW_SHOW);
            ShowWindow(hEsp[7], SW_SHOW);
            ShowWindow(hEsp[8], SW_SHOW);
            ShowWindow(hLblEspDist, SW_SHOW);
            ShowWindow(hSlEspDist, SW_SHOW);
            break;
        case 2: // Circles
            ShowWindow(hEsp[9], SW_SHOW);
            ShowWindow(hEsp[10], SW_SHOW);
            ShowWindow(hLblRadius, SW_SHOW);
            ShowWindow(hSlRadius, SW_SHOW);
            ShowWindow(hLblSelfRadius, SW_SHOW);
            ShowWindow(hSlSelfRadius, SW_SHOW);
            break;
        case 3: // Traces
            ShowWindow(hEsp[4], SW_SHOW);
            break;
        case 4: // Names
            ShowWindow(hEsp[1], SW_SHOW);
            ShowWindow(hEsp[3], SW_SHOW);
            ShowWindow(hLblNameSize, SW_SHOW);
            ShowWindow(hSlNameSize, SW_SHOW);
            break;
        }
    }

    for (int i = 0; i < 5; ++i)
        InvalidateRect(hEspTabs[i], NULL, TRUE);
}

static void ShowTab(int t) {
    g_tab = t;
    const int cameraState = t == 0 ? SW_SHOW : SW_HIDE;
    const int espState = t == 1 ? SW_SHOW : SW_HIDE;

    ShowWindow(hStatus, cameraState);
    ShowWindow(hCheck, cameraState);
    ShowWindow(hLblDist, cameraState);
    ShowWindow(hSlDist, cameraState);
    ShowWindow(hLblSpeed, cameraState);
    ShowWindow(hSlSpeed, cameraState);
    ShowWindow(hLblRot, cameraState);
    ShowWindow(hSlRot, cameraState);
    ShowWindow(hFovCheck, cameraState);
    ShowWindow(hLblFov, cameraState);
    ShowWindow(hSlFov, cameraState);

    ShowWindow(hLblEsp, SW_HIDE);
    for (int i = 0; i < 5; ++i)
        ShowWindow(hEspTabs[i], espState);

    if (t == 1)
        ShowEspSection(g_espSection);
    else {
        for (int i = 0; i < 11; ++i)
            ShowWindow(hEsp[i], SW_HIDE);
        ShowWindow(hLblRadius, SW_HIDE);
        ShowWindow(hSlRadius, SW_HIDE);
        ShowWindow(hLblSelfRadius, SW_HIDE);
        ShowWindow(hSlSelfRadius, SW_HIDE);
        ShowWindow(hLblEspDist, SW_HIDE);
        ShowWindow(hSlEspDist, SW_HIDE);
        ShowWindow(hLblNameSize, SW_HIDE);
        ShowWindow(hSlNameSize, SW_HIDE);
    }

    HeroesSetVisible(t == 2);
    InvalidateRect(hMenu, NULL, TRUE);
    InvalidateRect(hTabCam, NULL, TRUE);
    InvalidateRect(hTabEsp, NULL, TRUE);
    InvalidateRect(hTabHeroes, NULL, TRUE);
}

static bool IsYamiSlider(HWND control) {
    return control == hSlDist ||
        control == hSlSpeed ||
        control == hSlRot ||
        control == hSlFov ||
        control == hSlEspDist ||
        control == hSlRadius ||
        control == hSlSelfRadius ||
        control == hSlNameSize;
}

static void FormatSliderValue(HWND control, char* output, size_t outputSize) {
    if (!output || outputSize == 0) return;
    const int position = SliderPos(control);
    if (control == hSlRadius || control == hSlSelfRadius)
        sprintf_s(output, outputSize, "%.1f", position / 10.0f);
    else
        sprintf_s(output, outputSize, "%d", position);
}

static float SliderFraction(HWND control) {
    const int minimum = (int)SendMessageA(control, TBM_GETRANGEMIN, 0, 0);
    const int maximum = (int)SendMessageA(control, TBM_GETRANGEMAX, 0, 0);
    const int position = SliderPos(control);
    if (maximum <= minimum) return 0.0f;
    float fraction = (float)(position - minimum) / (float)(maximum - minimum);
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    return fraction;
}

static LRESULT DrawYamiSlider(LPNMCUSTOMDRAW custom, HWND control) {
    if (!custom || !control || custom->dwDrawStage != CDDS_PREPAINT)
        return CDRF_DODEFAULT;

    RECT client{};
    GetClientRect(control, &client);
    const bool enabled = IsWindowEnabled(control) != FALSE;
    const bool focused = GetFocus() == control;
    const COLORREF accent = enabled ? CLR_ACCENT : CLR_GOLDDK;
    const COLORREF frameColor = focused ? CLR_GOLD :
        (enabled ? CLR_SLIDER_FRAME : CLR_DIVDK);
    const COLORREF valueColor = enabled ? CLR_SLIDER_TEXT : CLR_MUTED;

    // Paint the complete control in one pass. The native trackbar remains
    // responsible for mouse/keyboard input, but never hides our visual thumb.
    HBRUSH background = CreateSolidBrush(CLR_SLIDER_BG);
    HPEN framePen = CreatePen(PS_SOLID, focused ? 2 : 1, frameColor);
    HBRUSH oldBrush = (HBRUSH)SelectObject(custom->hdc, background);
    HPEN oldPen = (HPEN)SelectObject(custom->hdc, framePen);
    RoundRect(custom->hdc,
        client.left, client.top,
        client.right, client.bottom,
        6, 6);
    SelectObject(custom->hdc, oldPen);
    SelectObject(custom->hdc, oldBrush);
    DeleteObject(framePen);
    DeleteObject(background);

    const int railLeft = client.left + 10;
    const int railRight = client.right - 10;
    const int railY = client.bottom - 7;
    const float fraction = SliderFraction(control);
    const int markerX = railLeft +
        (int)((railRight - railLeft) * fraction + 0.5f);

    RECT emptyRail{ railLeft, railY - 2, railRight, railY + 2 };
    HBRUSH railBrush = CreateSolidBrush(CLR_SLIDER_RAIL);
    FillRect(custom->hdc, &emptyRail, railBrush);
    DeleteObject(railBrush);

    RECT progressRail = emptyRail;
    progressRail.right = markerX;
    HBRUSH progressBrush = CreateSolidBrush(accent);
    FillRect(custom->hdc, &progressRail, progressBrush);
    DeleteObject(progressBrush);

    // Persistent circular thumb: always visible, not only while dragging.
    RECT thumb{ markerX - 5, railY - 5, markerX + 6, railY + 6 };
    HBRUSH thumbBrush = CreateSolidBrush(accent);
    HPEN thumbPen = CreatePen(PS_SOLID, 1, enabled ? CLR_GOLD : CLR_DIVDK);
    oldBrush = (HBRUSH)SelectObject(custom->hdc, thumbBrush);
    oldPen = (HPEN)SelectObject(custom->hdc, thumbPen);
    Ellipse(custom->hdc, thumb.left, thumb.top, thumb.right, thumb.bottom);
    SelectObject(custom->hdc, oldPen);
    SelectObject(custom->hdc, oldBrush);
    DeleteObject(thumbPen);
    DeleteObject(thumbBrush);

    // A small highlight keeps the thumb readable on bright backgrounds.
    HBRUSH highlight = CreateSolidBrush(enabled ? RGB(255, 235, 170) : CLR_MUTED);
    RECT highlightDot{ markerX - 1, railY - 2, markerX + 2, railY + 1 };
    FillRect(custom->hdc, &highlightDot, highlight);
    DeleteObject(highlight);

    char value[32]{};
    FormatSliderValue(control, value, sizeof(value));
    RECT valueRect = client;
    valueRect.bottom = railY - 3;
    SetBkMode(custom->hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(custom->hdc, hFontNorm);

    RECT shadow = valueRect;
    OffsetRect(&shadow, 1, 1);
    SetTextColor(custom->hdc, RGB(0, 0, 0));
    DrawTextA(custom->hdc, value, -1, &shadow,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    SetTextColor(custom->hdc, valueColor);
    DrawTextA(custom->hdc, value, -1, &valueRect,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(custom->hdc, oldFont);

    return CDRF_SKIPDEFAULT;
}

static int SliderPositionFromMouse(HWND control, int mouseX) {
    RECT client{};
    GetClientRect(control, &client);
    const int railLeft = client.left + 10;
    const int railRight = client.right - 10;
    if (mouseX < railLeft) mouseX = railLeft;
    if (mouseX > railRight) mouseX = railRight;

    const int minimum = (int)SendMessageA(control, TBM_GETRANGEMIN, 0, 0);
    const int maximum = (int)SendMessageA(control, TBM_GETRANGEMAX, 0, 0);
    if (railRight <= railLeft || maximum <= minimum) return minimum;

    const double fraction =
        (double)(mouseX - railLeft) / (double)(railRight - railLeft);
    int position = minimum +
        (int)((maximum - minimum) * fraction + 0.5);
    if (position < minimum) position = minimum;
    if (position > maximum) position = maximum;
    return position;
}

static void SetSliderPositionAndNotify(
    HWND control,
    int position,
    int notificationCode)
{
    if (!control || !IsWindowEnabled(control)) return;

    const int minimum = (int)SendMessageA(control, TBM_GETRANGEMIN, 0, 0);
    const int maximum = (int)SendMessageA(control, TBM_GETRANGEMAX, 0, 0);
    if (position < minimum) position = minimum;
    if (position > maximum) position = maximum;

    SendMessageA(control, TBM_SETPOS, TRUE, position);
    HWND parent = GetParent(control);
    if (parent) {
        SendMessageA(
            parent,
            WM_HSCROLL,
            MAKEWPARAM(notificationCode, position),
            (LPARAM)control);
    }
    InvalidateRect(control, NULL, FALSE);
    UpdateWindow(control);
}

static LRESULT CALLBACK YamiSliderSubclassProc(
    HWND control,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR referenceData)
{
    (void)subclassId;
    (void)referenceData;

    switch (message) {
    case WM_LBUTTONDOWN: {
        if (!IsWindowEnabled(control)) return 0;
        SetFocus(control);
        SetCapture(control);
        const int mouseX = (int)(short)LOWORD(lParam);
        const int position = SliderPositionFromMouse(control, mouseX);
        SetSliderPositionAndNotify(control, position, TB_THUMBTRACK);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (GetCapture() == control && (wParam & MK_LBUTTON)) {
            const int mouseX = (int)(short)LOWORD(lParam);
            const int position = SliderPositionFromMouse(control, mouseX);
            SetSliderPositionAndNotify(control, position, TB_THUMBTRACK);
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (GetCapture() == control) {
            const int mouseX = (int)(short)LOWORD(lParam);
            const int position = SliderPositionFromMouse(control, mouseX);
            SetSliderPositionAndNotify(control, position, TB_THUMBPOSITION);
            ReleaseCapture();

            HWND parent = GetParent(control);
            if (parent) {
                SendMessageA(
                    parent,
                    WM_HSCROLL,
                    MAKEWPARAM(TB_ENDTRACK, position),
                    (LPARAM)control);
            }
            InvalidateRect(control, NULL, FALSE);
            return 0;
        }
        break;

    case WM_MOUSEWHEEL:
        if (IsWindowEnabled(control)) {
            const short wheelDelta = (short)HIWORD(wParam);
            const int current = SliderPos(control);
            const int step = wheelDelta > 0 ? 1 : -1;
            SetSliderPositionAndNotify(
                control,
                current + step,
                TB_THUMBPOSITION);
            return 0;
        }
        break;

    case WM_KEYDOWN: {
        int position = SliderPos(control);
        bool handled = true;
        switch (wParam) {
        case VK_LEFT:
        case VK_DOWN:
            --position;
            break;
        case VK_RIGHT:
        case VK_UP:
            ++position;
            break;
        case VK_HOME:
            position = (int)SendMessageA(control, TBM_GETRANGEMIN, 0, 0);
            break;
        case VK_END:
            position = (int)SendMessageA(control, TBM_GETRANGEMAX, 0, 0);
            break;
        default:
            handled = false;
            break;
        }
        if (handled) {
            SetSliderPositionAndNotify(control, position, TB_THUMBPOSITION);
            return 0;
        }
        break;
    }

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(control, NULL, TRUE);
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(control, YamiSliderSubclassProc, 0x59414D49u);
        break;
    }

    return DefSubclassProc(control, message, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {

    switch (m) {

    case WM_TOGGLEMENU:
        if (w)BringMenuToFront(h);
        else ShowWindow(h, SW_HIDE);
        return 0;

    case WM_REFRESHLBL:
        UpdateLabels();
        return 0;

    case WM_SETDIST:
        if (hSlDist) {
            SendMessage(hSlDist, TBM_SETPOS, TRUE, (int)l);
            UpdateLabels();
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PaintFrame(h, dc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_NCHITTEST: {
        POINT p = {
            (int)(short)LOWORD(l),(int)(short)HIWORD(l)
        };
        ScreenToClient(h, &p);
        RECT rc;
        GetClientRect(h, &rc);
        if (p.y >= 0 && p.y < 50 && p.x >= 0 && p.x < rc.right)return HTCAPTION;
        return HTCLIENT;
    }

    case WM_ACTIVATE:
        if (LOWORD(w) != WA_INACTIVE)ClipCursor(NULL);
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        HWND c = (HWND)l;
        SetBkMode(dc, TRANSPARENT);
        LRESULT heroesResult = 0;
        if (HeroesHandleStaticColor(c, dc, heroesResult)) return heroesResult;
        bool gold = c == hStatus || c == hLblDist || c == hLblSpeed || c == hLblRot || c == hLblFov || c == hLblEsp || c == hLblRadius || c == hLblSelfRadius || c == hLblEspDist || c == hLblNameSize;
        SetTextColor(dc, gold ? CLR_GOLD : CLR_TEXT);
        return(LRESULT)hbrBack;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)l;
        if (HeroesDrawItem(d)) return TRUE;
        switch (d->CtlID) {
        case 100:
            DrawTab(d, "Camera", g_tab == 0);
            return TRUE;
        case 101:
            DrawTab(d, "ESP", g_tab == 1);
            return TRUE;
        case 102:
            DrawTab(d, "Heroes", g_tab == 2);
            return TRUE;
        case 110:
            DrawSubTab(d, "Chams", g_espSection == 0);
            return TRUE;
        case 111:
            DrawSubTab(d, "Boxes", g_espSection == 1);
            return TRUE;
        case 112:
            DrawSubTab(d, "Circles", g_espSection == 2);
            return TRUE;
        case 113:
            DrawSubTab(d, "Traces", g_espSection == 3);
            return TRUE;
        case 114:
            DrawSubTab(d, "Names", g_espSection == 4);
            return TRUE;
        case 1:
            DrawCheck(d, "Unlock Camera Distance", g_unlockChecked, true);
            return TRUE;
        case 2:
            DrawCheck(d, "Enable FOV (zoom out)", g_fovEnabled, true);
            return TRUE;
        case 10:
            DrawCheck(d, "Enable ESP", g_espEnabled, false);
            return TRUE;
        case 12:
            DrawCheck(d, "Names", g_drawNames, false);
            return TRUE;
        case 13:
            DrawCheck(d, "Health Bars", g_drawHP, false);
            return TRUE;
        case 14:
            DrawCheck(d, "Distance", g_drawDistance, false);
            return TRUE;
        case 15:
            DrawCheck(d, "Tracers", g_drawTracers, false);
            return TRUE;
        case 23:
            DrawCheck(d, "Target Radius", g_drawRadius, false);
            return TRUE;
        case 24:
            DrawCheck(d, "Self Radius", g_drawSelfRadius, false);
            return TRUE;
        case 20:
            DrawCheck(d, "Chams (model wallhack)", g_chamsEnabled, false);
            return TRUE;
        case 21:
            DrawCheck(d, "Show Players", g_espShowPlayers, false);
            return TRUE;
        case 22:
            DrawCheck(d, "Show NPCs (Mobs)", g_espShowNpcs, false);
            return TRUE;
        }
        break;
    }

    case WM_NOTIFY: {
        LPNMHDR header = (LPNMHDR)l;
        if (header && header->code == NM_CUSTOMDRAW && IsYamiSlider(header->hwndFrom))
            return DrawYamiSlider((LPNMCUSTOMDRAW)l, header->hwndFrom);
        break;
    }

    case WM_HSCROLL: {
        HWND s = (HWND)l;
        int code = LOWORD(w);
        bool released = code == TB_ENDTRACK;
        if (s == hSlDist && unlocked) {
            int pos = (code == TB_THUMBTRACK || code == TB_THUMBPOSITION) ? (int)(short)HIWORD(w) : SliderPos(s);
            if (pos < (int)DIST_MIN)pos = (int)DIST_MIN;
            if (pos > (int)DIST_MAX)pos = (int)DIST_MAX;
            g_targetDist = (float)pos;
        }
        else if (released && s == hSlSpeed)QueueCVarF("cameraDistanceMoveSpeed", (float)SliderPos(s));
        else if (released && s == hSlRot)QueueCVarF("cameraYawMoveSpeed", (float)SliderPos(s));
        else if (s == hSlFov) {
            g_fovTarget = (float)SliderPos(s);
            UpdateFovLabel();
        }
        else if (s == hSlRadius) {
            g_radiusYards = (float)SliderPos(s) / 10.0f;
            UpdateEspLabels();
        }
        else if (s == hSlSelfRadius) {
            g_selfRadiusYards = (float)SliderPos(s) / 10.0f;
            UpdateEspLabels();
        }
        else if (s == hSlEspDist) {
            g_espDist = (float)SliderPos(s);
            UpdateEspLabels();
        }
        else if (s == hSlNameSize) {
            g_nameFontSize = (float)SliderPos(s);
            UpdateEspLabels();
        }
        UpdateLabels();
        if (s && IsYamiSlider(s))
            InvalidateRect(s, NULL, FALSE);
        break;
    }

    case WM_COMMAND:
        if (HIWORD(w) == BN_CLICKED) {
            UINT id = LOWORD(w);
            if (HeroesHandleCommand(id)) break;
            if (id == 100)ShowTab(0);
            else if (id == 101)ShowTab(1);
            else if (id == 102)ShowTab(2);
            else if (id >= 110 && id <= 114)ShowEspSection((int)id - 110);
            else if (id == 1) {
                g_unlockChecked = !g_unlockChecked;
                InvalidateRect(hCheck, NULL, TRUE);
                ApplyUnlock(g_unlockChecked);
            }
            else if (id == 2) {
                g_fovEnabled = !g_fovEnabled;
                if (g_fovEnabled)g_fovTarget = (float)SliderPos(hSlFov);
                InvalidateRect(hFovCheck, NULL, TRUE);
            }
            else if (id >= 10 && id <= 24) {
                switch (id) {
                case 10:
                    g_espEnabled = !g_espEnabled;
                    break;
                case 12:
                    g_drawNames = !g_drawNames;
                    break;
                case 13:
                    g_drawHP = !g_drawHP;
                    break;
                case 14:
                    g_drawDistance = !g_drawDistance;
                    break;
                case 15:
                    g_drawTracers = !g_drawTracers;
                    break;
                case 23:
                    g_drawRadius = !g_drawRadius;
                    break;
                case 24:
                    g_drawSelfRadius = !g_drawSelfRadius;
                    break;
                case 20:
                    g_chamsEnabled = !g_chamsEnabled;
                    break;
                case 21:
                    g_espShowPlayers = !g_espShowPlayers;
                    break;
                case 22:
                    g_espShowNpcs = !g_espShowNpcs;
                    break;
                }
                InvalidateRect((HWND)l, NULL, TRUE);
            }
        }
        break;

    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        menuVisible = false;
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(h, m, w, l);

    }
    return 0;
}

static HWND mkLabel(const char* t, int x, int y, int wd, HFONT f) {
    HWND o = CreateWindowA("STATIC", t, WS_VISIBLE | WS_CHILD, x, y, wd, 22, hMenu, NULL, hSelf, NULL);
    SendMessage(o, WM_SETFONT, (WPARAM)f, TRUE);
    return o;
}

static HWND mkSlider(int x, int y, int wd, int lo, int hi, int pos) {
    HWND o = CreateWindowA(
        TRACKBAR_CLASSA, "",
        WS_VISIBLE | WS_CHILD | WS_TABSTOP |
        TBS_HORZ | TBS_NOTICKS | TBS_FIXEDLENGTH,
        x, y, wd, 28,
        hMenu, NULL, hSelf, NULL);
    SendMessageA(o, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    SendMessageA(o, TBM_SETLINESIZE, 0, 1);
    SendMessageA(o, TBM_SETPAGESIZE, 0, (LPARAM)((hi - lo) > 20 ? (hi - lo) / 20 : 1));
    SendMessageA(o, TBM_SETTHUMBLENGTH, 0, 14);
    SendMessageA(o, TBM_SETPOS, TRUE, pos);
    SetWindowSubclass(o, YamiSliderSubclassProc, 0x59414D49u, 0);
    return o;
}

static HWND mkCheck(int id, int x, int y, int wd) {
    return CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, x, y, wd, 24, hMenu, (HMENU)(INT_PTR)id, hSelf, NULL);
}

HWND FindGameWindow() {
    HWND h = FindWindowA("GxWindowClassD3d", NULL);
    if (!h)h = FindWindowA("GxWindowClass", NULL);
    if (!h)h = FindWindowA(NULL, "World of Warcraft");
    return h;
}

DWORD WINAPI MenuThread(LPVOID) {

    INITCOMMONCONTROLSEX ic = {
        sizeof(ic),ICC_BAR_CLASSES
    };
    InitCommonControlsEx(&ic);
    hGame = FindGameWindow();
    hbrBack = CreateSolidBrush(CLR_BG);
    hFontTitle = CreateFontA(28, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Palatino Linotype");
    hFontHead = CreateFontA(17, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Palatino Linotype");
    hFontNorm = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Segoe UI");

    WNDCLASSA wc = {
        0
    };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hSelf;
    wc.hbrBackground = hbrBack;
    wc.lpszClassName = "CamMod";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    const int W = 540, H = 600;
    int x = 200, y = 200;
    RECT gr;
    if (hGame && GetWindowRect(hGame, &gr)) {
        x = gr.left + ((gr.right - gr.left) - W) / 2;
        y = gr.top + ((gr.bottom - gr.top) - H) / 2;
    }

    hMenu = CreateWindowExA(WS_EX_TOPMOST, "CamMod", "YamiYami",
        WS_POPUP | WS_CLIPCHILDREN, x, y, W, H, hGame, NULL, hSelf, NULL);

    hTabCam = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        24, 52, 148, 30, hMenu, (HMENU)100, hSelf, NULL);
    hTabEsp = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        196, 52, 148, 30, hMenu, (HMENU)101, hSelf, NULL);
    hTabHeroes = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        368, 52, 148, 30, hMenu, (HMENU)102, hSelf, NULL);

    hStatus = mkLabel("Status:  LOCKED (50 yd)", 24, 110, 492, hFontNorm);
    hCheck = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        24, 138, 492, 26, hMenu, (HMENU)1, hSelf, NULL);
    hLblDist = mkLabel("Max Camera Distance:   15 yd", 24, 178, 492, hFontHead);
    hSlDist = mkSlider(24, 204, 492, 15, 1000, 15);
    hLblSpeed = mkLabel("Zoom Speed:   20", 24, 244, 492, hFontHead);
    hSlSpeed = mkSlider(24, 270, 492, 5, 50, 20);
    hLblRot = mkLabel("Rotation Speed:   180", 24, 310, 492, hFontHead);
    hSlRot = mkSlider(24, 336, 492, 90, 360, 180);
    hFovCheck = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        24, 382, 492, 26, hMenu, (HMENU)2, hSelf, NULL);
    hLblFov = mkLabel("FOV (zoom out):   90 deg", 24, 420, 492, hFontHead);
    hSlFov = mkSlider(24, 446, 492, 40, 150, 90);

    hEspTabs[0] = CreateWindowA("BUTTON", "", WS_CHILD | BS_OWNERDRAW,
        24, 100, 92, 26, hMenu, (HMENU)110, hSelf, NULL);
    hEspTabs[1] = CreateWindowA("BUTTON", "", WS_CHILD | BS_OWNERDRAW,
        124, 100, 92, 26, hMenu, (HMENU)111, hSelf, NULL);
    hEspTabs[2] = CreateWindowA("BUTTON", "", WS_CHILD | BS_OWNERDRAW,
        224, 100, 92, 26, hMenu, (HMENU)112, hSelf, NULL);
    hEspTabs[3] = CreateWindowA("BUTTON", "", WS_CHILD | BS_OWNERDRAW,
        324, 100, 92, 26, hMenu, (HMENU)113, hSelf, NULL);
    hEspTabs[4] = CreateWindowA("BUTTON", "", WS_CHILD | BS_OWNERDRAW,
        424, 100, 92, 26, hMenu, (HMENU)114, hSelf, NULL);

    hLblEsp = mkLabel("ESP", 24, 140, 492, hFontHead);

    // Boxes section.
    hEsp[0] = mkCheck(10, 24, 184, 232);
    hEsp[2] = mkCheck(13, 284, 184, 232);
    hEsp[7] = mkCheck(21, 24, 216, 232);
    hEsp[8] = mkCheck(22, 284, 216, 232);
    hLblEspDist = mkLabel("ESP distance:   200 yd", 24, 276, 492, hFontHead);
    hSlEspDist = mkSlider(24, 302, 492, 20, 1000, 200);

    // Names, traces, chams and circles sections.
    hEsp[1] = mkCheck(12, 24, 184, 232);
    hEsp[3] = mkCheck(14, 284, 184, 232);
    hEsp[4] = mkCheck(15, 24, 184, 232);
    hEsp[5] = mkCheck(20, 24, 184, 492);
    hEsp[9] = mkCheck(23, 24, 184, 232);
    hEsp[10] = mkCheck(24, 284, 184, 232);
    hLblRadius = mkLabel("Target radius:   12.0 yd", 24, 226, 232, hFontHead);
    hSlRadius = mkSlider(24, 252, 232, 5, 500, 120);
    hLblSelfRadius = mkLabel("Self radius:   10.0 yd", 284, 226, 232, hFontHead);
    hSlSelfRadius = mkSlider(284, 252, 232, 5, 500, 100);
    hLblNameSize = mkLabel("Name size:   16 px", 24, 226, 492, hFontHead);
    hSlNameSize = mkSlider(24, 252, 492, 10, 30, 16);
    HeroesUiTheme heroesTheme = {};
    heroesTheme.background = hbrBack;
    heroesTheme.headingFont = hFontHead;
    heroesTheme.normalFont = hFontNorm;
    heroesTheme.box = CLR_BOX;
    heroesTheme.gold = CLR_GOLD;
    heroesTheme.goldDark = CLR_GOLDDK;
    heroesTheme.text = CLR_TEXT;
    HeroesInitialize(hMenu, hSelf, heroesTheme, &ExecuteLuaNow, &QueueLua);

    hLblFoot = mkLabel("Menu:  '      Unload:  END      Wheel zooms to 1000 when ON", 24, H - 34, W - 48, hFontNorm);
    EnableWindow(hSlDist, FALSE);
    ShowTab(0);
    InstallWheelHook();
    if (!oGameWndProc)Log("WARNING: could not subclass game window for wheel zoom (slider still works)");
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;

}

DWORD WINAPI MainThread(LPVOID p) {

    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    FILE* lf;
    if (fopen_s(&lf, "CameraMod_Log.txt", "w") == 0)fclose(lf);
    Log("=== YamiYami loaded ===");
    InitializeCriticalSection(&g_cs);
    g_csReady = true;

    MH_STATUS initStatus = MH_Initialize();
    if ((initStatus == MH_OK || initStatus == MH_ERROR_ALREADY_INITIALIZED) && MH_CreateHook((LPVOID)CAM_UPDATE_ADDR, &hkCamUpd, (LPVOID*)&oCamUpd) == MH_OK && MH_EnableHook((LPVOID)CAM_UPDATE_ADDR) == MH_OK)Log("camera update hook installed (0x606F90); distance=0x1E8, fov=0x40");
    else Log("ERROR: failed to install camera hook");

    CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    CreateThread(NULL, 0, EspInitThread, NULL, 0, NULL);
    Log("ESP init thread started");

    while (true) {
        if (GetAsyncKeyState(VK_END) & 0x8000)break;
        if (GetAsyncKeyState(VK_OEM_7) & 0x8000) {
            if (hMenu) {
                menuVisible = !menuVisible;
                PostMessageA(hMenu, WM_TOGGLEMENU, menuVisible ? 1 : 0, 0);
            }
            Sleep(300);
        }
        Sleep(20);
    }

    HeroesShutdown();
    g_fovEnabled = false;
    RequestState(2);
    Sleep(150);
    unlocked = false;
    RemoveWheelHook();
    EspShutdown();
    MH_DisableHook((LPVOID)CAM_UPDATE_ADDR);
    Sleep(30);
    MH_Uninitialize();
    PatchClamp(false);
    SetMaxDistanceMemory(50.0f);
    SetBothDistance(50.0f);
    if (g_csReady) {
        g_csReady = false;
        DeleteCriticalSection(&g_cs);
    }
    Sleep(50);
    if (hMenu)DestroyWindow(hMenu);
    if (f)fclose(f);
    FreeConsole();
    FreeLibraryAndExitThread((HMODULE)p, 0);
    return 0;

}

BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        hSelf = h;
        DisableThreadLibraryCalls(h);
        CreateThread(NULL, 0, MainThread, h, 0, NULL);
    }
    return TRUE;
}
