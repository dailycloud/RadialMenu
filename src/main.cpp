#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <memory>
#include "core/config.h"
#include "core/updater.h"
#include "core/module.h"
#include "modules/audio_module.h"
#include "modules/media_module.h"
#include "modules/system_module.h"
#include "input/global_hook.h"
#include "ui/overlay_window.h"
#include "ui/radial_renderer.h"
#include "../third_party/imgui/imgui.h"

static float GetWindowsPointerMultiplier()
{
    int speed = 10;
    SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &speed, 0);
    static const float table[20] = {
        0.03125f, 0.0625f, 0.125f, 0.25f, 0.375f,
        0.50f, 0.625f, 0.75f, 0.875f, 1.0f,
        1.25f, 1.50f, 1.75f, 2.00f, 2.25f,
        2.50f, 2.75f, 3.00f, 3.25f, 3.50f
    };
    int idx = (speed < 1) ? 0 : ((speed > 20) ? 19 : speed - 1);
    return table[idx];
}

static bool IsOsCursorLockedOrHidden(int screenW, int screenH)
{
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING) == 0)
    {
        return true;
    }

    RECT clip{};
    if (GetClipCursor(&clip))
    {
        int cw = clip.right - clip.left;
        int ch = clip.bottom - clip.top;
        if (cw < screenW - 8 || ch < screenH - 8)
        {
            return true;
        }
    }
    return false;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    HANDLE singleMutex = CreateMutexW(nullptr, TRUE, L"Local\\RadialMenuWHook_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (singleMutex) CloseHandle(singleMutex);
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    AppConfig cfg = LoadAppConfig();
    OverlayWindow overlay;
    if (!overlay.Create(hInst))
    {
        if (singleMutex) CloseHandle(singleMutex);
        return 1;
    }

    bool showSettings = false;
    bool previewActive = false;
    bool menuOpenedWithLockedCursor = false;
    HWND prevFgWnd = nullptr;
    char newActLabel[32] = "";
    char newActTarget[128] = "";

    AudioModule audioMod(cfg);
    MediaModule mediaMod(cfg);
    SystemModule sysMod(cfg, [&]()
    {
        if (menuOpenedWithLockedCursor || IsOsCursorLockedOrHidden(overlay.GetWidth(), overlay.GetHeight()))
        {
            return;
        }
        showSettings = true;
        overlay.SetInteractive(true);
        overlay.ShowOverlay();
    });

    std::vector<RadialSector> sectors;
    auto rebuildSectors = [&]()
    {
        audioMod.Refresh();
        sysMod.SetCursorCaptured(menuOpenedWithLockedCursor);
        sysMod.Refresh();
        sectors.clear();
        audioMod.AppendSectors(sectors);
        mediaMod.AppendSectors(sectors);
        sysMod.AppendSectors(sectors);
    };
    rebuildSectors();

    RadialRenderer radial;
    overlay.SetRawMouseCallback([&](int dx, int dy)
    {
        if (!radial.IsOpen())
        {
            return;
        }

        if (IsOsCursorLockedOrHidden(overlay.GetWidth(), overlay.GetHeight()))
        {
            float scale = GetWindowsPointerMultiplier() * cfg.gameMouseSens;
            radial.ApplyRawDelta(dx, dy, scale);
        }
        else
        {
            POINT cur{};
            GetCursorPos(&cur);
            POINT org = overlay.GetOrigin();
            radial.SetAbsoluteCursor(
                static_cast<float>(cur.x - org.x),
                static_cast<float>(cur.y - org.y),
                false);
        }
    });
    GlobalHook* hookPtr = nullptr;

    HookCallbacks cbs{};
    cbs.onOpen = [&](POINT cursorPt)
    {
        previewActive = false;
        bool wasLocked = IsOsCursorLockedOrHidden(overlay.GetWidth(), overlay.GetHeight());
        menuOpenedWithLockedCursor = wasLocked;
        rebuildSectors();
        POINT org = overlay.GetOrigin();
        float w = static_cast<float>(overlay.GetWidth());
        float h = static_cast<float>(overlay.GetHeight());
        float cx = wasLocked ? (w * 0.5f) : static_cast<float>(cursorPt.x - org.x);
        float cy = wasLocked ? (h * 0.5f) : static_cast<float>(cursorPt.y - org.y);
        float minPad = cfg.outerRadius + 84.0f;
        if (cx < minPad) cx = minPad;
        if (cy < minPad) cy = minPad;
        if (cx > w - minPad) cx = w - minPad;
        if (cy > h - minPad) cy = h - minPad;
        radial.OpenAt(cx, cy, sectors);
        if (!showSettings && !cfg.blockGameInput) overlay.SetInteractive(false);
        overlay.ShowOverlay();
        if (cfg.blockGameInput)
        {
            overlay.CaptureFocus(&prevFgWnd);
            if (wasLocked)
            {
                SetCursorPos(org.x + static_cast<int>(cx), org.y + static_cast<int>(cy));
            }
        }
    };
    cbs.onClose = [&](bool activate)
    {
        if (activate)
        {
            radial.ExecuteHovered(sectors);
        }
        radial.Close();
        if (cfg.blockGameInput)
        {
            overlay.RestoreFocus(prevFgWnd, showSettings);
            prevFgWnd = nullptr;
        }
    };
    cbs.onWheel = [&](int delta)
    {
        radial.HandleWheel(delta, sectors);
        rebuildSectors();
    };
    cbs.onLeftClick = [&]()
    {
        if (hookPtr) hookPtr->SetMenuOpen(false);
        radial.ExecuteHovered(sectors);
        radial.Close();
        if (cfg.blockGameInput)
        {
            overlay.RestoreFocus(prevFgWnd, showSettings);
            prevFgWnd = nullptr;
        }
    };
    cbs.onRebound = [&](UINT, bool)
    {
        showSettings = true;
    };

    GlobalHook hook(cfg, std::move(cbs));
    hookPtr = &hook;
    hook.Install();

    AppUpdater updater;
    updater.CheckAsync();

    ULONGLONG lastTick = GetTickCount64();
    MSG msg{};
    bool quit = false;
    while (!quit)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                quit = true;
                break;
            }
            if (msg.message == WM_COMMAND)
            {
                UINT cmd = LOWORD(msg.wParam);
                if (cmd == OverlayWindow::kCmdExit)
                {
                    quit = true;
                }
                else if (cmd == OverlayWindow::kCmdOpenSettings)
                {
                    showSettings = true;
                    overlay.SetInteractive(true);
                    overlay.ShowOverlay();
                    SetForegroundWindow(overlay.GetHwnd());
                }
                else if (cmd == OverlayWindow::kCmdToggleMenu)
                {
                    previewActive = !previewActive;
                    if (previewActive)
                    {
                        rebuildSectors();
                        radial.OpenAt(overlay.GetWidth() * 0.62f, overlay.GetHeight() * 0.5f, sectors);
                        overlay.ShowOverlay();
                    }
                    else if (!hook.IsMenuOpen())
                    {
                        radial.Close();
                    }
                }
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (quit) break;

        ULONGLONG now = GetTickCount64();
        float dt = static_cast<float>(now - lastTick) / 1000.0f;
        lastTick = now;

        if (radial.IsOpen() && hook.IsMenuOpen())
        {
            rebuildSectors();
        }

        bool anim = radial.Update(dt, sectors);
        bool showUpdateNotify = updater.ShouldShowNotification() && !showSettings && !IsOsCursorLockedOrHidden(overlay.GetWidth(), overlay.GetHeight());
        bool needDraw = anim || showSettings || showUpdateNotify;

        if (!needDraw)
        {
            if (!hook.IsMenuOpen())
            {
                overlay.SetInteractive(false);
                overlay.HideOverlay();
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 25, QS_ALLINPUT);
            continue;
        }

        if (showUpdateNotify && !hook.IsMenuOpen())
        {
            POINT cur{};
            GetCursorPos(&cur);
            POINT org = overlay.GetOrigin();
            float nx = static_cast<float>(overlay.GetWidth()) - 250.0f;
            float ny = static_cast<float>(overlay.GetHeight()) - 120.0f;
            float lx = static_cast<float>(cur.x - org.x);
            float ly = static_cast<float>(cur.y - org.y);
            bool overNotify = (lx >= nx && lx <= nx + 236.0f && ly >= ny && ly <= ny + 96.0f);
            overlay.SetInteractive(overNotify);
            overlay.ShowOverlay();
        }

        if (!overlay.BeginFrame())
        {
            Sleep(16);
            continue;
        }

        if (anim)
        {
            radial.Render(cfg, sectors, sysMod.GetTimeString(), audioMod.GetMasterVolume(), audioMod.IsMasterMuted());
        }

        if (showSettings)
        {
            ImGui::SetNextWindowSize(ImVec2(390.0f, 540.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(
                ImVec2(overlay.GetWidth() * 0.5f - 195.0f, overlay.GetHeight() * 0.5f - 270.0f),
                ImGuiCond_FirstUseEver);

            if (ImGui::Begin("RadialMenuWHook Settings", &showSettings, ImGuiWindowFlags_NoCollapse))
            {
                std::string hkLabel = hook.GetRebindTarget() == RebindTarget::Hotkey
                    ? "Press any key or Mouse4/5..."
                    : ("Hotkey: " + FormatHotkeyName(cfg.hotkeyVk, cfg.isMouseHotkey));
                if (ImGui::Button(hkLabel.c_str(), ImVec2(-1.0f, 30.0f)))
                {
                    hook.StartRebind(RebindTarget::Hotkey);
                }

                std::string vdLabel = hook.GetRebindTarget() == RebindTarget::VolDown
                    ? "Press Vol -..."
                    : ("Vol -: " + FormatHotkeyName(cfg.volDownVk, cfg.volDownIsMouse));
                if (ImGui::Button(vdLabel.c_str(), ImVec2(166.0f, 26.0f)))
                {
                    hook.StartRebind(RebindTarget::VolDown);
                }
                ImGui::SameLine();
                std::string vuLabel = hook.GetRebindTarget() == RebindTarget::VolUp
                    ? "Press Vol +..."
                    : ("Vol +: " + FormatHotkeyName(cfg.volUpVk, cfg.volUpIsMouse));
                if (ImGui::Button(vuLabel.c_str(), ImVec2(-1.0f, 26.0f)))
                {
                    hook.StartRebind(RebindTarget::VolUp);
                }

                bool dirty = false;
                dirty |= ImGui::Checkbox("Hold hotkey to keep open", &cfg.holdToOpen);
                dirty |= ImGui::Checkbox("Block game input while open", &cfg.blockGameInput);
                dirty |= ImGui::SliderFloat("Outer Radius", &cfg.outerRadius, 120.0f, 230.0f, "%.0f px");
                dirty |= ImGui::SliderFloat("Inner Radius", &cfg.innerRadius, 40.0f, 80.0f, "%.0f px");
                float stepPct = cfg.wheelStep * 100.0f;
                if (ImGui::SliderFloat("Wheel Step", &stepPct, 1.0f, 12.0f, "%.0f %%"))
                {
                    cfg.wheelStep = stepPct / 100.0f;
                    dirty = true;
                }
                dirty |= ImGui::SliderFloat("In-Game Sens", &cfg.gameMouseSens, 0.05f, 2.00f, "%.2fx");
                dirty |= ImGui::SliderFloat("Opacity", &cfg.bgAlpha, 0.55f, 1.00f, "%.2f");
                dirty |= ImGui::ColorEdit3("Accent", cfg.accentColor);

                ImGui::SeparatorText("Modules");
                dirty |= ImGui::Checkbox("Master Sound", &cfg.enableMasterAudio);
                dirty |= ImGui::Checkbox("App Mixer (CS2 / Spotify / Discord)", &cfg.enableAppMixer);
                dirty |= ImGui::Checkbox("Microphone Mute", &cfg.enableMic);
                dirty |= ImGui::Checkbox("Media Controls", &cfg.enableMedia);
                dirty |= ImGui::Checkbox("System HUD", &cfg.enableSystem);

                ImGui::SeparatorText("Custom Fast Actions (App Mute / Vol)");
                if (ImGui::Button("+ Discord", ImVec2(106.0f, 24.0f)))
                {
                    cfg.customActions.push_back({ "Discord", "discord", 0 });
                    dirty = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("+ Browser", ImVec2(106.0f, 24.0f)))
                {
                    cfg.customActions.push_back({ "Browser", "chrome,msedge,firefox,opera,yandex,browser", 0 });
                    dirty = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("+ Spotify", ImVec2(-1.0f, 24.0f)))
                {
                    cfg.customActions.push_back({ "Spotify", "spotify", 0 });
                    dirty = true;
                }

                ImGui::SetNextItemWidth(110.0f);
                ImGui::InputTextWithHint("##actLbl", "Label", newActLabel, sizeof(newActLabel));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(155.0f);
                ImGui::InputTextWithHint("##actExe", "exe (e.g. discord)", newActTarget, sizeof(newActTarget));
                ImGui::SameLine();
                if (ImGui::Button("Add", ImVec2(-1.0f, 0.0f)) && newActLabel[0] != '\0' && newActTarget[0] != '\0')
                {
                    cfg.customActions.push_back({ newActLabel, newActTarget, 0 });
                    newActLabel[0] = '\0';
                    newActTarget[0] = '\0';
                    dirty = true;
                }

                int removeIdx = -1;
                for (int idx = 0; idx < static_cast<int>(cfg.customActions.size()); idx++)
                {
                    ImGui::PushID(idx);
                    if (ImGui::SmallButton("X"))
                    {
                        removeIdx = idx;
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s (%s)", cfg.customActions[idx].label.c_str(), cfg.customActions[idx].target.c_str());
                    ImGui::PopID();
                }
                if (removeIdx >= 0)
                {
                    cfg.customActions.erase(cfg.customActions.begin() + removeIdx);
                    dirty = true;
                }

                if (dirty)
                {
                    SaveAppConfig(cfg);
                    rebuildSectors();
                }

                ImGui::SeparatorText("Updates");
                UpdateInfo up = updater.GetSnapshot();
                ImGui::Text("Version: v%s  %s", kAppVersion, up.statusText.c_str());
                if (ImGui::Button(up.checking ? "Checking..." : "Check for Updates", ImVec2(170.0f, 26.0f)) && !up.checking)
                {
                    updater.CheckAsync();
                }
                if (up.available)
                {
                    ImGui::SameLine();
                    if (ImGui::Button(up.installing ? "Updating..." : "Install Update", ImVec2(-1.0f, 26.0f)) && !up.installing)
                    {
                        updater.InstallAsync();
                    }
                }

                ImGui::Spacing();
                if (ImGui::Button(previewActive ? "Stop Preview" : "Preview Radial Menu", ImVec2(170.0f, 28.0f)))
                {
                    previewActive = !previewActive;
                    if (previewActive)
                    {
                        rebuildSectors();
                        radial.OpenAt(overlay.GetWidth() * 0.65f, overlay.GetHeight() * 0.5f, sectors);
                    }
                    else if (!hook.IsMenuOpen())
                    {
                        radial.Close();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Hide", ImVec2(-1.0f, 28.0f)))
                {
                    showSettings = false;
                    previewActive = false;
                    if (!hook.IsMenuOpen()) radial.Close();
                }
            }
            ImGui::End();

            if (!showSettings)
            {
                previewActive = false;
                if (!hook.IsMenuOpen()) radial.Close();
                overlay.SetInteractive(false);
            }
        }

        if (showUpdateNotify)
        {
            UpdateInfo up = updater.GetSnapshot();
            ImGui::SetNextWindowPos(
                ImVec2(static_cast<float>(overlay.GetWidth()) - 250.0f, static_cast<float>(overlay.GetHeight()) - 120.0f),
                ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(236.0f, 92.0f), ImGuiCond_Always);
            bool openNotify = true;
            if (ImGui::Begin("Update##Notify", &openNotify, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
            {
                ImGui::Text("New ver. %s", up.latestVersion.c_str());
                ImGui::Spacing();
                if (ImGui::Button(up.installing ? "updating..." : "update", ImVec2(-1.0f, 24.0f)) && !up.installing)
                {
                    updater.InstallAsync();
                }
            }
            ImGui::End();
            if (!openNotify)
            {
                updater.DismissNotification();
                overlay.SetInteractive(false);
            }
        }

        overlay.EndFrame();
    }

    hook.Uninstall();
    overlay.Destroy();
    if (singleMutex) CloseHandle(singleMutex);
    return 0;
}
