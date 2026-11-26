#include "system_module.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

static ULONGLONG FtToU64(const FILETIME& ft)
{
    return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

SystemModule::SystemModule(const AppConfig& cfg, std::function<void()> onOpenSettings)
    : config_(cfg), onOpenSettings_(std::move(onOpenSettings))
{
    Refresh();
}

void SystemModule::Refresh()
{
    ULONGLONG now = GetTickCount64();
    if (now - lastSampleTick_ < 500 && lastSampleTick_ != 0)
    {
        return;
    }
    lastSampleTick_ = now;

    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user))
    {
        ULONGLONG i = FtToU64(idle);
        ULONGLONG k = FtToU64(kernel);
        ULONGLONG u = FtToU64(user);
        if (lastKernel_ != 0)
        {
            ULONGLONG di = i - lastIdle_;
            ULONGLONG dt = (k - lastKernel_) + (u - lastUser_);
            if (dt > 0)
            {
                cpuUsage_ = static_cast<float>(dt - di) / static_cast<float>(dt);
            }
        }
        lastIdle_ = i;
        lastKernel_ = k;
        lastUser_ = u;
    }

    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
    {
        ramUsage_ = static_cast<float>(ms.dwMemoryLoad) / 100.0f;
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    char tbuf[16]{};
    std::snprintf(tbuf, sizeof(tbuf), "%02u:%02u", st.wHour, st.wMinute);
    timeStr_ = tbuf;
}

void SystemModule::AppendSectors(std::vector<RadialSector>& out)
{
    if (!config_.enableSystem)
    {
        return;
    }

    RadialSector s{};
    s.id = "system_info";
    s.label = "System";
    char buf[24]{};
    std::snprintf(buf, sizeof(buf), "CPU %d%%", static_cast<int>(cpuUsage_ * 100.0f + 0.5f));
    s.valueText = buf;
    s.icon = IconKind::Monitor;
    if (!cursorCaptured_)
    {
        s.onActivate = [this]()
        {
            if (onOpenSettings_) onOpenSettings_();
        };
    }
    s.buildSubItems = [this]() -> std::vector<RadialSubItem>
    {
        std::vector<RadialSubItem> items;
        if (!cursorCaptured_)
        {
            items.push_back({ "sys_cfg", "Config", "", IconKind::Monitor, -1.0f, false, [this]() { if (onOpenSettings_) onOpenSettings_(); }, nullptr });
        }
        items.push_back({ "sys_shot", "Clip", "", IconKind::App, -1.0f, false, []()
        {
            INPUT in[6]{};
            in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_LWIN;
            in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = VK_LSHIFT;
            in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = 'S';
            in[3] = in[2]; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
            in[4] = in[1]; in[4].ki.dwFlags = KEYEVENTF_KEYUP;
            in[5] = in[0]; in[5].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(6, in, sizeof(INPUT));
        }, nullptr });
        return items;
    };
    out.push_back(std::move(s));
}
