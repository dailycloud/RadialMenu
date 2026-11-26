#pragma once

#include <string>
#include <atomic>

inline constexpr const char* kAppVersion = "1.0.0";
inline constexpr const char* kGitHubOwner = "dailycloud";
inline constexpr const char* kGitHubRepo = "RadialMenu";

struct UpdateInfo
{
    bool checked = false;
    bool checking = false;
    bool available = false;
    bool installing = false;
    std::string latestVersion;
    std::string assetUrl;
    std::string releaseUrl;
    std::string statusText;
};

class AppUpdater
{
public:
    ~AppUpdater();

    void CheckAsync();
    void InstallAsync();
    void DismissNotification();
    bool ShouldShowNotification() const;
    UpdateInfo GetSnapshot() const;

private:
    void CheckWorker();
    void InstallWorker(std::string url, std::string ver);

    mutable CRITICAL_SECTION lock_{};
    bool lockInit_ = false;
    UpdateInfo info_{};
    bool dismissed_ = false;
    std::atomic<bool> busy_{ false };

    void EnsureLock();
};
