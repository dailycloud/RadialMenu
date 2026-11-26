#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <functional>
#include "../core/config.h"

class OverlayWindow
{
public:
    OverlayWindow() = default;
    ~OverlayWindow();

    bool Create(HINSTANCE hInst);
    void Destroy();

    void SetInteractive(bool interactive);
    void CaptureFocus(HWND* outPrevFg);
    void RestoreFocus(HWND prevFg, bool keepInteractive);
    void SetRawMouseCallback(std::function<void(int dx, int dy)> cb);
    void ShowOverlay();
    void HideOverlay();

    bool BeginFrame();
    void EndFrame();

    HWND GetHwnd() const { return hwnd_; }
    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }
    POINT GetOrigin() const { return origin_; }

    void UpdateTrayIcon();
    void RemoveTrayIcon();

    static constexpr UINT kTrayMsg = WM_APP + 1;
    static constexpr UINT kCmdOpenSettings = 1001;
    static constexpr UINT kCmdToggleMenu = 1002;
    static constexpr UINT kCmdExit = 1003;

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
    bool CreateDeviceD3D();
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void ShowTrayContextMenu();

    HWND hwnd_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    POINT origin_{};
    int width_ = 0;
    int height_ = 0;
    bool visible_ = false;
    bool interactive_ = false;
    bool trayAdded_ = false;
    std::function<void(int dx, int dy)> rawMouseCb_;
};
