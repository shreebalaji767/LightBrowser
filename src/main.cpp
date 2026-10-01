#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commctrl.h>

#include <wrl.h>
#include <WebView2.h>

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <cwctype>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace fs = std::filesystem;

static constexpr wchar_t APP_NAME[] = L"LightBrowser";

static HWND g_mainWindow = nullptr;
static HWND g_toolbar = nullptr;
static HWND g_addressBar = nullptr;
static HWND g_backButton = nullptr;
static HWND g_forwardButton = nullptr;
static HWND g_reloadButton = nullptr;
static HWND g_homeButton = nullptr;
static HWND g_newTabButton = nullptr;
static HWND g_bookmarkButton = nullptr;
static HWND g_historyButton = nullptr;
static HWND g_downloadButton = nullptr;
static HWND g_bookmarksPanel = nullptr;
static HWND g_historyPanel = nullptr;
static HWND g_tabsBar = nullptr;

static HINSTANCE g_instance = nullptr;

static ComPtr<ICoreWebView2Environment> g_environment;

static std::wstring g_homePage = L"https://www.google.com/";

static std::wstring AppDataPath()
{
    wchar_t buffer[MAX_PATH]{};

    DWORD len = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        buffer,
        MAX_PATH
    );

    if (len == 0 || len >= MAX_PATH)
        return L".";

    std::wstring path(buffer);
    path += L"\\LightBrowser";

    CreateDirectoryW(path.c_str(), nullptr);

    return path;
}

static std::wstring HistoryFile()
{
    return AppDataPath() + L"\\history.txt";
}

static std::wstring BookmarkFile()
{
    return AppDataPath() + L"\\bookmarks.txt";
}

static std::wstring SessionFile()
{
    return AppDataPath() + L"\\session.txt";
}

static void SaveSession()
{
    std::wofstream out(SessionFile(), std::ios::trunc);
    if (!out)
        return;

    for (const auto& tab : g_tabs)
    {
        if (tab && !tab->url.empty())
            out << tab->url << L"\n";
    }
}

static std::vector<std::wstring> LoadSession()
{
    std::vector<std::wstring> urls;
    std::wifstream in(SessionFile());
    if (!in)
        return urls;

    std::wstring line;
    while (std::getline(in, line))
    {
        if (!line.empty())
            urls.push_back(line);

        if (urls.size() >= 12)
            break;
    }

    return urls;
}

static std::wstring DownloadsPath()
{
    wchar_t buffer[MAX_PATH]{};

    DWORD len = GetEnvironmentVariableW(
        L"USERPROFILE",
        buffer,
        MAX_PATH
    );

    if (!len || len >= MAX_PATH)
        return AppDataPath();

    std::wstring path(buffer);
    path += L"\\Downloads";

    CreateDirectoryW(path.c_str(), nullptr);

    return path;
}

static std::wstring GetFileNameFromUrl(const std::wstring& url)
{
    std::wstring clean = url;

    size_t query = clean.find(L'?');
    if (query != std::wstring::npos)
        clean.erase(query);

    size_t hash = clean.find(L'#');
    if (hash != std::wstring::npos)
        clean.erase(hash);

    size_t slash = clean.find_last_of(L'/');

    if (slash != std::wstring::npos &&
        slash + 1 < clean.length())
    {
        return clean.substr(slash + 1);
    }

    return L"download";
}

static std::wstring SanitizeFileName(std::wstring name)
{
    const std::wstring invalid = L"<>:\"/\\|?*";

    for (wchar_t& c : name)
    {
        if (invalid.find(c) != std::wstring::npos)
            c = L'_';
    }

    return name;
}

static std::wstring UrlEncode(const std::wstring& input)
{
    int bytesNeeded = WideCharToMultiByte(CP_UTF8, 0, input.c_str(),
        static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (bytesNeeded <= 0)
        return L"";

    std::string utf8(static_cast<size_t>(bytesNeeded), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.c_str(),
        static_cast<int>(input.size()), utf8.data(), bytesNeeded, nullptr, nullptr);

    std::wstring output;
    const wchar_t hex[] = L"0123456789ABCDEF";
    for (unsigned char c : utf8)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            output += static_cast<wchar_t>(c);
        else if (c == ' ')
            output += L'+';
        else
        {
            output += L'%';
            output += hex[(c >> 4) & 0x0F];
            output += hex[c & 0x0F];
        }
    }
    return output;
}

static bool LooksLikeUrl(const std::wstring& text)
{
    if (text.empty())
        return false;

    std::wstring lower = text;

    std::transform(
        lower.begin(),
        lower.end(),
        lower.begin(),
        towlower
    );

    if (
        lower.rfind(L"http://", 0) == 0 ||
        lower.rfind(L"https://", 0) == 0 ||
        lower.rfind(L"file://", 0) == 0 ||
        lower.rfind(L"about:", 0) == 0
    )
    {
        return true;
    }

    if (lower.find(L".") != std::wstring::npos &&
        lower.find(L" ") == std::wstring::npos)
    {
        return true;
    }

    if (lower.find(L"localhost") == 0)
        return true;

    return false;
}

static std::wstring MakeNavigationUrl(const std::wstring& input)
{
    if (LooksLikeUrl(input))
    {
        if (
            input.rfind(L"http://", 0) == 0 ||
            input.rfind(L"https://", 0) == 0 ||
            input.rfind(L"file://", 0) == 0 ||
            input.rfind(L"about:", 0) == 0
        )
        {
            return input;
        }

        return L"https://" + input;
    }

    return
        L"https://www.google.com/search?q=" +
        UrlEncode(input);
}

struct Tab
{
    HWND button = nullptr;

    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;

    std::wstring title = L"New Tab";
    std::wstring url = L"";
};

static std::vector<std::unique_ptr<Tab>> g_tabs;
static int g_activeTab = -1;

static Tab* ActiveTab()
{
    if (
        g_activeTab < 0 ||
        g_activeTab >= static_cast<int>(g_tabs.size())
    )
    {
        return nullptr;
    }

    return g_tabs[g_activeTab].get();
}

static void UpdateLayout();

static void SaveHistory(const std::wstring& url)
{
    if (url.empty())
        return;

    std::wofstream file(
        HistoryFile(),
        std::ios::app
    );

    if (file)
    {
        file << url << L"\n";
    }
}

static std::vector<std::wstring> LoadLines(
    const std::wstring& path
)
{
    std::vector<std::wstring> result;

    std::wifstream file(path);

    if (!file)
        return result;

    std::wstring line;

    while (std::getline(file, line))
    {
        if (!line.empty())
            result.push_back(line);
    }

    return result;
}

static bool IsBookmarked(const std::wstring& url)
{
    auto items = LoadLines(BookmarkFile());

    return std::find(
        items.begin(),
        items.end(),
        url
    ) != items.end();
}

static void ToggleBookmark()
{
    Tab* tab = ActiveTab();

    if (!tab || tab->url.empty())
        return;

    auto items = LoadLines(BookmarkFile());

    auto it = std::find(
        items.begin(),
        items.end(),
        tab->url
    );

    if (it != items.end())
    {
        items.erase(it);
    }
    else
    {
        items.push_back(tab->url);
    }

    std::wofstream file(
        BookmarkFile(),
        std::ios::trunc
    );

    for (const auto& item : items)
    {
        file << item << L"\n";
    }
}

static void UpdateBookmarkButton()
{
    Tab* tab = ActiveTab();

    if (!g_bookmarkButton)
        return;

    if (tab && IsBookmarked(tab->url))
    {
        SetWindowTextW(
            g_bookmarkButton,
            L"★"
        );
    }
    else
    {
        SetWindowTextW(
            g_bookmarkButton,
            L"☆"
        );
    }
}

static void Navigate(const std::wstring& text)
{
    Tab* tab = ActiveTab();

    if (!tab || !tab->webview)
        return;

    std::wstring url = MakeNavigationUrl(text);

    tab->webview->Navigate(url.c_str());
}

static void NavigateHome()
{
    Navigate(g_homePage);
}

static void GoBack()
{
    Tab* tab = ActiveTab();

    if (tab && tab->webview)
        tab->webview->GoBack();
}

static void GoForward()
{
    Tab* tab = ActiveTab();

    if (tab && tab->webview)
        tab->webview->GoForward();
}

static void Reload()
{
    Tab* tab = ActiveTab();

    if (tab && tab->webview)
        tab->webview->Reload();
}

static void Stop()
{
    Tab* tab = ActiveTab();
    if (tab && tab->webview)
        tab->webview->Stop();
}

static void SetZoom(double factor)
{
    Tab* tab = ActiveTab();
    if (!tab || !tab->controller)
        return;
    factor = std::max(0.25, std::min(5.0, factor));
    tab->controller->put_ZoomFactor(factor);
}

static void ZoomBy(double delta)
{
    Tab* tab = ActiveTab();
    if (!tab || !tab->controller)
        return;
    double zoom = 1.0;
    if (SUCCEEDED(tab->controller->get_ZoomFactor(&zoom)))
        SetZoom(zoom + delta);
}

static void FindOnPage()
{
    Tab* tab = ActiveTab();
    if (!tab || !tab->webview)
        return;

    tab->webview->ExecuteScript(
        L"window.find(window.prompt('Find text:'), false, false, true, false, false, false);",
        nullptr,
        nullptr);
}

static void OpenDevTools()
{
    Tab* tab = ActiveTab();
    if (tab && tab->webview)
        tab->webview->OpenDevToolsWindow();
}

static void PrintActiveTabToPdf()
{
    Tab* tab = ActiveTab();
    if (!tab || !tab->webview)
        return;

    ComPtr<ICoreWebView2_7> webview7;
    if (FAILED(tab->webview.As(&webview7)) || !webview7)
    {
        MessageBoxW(
            g_mainWindow,
            L"PDF printing requires a newer WebView2 SDK/runtime.",
            APP_NAME,
            MB_ICONINFORMATION);
        return;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);

    std::wstring name =
        L"LightBrowser_" +
        std::to_wstring(now.wYear) + L"-" +
        std::to_wstring(now.wMonth) + L"-" +
        std::to_wstring(now.wDay) + L"_" +
        std::to_wstring(now.wHour) + L"-" +
        std::to_wstring(now.wMinute) + L"-" +
        std::to_wstring(now.wSecond) + L".pdf";

    fs::path output = fs::path(DownloadsPath()) / name;

    webview7->PrintToPdf(
        output.wstring().c_str(),
        nullptr,
        Callback<ICoreWebView2PrintToPdfCompletedHandler>(
            [output](HRESULT errorCode, BOOL isSuccessful)
            {
                if (SUCCEEDED(errorCode) && isSuccessful)
                {
                    ShellExecuteW(
                        nullptr,
                        L"open",
                        output.wstring().c_str(),
                        nullptr,
                        nullptr,
                        SW_SHOWNORMAL);
                }
                else
                {
                    MessageBoxW(
                        g_mainWindow,
                        L"Could not create the PDF.",
                        APP_NAME,
                        MB_ICONERROR);
                }

                return S_OK;
            }).Get());
}

static void OpenDownloadsFolder()
{
    std::wstring path = DownloadsPath();
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}


