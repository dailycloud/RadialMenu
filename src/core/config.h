#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

struct CustomAction
{
    std::string label;
    std::string target;
    int type = 0;
};

struct AppConfig
{
    UINT hotkeyVk = VK_CAPITAL;
    bool isMouseHotkey = false;
    UINT volDownVk = VK_XBUTTON1;
    bool volDownIsMouse = true;
    UINT volUpVk = VK_XBUTTON2;
    bool volUpIsMouse = true;
    bool holdToOpen = true;
    bool blockGameInput = false;
    float outerRadius = 152.0f;
    float innerRadius = 46.0f;
    float wheelStep = 0.04f;
    float gameMouseSens = 0.50f;
    float bgAlpha = 0.92f;
    float accentColor[3] = { 0.86f, 0.72f, 0.42f };
    bool enableMasterAudio = true;
    bool enableAppMixer = true;
    bool enableMic = true;
    bool enableMedia = true;
    bool enableSystem = true;
    std::vector<CustomAction> customActions;
};

AppConfig LoadAppConfig();
void SaveAppConfig(const AppConfig& cfg);
std::string FormatHotkeyName(UINT vk, bool isMouse);
