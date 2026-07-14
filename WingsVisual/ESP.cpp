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
#include <cfloat>
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
extern HWND hEsp[11];
extern int g_tab, g_espSection;
extern HMODULE hSelf;
extern HFONT hFontNorm, hFontHead;
extern HBRUSH hbrBack;
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
	int category = g_espCategory;
	if (category < 0 || category >= ESP_CATEGORY_COUNT)
		category = 0;

	switch (g_espSection) {
	case 1:
		ysv.c = &g_boxColorByCategory[category];
		ysv.title = "Box color";
		ysh.c = &g_chamsHiddenByCategory[category];
		ysh.title = "Behind walls";
		break;
	case 2:
		ysv.c = &g_radiusColorByCategory[category];
		ysv.title = "Target radius color";
		ysh.c = &g_selfRadiusColor;
		ysh.title = "Self radius color";
		break;
	case 3:
		ysv.c = &g_tracerColorByCategory[category];
		ysv.title = "Trace color";
		ysh.c = &g_chamsHiddenByCategory[category];
		ysh.title = "Behind walls";
		break;
	case 4:
		ysv.c = &g_nameColorByCategory[category];
		ysv.title = "Name color";
		ysh.c = &g_chamsHiddenByCategory[category];
		ysh.title = "Behind walls";
		break;
	default:
		ysv.c = &g_chamsVisibleByCategory[category];
		ysv.title = "Visible CHAMS";
		ysh.c = &g_chamsHiddenByCategory[category];
		ysh.title = "Behind walls";
		break;
	}

	if (ycVis) InvalidateRect(ycVis, 0, FALSE);
	if (ycHid) InvalidateRect(ycHid, 0, FALSE);
}

static void ycSync() {
	if (!ycCombo) return;

	static int previousSection = -1;
	static int previousCategory = -1;
	if (previousSection != g_espSection || previousCategory != g_espCategory) {
		previousSection = g_espSection;
		previousCategory = g_espCategory;
		ycBind();
	}

	const bool espVisible = g_tab == 1;
	const bool secondWheelVisible = espVisible &&
		(g_espSection == 0 || g_espSection == 2);
	int wheelY = 220;
	switch (g_espSection) {
	case 1: wheelY = 340; break;
	case 2: wheelY = 295; break;
	case 3: wheelY = 230; break;
	case 4: wheelY = 310; break;
	default: wheelY = 220; break;
	}

	if (g_espSection == 0 || g_espSection == 2) {
		MoveWindow(ycVis, 24, wheelY, 180, 175, TRUE);
		MoveWindow(ycHid, 284, wheelY, 180, 175, TRUE);
	}
	else {
		MoveWindow(ycVis, 24, wheelY, 180, 175, TRUE);
	}

	ShowWindow(ycLabel, espVisible ? SW_SHOW : SW_HIDE);
	ShowWindow(ycCombo, espVisible ? SW_SHOW : SW_HIDE);
	ShowWindow(ycVis, espVisible ? SW_SHOW : SW_HIDE);
	ShowWindow(ycHid, secondWheelVisible ? SW_SHOW : SW_HIDE);
}

static void ycCreate(HWND w) {
	WNDCLASSA c{};
	c.lpfnWndProc = ycWheel;
	c.hInstance = hSelf;
	c.hCursor = LoadCursor(0, IDC_CROSS);
	c.lpszClassName = "YamiColorWheel";
	RegisterClassA(&c);
	ycLabel = CreateWindowA("STATIC", "Target category", WS_CHILD, 24, 142, 180, 22, w, 0, hSelf, 0);
	ycCombo = CreateWindowA("COMBOBOX", "", WS_CHILD | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL, 220, 138, 296, 180, w, (HMENU)YC_COMBO, hSelf, 0);
	SendMessageA(ycCombo, CB_ADDSTRING, 0, (LPARAM)"NPCs");
	SendMessageA(ycCombo, CB_ADDSTRING, 0, (LPARAM)"Players");
	SendMessageA(ycCombo, CB_ADDSTRING, 0, (LPARAM)"Resources");
	SendMessageA(ycCombo, CB_SETCURSEL, g_espCategory, 0);
	SendMessageA(ycCombo, CB_SETITEMHEIGHT, (WPARAM)-1, 24);
	SendMessageA(ycCombo, CB_SETITEMHEIGHT, 0, 22);
	ysv.title = "Visible CHAMS";
	ysh.title = "Behind walls";
	ycBind();
	ycVis = CreateWindowA("YamiColorWheel", "", WS_CHILD, 24, 220, 180, 175, w, 0, hSelf, &ysv);
	ycHid = CreateWindowA("YamiColorWheel", "", WS_CHILD, 284, 220, 180, 175, w, 0, hSelf, &ysh);
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
	if (m == WM_DRAWITEM && LOWORD(a) == YC_COMBO) {
		LPDRAWITEMSTRUCT item = reinterpret_cast<LPDRAWITEMSTRUCT>(l);
		if (!item) return TRUE;

		const bool selected = (item->itemState & ODS_SELECTED) != 0;
		HBRUSH background = CreateSolidBrush(selected ? RGB(112, 43, 18) : RGB(42, 27, 17));
		FillRect(item->hDC, &item->rcItem, background);
		DeleteObject(background);

		char text[64] = { 0 };
		if (item->itemID != (UINT)-1)
			SendMessageA(ycCombo, CB_GETLBTEXT, item->itemID, (LPARAM)text);
		else {
			const int current = (int)SendMessageA(ycCombo, CB_GETCURSEL, 0, 0);
			if (current >= 0)
				SendMessageA(ycCombo, CB_GETLBTEXT, current, (LPARAM)text);
		}

		SetBkMode(item->hDC, TRANSPARENT);
		SetTextColor(item->hDC, selected ? RGB(255, 209, 10) : RGB(255, 232, 188));
		HFONT oldFont = (HFONT)SelectObject(item->hDC, hFontNorm);
		RECT textRect = item->rcItem;
		textRect.left += 8;
		DrawTextA(item->hDC, text, -1, &textRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
		SelectObject(item->hDC, oldFont);

		if (item->itemState & ODS_FOCUS)
			DrawFocusRect(item->hDC, &item->rcItem);
		return TRUE;
	}
	if (m == WM_CTLCOLORLISTBOX) {
		HDC dc = (HDC)a;
		SetBkColor(dc, RGB(42, 27, 17));
		SetTextColor(dc, RGB(255, 232, 188));
		return (LRESULT)hbrBack;
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
	if (!hMenu) return;
	if (InterlockedCompareExchange(&ycInstalled, 1, 0) != 0) return;
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


// Projects with the camera snapshot already captured for this frame.
// This avoids rereading camera memory for every point of every radius circle.
static bool ProjectWithCamera(const Vector3& world, Vector2& screen, int w, int h,
	const CGCameraSnapshot& camera)
{
	if (w <= 0 || h <= 0)
		return false;

	const Vector3 forward{ camera.m[0][0], camera.m[0][1], camera.m[0][2] };
	const Vector3 left{ camera.m[1][0], camera.m[1][1], camera.m[1][2] };
	const Vector3 up{ camera.m[2][0], camera.m[2][1], camera.m[2][2] };
	const Vector3 right{ -left.x, -left.y, -left.z };
	const Vector3 relative{
		world.x - camera.pos.x,
		world.y - camera.pos.y,
		world.z - camera.pos.z
	};

	const float depth = Dot3(relative, forward);
	if (depth < 0.1f)
		return false;

	const float horizontal = Dot3(relative, right);
	const float vertical = Dot3(relative, up);

	float xScale = 0.0f;
	float yScale = 0.0f;
	if (g_projScaleValid && g_projXScale > 0.01f && g_projYScale > 0.01f) {
		xScale = g_projXScale;
		yScale = g_projYScale;
	}
	else {
		float verticalFov = (g_fovDegrees > 1.0f)
			? (g_fovDegrees * DEG2RAD)
			: camera.fov;
		float tangent = tanf(verticalFov * 0.5f);
		if (tangent < 0.0001f)
			tangent = 0.0001f;

		const float aspect = (camera.aspectMem > 0.1f)
			? camera.aspectMem
			: (static_cast<float>(w) / static_cast<float>(h));
		yScale = 1.0f / tangent;
		xScale = yScale / aspect;
	}

	screen.x = static_cast<float>(w) * 0.5f *
		(1.0f + (horizontal / depth) * xScale);
	screen.y = static_cast<float>(h) * 0.5f *
		(1.0f - (vertical / depth) * yScale);
	return true;
}

static void DrawWorldRadius(const Vector3& center, float radius, int w, int h,
	const CGCameraSnapshot& camera, ImDrawList* drawList, ImU32 color)
{
	if (!drawList || radius < 0.5f)
		return;

	constexpr int segmentCount = 144;
	constexpr float twoPi = 6.28318530718f;
	const float groundZ = center.z + 0.05f;

	Vector2 previous{};
	bool previousValid = false;
	for (int index = 0; index <= segmentCount; ++index) {
		const float angle = twoPi *
			(static_cast<float>(index) / static_cast<float>(segmentCount));
		const Vector3 point{
			center.x + cosf(angle) * radius,
			center.y + sinf(angle) * radius,
			groundZ
		};

		Vector2 current{};
		const bool currentValid = ProjectWithCamera(point, current, w, h, camera);
		if (previousValid && currentValid) {
			drawList->AddLine(
				ImVec2(previous.x, previous.y),
				ImVec2(current.x, current.y),
				color,
				2.0f);
		}
		previous = current;
		previousValid = currentValid;
	}
}

static ImU32 EspArgbToColor(uint32_t color)
{
	return IM_COL32(
		(color >> 16) & 255,
		(color >> 8) & 255,
		color & 255,
		(color >> 24) & 255);
}

static void DrawCornerBox(ImDrawList* drawList, float minX, float minY,
	float maxX, float maxY, ImU32 color, float thickness)
{
	if (!drawList) return;
	const float width = maxX - minX;
	const float height = maxY - minY;
	float cornerX = width * 0.30f;
	float cornerY = height * 0.24f;
	if (cornerX < 6.0f) cornerX = 6.0f;
	if (cornerY < 7.0f) cornerY = 7.0f;
	if (cornerX > 24.0f) cornerX = 24.0f;
	if (cornerY > 28.0f) cornerY = 28.0f;

	// Top-left and top-right.
	drawList->AddLine(ImVec2(minX, minY), ImVec2(minX + cornerX, minY), color, thickness);
	drawList->AddLine(ImVec2(minX, minY), ImVec2(minX, minY + cornerY), color, thickness);
	drawList->AddLine(ImVec2(maxX, minY), ImVec2(maxX - cornerX, minY), color, thickness);
	drawList->AddLine(ImVec2(maxX, minY), ImVec2(maxX, minY + cornerY), color, thickness);

	// Bottom-left and bottom-right.
	drawList->AddLine(ImVec2(minX, maxY), ImVec2(minX + cornerX, maxY), color, thickness);
	drawList->AddLine(ImVec2(minX, maxY), ImVec2(minX, maxY - cornerY), color, thickness);
	drawList->AddLine(ImVec2(maxX, maxY), ImVec2(maxX - cornerX, maxY), color, thickness);
	drawList->AddLine(ImVec2(maxX, maxY), ImVec2(maxX, maxY - cornerY), color, thickness);
}

static ImU32 HealthBarColor(float fraction)
{
	if (fraction > 0.60f) return IM_COL32(45, 235, 70, 255);
	if (fraction > 0.30f) return IM_COL32(255, 190, 35, 255);
	return IM_COL32(245, 55, 55, 255);
}

static void DrawEntity(const PlayerInfo& p, int w, int h, const CGCameraSnapshot& cam,
	bool tracerValid, float tracerX, float tracerY, bool drawStandardEsp)
{
	const bool isPlayer = (p.type == OT_PLAYER);
	const bool isNpc = (p.type == OT_UNIT);
	const bool isResource = (p.type == OT_GAMEOBJECT);
	const bool isTarget = IsTargetName(p.name);
	if (!isPlayer && !isNpc && !isResource) return;
	if (isNpc && !isTarget && p.maxHealth == 0) return;

	int entityCategory = ESP_CATEGORY_NPCS;
	if (isPlayer) entityCategory = ESP_CATEGORY_PLAYERS;
	else if (isResource) entityCategory = ESP_CATEGORY_RESOURCES;

	int selectedCategory = g_espCategory;
	if (selectedCategory < 0 || selectedCategory >= ESP_CATEGORY_COUNT)
		selectedCategory = ESP_CATEGORY_NPCS;

	// Strict category separation: the selected Target category controls
	// Enable ESP, Boxes, HP and Names as well as Circles and Traces.
	const bool selectedForCategoryFeature =
		entityCategory == selectedCategory;
	const bool categoryToggleEnabled =
		(isPlayer && g_espShowPlayers) ||
		(isNpc && g_espShowNpcs) ||
		isResource;
	const bool standardEspVisible =
		selectedForCategoryFeature && categoryToggleEnabled;

	const Vector3 world{ p.x, p.y, p.z };
	const float originX = g_localValid ? g_localX : cam.pos.x;
	const float originY = g_localValid ? g_localY : cam.pos.y;
	const float originZ = g_localValid ? g_localZ : cam.pos.z;
	const float dx = world.x - originX;
	const float dy = world.y - originY;
	const float dz = world.z - originZ;
	(void)dz;
	// WoW world X/Y units are yards. Horizontal distance exactly matches the
	// ground-plane radius circles, so range checks and displayed yards agree.
	const float distance = sqrtf(dx * dx + dy * dy);
	if (!isTarget && g_espDist > 0.0f && distance > g_espDist) return;
	if (!CoordsSane(p.x, p.y, p.z)) return;

	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	const bool alive = p.health > 0;
	const ImU32 targetColor = IM_COL32(255, 215, 0, 255);
	const ImU32 deadColor = IM_COL32(155, 155, 155, 255);
	const ImU32 boxColor = isTarget
		? targetColor
		: ((!alive && !isResource) ? deadColor : EspArgbToColor(g_boxColorByCategory[entityCategory]));
	const ImU32 radiusColor = EspArgbToColor(g_radiusColorByCategory[entityCategory]);
	const ImU32 tracerColor = isTarget ? targetColor : EspArgbToColor(g_tracerColorByCategory[entityCategory]);
	const ImU32 nameColor = isTarget ? targetColor : EspArgbToColor(g_nameColorByCategory[entityCategory]);

	if (g_drawRadius && selectedForCategoryFeature && (isNpc || isPlayer))
		DrawWorldRadius(world, g_radiusYards, w, h, cam, drawList, radiusColor);

	const float modelRadius = isResource ? 0.35f : g_boxRadius;
	const float modelHeight = isResource ? 1.0f : g_boxHeight;
	if ((g_drawTracers && selectedForCategoryFeature) ||
		(drawStandardEsp && standardEspVisible && isTarget)) {
		Vector2 tracePoint{};
		const Vector3 traceWorld{ world.x, world.y, world.z + modelHeight * 0.5f };
		if (ProjectWithCamera(traceWorld, tracePoint, w, h, cam)) {
			const float originScreenX = tracerValid ? tracerX : (float)w * 0.5f;
			const float originScreenY = tracerValid ? tracerY : (float)h;
			drawList->AddLine(
				ImVec2(originScreenX, originScreenY),
				ImVec2(tracePoint.x, tracePoint.y),
				tracerColor,
				isTarget ? 2.5f : 1.8f);
		}
	}

	// Traces and radii are standalone. Boxes/HP/names require Enable ESP
	// and obey Show Players / Show NPCs independently.
	if (!drawStandardEsp || !standardEspVisible) return;
	float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
	int projectedCorners = 0;
	for (int xIndex = 0; xIndex < 2; ++xIndex)
		for (int yIndex = 0; yIndex < 2; ++yIndex)
			for (int zIndex = 0; zIndex < 2; ++zIndex) {
				Vector3 corner{
					world.x + (xIndex ? modelRadius : -modelRadius),
					world.y + (yIndex ? modelRadius : -modelRadius),
					world.z + (zIndex ? modelHeight : 0.0f)
				};
				Vector2 screen;
				if (!WorldToScreen(corner, screen, w, h)) continue;
				if (screen.x < minX) minX = screen.x;
				if (screen.y < minY) minY = screen.y;
				if (screen.x > maxX) maxX = screen.x;
				if (screen.y > maxY) maxY = screen.y;
				++projectedCorners;
			}
	if (projectedCorners < 4) return;

	float boxHeight = maxY - minY;
	float boxWidth = maxX - minX;
	if (isTarget) {
		if (boxHeight < 10.0f) {
			const float center = (minY + maxY) * 0.5f;
			minY = center - 5.0f;
			maxY = center + 5.0f;
			boxHeight = 10.0f;
		}
		if (boxWidth < 10.0f) {
			const float center = (minX + maxX) * 0.5f;
			minX = center - 5.0f;
			maxX = center + 5.0f;
			boxWidth = 10.0f;
		}
	}
	else if (boxHeight < 3.0f || boxWidth < 1.0f) {
		return;
	}

	const float centerX = (minX + maxX) * 0.5f;
	const float centerY = (minY + maxY) * 0.5f;
	const float boxThickness = isTarget ? 3.0f : 2.0f;


	// Bracket/corner ESP like the reference instead of a full rectangle.
	DrawCornerBox(drawList, minX, minY, maxX, maxY, boxColor, boxThickness);

	if (g_drawHP && p.maxHealth > 0) {
		float healthFraction = (float)p.health / (float)p.maxHealth;
		if (healthFraction < 0.0f) healthFraction = 0.0f;
		if (healthFraction > 1.0f) healthFraction = 1.0f;

		const float barLeft = minX - 13.0f;
		const float barRight = minX - 5.0f;
		drawList->AddRectFilled(
			ImVec2(barLeft, minY - 1.0f),
			ImVec2(barRight, maxY + 1.0f),
			IM_COL32(0, 0, 0, 225));
		drawList->AddRectFilled(
			ImVec2(barLeft + 2.0f, maxY - (boxHeight - 2.0f) * healthFraction),
			ImVec2(barRight - 2.0f, maxY - 1.0f),
			HealthBarColor(healthFraction));
		drawList->AddRect(
			ImVec2(barLeft, minY - 1.0f),
			ImVec2(barRight, maxY + 1.0f),
			IM_COL32(255, 255, 255, 210),
			0.0f, 0, 1.0f);

		char healthText[64];
		_snprintf_s(healthText, _TRUNCATE, "%d/%d", p.health, p.maxHealth);
		ImFont* healthFont = ImGui::GetFont();
		const float healthFontSize = 12.0f;
		const ImVec2 healthSize = healthFont->CalcTextSizeA(
			healthFontSize, FLT_MAX, 0.0f, healthText);
		const ImVec2 healthPosition{
			(barLeft + barRight) * 0.5f - healthSize.x * 0.5f,
			minY - healthSize.y - 2.0f
		};
		drawList->AddText(
			healthFont, healthFontSize,
			ImVec2(healthPosition.x + 1.0f, healthPosition.y + 1.0f),
			IM_COL32(0, 0, 0, 255), healthText);
		drawList->AddText(
			healthFont, healthFontSize,
			healthPosition,
			IM_COL32(255, 255, 255, 255), healthText);
	}

	if (g_drawNames || g_drawDistance || isTarget) {
		char line[160];
		if (isTarget)
			_snprintf_s(line, _TRUNCATE, ">> %s << [%.1f yd]", p.name, distance);
		else if (g_drawNames && g_drawDistance)
			_snprintf_s(line, _TRUNCATE, "%s [%.1f yd]", p.name, distance);
		else if (g_drawNames)
			_snprintf_s(line, _TRUNCATE, "%s", p.name);
		else
			_snprintf_s(line, _TRUNCATE, "[%.1f yd]", distance);

		float fontSize = g_nameFontSize;
		if (fontSize < 10.0f) fontSize = 10.0f;
		if (fontSize > 30.0f) fontSize = 30.0f;
		ImFont* font = ImGui::GetFont();
		const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, line);
		const ImVec2 textPosition{
			centerX - textSize.x * 0.5f,
			minY - textSize.y - 3.0f
		};
		drawList->AddRectFilled(
			ImVec2(textPosition.x - 3.0f, textPosition.y - 2.0f),
			ImVec2(textPosition.x + textSize.x + 3.0f, textPosition.y + textSize.y + 2.0f),
			IM_COL32(0, 0, 0, 175));
		drawList->AddText(font, fontSize, textPosition, nameColor, line);
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

	if ((g_espEnabled || g_chamsEnabled || g_drawRadius || g_drawSelfRadius || g_drawTracers) && cameraValid) {
		int entityCount = RefreshEntities(true, true, false);

		if (g_chamsEnabled)
			RebuildModelOwnerMap();
		else
			g_modelOwnerCount = 0;

		if (g_espEnabled) {
			AppendGameObjectsToEsp();
			entityCount = g_playerCount;
		}

		bool tracerValid = false;
		float tracerX = w * 0.5f;
		float tracerY = (float)h;
		if (g_localValid) {
			Vector2 localPoint;
			if (WorldToScreen(Vector3{ g_localX, g_localY, g_localZ }, localPoint, w, h)) {
				tracerX = localPoint.x;
				tracerY = localPoint.y;
				tracerValid = true;
			}
		}

		if (g_espEnabled || g_drawRadius || g_drawTracers) {
			for (int i = 0; i < entityCount; ++i)
				DrawEntity(g_players[i], w, h, cam, tracerValid, tracerX, tracerY, g_espEnabled);
		}

		if (g_drawSelfRadius && g_localValid) {
			const Vector3 selfPosition{ g_localX, g_localY, g_localZ };
			const ImU32 selfColor = EspArgbToColor(g_selfRadiusColor);
			DrawWorldRadius(
				selfPosition,
				g_selfRadiusYards,
				w, h, cam,
				ImGui::GetForegroundDrawList(),
				selfColor);
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
