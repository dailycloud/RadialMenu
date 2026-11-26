#include "audio_module.h"
#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <functiondiscoverykeys_devpkey.h>

#pragma comment(lib, "ole32.lib")

static std::string QueryProcessShortName(DWORD pid)
{
    if (pid == 0)
    {
        return "System";
    }

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
    {
        return "App";
    }

    wchar_t buf[MAX_PATH]{};
    DWORD len = MAX_PATH;
    std::string out = "App";
    if (QueryFullProcessImageNameW(h, 0, buf, &len) && len > 0)
    {
        std::wstring ws(buf, len);
        auto slash = ws.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
        {
            ws = ws.substr(slash + 1);
        }
        auto dot = ws.rfind(L'.');
        if (dot != std::wstring::npos)
        {
            ws = ws.substr(0, dot);
        }
        if (!ws.empty())
        {
            ws[0] = static_cast<wchar_t>(std::towupper(ws[0]));
            char utf8[128]{};
            WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, utf8, sizeof(utf8), nullptr, nullptr);
            out = utf8;
        }
    }

    CloseHandle(h);
    return out;
}

template <typename Fn>
static void WithEndpointVolume(EDataFlow flow, ERole role, Fn&& fn)
{
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) || !enumerator)
    {
        return;
    }

    IMMDevice* device = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, role, &device)) && device)
    {
        IAudioEndpointVolume* epVol = nullptr;
        if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&epVol))) && epVol)
        {
            fn(epVol);
            epVol->Release();
        }
        device->Release();
    }
    enumerator->Release();
}

template <typename Fn>
static void WithAudioSession(DWORD targetPid, Fn&& fn)
{
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) || !enumerator)
    {
        return;
    }

    IMMDevice* device = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)) && device)
    {
        IAudioSessionManager2* mgr = nullptr;
        if (SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&mgr))) && mgr)
        {
            IAudioSessionEnumerator* enumSessions = nullptr;
            if (SUCCEEDED(mgr->GetSessionEnumerator(&enumSessions)) && enumSessions)
            {
                int count = 0;
                enumSessions->GetCount(&count);
                for (int i = 0; i < count; i++)
                {
                    IAudioSessionControl* ctrl = nullptr;
                    if (FAILED(enumSessions->GetSession(i, &ctrl)) || !ctrl) continue;
                    IAudioSessionControl2* ctrl2 = nullptr;
                    if (SUCCEEDED(ctrl->QueryInterface(IID_PPV_ARGS(&ctrl2))) && ctrl2)
                    {
                        DWORD pid = 0;
                        if (SUCCEEDED(ctrl2->GetProcessId(&pid)) && pid == targetPid)
                        {
                            ISimpleAudioVolume* simple = nullptr;
                            if (SUCCEEDED(ctrl2->QueryInterface(IID_PPV_ARGS(&simple))) && simple)
                            {
                                fn(simple);
                                simple->Release();
                            }
                        }
                        ctrl2->Release();
                    }
                    ctrl->Release();
                }
                enumSessions->Release();
            }
            mgr->Release();
        }
        device->Release();
    }
    enumerator->Release();
}

AudioModule::AudioModule(const AppConfig& cfg) : config_(cfg)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    comOwned_ = SUCCEEDED(hr);
    Refresh();
}

AudioModule::~AudioModule()
{
    if (comOwned_)
    {
        CoUninitialize();
    }
}

void AudioModule::RefreshEndpoints()
{
    WithEndpointVolume(eRender, eMultimedia, [&](IAudioEndpointVolume* ep)
    {
        float lvl = 0.5f;
        BOOL mute = FALSE;
        if (SUCCEEDED(ep->GetMasterVolumeLevelScalar(&lvl))) masterVolume_ = lvl;
        if (SUCCEEDED(ep->GetMute(&mute))) masterMuted_ = (mute != FALSE);
    });

    WithEndpointVolume(eCapture, eCommunications, [&](IAudioEndpointVolume* ep)
    {
        float lvl = 1.0f;
        BOOL mute = FALSE;
        if (SUCCEEDED(ep->GetMasterVolumeLevelScalar(&lvl))) micVolume_ = lvl;
        if (SUCCEEDED(ep->GetMute(&mute))) micMuted_ = (mute != FALSE);
    });
}

void AudioModule::RefreshSessions()
{
    ULONGLONG now = GetTickCount64();
    if (now - lastSessionScanTick_ < 350 && !sessions_.empty())
    {
        return;
    }
    lastSessionScanTick_ = now;

    HWND fg = GetForegroundWindow();
    DWORD fgPid = 0;
    if (fg)
    {
        GetWindowThreadProcessId(fg, &fgPid);
    }

    std::vector<AudioSessionInfo> found;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) || !enumerator)
    {
        return;
    }

    IMMDevice* device = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)) && device)
    {
        IAudioSessionManager2* mgr = nullptr;
        if (SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&mgr))) && mgr)
        {
            IAudioSessionEnumerator* enumSessions = nullptr;
            if (SUCCEEDED(mgr->GetSessionEnumerator(&enumSessions)) && enumSessions)
            {
                int count = 0;
                enumSessions->GetCount(&count);
                for (int i = 0; i < count; i++)
                {
                    IAudioSessionControl* ctrl = nullptr;
                    if (FAILED(enumSessions->GetSession(i, &ctrl)) || !ctrl) continue;

                    AudioSessionState st{};
                    ctrl->GetState(&st);
                    IAudioSessionControl2* ctrl2 = nullptr;
                    if (SUCCEEDED(ctrl->QueryInterface(IID_PPV_ARGS(&ctrl2))) && ctrl2)
                    {
                        DWORD pid = 0;
                        bool isSystem = (ctrl2->IsSystemSoundsSession() == S_OK);
                        if (!isSystem && SUCCEEDED(ctrl2->GetProcessId(&pid)) && pid != 0 && st != AudioSessionStateExpired)
                        {
                            bool duplicate = false;
                            for (const auto& existing : found)
                            {
                                if (existing.pid == pid)
                                {
                                    duplicate = true;
                                    break;
                                }
                            }

                            if (!duplicate)
                            {
                                ISimpleAudioVolume* simple = nullptr;
                                if (SUCCEEDED(ctrl2->QueryInterface(IID_PPV_ARGS(&simple))) && simple)
                                {
                                    float v = 1.0f;
                                    BOOL m = FALSE;
                                    simple->GetMasterVolume(&v);
                                    simple->GetMute(&m);
                                    simple->Release();

                                    AudioSessionInfo info{};
                                    info.pid = pid;
                                    info.exeName = QueryProcessShortName(pid);
                                    info.volume = v;
                                    info.muted = (m != FALSE);
                                    info.isForeground = (pid == fgPid);
                                    info.isActive = (st == AudioSessionStateActive);
                                    found.push_back(info);
                                }
                            }
                        }
                        ctrl2->Release();
                    }
                    ctrl->Release();
                }
                enumSessions->Release();
            }
            mgr->Release();
        }
        device->Release();
    }
    enumerator->Release();

    std::sort(found.begin(), found.end(), [](const AudioSessionInfo& a, const AudioSessionInfo& b)
    {
        if (a.isForeground != b.isForeground) return a.isForeground > b.isForeground;
        if (a.isActive != b.isActive) return a.isActive > b.isActive;
        return a.exeName < b.exeName;
    });

    sessions_ = std::move(found);
}

void AudioModule::Refresh()
{
    RefreshEndpoints();
    RefreshSessions();
}

void AudioModule::AdjustMasterVolume(float delta)
{
    WithEndpointVolume(eRender, eMultimedia, [&](IAudioEndpointVolume* ep)
    {
        float cur = masterVolume_;
        ep->GetMasterVolumeLevelScalar(&cur);
        float next = (std::max)(0.0f, (std::min)(1.0f, cur + delta));
        ep->SetMasterVolumeLevelScalar(next, nullptr);
        if (next > 0.001f && masterMuted_)
        {
            ep->SetMute(FALSE, nullptr);
            masterMuted_ = false;
        }
        masterVolume_ = next;
    });
}

void AudioModule::ToggleMasterMute()
{
    WithEndpointVolume(eRender, eMultimedia, [&](IAudioEndpointVolume* ep)
    {
        BOOL cur = FALSE;
        ep->GetMute(&cur);
        BOOL next = !cur;
        ep->SetMute(next, nullptr);
        masterMuted_ = (next != FALSE);
    });
}

void AudioModule::ToggleMicMute()
{
    WithEndpointVolume(eCapture, eCommunications, [&](IAudioEndpointVolume* ep)
    {
        BOOL cur = FALSE;
        ep->GetMute(&cur);
        BOOL next = !cur;
        ep->SetMute(next, nullptr);
        micMuted_ = (next != FALSE);
    });
}

void AudioModule::AdjustSessionVolume(DWORD pid, float delta)
{
    WithAudioSession(pid, [&](ISimpleAudioVolume* simple)
    {
        float cur = 1.0f;
        simple->GetMasterVolume(&cur);
        float next = (std::max)(0.0f, (std::min)(1.0f, cur + delta));
        simple->SetMasterVolume(next, nullptr);
        for (auto& s : sessions_)
        {
            if (s.pid == pid)
            {
                s.volume = next;
            }
        }
    });
}

void AudioModule::ToggleSessionMute(DWORD pid)
{
    WithAudioSession(pid, [&](ISimpleAudioVolume* simple)
    {
        BOOL cur = FALSE;
        simple->GetMute(&cur);
        BOOL next = !cur;
        simple->SetMute(next, nullptr);
        for (auto& s : sessions_)
        {
            if (s.pid == pid)
            {
                s.muted = (next != FALSE);
            }
        }
    });
}

static std::string ToLowerAscii(std::string s)
{
    for (char& ch : s)
    {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return s;
}

static bool MatchesPatternList(const std::string& exeName, const std::string& pattern)
{
    std::string lowerExe = ToLowerAscii(exeName);
    std::string lowerPat = ToLowerAscii(pattern);
    size_t start = 0;
    while (start < lowerPat.size())
    {
        size_t comma = lowerPat.find(',', start);
        std::string token = (comma == std::string::npos)
            ? lowerPat.substr(start)
            : lowerPat.substr(start, comma - start);
        while (!token.empty() && token.front() == ' ') token.erase(token.begin());
        while (!token.empty() && token.back() == ' ') token.pop_back();
        if (!token.empty() && lowerExe.find(token) != std::string::npos)
        {
            return true;
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return false;
}

bool AudioModule::FindSessionByPattern(const std::string& pattern, AudioSessionInfo* out) const
{
    for (const auto& ses : sessions_)
    {
        if (MatchesPatternList(ses.exeName, pattern))
        {
            if (out) *out = ses;
            return true;
        }
    }
    return false;
}

void AudioModule::AdjustSessionsByPattern(const std::string& pattern, float delta)
{
    for (const auto& ses : sessions_)
    {
        if (MatchesPatternList(ses.exeName, pattern))
        {
            AdjustSessionVolume(ses.pid, delta);
        }
    }
}

void AudioModule::ToggleSessionsByPattern(const std::string& pattern)
{
    for (const auto& ses : sessions_)
    {
        if (MatchesPatternList(ses.exeName, pattern))
        {
            ToggleSessionMute(ses.pid);
        }
    }
}

void AudioModule::AppendSectors(std::vector<RadialSector>& out)
{
    if (config_.enableMasterAudio)
    {
        RadialSector s{};
        s.id = "audio_master";
        s.label = "Sound";
        char buf[16]{};
        if (masterMuted_) std::snprintf(buf, sizeof(buf), "MUTE");
        else std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(masterVolume_ * 100.0f + 0.5f));
        s.valueText = buf;
        s.icon = IconKind::Volume;
        s.value = masterVolume_;
        s.muted = masterMuted_;
        s.onActivate = [this]() { ToggleMasterMute(); };
        s.onWheel = [this](int delta)
        {
            AdjustMasterVolume((delta > 0 ? 1.0f : -1.0f) * config_.wheelStep);
        };
        out.push_back(std::move(s));
    }

    if (config_.enableAppMixer)
    {
        RadialSector s{};
        s.id = "audio_mixer";
        s.label = "Apps";
        char buf[16]{};
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(sessions_.size()));
        s.valueText = buf;
        s.icon = IconKind::Mixer;
        s.value = -1.0f;
        s.onWheel = [this](int delta)
        {
            if (!sessions_.empty())
            {
                AdjustSessionVolume(sessions_[0].pid, (delta > 0 ? 1.0f : -1.0f) * config_.wheelStep);
            }
        };
        s.buildSubItems = [this]() -> std::vector<RadialSubItem>
        {
            std::vector<RadialSubItem> items;
            size_t limit = (std::min)(sessions_.size(), static_cast<size_t>(5));
            for (size_t i = 0; i < limit; i++)
            {
                const auto& ses = sessions_[i];
                RadialSubItem item{};
                item.id = "app_" + std::to_string(ses.pid);
                item.label = ses.exeName;
                char pbuf[16]{};
                if (ses.muted) std::snprintf(pbuf, sizeof(pbuf), "MUTE");
                else std::snprintf(pbuf, sizeof(pbuf), "%d%%", static_cast<int>(ses.volume * 100.0f + 0.5f));
                item.subLabel = pbuf;
                item.icon = IconKind::App;
                item.value = ses.volume;
                item.muted = ses.muted;
                DWORD pid = ses.pid;
                item.onActivate = [this, pid]() { ToggleSessionMute(pid); };
                item.onWheel = [this, pid](int delta)
                {
                    AdjustSessionVolume(pid, (delta > 0 ? 1.0f : -1.0f) * config_.wheelStep);
                };
                items.push_back(std::move(item));
            }
            return items;
        };
        out.push_back(std::move(s));
    }

    if (config_.enableMic)
    {
        RadialSector s{};
        s.id = "audio_mic";
        s.label = "Mic";
        s.valueText = micMuted_ ? "OFF" : "ON";
        s.icon = IconKind::Mic;
        s.value = micMuted_ ? 0.0f : 1.0f;
        s.muted = micMuted_;
        s.onActivate = [this]() { ToggleMicMute(); };
        out.push_back(std::move(s));
    }

    for (size_t i = 0; i < config_.customActions.size(); i++)
    {
        const auto& act = config_.customActions[i];
        AudioSessionInfo match{};
        bool found = FindSessionByPattern(act.target, &match);

        RadialSector s{};
        s.id = "custom_" + std::to_string(i);
        s.label = act.label;
        if (!found)
        {
            s.valueText = "N/A";
            s.value = -1.0f;
        }
        else if (match.muted)
        {
            s.valueText = "MUTE";
            s.value = 0.0f;
            s.muted = true;
        }
        else
        {
            char buf[16]{};
            std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(match.volume * 100.0f + 0.5f));
            s.valueText = buf;
            s.value = match.volume;
        }
        s.icon = IconKind::App;
        std::string targetPat = act.target;
        s.onActivate = [this, targetPat]() { ToggleSessionsByPattern(targetPat); };
        s.onWheel = [this, targetPat](int delta)
        {
            AdjustSessionsByPattern(targetPat, (delta > 0 ? 1.0f : -1.0f) * config_.wheelStep);
        };
        out.push_back(std::move(s));
    }
}
