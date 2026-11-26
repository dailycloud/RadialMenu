#include "config.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

static std::wstring GetConfigPath()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path);
    auto pos = s.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
    {
        s = s.substr(0, pos + 1);
    }
    s += L"radial_config.ini";
    return s;
}

AppConfig LoadAppConfig()
{
    AppConfig cfg{};
    std::ifstream in(GetConfigPath());
    if (!in.is_open())
    {
        return cfg;
    }

    std::string line;
    while (std::getline(in, line))
    {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        if (k == "hotkeyVk") cfg.hotkeyVk = static_cast<UINT>(std::strtoul(v.c_str(), nullptr, 10));
        else if (k == "isMouseHotkey") cfg.isMouseHotkey = (v == "1");
        else if (k == "volDownVk") cfg.volDownVk = static_cast<UINT>(std::strtoul(v.c_str(), nullptr, 10));
        else if (k == "volDownIsMouse") cfg.volDownIsMouse = (v == "1");
        else if (k == "volUpVk") cfg.volUpVk = static_cast<UINT>(std::strtoul(v.c_str(), nullptr, 10));
        else if (k == "volUpIsMouse") cfg.volUpIsMouse = (v == "1");
        else if (k == "holdToOpen") cfg.holdToOpen = (v == "1");
        else if (k == "blockGameInput") cfg.blockGameInput = (v == "1");
        else if (k == "outerRadius") cfg.outerRadius = std::strtof(v.c_str(), nullptr);
        else if (k == "innerRadius") cfg.innerRadius = std::strtof(v.c_str(), nullptr);
        else if (k == "wheelStep") cfg.wheelStep = std::strtof(v.c_str(), nullptr);
        else if (k == "gameMouseSens") cfg.gameMouseSens = std::strtof(v.c_str(), nullptr);
        else if (k == "bgAlpha") cfg.bgAlpha = std::strtof(v.c_str(), nullptr);
        else if (k == "accentR") cfg.accentColor[0] = std::strtof(v.c_str(), nullptr);
        else if (k == "accentG") cfg.accentColor[1] = std::strtof(v.c_str(), nullptr);
        else if (k == "accentB") cfg.accentColor[2] = std::strtof(v.c_str(), nullptr);
        else if (k == "enableMasterAudio") cfg.enableMasterAudio = (v == "1");
        else if (k == "enableAppMixer") cfg.enableAppMixer = (v == "1");
        else if (k == "enableMic") cfg.enableMic = (v == "1");
        else if (k == "enableMedia") cfg.enableMedia = (v == "1");
        else if (k == "enableSystem") cfg.enableSystem = (v == "1");
        else if (k == "customAction")
        {
            auto p1 = v.find('|');
            auto p2 = (p1 != std::string::npos) ? v.find('|', p1 + 1) : std::string::npos;
            if (p1 != std::string::npos)
            {
                CustomAction act{};
                act.label = v.substr(0, p1);
                act.target = (p2 != std::string::npos) ? v.substr(p1 + 1, p2 - p1 - 1) : v.substr(p1 + 1);
                act.type = (p2 != std::string::npos) ? std::atoi(v.substr(p2 + 1).c_str()) : 0;
                if (!act.label.empty() && !act.target.empty())
                {
                    cfg.customActions.push_back(std::move(act));
                }
            }
        }
    }
    return cfg;
}

void SaveAppConfig(const AppConfig& cfg)
{
    std::ofstream out(GetConfigPath(), std::ios::trunc);
    if (!out.is_open())
    {
        return;
    }

    out << "hotkeyVk=" << cfg.hotkeyVk << "\n";
    out << "isMouseHotkey=" << (cfg.isMouseHotkey ? 1 : 0) << "\n";
    out << "volDownVk=" << cfg.volDownVk << "\n";
    out << "volDownIsMouse=" << (cfg.volDownIsMouse ? 1 : 0) << "\n";
    out << "volUpVk=" << cfg.volUpVk << "\n";
    out << "volUpIsMouse=" << (cfg.volUpIsMouse ? 1 : 0) << "\n";
    out << "holdToOpen=" << (cfg.holdToOpen ? 1 : 0) << "\n";
    out << "blockGameInput=" << (cfg.blockGameInput ? 1 : 0) << "\n";
    out << "outerRadius=" << cfg.outerRadius << "\n";
    out << "innerRadius=" << cfg.innerRadius << "\n";
    out << "wheelStep=" << cfg.wheelStep << "\n";
    out << "gameMouseSens=" << cfg.gameMouseSens << "\n";
    out << "bgAlpha=" << cfg.bgAlpha << "\n";
    out << "accentR=" << cfg.accentColor[0] << "\n";
    out << "accentG=" << cfg.accentColor[1] << "\n";
    out << "accentB=" << cfg.accentColor[2] << "\n";
    out << "enableMasterAudio=" << (cfg.enableMasterAudio ? 1 : 0) << "\n";
    out << "enableAppMixer=" << (cfg.enableAppMixer ? 1 : 0) << "\n";
    out << "enableMic=" << (cfg.enableMic ? 1 : 0) << "\n";
    out << "enableMedia=" << (cfg.enableMedia ? 1 : 0) << "\n";
    out << "enableSystem=" << (cfg.enableSystem ? 1 : 0) << "\n";
    for (const auto& act : cfg.customActions)
    {
        out << "customAction=" << act.label << "|" << act.target << "|" << act.type << "\n";
    }
}

std::string FormatHotkeyName(UINT vk, bool isMouse)
{
    if (isMouse)
    {
        if (vk == VK_XBUTTON1) return "Mouse 4";
        if (vk == VK_XBUTTON2) return "Mouse 5";
        if (vk == VK_MBUTTON) return "Middle Mouse";
        return "MouseBtn";
    }

    switch (vk)
    {
    case VK_CAPITAL: return "CapsLock";
    case VK_OEM_3: return "~ (Tilde)";
    case VK_LMENU: case VK_MENU: return "Alt";
    case VK_F8: return "F8";
    case VK_F9: return "F9";
    case VK_F10: return "F10";
    case VK_INSERT: return "Insert";
    default:
    {
        UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        wchar_t name[64]{};
        if (GetKeyNameTextW(static_cast<LONG>(sc << 16), name, 64) > 0)
        {
            char utf8[128]{};
            WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof(utf8), nullptr, nullptr);
            return utf8;
        }
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "VK 0x%X", vk);
        return buf;
    }
    }
}
