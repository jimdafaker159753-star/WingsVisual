#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <cmath>
#include "EspSettings.h"

// ---------------- типы ----------------
struct Vector3 { float x, y, z; };
struct Vector2 { float x, y; };

struct CGCameraSnapshot {
    Vector3 pos;
    float   m[3][3];   // row0=forward, row1=left, row2=up
    float   fov;
    float   aspectMem;
};

// ---------------- адреса камеры (12340) ----------------
namespace Cam {
    constexpr uintptr_t CAM_MGR_PTR = 0x00B7436C;
    constexpr uintptr_t CAM_OBJ_OFFSET = 0x7E20;
    constexpr uintptr_t POS_OFFSET = 0x08;
    constexpr uintptr_t MATRIX_OFFSET = 0x14;
    constexpr uintptr_t FOV_OFFSET = 0x40;
    constexpr uintptr_t ASPECT_OFFSET = 0x44;
}

static IDirect3DDevice9* g_espDev = nullptr;

// Настоящая линза игры (пишется хуком SetTransform в ESP.cpp)
static volatile bool  g_projScaleValid = false;
static volatile float g_projXScale = 0.0f; // proj._11
static volatile float g_projYScale = 0.0f; // proj._22

static const float DEG2RAD = 0.01745329252f;

// ---------------- безопасное чтение ----------------
template <typename T>
static bool SafeReadT(uintptr_t addr, T& out) {
    __try { out = *reinterpret_cast<T*>(addr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool SafeReadBytes(uintptr_t addr, void* dst, size_t n) {
    __try { memcpy(dst, reinterpret_cast<void*>(addr), n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---------------- снимок камеры (double-deref) ----------------
static bool GetCameraSnapshot(CGCameraSnapshot& out) {
    uintptr_t mgr = 0;
    if (!SafeReadT(Cam::CAM_MGR_PTR, mgr) || !mgr) return false;
    uintptr_t camObj = 0;
    if (!SafeReadT(mgr + Cam::CAM_OBJ_OFFSET, camObj) || !camObj) return false;
    if (!SafeReadBytes(camObj + Cam::POS_OFFSET, &out.pos, sizeof(Vector3))) return false;
    if (!SafeReadBytes(camObj + Cam::MATRIX_OFFSET, out.m, sizeof(float) * 9)) return false;
    if (!SafeReadT(camObj + Cam::FOV_OFFSET, out.fov)) out.fov = 1.5708f;
    if (!SafeReadT(camObj + Cam::ASPECT_OFFSET, out.aspectMem)) out.aspectMem = 0.0f;

    float len0 = out.m[0][0] * out.m[0][0] + out.m[0][1] * out.m[0][1] + out.m[0][2] * out.m[0][2];
    if (len0 < 0.5f || len0 > 1.5f) return false;
    if (out.fov < 0.1f || out.fov > 3.14f) out.fov = 1.5708f;
    return true;
}

static inline float Dot3(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// ---------------- World -> Screen ----------------
static bool WorldToScreen(const Vector3& wp, Vector2& out, int w, int h) {
    CGCameraSnapshot c;
    if (!GetCameraSnapshot(c)) return false;

    Vector3 fwd{ c.m[0][0], c.m[0][1], c.m[0][2] };
    Vector3 left{ c.m[1][0], c.m[1][1], c.m[1][2] };
    Vector3 up{ c.m[2][0], c.m[2][1], c.m[2][2] };
    Vector3 right{ -left.x, -left.y, -left.z };

    Vector3 rel{ wp.x - c.pos.x, wp.y - c.pos.y, wp.z - c.pos.z };
    float vz = Dot3(rel, fwd);
    if (vz < 0.1f) return false;
    float vx = Dot3(rel, right);
    float vy = Dot3(rel, up);

    float xs, ys;
    if (g_projScaleValid && g_projXScale > 0.01f && g_projYScale > 0.01f) {
        xs = g_projXScale;   // реальная линза игры (FOV + aspect автоматически)
        ys = g_projYScale;
    }
    else {
        float fovV = (g_fovDegrees > 1.0f) ? (g_fovDegrees * DEG2RAD) : c.fov;
        float t = tanf(fovV * 0.5f); if (t < 0.0001f) t = 0.0001f;
        float aspect = (c.aspectMem > 0.1f) ? c.aspectMem : ((float)w / (float)h);
        ys = 1.0f / t;
        xs = ys / aspect;
    }

    out.x = (float)w * 0.5f * (1.0f + (vx / vz) * xs);
    out.y = (float)h * 0.5f * (1.0f - (vy / vz) * ys);
    return true;
}