#pragma once

#include "../core/config.h"
#include "../core/module.h"

class MediaModule : public IRadialModule
{
public:
    explicit MediaModule(const AppConfig& cfg);
    const char* Name() const override { return "Media"; }
    void Refresh() override {}
    void AppendSectors(std::vector<RadialSector>& out) override;

private:
    static void SendMediaKey(WORD vk);
    const AppConfig& config_;
};
