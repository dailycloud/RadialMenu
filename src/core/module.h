#pragma once

#include <functional>
#include <string>
#include <vector>

enum class IconKind
{
    Volume,
    Mixer,
    Mic,
    Media,
    Monitor,
    PlayPause,
    Next,
    Prev,
    App
};

struct RadialSubItem
{
    std::string id;
    std::string label;
    std::string subLabel;
    IconKind icon = IconKind::App;
    float value = -1.0f;
    bool muted = false;
    std::function<void()> onActivate;
    std::function<void(int delta)> onWheel;
};

struct RadialSector
{
    std::string id;
    std::string label;
    std::string valueText;
    IconKind icon = IconKind::Volume;
    float value = -1.0f;
    bool muted = false;
    std::function<void()> onActivate;
    std::function<void(int delta)> onWheel;
    std::function<std::vector<RadialSubItem>()> buildSubItems;
};

class IRadialModule
{
public:
    virtual ~IRadialModule() = default;
    virtual const char* Name() const = 0;
    virtual void Refresh() = 0;
    virtual void AppendSectors(std::vector<RadialSector>& out) = 0;
};
