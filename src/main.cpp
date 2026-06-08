// Video Frame Extractor — main entry point
// Win32 window + ImGui + DirectX 11 backend

#include "app.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>   // DragQueryFile

#include <cstdio>
#include <cstring>

// ──────────────────────────────────────────────
//  Globals
// ──────────────────────────────────────────────

static AppState              g_state;
static ID3D11Device*         g_pd3dDevice   = nullptr;
static ID3D11DeviceContext*  g_pd3dContext  = nullptr;
static IDXGISwapChain*       g_pSwapChain   = nullptr;
static ID3D11RenderTargetView* g_pRTV       = nullptr;

// Forward declarations
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ──────────────────────────────────────────────
//  Render target helpers
// ──────────────────────────────────────────────

static bool CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                          (void**)&pBackBuffer);
    if (FAILED(hr) || !pBackBuffer) return false;

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRTV);
    pBackBuffer->Release();
    return SUCCEEDED(hr);
}

static void CleanupRenderTarget() {
    if (g_pRTV) { g_pRTV->Release(); g_pRTV = nullptr; }
}

// ──────────────────────────────────────────────
//  WinMain
// ──────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                   LPSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // --- COM init (needed for file dialogs) ---
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // --- Win32 window ---
    const wchar_t CLASS_NAME[] = L"VideoFrameExtractor";

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    // Client area = 500×800  →  work out total window size
    RECT rect = { 0, 0, 500, 800 };
    AdjustWindowRect(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Video Frame Extractor",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd) {
        CoUninitialize();
        return 1;
    }

    // Enable drag & drop
    DragAcceptFiles(hwnd, TRUE);

    // --- DirectX 11 init ---
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width                     = 0;  // auto from window
    sd.BufferDesc.Height                    = 0;
    sd.BufferDesc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator     = 60;
    sd.BufferDesc.RefreshRate.Denominator   = 1;
    sd.SampleDesc.Count                     = 1;
    sd.SampleDesc.Quality                   = 0;
    sd.BufferUsage                          = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount                          = 2;
    sd.OutputWindow                         = hwnd;
    sd.Windowed                             = TRUE;
    sd.SwapEffect                           = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        &featureLevel, 1, D3D11_SDK_VERSION,
        &sd, &g_pSwapChain, &g_pd3dDevice, nullptr, &g_pd3dContext
    );

    // Fallback to WARP (software rasterizer) if no hardware GPU
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            &featureLevel, 1, D3D11_SDK_VERSION,
            &sd, &g_pSwapChain, &g_pd3dDevice, nullptr, &g_pd3dContext
        );
    }

    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        CoUninitialize();
        return 1;
    }

    CreateRenderTarget();

    // --- ImGui init ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;   // no ini file

    // Style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding  = 3.0f;

    // Load system Chinese font (Microsoft YaHei)
    io.Fonts->AddFontFromFileTTF("c:/Windows/Fonts/msyh.ttc", 16.0f, nullptr,
        io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

    // Init ImGui backends for Win32 + DX11
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dContext);

    // Show window
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // --- Main loop ---
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Disable padding/margin for full-window UI
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);

        render_ui(&g_state);

        ImGui::PopStyleVar(2);

        // Render
        ImGui::Render();

        float clear_color[4] = { 0.1f, 0.1f, 0.12f, 1.0f };
        g_pd3dContext->OMSetRenderTargets(1, &g_pRTV, nullptr);
        g_pd3dContext->ClearRenderTargetView(g_pRTV, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);  // vsync
    }

    // --- Cleanup ---
    if (g_state.extracting) {
        g_state.cancel_extraction();
        g_state.extracting = false;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupRenderTarget();
    if (g_pSwapChain)  { g_pSwapChain->Release();  g_pSwapChain  = nullptr; }
    if (g_pd3dContext) { g_pd3dContext->Release(); g_pd3dContext = nullptr; }
    if (g_pd3dDevice)  { g_pd3dDevice->Release();  g_pd3dDevice  = nullptr; }

    DestroyWindow(hwnd);
    UnregisterClassW(CLASS_NAME, hInstance);
    CoUninitialize();

    return 0;
}

// ──────────────────────────────────────────────
//  Win32 Window Procedure
// ──────────────────────────────────────────────

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Let ImGui process input messages first
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {

    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0,
                (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;

    case WM_DROPFILES: {
        // Drag & drop video file
        wchar_t wide_path[MAX_PATH];
        HDROP hDrop = (HDROP)wParam;
        if (DragQueryFileW(hDrop, 0, wide_path, MAX_PATH) > 0) {
            WideCharToMultiByte(CP_UTF8, 0, wide_path, -1,
                g_state.video_path, sizeof(g_state.video_path),
                nullptr, nullptr);
            g_state.load_video_info();
        }
        DragFinish(hDrop);
        return 0;
    }

    case WM_KEYDOWN:
        // ESC to cancel extraction (only if ImGui didn't consume the key)
        if (wParam == VK_ESCAPE && g_state.extracting) {
            g_state.cancel_extraction();
            return 0;
        }
        break;  // let DefWindowProc handle other keys

    case WM_DESTROY:
        if (g_state.extracting) {
            g_state.cancel_extraction();
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
