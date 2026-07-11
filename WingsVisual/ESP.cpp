// ===============================================================
//  ESP.cpp — WingsVisual high-level entity CHAMS, stage 1.
//  Exact static targets for the supplied WoW 3.3.5a binary:
//    CGObject_C::SetModel      0x00743730 (model field +0xB4)
//    CM2SceneRender::DrawBatch 0x008203B0 (current model +0x60)
//  Trees/doodads fail closed because only models owned by Type 3/4 objects
//  are allowed to reach the CHAMS passes. No stack anchors or size guesses.
// ===============================================================
#include <windows.h>
#include <windowsx.h>
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

// ===== YamiYami category + HSV color wheels =====

extern HWND hMenu, hLblFoot, hTabCam, hTabEsp;
extern HWND hEsp[9];
extern HMODULE hSelf;
extern HFONT hFontNorm, hFontHead;
static HWND ycLabel = 0, ycCombo = 0, ycVis = 0, ycHid = 0;
static WNDPROC ycOld = 0;
static volatile LONG ycInstalled = 0;
static const UINT YC_INIT = WM_APP + 70, YC_REMOVE = WM_APP + 71;
static const int YC_COMBO = 4100;
static const UINT_PTR YC_TIMER = 4077;
struct YCState {
	uint32_t* c;
	const char* title;
	bool wheel;
	bool value;

	HDC memoryDc;
	HBITMAP bitmap;
	HBITMAP oldBitmap;
	uint32_t* pixels;
	int bufferWidth;
	int bufferHeight;
};
static YCState ysv{}, ysh{};
static float yc01(float v) {
	return v < 0 ? 0 : v>1 ? 1 : v;
}
static uint32_t ycHSV(float h, float s, float v) {
	h -= floorf(h);
	s = yc01(s);
	v = yc01(v);
	float r = v, g = v, b = v;
	if (s > .0001f) {
		float z = h * 6, f = z - floorf(z), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
		switch (((int)z) % 6) {
		case 0:r = v;
			g = t;
			b = p;
			break;
		case 1:r = q;
			g = v;
			b = p;
			break;
		case 2:r = p;
			g = v;
			b = t;
			break;
		case 3:r = p;
			g = q;
			b = v;
			break;
		case 4:r = t;
			g = p;
			b = v;
			break;
		default:r = v;
			g = p;
			b = q;
		}
	}
	return 0xFF000000u | ((uint32_t)(r * 255 + .5f) << 16) | ((uint32_t)(g * 255 + .5f) << 8) | (uint32_t)(b * 255 + .5f);
}
static void ycRGB(uint32_t c, float& h, float& s, float& v) {
	float r = ((c >> 16) & 255) / 255.f, g = ((c >> 8) & 255) / 255.f, b = (c & 255) / 255.f;
	float mx = max(r, max(g, b)), mn = min(r, min(g, b)), d = mx - mn;
	v = mx;
	s = mx > .0001f ? d / mx : 0;
	h = 0;
	if (d > .0001f) {
		if (mx == r)h = fmodf((g - b) / d, 6.f) / 6.f;
		else if (mx == g)h = ((b - r) / d + 2) / 6.f;
		else h = ((r - g) / d + 4) / 6.f;
		if (h < 0)h += 1;
	}
}
static void ycReleaseBuffer(YCState* state) {
	if (!state) return;

	if (state->memoryDc && state->oldBitmap) {
		SelectObject(state->memoryDc, state->oldBitmap);
	}
	if (state->bitmap) {
		DeleteObject(state->bitmap);
	}
	if (state->memoryDc) {
		DeleteDC(state->memoryDc);
	}

	state->memoryDc = nullptr;
	state->bitmap = nullptr;
	state->oldBitmap = nullptr;
	state->pixels = nullptr;
	state->bufferWidth = 0;
	state->bufferHeight = 0;
}

static bool ycEnsureBuffer(HWND window, YCState* state, int width, int height) {
	if (!state || width <= 0 || height <= 0) return false;

	if (state->memoryDc && state->bitmap && state->pixels &&
		state->bufferWidth == width && state->bufferHeight == height) {
		return true;
	}

	ycReleaseBuffer(state);

	HDC windowDc = GetDC(window);
	if (!windowDc) return false;

	BITMAPINFO bitmapInfo{};
	bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bitmapInfo.bmiHeader.biWidth = width;
	bitmapInfo.bmiHeader.biHeight = -height;
	bitmapInfo.bmiHeader.biPlanes = 1;
	bitmapInfo.bmiHeader.biBitCount = 32;
	bitmapInfo.bmiHeader.biCompression = BI_RGB;

	void* rawPixels = nullptr;
	HBITMAP bitmap = CreateDIBSection(
		windowDc,
		&bitmapInfo,
		DIB_RGB_COLORS,
		&rawPixels,
		nullptr,
		0);

	HDC memoryDc = CreateCompatibleDC(windowDc);
	ReleaseDC(window, windowDc);

	if (!bitmap || !memoryDc || !rawPixels) {
		if (bitmap) DeleteObject(bitmap);
		if (memoryDc) DeleteDC(memoryDc);
		return false;
	}

	state->memoryDc = memoryDc;
	state->bitmap = bitmap;
	state->oldBitmap = static_cast<HBITMAP>(SelectObject(memoryDc, bitmap));
	state->pixels = static_cast<uint32_t*>(rawPixels);
	state->bufferWidth = width;
	state->bufferHeight = height;
	return true;
}

static void ycPaint(HWND window, YCState* state) {
	PAINTSTRUCT paint{};
	HDC windowDc = BeginPaint(window, &paint);

	RECT client{};
	GetClientRect(window, &client);
	const int width = client.right - client.left;
	const int height = client.bottom - client.top;

	if (!windowDc || !state || !state->c ||
		!ycEnsureBuffer(window, state, width, height)) {
		EndPaint(window, &paint);
		return;
	}

	const uint32_t background = 0x0012100Cu;
	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
	for (size_t i = 0; i < pixelCount; ++i) {
		state->pixels[i] = background;
	}

	float hue = 0.0f;
	float saturation = 0.0f;
	float brightness = 1.0f;
	ycRGB(*state->c, hue, saturation, brightness);

	const int centerX = 68;
	const int centerY = 82;
	const int radius = 56;

	for (int y = 25; y < 140 && y < height; ++y) {
		for (int x = 10; x < 175 && x < width; ++x) {
			const float dx = static_cast<float>(x - centerX);
			const float dy = static_cast<float>(y - centerY);
			const float distance = sqrtf(dx * dx + dy * dy);
			uint32_t color = 0;

			if (distance <= static_cast<float>(radius)) {
				float pixelHue = atan2f(dy, dx) / 6.2831853f;
				if (pixelHue < 0.0f) pixelHue += 1.0f;
				color = ycHSV(pixelHue, distance / radius, brightness);
			}
			else if (x >= 145 && x <= 163) {
				const float pixelBrightness =
					1.0f - static_cast<float>(y - 25) / 115.0f;
				color = ycHSV(hue, saturation, pixelBrightness);
			}

			if (color != 0) {
				state->pixels[static_cast<size_t>(y) * width + x] =
					color & 0x00FFFFFFu;
			}
		}
	}

	HDC dc = state->memoryDc;
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(255, 209, 10));

	HFONT selectedFont = hFontHead
		? hFontHead
		: static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
	HFONT oldFont = static_cast<HFONT>(SelectObject(dc, selectedFont));

	TextOutA(dc, 4, 3, state->title, static_cast<int>(strlen(state->title)));

	const float angle = hue * 6.2831853f;
	const int markerX = centerX +
		static_cast<int>(cosf(angle) * saturation * radius);
	const int markerY = centerY +
		static_cast<int>(sinf(angle) * saturation * radius);

	HPEN markerPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
	HPEN oldPen = static_cast<HPEN>(SelectObject(dc, markerPen));
	HBRUSH oldBrush = static_cast<HBRUSH>(
		SelectObject(dc, GetStockObject(NULL_BRUSH)));

	Ellipse(dc, markerX - 5, markerY - 5, markerX + 6, markerY + 6);

	const int brightnessY = 25 +
		static_cast<int>((1.0f - brightness) * 115.0f);
	MoveToEx(dc, 142, brightnessY, nullptr);
	LineTo(dc, 167, brightnessY);

	char text[16];
	sprintf_s(text, "#%06X", *state->c & 0x00FFFFFFu);
	SetTextColor(dc, RGB(240, 225, 180));
	TextOutA(dc, 10, 148, text, static_cast<int>(strlen(text)));

	BitBlt(windowDc, 0, 0, width, height, dc, 0, 0, SRCCOPY);

	SelectObject(dc, oldBrush);
	SelectObject(dc, oldPen);
	SelectObject(dc, oldFont);
	DeleteObject(markerPen);
	EndPaint(window, &paint);
}

static void ycMouse(HWND w, YCState* st, int x, int y) {
	float h, s, v;
	ycRGB(*st->c, h, s, v);
	if (st->wheel) {
		float dx = x - 68.f, dy = y - 82.f;
		h = atan2f(dy, dx) / 6.2831853f;
		if (h < 0)h += 1;
		s = yc01(sqrtf(dx * dx + dy * dy) / 56.f);
	}
	if (st->value)v = yc01(1 - (y - 25) / 115.f);
	*st->c = ycHSV(h, s, v);
	InvalidateRect(w, 0, FALSE);
}
static LRESULT CALLBACK ycWheel(HWND w, UINT m, WPARAM a, LPARAM l) {
	YCState* st = (YCState*)GetWindowLongPtrA(w, GWLP_USERDATA);
	if (m == WM_NCCREATE) {
		SetWindowLongPtrA(w, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTA*)l)->lpCreateParams);
		return TRUE;
	}
	if (m == WM_NCDESTROY) {
		ycReleaseBuffer(st);
		SetWindowLongPtrA(w, GWLP_USERDATA, 0);
		return DefWindowProcA(w, m, a, l);
	}
	if (m == WM_ERASEBKGND) return 1;
	if (m == WM_PAINT) {
		ycPaint(w, st);
		return 0;
	}
	if (m == WM_LBUTTONDOWN && st) {
		int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
		float dx = x - 68.f, dy = y - 82.f;
		st->wheel = sqrtf(dx * dx + dy * dy) <= 60;
		st->value = x >= 140 && x <= 170 && y >= 20 && y <= 145;
		if (st->wheel || st->value) {
			SetCapture(w);
			ycMouse(w, st, x, y);
		}
		return 0;
	}
	if (m == WM_MOUSEMOVE && st && (st->wheel || st->value)) {
		ycMouse(w, st, GET_X_LPARAM(l), GET_Y_LPARAM(l));
		return 0;
	}
	if (m == WM_LBUTTONUP && st) {
		st->wheel = st->value = false;
		if (GetCapture() == w)ReleaseCapture();
		return 0;
	}
	return DefWindowProcA(w, m, a, l);
}
static void ycBind() {
	int i = g_espCategory;
	if (i < 0 || i >= 3)i = 0;
	ysv.c = &g_chamsVisibleByCategory[i];
	ysh.c = &g_chamsHiddenByCategory[i];
	if (ycVis)InvalidateRect(ycVis, 0, FALSE);
	if (ycHid)InvalidateRect(ycHid, 0, FALSE);
}
static void ycSync() {
	if (!ycCombo)return;
	bool e = hEsp[0] && IsWindowVisible(hEsp[0]);
	static int old = -1;
	if (old != (int)e) {
		old = e;
		int W = e ? 540 : 452, H = e ? 700 : 500;
		RECT wr{};
		GetWindowRect(hMenu, &wr);
		int nx = (wr.left + wr.right - W) / 2, ny = (wr.top + wr.bottom - H) / 2;
		SetWindowPos(hMenu, 0, nx, ny, W, H, SWP_NOZORDER | SWP_NOACTIVATE);
		if (hLblFoot)MoveWindow(hLblFoot, 24, H - 34, W - 48, 22, TRUE);
	}
	int sw = e ? SW_SHOW : SW_HIDE;
	ShowWindow(ycLabel, sw);
	ShowWindow(ycCombo, sw);
	ShowWindow(ycVis, sw);
	ShowWindow(ycHid, sw);
}
static void ycCreate(HWND w) {
	WNDCLASSA c{};
	c.lpfnWndProc = ycWheel;
	c.hInstance = hSelf;
	c.hCursor = LoadCursor(0, IDC_CROSS);
	c.lpszClassName = "YamiColorWheel";
	RegisterClassA(&c);
	ycLabel = CreateWindowA("STATIC", "Highlight category", WS_CHILD, 24, 350, 180, 22, w, 0, hSelf, 0);
	ycCombo = CreateWindowA("COMBOBOX", "", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 210, 346, 220, 180, w, (HMENU)YC_COMBO, hSelf, 0);
	SendMessageA(ycCombo, CB_ADDSTRING, 0, (LPARAM)"NPCs");
	SendMessageA(ycCombo, CB_ADDSTRING, 0, (LPARAM)"Players");
	SendMessageA(ycCombo, CB_ADDSTRING, 0, (LPARAM)"Resources");
	SendMessageA(ycCombo, CB_SETCURSEL, g_espCategory, 0);
	ysv.title = "Visible CHAMS";
	ysh.title = "Behind walls";
	ycBind();
	ycVis = CreateWindowA("YamiColorWheel", "", WS_CHILD, 24, 385, 180, 175, w, 0, hSelf, &ysv);
	ycHid = CreateWindowA("YamiColorWheel", "", WS_CHILD, 270, 385, 180, 175, w, 0, hSelf, &ysh);
	SendMessageA(ycLabel, WM_SETFONT, (WPARAM)hFontHead, TRUE);
	SendMessageA(ycCombo, WM_SETFONT, (WPARAM)hFontNorm, TRUE);
	SetTimer(w, YC_TIMER, 80, 0);
	ycSync();
}
static LRESULT CALLBACK ycMenu(HWND w, UINT m, WPARAM a, LPARAM l) {
	if (m == YC_INIT) {
		ycCreate(w);
		return 0;
	}
	if (m == YC_REMOVE) {
		KillTimer(w, YC_TIMER);
		if (ycVis)DestroyWindow(ycVis);
		if (ycHid)DestroyWindow(ycHid);
		if (ycCombo)DestroyWindow(ycCombo);
		if (ycLabel)DestroyWindow(ycLabel);
		SetWindowLongPtrA(w, GWLP_WNDPROC, (LONG_PTR)ycOld);
		ycOld = 0;
		InterlockedExchange(&ycInstalled, 0);
		return 0;
	}
	if (m == WM_TIMER && a == YC_TIMER) {
		ycSync();
		return 0;
	}
	if (m == WM_COMMAND && LOWORD(a) == YC_COMBO && HIWORD(a) == CBN_SELCHANGE) {
		int i = (int)SendMessageA(ycCombo, CB_GETCURSEL, 0, 0);
		if (i >= 0 && i < 3) {
			g_espCategory = i;
			ycBind();
		}
		return 0;
	}
	return ycOld ? CallWindowProcA(ycOld, w, m, a, l) : DefWindowProcA(w, m, a, l);
}
static void EnsureYamiPanel() {
	if (!hMenu || ycInstalled)return;
	if (InterlockedCompareExchange(&ycInstalled, 1, 0))return;
	ycOld = (WNDPROC)SetWindowLongPtrA(hMenu, GWLP_WNDPROC, (LONG_PTR)ycMenu);
	if (!ycOld) {
		InterlockedExchange(&ycInstalled, 0);
		return;
	}
	PostMessageA(hMenu, YC_INIT, 0, 0);
}
static void RemoveYamiPanel() {
	if (hMenu && ycOld)SendMessageA(hMenu, YC_REMOVE, 0, 0);
}

static constexpr uintptr_t DRAW_BATCH_ADDR = 0x008203B0;
static constexpr uintptr_t SCENE_RENDER_CUR_MODEL = 0x60;

// Statically derived from CM2Model attachment traversal in the supplied binary.
// 0x82506F walks child models via [model + 0x58] then [child + 0x60].
static constexpr uintptr_t MODEL_ATTACH_CHILD_LIST = 0x58;
static constexpr uintptr_t MODEL_ATTACH_NEXT_SIBLING = 0x60;

static constexpr BYTE CHAMS_HIDDEN_ALPHA = 255;
static constexpr BYTE CHAMS_VISIBLE_ALPHA = 210;
static constexpr int CHAMS_VISIBLE_PASSES = 2;
static constexpr UINT CHAMS_MIN_VERTICES = 20;
static constexpr UINT CHAMS_MIN_PRIMITIVES = 6;

static const char* g_targetNames[] = { "%%NOTARGET%%" };
static const int g_targetCount = (int)(sizeof(g_targetNames) / sizeof(g_targetNames[0]));
static bool IsTargetName(const char* n) {
	if (!n || !*n) return false;
	for (int i = 0; i < g_targetCount; i++)
		if (g_targetNames[i] && g_targetNames[i][0] && strstr(n, g_targetNames[i]))
			return true;
	return false;
}

typedef HRESULT(WINAPI* EndScene_t)(IDirect3DDevice9*);
typedef HRESULT(WINAPI* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef HRESULT(WINAPI* SetTransform_t)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);
typedef HRESULT(WINAPI* DIP_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef void(__fastcall* DrawBatch_t)(void* ecx, void* edx);

static EndScene_t oEndScene = nullptr;
static Reset_t oReset = nullptr;
static SetTransform_t oSetTransform = nullptr;
static DIP_t oDIP = nullptr;
static DrawBatch_t oDrawBatch = nullptr;

static void* g_pEndScene = nullptr;
static void* g_pReset = nullptr;
static void* g_pSetTransform = nullptr;
static void* g_pDIP = nullptr;
static void* g_pDrawBatch = reinterpret_cast<void*>(DRAW_BATCH_ADDR);

static HWND g_hGameWnd = nullptr;
static std::atomic<bool> g_imguiReady{ false };
static std::atomic<bool> g_unloading{ false };
static std::atomic<bool> g_inImGui{ false };

static float g_boxRadius = 0.55f;
// Exact model ownership snapshot rebuilt from ObjectManager once per frame.
// Unknown models fail closed and never receive CHAMS.
struct ModelOwnerEntry {
	uintptr_t model;
	uintptr_t rootModel;
	uintptr_t objectBase;
	uint64_t guid;
	uint8_t type;
};

constexpr int MAX_MODEL_OWNERS = 1024;
static ModelOwnerEntry g_modelOwners[MAX_MODEL_OWNERS] = {};
static int g_modelOwnerCount = 0;

struct ActiveRenderOwner {
	uintptr_t model;
	uintptr_t objectBase;
	uint64_t guid;
	uint8_t type;
	bool valid;
};

static thread_local ActiveRenderOwner g_activeRenderOwner = {};

static bool InsertModelOwner(uintptr_t renderModel, uintptr_t rootModel, const PlayerInfo& entity) {
	if (!renderModel || !rootModel || !entity.objectBase)
		return false;

	for (int i = 0; i < g_modelOwnerCount; ++i) {
		if (g_modelOwners[i].model == renderModel)
			return true;
	}

	if (g_modelOwnerCount >= MAX_MODEL_OWNERS)
		return false;

	ModelOwnerEntry& out = g_modelOwners[g_modelOwnerCount++];
	out.model = renderModel;
	out.rootModel = rootModel;
	out.objectBase = entity.objectBase;
	out.guid = entity.guid;
	out.type = entity.type;
	return true;
}

static void AddModelOwnerTree(const PlayerInfo& entity, uintptr_t rootModel, uintptr_t renderModel, int depth) {
	if (!renderModel || !rootModel)
		return;
	if (depth > 8)
		return;
	if (!InsertModelOwner(renderModel, rootModel, entity))
		return;

	uintptr_t child = 0;
	if (!SafeRead(renderModel + MODEL_ATTACH_CHILD_LIST, child))
		return;

	int siblingBudget = 64;
	while (child && siblingBudget-- > 0 && g_modelOwnerCount < MAX_MODEL_OWNERS) {
		AddModelOwnerTree(entity, rootModel, child, depth + 1);

		uintptr_t next = 0;
		if (!SafeRead(child + MODEL_ATTACH_NEXT_SIBLING, next))
			break;
		child = next;
	}
}

static void RebuildModelOwnerMap() {
	g_modelOwnerCount = 0;

	for (int i = 0; i < g_playerCount && g_modelOwnerCount < MAX_MODEL_OWNERS; ++i) {
		const PlayerInfo& entity = g_players[i];
		if (!entity.model || !entity.objectBase)
			continue;
		if (entity.type != OT_UNIT && entity.type != OT_PLAYER)
			continue;

		AddModelOwnerTree(entity, entity.model, entity.model, 0);
	}
}

static bool ValidateModelOwner(const ModelOwnerEntry& owner) {
	uint8_t type = OT_NONE;
	uint64_t guid = 0;
	uintptr_t model = 0;

	if (!SafeRead(owner.objectBase + Off::OBJ_TYPE, type))
		return false;
	if (!SafeRead(owner.objectBase + Off::OBJ_GUID, guid))
		return false;
	if (!SafeRead(owner.objectBase + Off::MODEL_PTR, model))
		return false;

	return type == owner.type &&
		guid == owner.guid &&
		model == owner.rootModel &&
		(type == OT_UNIT || type == OT_PLAYER);
}

static bool FindModelOwner(uintptr_t model, ActiveRenderOwner& out) {
	if (!model)
		return false;

	for (int i = 0; i < g_modelOwnerCount; ++i) {
		const ModelOwnerEntry& candidate = g_modelOwners[i];
		if (candidate.model != model)
			continue;
		if (!ValidateModelOwner(candidate))
			return false;

		out.model = model;
		out.objectBase = candidate.objectBase;
		out.guid = candidate.guid;
		out.type = candidate.type;
		out.valid = true;
		return true;
	}

	return false;
}

static void __fastcall hkDrawBatch(void* ecx, void* edx) {
	ActiveRenderOwner previous = g_activeRenderOwner;
	ActiveRenderOwner current = {};

	uintptr_t model = 0;
	if (ecx)
		SafeRead(reinterpret_cast<uintptr_t>(ecx) + SCENE_RENDER_CUR_MODEL, model);

	FindModelOwner(model, current);
	g_activeRenderOwner = current;

	if (oDrawBatch)
		oDrawBatch(ecx, edx);

	g_activeRenderOwner = previous;
}

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

struct ChamsState {
	DWORD zEnable, zFunc, zWrite;
	DWORD lighting, fog;
	DWORD alphaBlend, alphaTest, alphaRef, alphaFunc;
	DWORD textureFactor, srcBlend, dstBlend, cullMode, colorWrite;
	DWORD colorOp, colorArg1, colorArg2;
	DWORD alphaOp, alphaArg1, alphaArg2;
	IDirect3DPixelShader9* pixelShader;
};

static void SaveState(IDirect3DDevice9* dev, ChamsState& s) {
	ZeroMemory(&s, sizeof(s));
	dev->GetRenderState(D3DRS_ZENABLE, &s.zEnable);
	dev->GetRenderState(D3DRS_ZFUNC, &s.zFunc);
	dev->GetRenderState(D3DRS_ZWRITEENABLE, &s.zWrite);
	dev->GetRenderState(D3DRS_LIGHTING, &s.lighting);
	dev->GetRenderState(D3DRS_FOGENABLE, &s.fog);
	dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &s.alphaBlend);
	dev->GetRenderState(D3DRS_ALPHATESTENABLE, &s.alphaTest);
	dev->GetRenderState(D3DRS_ALPHAREF, &s.alphaRef);
	dev->GetRenderState(D3DRS_ALPHAFUNC, &s.alphaFunc);
	dev->GetRenderState(D3DRS_TEXTUREFACTOR, &s.textureFactor);
	dev->GetRenderState(D3DRS_SRCBLEND, &s.srcBlend);
	dev->GetRenderState(D3DRS_DESTBLEND, &s.dstBlend);
	dev->GetRenderState(D3DRS_CULLMODE, &s.cullMode);
	dev->GetRenderState(D3DRS_COLORWRITEENABLE, &s.colorWrite);
	dev->GetTextureStageState(0, D3DTSS_COLOROP, &s.colorOp);
	dev->GetTextureStageState(0, D3DTSS_COLORARG1, &s.colorArg1);
	dev->GetTextureStageState(0, D3DTSS_COLORARG2, &s.colorArg2);
	dev->GetTextureStageState(0, D3DTSS_ALPHAOP, &s.alphaOp);
	dev->GetTextureStageState(0, D3DTSS_ALPHAARG1, &s.alphaArg1);
	dev->GetTextureStageState(0, D3DTSS_ALPHAARG2, &s.alphaArg2);
	dev->GetPixelShader(&s.pixelShader);
}

static void RestoreState(IDirect3DDevice9* dev, ChamsState& s) {
	dev->SetRenderState(D3DRS_ZENABLE, s.zEnable);
	dev->SetRenderState(D3DRS_ZFUNC, s.zFunc);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, s.zWrite);
	dev->SetRenderState(D3DRS_LIGHTING, s.lighting);
	dev->SetRenderState(D3DRS_FOGENABLE, s.fog);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, s.alphaBlend);
	dev->SetRenderState(D3DRS_ALPHATESTENABLE, s.alphaTest);
	dev->SetRenderState(D3DRS_ALPHAREF, s.alphaRef);
	dev->SetRenderState(D3DRS_ALPHAFUNC, s.alphaFunc);
	dev->SetRenderState(D3DRS_TEXTUREFACTOR, s.textureFactor);
	dev->SetRenderState(D3DRS_SRCBLEND, s.srcBlend);
	dev->SetRenderState(D3DRS_DESTBLEND, s.dstBlend);
	dev->SetRenderState(D3DRS_CULLMODE, s.cullMode);
	dev->SetRenderState(D3DRS_COLORWRITEENABLE, s.colorWrite);
	dev->SetTextureStageState(0, D3DTSS_COLOROP, s.colorOp);
	dev->SetTextureStageState(0, D3DTSS_COLORARG1, s.colorArg1);
	dev->SetTextureStageState(0, D3DTSS_COLORARG2, s.colorArg2);
	dev->SetTextureStageState(0, D3DTSS_ALPHAOP, s.alphaOp);
	dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, s.alphaArg1);
	dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, s.alphaArg2);
	dev->SetPixelShader(s.pixelShader);
	if (s.pixelShader) {
		s.pixelShader->Release();
		s.pixelShader = nullptr;
	}
}

static D3DCOLOR ColorWithAlpha(DWORD rgb, BYTE alpha) {
	return (D3DCOLOR)((rgb & 0x00FFFFFFu) | ((DWORD)alpha << 24));
}

// Мягкий цветной слой. Исходный CULLMODE намеренно не меняется:
// так обратные и внутренние полигоны модели не просвечивают.
static void SetSoftOverlayState(IDirect3DDevice9* dev, D3DCOLOR color, D3DCMPFUNC depthFunc) {
	dev->SetPixelShader(nullptr);
	dev->SetRenderState(D3DRS_ZENABLE, TRUE);
	dev->SetRenderState(D3DRS_ZFUNC, depthFunc);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	dev->SetRenderState(D3DRS_LIGHTING, FALSE);
	dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
	dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	dev->SetRenderState(D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
		D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
	dev->SetRenderState(D3DRS_TEXTUREFACTOR, color);
	dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
	dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
}

static HRESULT WINAPI hkDIP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, INT baseVI,
	UINT minVI, UINT numV, UINT startIdx, UINT primCount)
{
	if (!oDIP) return D3D_OK;
	if (g_inImGui.load() || g_unloading.load())
		return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

	if (!g_chamsEnabled || !g_activeRenderOwner.valid)
		return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

	// Exact category gate. Resources and unknown models always fail closed.
	if (g_espCategory == ESP_CATEGORY_NPCS && g_activeRenderOwner.type != OT_UNIT)
		return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);
	if (g_espCategory == ESP_CATEGORY_PLAYERS && g_activeRenderOwner.type != OT_PLAYER)
		return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);
	if (g_espCategory == ESP_CATEGORY_RESOURCES)
		return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

	IDirect3DBaseTexture9* tex = nullptr;
	bool hasTex = (dev->GetTexture(0, &tex) == D3D_OK && tex);
	if (tex) tex->Release();
	if (!hasTex)
		return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

	// Spell billboards / cast glints commonly arrive as tiny quad-like draws.
	// Skip CHAMS on those very small passes while preserving full body meshes.
	if (numV < CHAMS_MIN_VERTICES || primCount < CHAMS_MIN_PRIMITIVES)
		return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

	// Temporary cast glints / spell visuals often ride the same owner model tree,
	// but the broad blended-pass filter from stage 3 also removed valid body passes
	// behind thick objects. Keep only a narrow additive-pass rejection here.
	DWORD alphaBlendEnable = FALSE;
	DWORD srcBlend = D3DBLEND_ONE;
	DWORD dstBlend = D3DBLEND_ZERO;
	dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &alphaBlendEnable);
	dev->GetRenderState(D3DRS_SRCBLEND, &srcBlend);
	dev->GetRenderState(D3DRS_DESTBLEND, &dstBlend);

	const bool additivePass =
		(alphaBlendEnable != FALSE) &&
		(srcBlend == D3DBLEND_ONE || srcBlend == D3DBLEND_SRCALPHA || srcBlend == D3DBLEND_BOTHSRCALPHA) &&
		(dstBlend == D3DBLEND_ONE || dstBlend == D3DBLEND_SRCCOLOR || dstBlend == D3DBLEND_INVSRCCOLOR);
	if (additivePass)
		return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

	float materialC28[4] = { 0, 0, 0, 0 };
	if (SUCCEEDED(dev->GetVertexShaderConstantF(28, materialC28, 1))) {
		const float materialAlpha = materialC28[3];
		if (materialAlpha > 0.01f && materialAlpha < 0.90f)
			return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);
	}

	HRESULT baseResult = D3D_OK;
	bool originalDrawn = false;
	ChamsState saved{};
	bool stateSaved = false;

	__try {
		SaveState(dev, saved);
		stateSaved = true;

		SetSoftOverlayState(
			dev,
			ColorWithAlpha(g_chamsHiddenByCategory[g_espCategory], CHAMS_HIDDEN_ALPHA),
			D3DCMP_ALWAYS);

		oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

		RestoreState(dev, saved);
		stateSaved = false;

		baseResult = oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);
		originalDrawn = true;
		if (FAILED(baseResult))
			return baseResult;

		SaveState(dev, saved);
		stateSaved = true;

		SetSoftOverlayState(
			dev,
			ColorWithAlpha(g_chamsVisibleByCategory[g_espCategory], CHAMS_VISIBLE_ALPHA),
			D3DCMP_LESSEQUAL);

		for (int pass = 0; pass < CHAMS_VISIBLE_PASSES; ++pass)
			oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

		RestoreState(dev, saved);
		stateSaved = false;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		if (stateSaved)
			RestoreState(dev, saved);

		if (!originalDrawn)
			return oDIP(dev, type, baseVI, minVI, numV, startIdx, primCount);

		return baseResult;
	}

	return baseResult;
}

static void AppendGameObjectsToEsp() {
	if (g_espCategory != ESP_CATEGORY_RESOURCES) return;
	uintptr_t mgr = GetObjectManager();
	if (!mgr) return;
	uintptr_t obj = 0;
	if (!SafeRead(mgr + Off::FIRST_OBJECT, obj)) return;
	int guard = 0;
	while (obj && !(obj & 1) && g_playerCount < MAX_PLAYERS && guard++ < 8192) {
		uint8_t type = OT_NONE;
		SafeRead(obj + Off::OBJ_TYPE, type);
		if (type == OT_GAMEOBJECT) {
			PlayerInfo info{};
			SafeRead(obj + Off::OBJ_GUID, info.guid);
			SafeRead(obj + 0xE8, info.x);
			SafeRead(obj + 0xEC, info.y);
			SafeRead(obj + 0xF0, info.z);
			info.type = OT_GAMEOBJECT;
			strcpy_s(info.name, sizeof(info.name), "Resource");
			if (info.guid && CoordsSane(info.x, info.y, info.z)) g_players[g_playerCount++] = info;
		}
		uintptr_t next = 0;
		if (!SafeRead(obj + Off::NEXT_OBJECT, next) || next == obj) break;
		obj = next;
	}
}

static void DrawEntity(const PlayerInfo& p, int w, int h, const CGCameraSnapshot& cam,
	bool tracerValid, float tracerX, float tracerY)
{
	const bool isPlayer = (p.type == OT_PLAYER);
	const bool isNpc = (p.type == OT_UNIT);
	const bool isResource = (p.type == OT_GAMEOBJECT);
	const bool isTarget = IsTargetName(p.name);
	if (g_espCategory == ESP_CATEGORY_PLAYERS && !isPlayer) return;
	if (g_espCategory == ESP_CATEGORY_NPCS && !isNpc) return;
	if (g_espCategory == ESP_CATEGORY_RESOURCES && !isResource) return;
	if (!isPlayer && !isNpc && !isResource) return;
	if (isNpc && !isTarget && p.maxHealth == 0) return;

	const Vector3 world{ p.x, p.y, p.z };
	const float ox0 = g_localValid ? g_localX : cam.pos.x;
	const float oy0 = g_localValid ? g_localY : cam.pos.y;
	const float oz0 = g_localValid ? g_localZ : cam.pos.z;
	const float dx = world.x - ox0, dy = world.y - oy0, dz = world.z - oz0;
	const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
	if (!isTarget && g_espDist > 0.0f && dist > g_espDist) return;
	if (!CoordsSane(p.x, p.y, p.z)) return;

	const float r = isResource ? 0.35f : g_boxRadius;
	const float zTop = isResource ? 1.0f : g_boxHeight;
	float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
	int okCount = 0;
	for (int ix = 0; ix < 2; ix++)
		for (int iy = 0; iy < 2; iy++)
			for (int iz = 0; iz < 2; iz++) {
				Vector3 corner{ world.x + (ix ? r : -r), world.y + (iy ? r : -r), world.z + (iz ? zTop : 0.0f) };
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
	else if (!alive && !isResource) col = IM_COL32(160, 160, 160, 255);
	else {
		uint32_t c = g_chamsVisibleByCategory[g_espCategory];
		col = IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, 255);
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
		if (isTarget) _snprintf_s(line, _TRUNCATE, ">> %s << [%.0f]", p.name, dist);
		else if (g_drawNames && g_drawDistance) _snprintf_s(line, _TRUNCATE, "%s [%.0f]", p.name, dist);
		else if (g_drawNames) _snprintf_s(line, _TRUNCATE, "%s", p.name);
		else _snprintf_s(line, _TRUNCATE, "[%.0f]", dist);
		ImVec2 ts = ImGui::CalcTextSize(line);
		ImVec2 tp{ cx - ts.x * 0.5f, minY - ts.y - 2.0f };
		dl->AddRectFilled(ImVec2(tp.x - 2, tp.y - 1), ImVec2(tp.x + ts.x + 2, tp.y + ts.y + 1), IM_COL32(0, 0, 0, 160));
		dl->AddText(tp, col, line);
	}
}

static void RenderOverlay(IDirect3DDevice9* dev) {
	if (g_unloading.load()) return;
	EnsureYamiPanel();
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
		io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 16.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());
		if (!ImGui_ImplWin32_Init(g_hGameWnd)) return;
		if (!ImGui_ImplDX9_Init(dev)) { ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); return; }
		g_imguiReady.store(true);
	}
	ImGui_ImplDX9_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();

	D3DVIEWPORT9 vpq{}; dev->GetViewport(&vpq);
	const int w = (int)vpq.Width, h = (int)vpq.Height;

	CGCameraSnapshot cam;
	const bool cameraValid = GetCameraSnapshot(cam);

	if ((g_espEnabled || g_chamsEnabled) && cameraValid) {
		int n = RefreshEntities(true, true, g_drawSelf);
		RebuildModelOwnerMap();

		if (g_espEnabled) {
			AppendGameObjectsToEsp();
			n = g_playerCount;

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
	}
	else {
		g_modelOwnerCount = 0;
		g_localValid = false;
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
		&& FindWindowA("GxWindowClass", NULL) == NULL) Sleep(50);
	if (g_unloading.load()) return 0;
	void* pEndScene = nullptr, * pReset = nullptr, * pSetTransform = nullptr, * pDIP = nullptr;
	if (!GetD3D9VTable(&pEndScene, &pReset, &pSetTransform, &pDIP)) return 1;
	g_pEndScene = pEndScene; g_pReset = pReset; g_pSetTransform = pSetTransform; g_pDIP = pDIP;
	g_pDrawBatch = reinterpret_cast<void*>(DRAW_BATCH_ADDR);
	MH_STATUS st = MH_Initialize();
	if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return 1;
	if (MH_CreateHook(pEndScene, &hkEndScene, (void**)&oEndScene) != MH_OK) return 1;
	if (MH_CreateHook(pReset, &hkReset, (void**)&oReset) != MH_OK) return 1;
	MH_CreateHook(pSetTransform, &hkSetTransform, (void**)&oSetTransform);
	if (MH_CreateHook(g_pDrawBatch, &hkDrawBatch, (void**)&oDrawBatch) != MH_OK) return 1;
	if (MH_CreateHook(pDIP, &hkDIP, (void**)&oDIP) != MH_OK) return 1;
	if (MH_EnableHook(pEndScene) != MH_OK) return 1;
	if (MH_EnableHook(pReset) != MH_OK) return 1;
	MH_EnableHook(pSetTransform);
	if (MH_EnableHook(g_pDrawBatch) != MH_OK) return 1;
	if (MH_EnableHook(pDIP) != MH_OK) return 1;
	return 0;
}

void EspShutdown() {
	g_unloading.store(true);
	RemoveYamiPanel();
	if (g_pDIP) MH_DisableHook(g_pDIP);
	if (g_pDrawBatch) MH_DisableHook(g_pDrawBatch);
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
