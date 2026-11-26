#pragma once

#include "../core/config.h"
#include "../core/module.h"
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <vector>
#include <string>

struct AudioSessionInfo
{
    DWORD pid = 0;
    std::string exeName;
    float volume = 1.0f;
    bool muted = false;
    bool isForeground = false;
    bool isActive = false;
};

class AudioModule : public IRadialModule
{
public:
    explicit AudioModule(const AppConfig& cfg);
    ~AudioModule() override;

    const char* Name() const override { return "Audio"; }
    void Refresh() override;
    void AppendSectors(std::vector<RadialSector>& out) override;

    float GetMasterVolume() const { return masterVolume_; }
    bool IsMasterMuted() const { return masterMuted_; }
    float GetMicVolume() const { return micVolume_; }
    bool IsMicMuted() const { return micMuted_; }

    void AdjustMasterVolume(float delta);
    void ToggleMasterMute();
    void ToggleMicMute();
    void AdjustSessionVolume(DWORD pid, float delta);
    void ToggleSessionMute(DWORD pid);
    void AdjustSessionsByPattern(const std::string& pattern, float delta);
    void ToggleSessionsByPattern(const std::string& pattern);

private:
    bool FindSessionByPattern(const std::string& pattern, AudioSessionInfo* out) const;
    void RefreshEndpoints();
    void RefreshSessions();

    const AppConfig& config_;
    bool comOwned_ = false;
    float masterVolume_ = 0.5f;
    bool masterMuted_ = false;
    float micVolume_ = 1.0f;
    bool micMuted_ = false;
    std::vector<AudioSessionInfo> sessions_;
    ULONGLONG lastSessionScanTick_ = 0;
};
