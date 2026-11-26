#include "media_module.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

MediaModule::MediaModule(const AppConfig& cfg) : config_(cfg)
{
}

void MediaModule::SendMediaKey(WORD vk)
{
    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

void MediaModule::AppendSectors(std::vector<RadialSector>& out)
{
    if (!config_.enableMedia)
    {
        return;
    }

    RadialSector s{};
    s.id = "media_ctrl";
    s.label = "Media";
    s.valueText = "Play/Pause";
    s.icon = IconKind::Media;
    s.onActivate = []() { SendMediaKey(VK_MEDIA_PLAY_PAUSE); };
    s.onWheel = [](int delta)
    {
        SendMediaKey(delta > 0 ? VK_MEDIA_NEXT_TRACK : VK_MEDIA_PREV_TRACK);
    };
    s.buildSubItems = []() -> std::vector<RadialSubItem>
    {
        std::vector<RadialSubItem> items;
        items.push_back({ "media_prev", "Prev", "", IconKind::Prev, -1.0f, false, []() { SendMediaKey(VK_MEDIA_PREV_TRACK); }, nullptr });
        items.push_back({ "media_pp", "Play", "", IconKind::PlayPause, -1.0f, false, []() { SendMediaKey(VK_MEDIA_PLAY_PAUSE); }, nullptr });
        items.push_back({ "media_next", "Next", "", IconKind::Next, -1.0f, false, []() { SendMediaKey(VK_MEDIA_NEXT_TRACK); }, nullptr });
        return items;
    };
    out.push_back(std::move(s));
}
