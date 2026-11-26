#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include "updater.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

static std::string ExtractJsonString(const std::string& json, const char* key, size_t startPos = 0)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t pos = json.find(pat, startPos);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    size_t end = pos + 1;
    while (end < json.size())
    {
        if (json[end] == '"' && json[end - 1] != '\\') break;
        end++;
    }
    if (end >= json.size()) return "";
    return json.substr(pos + 1, end - pos - 1);
}

static std::string FindExeAssetUrl(const std::string& json)
{
    size_t pos = 0;
    while (true)
    {
        size_t found = json.find("\"browser_download_url\"", pos);
        if (found == std::string::npos) break;
        std::string url = ExtractJsonString(json, "browser_download_url", found);
        if (url.size() >= 4 && url.substr(url.size() - 4) == ".exe")
        {
            return url;
        }
        pos = found + 20;
    }
    return "";
}

static bool ParseSemVer(const std::string& s, int out[3])
{
    out[0] = out[1] = out[2] = 0;
    size_t i = 0;
    if (i < s.size() && (s[i] == 'v' || s[i] == 'V')) i++;
    int part = 0;
    while (i < s.size() && part < 3)
    {
        if (s[i] >= '0' && s[i] <= '9')
        {
            out[part] = out[part] * 10 + (s[i] - '0');
        }
        else if (s[i] == '.')
        {
            part++;
        }
        else
        {
            break;
        }
        i++;
    }
    return out[0] > 0 || out[1] > 0 || out[2] > 0;
}

static bool IsNewerVersion(const std::string& candidate, const std::string& current)
{
    int a[3]{}, b[3]{};
    if (!ParseSemVer(candidate, a) || !ParseSemVer(current, b)) return false;
    for (int i = 0; i < 3; i++)
    {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

static bool HttpGetString(const wchar_t* host, const wchar_t* path, std::string& outBody)
{
    HINTERNET hSession = WinHttpOpen(L"RadialMenuWHook/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    WinHttpAddRequestHeaders(hRequest, L"User-Agent: RadialMenuWHook\r\nAccept: application/vnd.github+json\r\n", -1L, WINHTTP_ADDREQ_FLAG_ADD);

    bool ok = false;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr))
    {
        DWORD status = 0;
        DWORD sz = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status >= 200 && status < 300)
        {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0)
            {
                std::vector<char> buf(avail);
                DWORD read = 0;
                if (WinHttpReadData(hRequest, buf.data(), avail, &read) && read > 0)
                {
                    outBody.append(buf.data(), read);
                }
            }
            ok = !outBody.empty();
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

static bool HttpDownloadFile(const std::string& urlUtf8, const std::wstring& destPath)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, urlUtf8.c_str(), -1, nullptr, 0);
    if (wlen <= 1) return false;
    std::wstring wurl(wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, urlUtf8.c_str(), -1, &wurl[0], wlen);

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{};
    wchar_t path[2048]{};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
    {
        return false;
    }

    HINTERNET hSession = WinHttpOpen(L"RadialMenuWHook/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));
    WinHttpAddRequestHeaders(hRequest, L"User-Agent: RadialMenuWHook\r\n", -1L, WINHTTP_ADDREQ_FLAG_ADD);

    bool ok = false;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr))
    {
        DWORD status = 0;
        DWORD sz = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status >= 200 && status < 300)
        {
            std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
            if (out.is_open())
            {
                DWORD avail = 0;
                size_t total = 0;
                while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0)
                {
                    std::vector<char> buf(avail);
                    DWORD read = 0;
                    if (WinHttpReadData(hRequest, buf.data(), avail, &read) && read > 0)
                    {
                        out.write(buf.data(), read);
                        total += read;
                    }
                }
                ok = total > 32768;
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

AppUpdater::~AppUpdater()
{
    if (lockInit_)
    {
        DeleteCriticalSection(&lock_);
    }
}

void AppUpdater::EnsureLock()
{
    if (!lockInit_)
    {
        InitializeCriticalSection(&lock_);
        lockInit_ = true;
    }
}

void AppUpdater::CheckAsync()
{
    EnsureLock();
    if (busy_.exchange(true)) return;
    EnterCriticalSection(&lock_);
    info_.checking = true;
    info_.statusText = "Checking...";
    LeaveCriticalSection(&lock_);
    std::thread(&AppUpdater::CheckWorker, this).detach();
}

void AppUpdater::CheckWorker()
{
    wchar_t path[256]{};
    std::swprintf(path, 256, L"/repos/%S/%S/releases/latest", kGitHubOwner, kGitHubRepo);

    std::string body;
    bool got = HttpGetString(L"api.github.com", path, body);

    EnsureLock();
    EnterCriticalSection(&lock_);
    info_.checking = false;
    info_.checked = true;
    if (!got)
    {
        info_.available = false;
        info_.statusText = "Up to date / no release";
    }
    else
    {
        std::string tag = ExtractJsonString(body, "tag_name");
        std::string htmlUrl = ExtractJsonString(body, "html_url");
        std::string exeUrl = FindExeAssetUrl(body);
        info_.latestVersion = tag;
        info_.releaseUrl = htmlUrl;
        info_.assetUrl = exeUrl;
        if (IsNewerVersion(tag, kAppVersion))
        {
            info_.available = true;
            dismissed_ = false;
            info_.statusText = "Update available: " + tag;
        }
        else
        {
            info_.available = false;
            info_.statusText = "Up to date (v" + std::string(kAppVersion) + ")";
        }
    }
    LeaveCriticalSection(&lock_);
    busy_.store(false);
}

void AppUpdater::InstallAsync()
{
    EnsureLock();
    EnterCriticalSection(&lock_);
    std::string url = info_.assetUrl;
    std::string rel = info_.releaseUrl;
    std::string ver = info_.latestVersion;
    if (url.empty())
    {
        LeaveCriticalSection(&lock_);
        if (!rel.empty())
        {
            ShellExecuteA(nullptr, "open", rel.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return;
    }
    if (busy_.exchange(true))
    {
        LeaveCriticalSection(&lock_);
        return;
    }
    info_.installing = true;
    info_.statusText = "Downloading " + ver + "...";
    LeaveCriticalSection(&lock_);

    std::thread(&AppUpdater::InstallWorker, this, url, ver).detach();
}

void AppUpdater::InstallWorker(std::string url, std::string)
{
    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring newExe = std::wstring(tempDir) + L"RadialMenuWHook_new.exe";
    std::wstring batPath = std::wstring(tempDir) + L"RadialMenuWHook_update.bat";

    if (!HttpDownloadFile(url, newExe))
    {
        EnsureLock();
        EnterCriticalSection(&lock_);
        info_.installing = false;
        info_.statusText = "Download failed";
        LeaveCriticalSection(&lock_);
        busy_.store(false);
        return;
    }

    wchar_t curExe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, curExe, MAX_PATH);

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, batPath.c_str(), L"w") == 0 && fp)
    {
        std::fwprintf(fp, L"@echo off\n");
        std::fwprintf(fp, L"timeout /t 1 /nobreak >nul\n");
        std::fwprintf(fp, L":loop\n");
        std::fwprintf(fp, L"move /y \"%ls\" \"%ls\" >nul 2>&1\n", newExe.c_str(), curExe);
        std::fwprintf(fp, L"if errorlevel 1 (\n  timeout /t 1 /nobreak >nul\n  goto loop\n)\n");
        std::fwprintf(fp, L"start \"\" \"%ls\"\n", curExe);
        std::fwprintf(fp, L"del \"%%~f0\"\n");
        std::fclose(fp);

        ShellExecuteW(nullptr, L"open", batPath.c_str(), nullptr, nullptr, SW_HIDE);
        PostQuitMessage(0);
    }
    busy_.store(false);
}

void AppUpdater::DismissNotification()
{
    EnsureLock();
    EnterCriticalSection(&lock_);
    dismissed_ = true;
    LeaveCriticalSection(&lock_);
}

bool AppUpdater::ShouldShowNotification() const
{
    if (!lockInit_) return false;
    EnterCriticalSection(&lock_);
    bool show = info_.available && !dismissed_;
    LeaveCriticalSection(&lock_);
    return show;
}

UpdateInfo AppUpdater::GetSnapshot() const
{
    if (!lockInit_) return {};
    EnterCriticalSection(&lock_);
    UpdateInfo copy = info_;
    LeaveCriticalSection(&lock_);
    return copy;
}
