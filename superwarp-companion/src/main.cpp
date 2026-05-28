// main.cpp - Win32 + Direct3D11 host for the superwarp companion v0.2.0.
//
// Restores window pos/size from superwarp_gui.json, drives the app::Tick +
// app::RenderFrame loop, updates the title bar dynamically with character +
// addon version, persists geometry + always-on-top to the config.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include "app.h"
#include "resource.h"
#include "theme.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <tchar.h>
#include <string>
#include <cstdio>

#pragma comment(lib, "dwmapi.lib")

// DWMWA_USE_IMMERSIVE_DARK_MODE values. Windows 10 build 18985+ uses 20; older
// builds (17763 - 18984) used the undocumented value 19. Try the modern value
// first, fall back to the legacy one. Silently no-ops on older systems.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#  define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1
#  define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19
#endif

static void EnableDarkTitleBar(HWND hwnd)
{
    BOOL dark = TRUE;
    if (FAILED(::DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark))))
        ::DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1, &dark, sizeof(dark));
}

// ---- D3D11 state -------------------------------------------------------
static ID3D11Device*           g_pd3dDevice           = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext    = nullptr;
static IDXGISwapChain*         g_pSwapChain           = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static UINT                    g_ResizeWidth = 0, g_ResizeHeight = 0;

// ---- DPI state ---------------------------------------------------------
// Track the currently-applied scale so we can rebuild fonts + style if the
// window moves to a monitor with a different DPI (WM_DPICHANGED).
static float                   g_AppliedDpiScale = 1.0f;
static float                   g_PendingDpiScale = 0.0f; // != 0 -> rebuild requested

// Build (or rebuild) the ImGui font atlas at the given pixel size.
// Uses Segoe UI when available for crisp text at high-DPI; falls back to
// ImGui's default ProggyClean otherwise. Includes glyph ranges beyond basic
// Latin so the destination state dots (●○·) and the em-dash in the title
// bar actually render instead of showing as boxes.
static void BuildFontAtScale(float scale)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    const float px = 12.0f * scale;

    // Persistent so the font atlas can keep referencing it.
    static const ImWchar ranges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement (includes ·)
        0x2010, 0x2030, // hyphens, dashes (— em-dash), ellipsis
        0x25A0, 0x25FF, // Geometric Shapes (● ○)
        0,
    };

    bool loaded = false;
    const char* candidates[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
    };
    for (const char* p : candidates)
    {
        if (io.Fonts->AddFontFromFileTTF(p, px, nullptr, ranges) != nullptr) { loaded = true; break; }
    }
    if (!loaded)
    {
        ImFontConfig cfg;
        cfg.SizePixels = 11.0f * scale;
        cfg.GlyphRanges = ranges;
        io.Fonts->AddFontDefault(&cfg);
    }
    io.Fonts->Build();
}

// Re-setup the aether style from scratch and scale it. Avoids the
// compounding-scale bug that would happen if we applied ScaleAllSizes()
// twice across a DPI change.
static void ApplyDpiScale(float scale)
{
    theme::setup(ImGui::GetStyle());
    if (scale != 1.0f) ImGui::GetStyle().ScaleAllSizes(scale);
    g_AppliedDpiScale = scale;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static bool   CreateDeviceD3D(HWND hWnd);
static void   CleanupDeviceD3D();
static void   CreateRenderTarget();
static void   CleanupRenderTarget();
static LRESULT WINAPI WndProc(HWND, UINT, WPARAM, LPARAM);

// Convert UTF-8 -> UTF-16 for Win32 wide APIs (SetWindowTextW).
static std::wstring widen(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    ImGui_ImplWin32_EnableDpiAwareness();

    // Embedded app icon (superwarp-companion.rc -> resources/icon.ico).
    // hIcon is the title-bar / alt-tab icon; hIconSm is the small variant
    // Windows picks for the upper-left corner. Both null-safe -- if loading
    // fails for any reason, Win32 falls back to the default app icon.
    HICON app_icon = ::LoadIconW((HINSTANCE)hInstance, MAKEINTRESOURCEW(IDI_APPICON));

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInstance,
                       app_icon, nullptr, nullptr, nullptr,
                       L"superwarp_companion", app_icon };
    ::RegisterClassExW(&wc);

    // Load config early so the window can be created with the persisted geometry.
    Config cfg = Config::load();
    // First-launch defaults are scaled to the system DPI so we don't open as a
    // tiny rectangle on a 200% / 250% display. Persisted geometry is honored
    // as-is (already in physical pixels).
    const UINT sys_dpi = ::GetDpiForSystem();
    const float sdpi = (sys_dpi > 0) ? (float)sys_dpi / 96.0f : 1.0f;
    int x = cfg.window_x ? (int)*cfg.window_x : 100;
    int y = cfg.window_y ? (int)*cfg.window_y : 100;
    int w = cfg.window_w ? (int)*cfg.window_w : (int)(540 * sdpi);
    int h = cfg.window_h ? (int)*cfg.window_h : (int)(780 * sdpi);
    if (w < 320) w = 320;
    if (h < 380) h = 380;

    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"superwarp \u2014 FFXI",
                                WS_OVERLAPPEDWINDOW, x, y, w, h,
                                nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Opt the title bar / non-client frame into Windows' dark theme so the
    // chrome doesn't look like a white slab on top of the aether panel.
    // No-op on pre-1809 Windows 10; the rest of the app is unaffected.
    EnableDarkTitleBar(hwnd);

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    if (cfg.always_on_top)
        ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // High-DPI bootstrap: query the actual scale for this window and build
    // the font + style at that size. Without this the entire panel paints at
    // logical (96 DPI) sizes and looks micro on a 200% / 250% display.
    const float startup_dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    BuildFontAtScale(startup_dpi);
    ApplyDpiScale(startup_dpi);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    App a;
    app::Init(a);
    bool last_aot = a.always_on_top;
    std::string last_title;

    bool running = true;
    while (running)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0u, 0u, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        if (g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Pending DPI rebuild (set by WM_DPICHANGED). Must run before NewFrame.
        if (g_PendingDpiScale != 0.0f)
        {
            const float new_scale = g_PendingDpiScale;
            g_PendingDpiScale = 0.0f;
            ImGui_ImplDX11_InvalidateDeviceObjects();
            BuildFontAtScale(new_scale);
            ApplyDpiScale(new_scale);
            ImGui_ImplDX11_CreateDeviceObjects();
        }

        // --- pre-frame ---
        app::Tick(a);

        // Always-on-top toggle (driven by the header pin checkbox).
        if (a.always_on_top != last_aot)
        {
            ::SetWindowPos(hwnd, a.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                           0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            last_aot = a.always_on_top;
        }

        // Geometry persistence: only write when it actually changes.
        RECT wr; ::GetWindowRect(hwnd, &wr);
        float wx = (float)wr.left;
        float wy = (float)wr.top;
        float ww = (float)(wr.right - wr.left);
        float wh = (float)(wr.bottom - wr.top);
        bool geom_changed =
            !a.config.window_x || *a.config.window_x != wx ||
            !a.config.window_y || *a.config.window_y != wy ||
            !a.config.window_w || *a.config.window_w != ww ||
            !a.config.window_h || *a.config.window_h != wh;
        if (geom_changed)
        {
            a.config.window_x = wx;
            a.config.window_y = wy;
            a.config.window_w = ww;
            a.config.window_h = wh;
            a.config.mark_dirty();
        }

        // Dynamic title: "<Character> -- superwarp v<addon> (GUI v<gui>)"
        // (u2014 em-dash to match the Rust GUI's title style.)
        std::string title;
        if (a.character.empty())
        {
            char buf[160];
            std::snprintf(buf, sizeof(buf), u8"superwarp \u2014 FFXI  (GUI v%s)", GUI_VERSION);
            title = buf;
        }
        else
        {
            const char* av = a.addon_version.empty() ? "..." : a.addon_version.c_str();
            char buf[200];
            std::snprintf(buf, sizeof(buf), u8"%s \u2014 superwarp v%s  (GUI v%s)",
                          a.character.c_str(), av, GUI_VERSION);
            title = buf;
        }
        if (title != last_title)
        {
            ::SetWindowTextW(hwnd, widen(title).c_str());
            last_title = title;
        }

        // --- frame ---
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        app::RenderFrame(a);

        ImGui::Render();
        const float clear[4] = { 14/255.0f, 15/255.0f, 26/255.0f, 1.0f }; // theme::BG
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // vsync
    }

    app::Shutdown(a);

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd; ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount        = 2;
    sd.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow       = hWnd;
    sd.SampleDesc.Count   = 1;
    sd.Windowed           = TRUE;
    sd.SwapEffect         = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, &fl, &g_pd3dDeviceContext);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            levels, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
            &g_pd3dDevice, &fl, &g_pd3dDeviceContext);
    if (hr != S_OK) return false;
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain)        { g_pSwapChain->Release();        g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice)        { g_pd3dDevice->Release();        g_pd3dDevice = nullptr; }
}

static void CreateRenderTarget()
{
    ID3D11Texture2D* back = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back)
    {
        g_pd3dDevice->CreateRenderTargetView(back, nullptr, &g_mainRenderTargetView);
        back->Release();
    }
}

static void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth  = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 320;
        mmi->ptMinTrackSize.y = 380;
        return 0;
    }
    case WM_DPICHANGED:
    {
        // Window was moved to (or system changed to) a monitor with a
        // different DPI. lParam holds Windows' suggested new window rect;
        // honor it, then queue a font + style rebuild for the next frame.
        const float new_scale = (float)LOWORD(wParam) / 96.0f;
        const RECT* sug = (const RECT*)lParam;
        ::SetWindowPos(hWnd, nullptr,
                       sug->left, sug->top,
                       sug->right  - sug->left,
                       sug->bottom - sug->top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        g_PendingDpiScale = new_scale;
        return 0;
    }
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
