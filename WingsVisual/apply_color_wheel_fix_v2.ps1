$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$espPath = Join-Path $repoRoot 'WingsVisual\ESP.cpp'

if (-not (Test-Path $espPath)) {
    $espPath = Join-Path $repoRoot 'ESP.cpp'
}

if (-not (Test-Path $espPath)) {
    throw 'ESP.cpp not found. Put this script in the repository root or in the WingsVisual folder.'
}

$source = [System.IO.File]::ReadAllText($espPath)
$backupPath = "$espPath.before-color-wheel-fix.bak"
[System.IO.File]::WriteAllText($backupPath, $source, [System.Text.UTF8Encoding]::new($false))

$statePattern = '(?s)struct YCState\s*\{.*?\}\s*;'
$stateReplacement = @'
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
'@

$stateMatches = [regex]::Matches($source, $statePattern)
if ($stateMatches.Count -ne 1) {
    throw "Expected one YCState definition, found $($stateMatches.Count). No files were changed."
}
$source = [regex]::Replace($source, $statePattern, $stateReplacement, 1)

$paintPattern = '(?s)static void ycPaint\(HWND w, YCState\* st\)\s*\{.*?\n\}\s*static void ycMouse'
$paintReplacement = @'
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
            } else if (x >= 145 && x <= 163) {
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

static void ycMouse
'@

$paintMatches = [regex]::Matches($source, $paintPattern)
if ($paintMatches.Count -ne 1) {
    throw "Expected one ycPaint function, found $($paintMatches.Count). No files were changed."
}
$source = [regex]::Replace($source, $paintPattern, $paintReplacement, 1)

$erasePattern = 'if\s*\(\s*m\s*==\s*WM_ERASEBKGND\s*\)\s*return\s+1\s*;'
$eraseMatches = [regex]::Matches($source, $erasePattern)
if ($eraseMatches.Count -ne 1) {
    throw "Expected one WM_ERASEBKGND handler, found $($eraseMatches.Count). No files were changed."
}

$eraseReplacement = @'
if (m == WM_NCDESTROY) {
    ycReleaseBuffer(st);
    SetWindowLongPtrA(w, GWLP_USERDATA, 0);
    return DefWindowProcA(w, m, a, l);
}
if (m == WM_ERASEBKGND) return 1;
'@

$source = [regex]::Replace(
    $source,
    $erasePattern,
    $eraseReplacement,
    1)

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($espPath, $source, $utf8NoBom)

Write-Host "Updated: $espPath"
Write-Host "Backup:  $backupPath"
Write-Host 'Only YCState, ycPaint buffering, and wheel cleanup were changed.'
