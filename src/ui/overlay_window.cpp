#include "overlay_window.h"
#include "../../third_party/imgui/imgui.h"
#include "../../third_party/imgui/backends/imgui_impl_win32.h"
#include "../../third_party/imgui/backends/imgui_impl_dx11.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <cstdio>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static OverlayWindow* g_overlay = nullptr;

OverlayWindow::~OverlayWindow()
{
    Destroy();
}

bool OverlayWindow::Create(HINSTANCE hInst)
{
    g_overlay = this;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.lpszClassName = L"RadialMenuWHookOverlayWnd";
    RegisterClassExW(&wc);

    origin_.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    origin_.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    width_ = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height_ = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width_ <= 0 || height_ <= 0)
    {
        origin_ = { 0, 0 };
        width_ = GetSystemMetrics(SM_CXSCREEN);
        height_ = GetSystemMetrics(SM_CYSCREEN);
    }

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED,
        wc.lpszClassName,
        L"RadialMenuWHook",
        WS_POPUP,
        origin_.x,
        origin_.y,
        width_,
        height_,
        nullptr,
        nullptr,
        hInst,
        nullptr);

    if (!hwnd_)
    {
        return false;
    }

    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);

    DWM_BLURBEHIND bb{};
    bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
    bb.fEnable = TRUE;
    bb.hRgnBlur = CreateRectRgn(0, 0, -1, -1);
    DwmEnableBlurBehindWindow(hwnd_, &bb);
    if (bb.hRgnBlur)
    {
        DeleteObject(bb.hRgnBlur);
    }

    if (!CreateDeviceD3D())
    {
        CleanupDeviceD3D();
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImFontConfig fc{};
    fc.OversampleH = 2;
    fc.OversampleV = 2;
    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\segoeui.ttf") != INVALID_FILE_ATTRIBUTES)
    {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f, &fc, io.Fonts->GetGlyphRangesCyrillic());
    }
    else
    {
        io.Fonts->AddFontDefault();
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 10.0f;
    st.FrameRounding = 6.0f;
    st.GrabRounding = 6.0f;
    st.WindowBorderSize = 1.0f;
    st.FrameBorderSize = 0.0f;
    st.WindowPadding = ImVec2(14.0f, 12.0f);
    st.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.11f, 0.96f);
    st.Colors[ImGuiCol_Border] = ImVec4(0.24f, 0.28f, 0.36f, 0.85f);
    st.Colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.11f, 0.15f, 1.0f);
    st.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.11f, 0.14f, 0.20f, 1.0f);
    st.Colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.16f, 0.22f, 1.0f);
    st.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.22f, 0.30f, 1.0f);

    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_, context_);

    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = hwnd_;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));

    UpdateTrayIcon();
    return true;
}

void OverlayWindow::Destroy()
{
    RemoveTrayIcon();
    if (device_)
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        CleanupDeviceD3D();
    }
    if (hwnd_)
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (g_overlay == this)
    {
        g_overlay = nullptr;
    }
}

void OverlayWindow::SetInteractive(bool interactive)
{
    if (!hwnd_ || interactive_ == interactive)
    {
        return;
    }
    interactive_ = interactive;
    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (interactive)
    {
        ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    }
    else
    {
        ex |= (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    }
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);
}

void OverlayWindow::CaptureFocus(HWND* outPrevFg)
{
    HWND prev = GetForegroundWindow();
    if (outPrevFg)
    {
        *outPrevFg = prev;
    }
    if (!hwnd_ || prev == hwnd_)
    {
        return;
    }

    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex & ~(WS_EX_NOACTIVATE | WS_EX_TRANSPARENT));
    interactive_ = true;

    DWORD curTid = GetCurrentThreadId();
    DWORD prevTid = prev ? GetWindowThreadProcessId(prev, nullptr) : 0;
    bool attached = (prevTid != 0 && prevTid != curTid) ? (AttachThreadInput(curTid, prevTid, TRUE) != FALSE) : false;

    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd_);
    BringWindowToTop(hwnd_);
    SetFocus(hwnd_);

    if (attached)
    {
        AttachThreadInput(curTid, prevTid, FALSE);
    }
}

void OverlayWindow::RestoreFocus(HWND prevFg, bool keepInteractive)
{
    if (hwnd_ && !keepInteractive)
    {
        LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT);
        interactive_ = false;
    }
    if (prevFg && IsWindow(prevFg) && prevFg != hwnd_)
    {
        DWORD curTid = GetCurrentThreadId();
        DWORD prevTid = GetWindowThreadProcessId(prevFg, nullptr);
        bool attached = (prevTid != 0 && prevTid != curTid) ? (AttachThreadInput(curTid, prevTid, TRUE) != FALSE) : false;
        SetForegroundWindow(prevFg);
        if (attached)
        {
            AttachThreadInput(curTid, prevTid, FALSE);
        }
    }
}

void OverlayWindow::SetRawMouseCallback(std::function<void(int dx, int dy)> cb)
{
    rawMouseCb_ = std::move(cb);
}

void OverlayWindow::ShowOverlay()
{
    if (!hwnd_)
    {
        return;
    }
    SetWindowPos(
        hwnd_,
        HWND_TOPMOST,
        origin_.x,
        origin_.y,
        width_,
        height_,
        SWP_NOACTIVATE | (visible_ ? 0 : SWP_SHOWWINDOW));
    visible_ = true;
}

void OverlayWindow::HideOverlay()
{
    if (!hwnd_ || !visible_)
    {
        return;
    }
    ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
}

bool OverlayWindow::BeginFrame()
{
    if (!visible_ || !device_)
    {
        return false;
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    return true;
}

void OverlayWindow::EndFrame()
{
    ImGui::Render();
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    context_->OMSetRenderTargets(1, &rtv_, nullptr);
    context_->ClearRenderTargetView(rtv_, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    swapChain_->Present(1, 0);
}

bool OverlayWindow::CreateDeviceD3D()
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd_;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl{};
    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &swapChain_, &device_, &fl, &context_)))
    {
        return false;
    }

    CreateRenderTarget();
    return rtv_ != nullptr;
}

void OverlayWindow::CreateRenderTarget()
{
    ID3D11Texture2D* bb = nullptr;
    if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&bb))) && bb)
    {
        device_->CreateRenderTargetView(bb, nullptr, &rtv_);
        bb->Release();
    }
}

void OverlayWindow::CleanupDeviceD3D()
{
    if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
    if (swapChain_) { swapChain_->Release(); swapChain_ = nullptr; }
    if (context_) { context_->Release(); context_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
}

void OverlayWindow::UpdateTrayIcon()
{
    if (!hwnd_ || trayAdded_)
    {
        return;
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kTrayMsg;
    nid.hIcon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));
    lstrcpynW(nid.szTip, L"RadialMenuWHook", ARRAYSIZE(nid.szTip));
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

void OverlayWindow::RemoveTrayIcon()
{
    if (!hwnd_ || !trayAdded_)
    {
        return;
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    trayAdded_ = false;
}

void OverlayWindow::ShowTrayContextMenu()
{
    POINT pt{};
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCmdToggleMenu, L"Open Radial Menu");
    AppendMenuW(menu, MF_STRING, kCmdOpenSettings, L"Settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"Exit");
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp))
    {
        return 1;
    }

    if (msg == kTrayMsg)
    {
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU)
        {
            if (g_overlay) g_overlay->ShowTrayContextMenu();
            return 0;
        }
        if (LOWORD(lp) == WM_LBUTTONUP)
        {
            PostMessageW(hWnd, WM_COMMAND, kCmdOpenSettings, 0);
            return 0;
        }
    }

    if (msg == WM_INPUT && g_overlay && g_overlay->rawMouseCb_)
    {
        UINT size = 0;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        if (size > 0 && size <= 128)
        {
            BYTE buf[128]{};
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) == size)
            {
                auto* raw = reinterpret_cast<RAWINPUT*>(buf);
                if (raw->header.dwType == RIM_TYPEMOUSE && (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
                {
                    int dx = static_cast<int>(raw->data.mouse.lLastX);
                    int dy = static_cast<int>(raw->data.mouse.lLastY);
                    if (dx != 0 || dy != 0)
                    {
                        g_overlay->rawMouseCb_(dx, dy);
                    }
                }
            }
        }
        return 0;
    }

    if (msg == WM_MOUSEACTIVATE && g_overlay && !g_overlay->interactive_)
    {
        return MA_NOACTIVATE;
    }

    if (msg == WM_SIZE && g_overlay && g_overlay->device_ && wp != SIZE_MINIMIZED)
    {
        if (g_overlay->rtv_) { g_overlay->rtv_->Release(); g_overlay->rtv_ = nullptr; }
        g_overlay->swapChain_->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
        g_overlay->CreateRenderTarget();
        return 0;
    }

    if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wp, lp);
}
