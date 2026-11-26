#pragma once

#include "../core/config.h"
#include <functional>

struct HookCallbacks
{
    std::function<void(POINT cursorScreen)> onOpen;
    std::function<void(bool activate)> onClose;
    std::function<void(int delta)> onWheel;
    std::function<void()> onLeftClick;
    std::function<void(UINT vk, bool isMouse)> onRebound;
};

enum class RebindTarget
{
    None,
    Hotkey,
    VolDown,
    VolUp
};

class GlobalHook
{
public:
    GlobalHook(AppConfig& cfg, HookCallbacks cb);
    ~GlobalHook();

    bool Install();
    void Uninstall();

    void SetMenuOpen(bool open);
    bool IsMenuOpen() const { return menuOpen_; }
    void StartRebind(RebindTarget target = RebindTarget::Hotkey);
    bool IsRebinding() const { return rebindTarget_ != RebindTarget::None; }
    RebindTarget GetRebindTarget() const { return rebindTarget_; }

private:
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

    LRESULT HandleKeyboard(int nCode, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMouse(int nCode, WPARAM wParam, LPARAM lParam);

    AppConfig& config_;
    HookCallbacks cb_;
    HHOOK kbHook_ = nullptr;
    HHOOK msHook_ = nullptr;
    bool menuOpen_ = false;
    bool keyIsDown_ = false;
    RebindTarget rebindTarget_ = RebindTarget::None;
    POINT lastMousePt_{};
    bool hasLastMousePt_ = false;
};
