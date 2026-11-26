#include "global_hook.h"

static GlobalHook* g_instance = nullptr;

GlobalHook::GlobalHook(AppConfig& cfg, HookCallbacks cb)
    : config_(cfg), cb_(std::move(cb))
{
    g_instance = this;
}

GlobalHook::~GlobalHook()
{
    Uninstall();
    if (g_instance == this)
    {
        g_instance = nullptr;
    }
}

bool GlobalHook::Install()
{
    HINSTANCE mod = GetModuleHandleW(nullptr);
    kbHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, mod, 0);
    msHook_ = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, mod, 0);
    return kbHook_ != nullptr && msHook_ != nullptr;
}

void GlobalHook::Uninstall()
{
    if (kbHook_)
    {
        UnhookWindowsHookEx(kbHook_);
        kbHook_ = nullptr;
    }
    if (msHook_)
    {
        UnhookWindowsHookEx(msHook_);
        msHook_ = nullptr;
    }
}

void GlobalHook::SetMenuOpen(bool open)
{
    menuOpen_ = open;
    if (!open)
    {
        hasLastMousePt_ = false;
    }
}

void GlobalHook::StartRebind(RebindTarget target)
{
    rebindTarget_ = target;
}

LRESULT CALLBACK GlobalHook::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (g_instance && nCode >= 0)
    {
        LRESULT res = g_instance->HandleKeyboard(nCode, wParam, lParam);
        if (res != 0)
        {
            return res;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK GlobalHook::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (g_instance && nCode >= 0)
    {
        LRESULT res = g_instance->HandleMouse(nCode, wParam, lParam);
        if (res != 0)
        {
            return res;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT GlobalHook::HandleKeyboard(int nCode, WPARAM wParam, LPARAM lParam)
{
    auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    if (!kb || (kb->flags & LLKHF_INJECTED) != 0)
    {
        return 0;
    }

    bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

    if (rebindTarget_ != RebindTarget::None && down)
    {
        RebindTarget target = rebindTarget_;
        rebindTarget_ = RebindTarget::None;
        if (kb->vkCode == VK_ESCAPE)
        {
            return 1;
        }
        if (target == RebindTarget::Hotkey)
        {
            config_.hotkeyVk = kb->vkCode;
            config_.isMouseHotkey = false;
        }
        else if (target == RebindTarget::VolDown)
        {
            config_.volDownVk = kb->vkCode;
            config_.volDownIsMouse = false;
        }
        else if (target == RebindTarget::VolUp)
        {
            config_.volUpVk = kb->vkCode;
            config_.volUpIsMouse = false;
        }
        SaveAppConfig(config_);
        if (cb_.onRebound) cb_.onRebound(kb->vkCode, false);
        return 1;
    }

    if (menuOpen_ && down && (kb->vkCode == VK_ESCAPE || kb->vkCode == VK_TAB))
    {
        menuOpen_ = false;
        keyIsDown_ = false;
        if (cb_.onClose) cb_.onClose(false);
        return (kb->vkCode == VK_ESCAPE) ? 1 : 0;
    }

    if (menuOpen_ && down)
    {
        if (!config_.volDownIsMouse && kb->vkCode == config_.volDownVk)
        {
            if (cb_.onWheel) cb_.onWheel(-120);
            return 1;
        }
        if (!config_.volUpIsMouse && kb->vkCode == config_.volUpVk)
        {
            if (cb_.onWheel) cb_.onWheel(120);
            return 1;
        }
    }

    if (config_.isMouseHotkey || kb->vkCode != config_.hotkeyVk)
    {
        return 0;
    }

    bool passThroughKey = (kb->vkCode == VK_LMENU || kb->vkCode == VK_RMENU || kb->vkCode == VK_MENU);

    if (config_.holdToOpen)
    {
        if (down && !keyIsDown_)
        {
            keyIsDown_ = true;
            menuOpen_ = true;
            hasLastMousePt_ = false;
            POINT pt{};
            GetCursorPos(&pt);
            if (cb_.onOpen) cb_.onOpen(pt);
        }
        else if (up && keyIsDown_)
        {
            keyIsDown_ = false;
            if (menuOpen_)
            {
                menuOpen_ = false;
                if (cb_.onClose) cb_.onClose(true);
            }
        }
        return passThroughKey ? 0 : 1;
    }
    else
    {
        if (down && !keyIsDown_)
        {
            keyIsDown_ = true;
            menuOpen_ = !menuOpen_;
            if (menuOpen_)
            {
                hasLastMousePt_ = false;
                POINT pt{};
                GetCursorPos(&pt);
                if (cb_.onOpen) cb_.onOpen(pt);
            }
            else
            {
                if (cb_.onClose) cb_.onClose(true);
            }
        }
        else if (up)
        {
            keyIsDown_ = false;
        }
        return passThroughKey ? 0 : 1;
    }
}

LRESULT GlobalHook::HandleMouse(int nCode, WPARAM wParam, LPARAM lParam)
{
    auto* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
    if (!ms || (ms->flags & LLMHF_INJECTED) != 0)
    {
        return 0;
    }

    UINT xBtn = 0;
    bool btnDown = false;
    bool btnUp = false;
    if (wParam == WM_MBUTTONDOWN) { xBtn = VK_MBUTTON; btnDown = true; }
    else if (wParam == WM_MBUTTONUP) { xBtn = VK_MBUTTON; btnUp = true; }
    else if (wParam == WM_XBUTTONDOWN)
    {
        xBtn = (HIWORD(ms->mouseData) == XBUTTON1) ? VK_XBUTTON1 : VK_XBUTTON2;
        btnDown = true;
    }
    else if (wParam == WM_XBUTTONUP)
    {
        xBtn = (HIWORD(ms->mouseData) == XBUTTON1) ? VK_XBUTTON1 : VK_XBUTTON2;
        btnUp = true;
    }

    if (rebindTarget_ != RebindTarget::None && btnDown && xBtn != 0)
    {
        RebindTarget target = rebindTarget_;
        rebindTarget_ = RebindTarget::None;
        if (target == RebindTarget::Hotkey)
        {
            config_.hotkeyVk = xBtn;
            config_.isMouseHotkey = true;
        }
        else if (target == RebindTarget::VolDown)
        {
            config_.volDownVk = xBtn;
            config_.volDownIsMouse = true;
        }
        else if (target == RebindTarget::VolUp)
        {
            config_.volUpVk = xBtn;
            config_.volUpIsMouse = true;
        }
        SaveAppConfig(config_);
        if (cb_.onRebound) cb_.onRebound(xBtn, true);
        return 1;
    }

    if (config_.isMouseHotkey && xBtn == config_.hotkeyVk)
    {
        if (config_.holdToOpen)
        {
            if (btnDown && !keyIsDown_)
            {
                keyIsDown_ = true;
                menuOpen_ = true;
                hasLastMousePt_ = false;
                if (cb_.onOpen) cb_.onOpen(ms->pt);
            }
            else if (btnUp && keyIsDown_)
            {
                keyIsDown_ = false;
                if (menuOpen_)
                {
                    menuOpen_ = false;
                    if (cb_.onClose) cb_.onClose(true);
                }
            }
            return 1;
        }
        else if (btnDown)
        {
            menuOpen_ = !menuOpen_;
            if (menuOpen_)
            {
                hasLastMousePt_ = false;
                if (cb_.onOpen) cb_.onOpen(ms->pt);
            }
            else
            {
                if (cb_.onClose) cb_.onClose(true);
            }
            return 1;
        }
    }

    if (!menuOpen_)
    {
        return 0;
    }

    if (xBtn != 0)
    {
        if (config_.volDownIsMouse && xBtn == config_.volDownVk)
        {
            if (btnDown && cb_.onWheel) cb_.onWheel(-120);
            return 1;
        }
        if (config_.volUpIsMouse && xBtn == config_.volUpVk)
        {
            if (btnDown && cb_.onWheel) cb_.onWheel(120);
            return 1;
        }
    }

    if (wParam == WM_MOUSEWHEEL)
    {
        short delta = static_cast<short>(HIWORD(ms->mouseData));
        if (cb_.onWheel && delta != 0)
        {
            cb_.onWheel(delta);
        }
        return 1;
    }

    if (wParam == WM_LBUTTONDOWN)
    {
        if (cb_.onLeftClick)
        {
            cb_.onLeftClick();
        }
        return 1;
    }

    if (wParam == WM_RBUTTONDOWN)
    {
        menuOpen_ = false;
        keyIsDown_ = false;
        if (cb_.onClose) cb_.onClose(false);
        return 1;
    }

    if (config_.blockGameInput && (wParam == WM_LBUTTONUP || wParam == WM_RBUTTONUP || wParam == WM_MBUTTONDOWN || wParam == WM_MBUTTONUP || wParam == WM_XBUTTONDOWN || wParam == WM_XBUTTONUP))
    {
        return 1;
    }

    return 0;
}
