#pragma once

#include "../core/config.h"
#include "../core/module.h"
#include <functional>

class SystemModule : public IRadialModule
{
public:
    SystemModule(const AppConfig& cfg, std::function<void()> onOpenSettings);
    const char* Name() const override { return "System"; }
    void Refresh() override;
    void AppendSectors(std::vector<RadialSector>& out) override;
    void SetCursorCaptured(bool captured) { cursorCaptured_ = captured; }

    float GetCpuUsage() const { return cpuUsage_; }
    float GetRamUsage() const { return ramUsage_; }
    std::string GetTimeString() const { return timeStr_; }

private:
    const AppConfig& config_;
    std::function<void()> onOpenSettings_;
    bool cursorCaptured_ = false;
    ULONGLONG lastIdle_ = 0;
    ULONGLONG lastKernel_ = 0;
    ULONGLONG lastUser_ = 0;
    ULONGLONG lastSampleTick_ = 0;
    float cpuUsage_ = 0.0f;
    float ramUsage_ = 0.0f;
    std::string timeStr_ = "00:00";
};
