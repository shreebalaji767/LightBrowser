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
    std::wstring output;

    const wchar_t hex[] = L"0123456789ABCDEF";

    for (unsigned char c : std::string(
        input.begin(),
        input.end()
    ))
    {
        if (
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' ||
            c == '_' ||
            c == '.' ||
            c == '~'
        )
        {
            output += static_cast<wchar_t>(c);
        }
        else if (c == L' ')
        {
            output += L'+';
        }
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

static void SetAddressBarText(
    const std::wstring& text
)
{
    if (!g_addressBar)
        return;

    SetWindowTextW(
        g_addressBar,
        text.c_str()
    );
}

static void UpdateNavigationState()
{
    Tab* tab = ActiveTab();

    if (!tab || !tab->webview)
        return;

    BOOL canBack = FALSE;
    BOOL canForward = FALSE;

    tab->webview->get_CanGoBack(&canBack);
    tab->webview->get_CanGoForward(&canForward);

    EnableWindow(
        g_backButton,
        canBack
    );

    EnableWindow(
        g_forwardButton,
        canForward
    );

    SetAddressBarText(tab->url);

    UpdateBookmarkButton();
}

static void ResizeTab()
{
    Tab* tab = ActiveTab();

    if (!tab || !tab->controller)
        return;

    RECT rect{};

    GetClientRect(
        g_mainWindow,
        &rect
    );

    int top =
        90;

    RECT toolbarRect{};

    if (g_toolbar)
    {
        GetWindowRect(
            g_toolbar,
            &toolbarRect
        );

        POINT p{
            toolbarRect.left,
            toolbarRect.bottom
        };

        ScreenToClient(
            g_mainWindow,
            &p
        );

        top = p.y;
    }

    RECT webRect{
        0,
        top,
        rect.right,
        rect.bottom
    };

    tab->controller->put_Bounds(webRect);
}

static void ActivateTab(int index)
{
    if (
        index < 0 ||
        index >= static_cast<int>(g_tabs.size())
    )
    {
        return;
    }

    for (size_t i = 0; i < g_tabs.size(); ++i)
    {
        if (g_tabs[i]->controller)
        {
            g_tabs[i]->controller->put_IsVisible(
                static_cast<BOOL>(i == static_cast<size_t>(index))
            );
        }
    }

    g_activeTab = index;

    UpdateNavigationState();
    ResizeTab();

    InvalidateRect(
        g_tabsBar,
        nullptr,
        TRUE
    );
}

static void CloseTab(int index)
{
    if (
        index < 0 ||
        index >= static_cast<int>(g_tabs.size())
    )
    {
        return;
    }

    if (g_tabs[index]->controller)
    {
        g_tabs[index]->controller->Close();
        g_tabs[index]->controller.Reset();
    }

    if (g_tabs[index]->button)
    {
        DestroyWindow(
            g_tabs[index]->button
        );
    }

    g_tabs.erase(
        g_tabs.begin() + index
    );

    if (g_tabs.empty())
    {
        PostMessageW(
            g_mainWindow,
            WM_CLOSE,
            0,
            0
        );

        return;
    }

    if (g_activeTab >= index)
        --g_activeTab;

    if (g_activeTab < 0)
        g_activeTab = 0;

    RebuildTabButtons();
    ActivateTab(g_activeTab);
}

static void AddTabButton(int index);

static void UpdateTabButtons()
{
    if (!g_tabsBar)
        return;

    RECT rect{};

    GetClientRect(
        g_tabsBar,
        &rect
    );

    int x = 4;

    for (size_t i = 0; i < g_tabs.size(); ++i)
    {
        HWND button =
            g_tabs[i]->button;

        if (!button)
            continue;

        int width = 180;

        if (x + width > rect.right)
            width = rect.right - x;

        if (width < 80)
            width = 80;

        SetWindowPos(
            button,
            nullptr,
            x,
            4,
            width,
            rect.bottom - 8,
            SWP_NOZORDER
        );

        std::wstring caption =
            g_tabs[i]->title;

        if (caption.empty())
            caption = L"New Tab";

        SetWindowTextW(
            button,
            caption.c_str()
        );

        x += width + 4;
    }
}

static void AddTabButton(
    int index
)
{
    if (
        index < 0 ||
        index >= static_cast<int>(g_tabs.size())
    )
    {
        return;
    }

    HWND button = CreateWindowExW(
        0,
        L"BUTTON",
        L"New Tab",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        0,
        0,
        180,
        28,
        g_tabsBar,
        reinterpret_cast<HMENU>(
            10000 + index
        ),
        g_instance,
        nullptr
    );

    g_tabs[index]->button = button;

    UpdateTabButtons();
}

static void OpenNewTab(
    const std::wstring& url = L""
);

static void HandleNewWindowRequest(
    ICoreWebView2NewWindowRequestedEventArgs* args
)
{
    if (!args)
        return;

    LPWSTR requestedUri = nullptr;

    args->get_Uri(
        &requestedUri
    );

    std::wstring url;

    if (requestedUri)
    {
        url = requestedUri;
        CoTaskMemFree(requestedUri);
    }

    args->put_Handled(TRUE);

    OpenNewTab(url);
}

static void ConfigureWebView(
    Tab* tab
)
{
    if (!tab || !tab->webview)
        return;

    tab->webview->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [tab](
                ICoreWebView2*,
                ICoreWebView2NavigationStartingEventArgs* args
            )
            {
                LPWSTR uri = nullptr;

                if (
                    SUCCEEDED(
                        args->get_Uri(&uri)
                    ) &&
                    uri
                )
                {
                    tab->url = uri;
                    SetAddressBarText(tab->url);

                    CoTaskMemFree(uri);
                }

                return S_OK;
            }
        ).Get(),
        nullptr
    );

    tab->webview->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [tab](
                ICoreWebView2*,
                ICoreWebView2NavigationCompletedEventArgs*
            )
            {
                if (tab->url.empty())
                    tab->url = g_homePage;

                SaveHistory(tab->url);

                UpdateNavigationState();

                return S_OK;
            }
        ).Get(),
        nullptr
    );

    tab->webview->add_SourceChanged(
        Callback<ICoreWebView2SourceChangedEventHandler>(
            [tab](
                ICoreWebView2*,
                ICoreWebView2SourceChangedEventArgs*
            )
            {
                LPWSTR uri = nullptr;

                if (
                    SUCCEEDED(
                        tab->webview->get_Source(&uri)
                    ) &&
                    uri
                )
                {
                    tab->url = uri;

                    SetAddressBarText(
                        tab->url
                    );

                    CoTaskMemFree(uri);
                }

                UpdateNavigationState();

                return S_OK;
            }
        ).Get(),
        nullptr
    );

    tab->webview->add_DocumentTitleChanged(
        Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [tab](
                ICoreWebView2*,
                IUnknown*
            )
            {
                LPWSTR title = nullptr;

                if (
                    SUCCEEDED(
                        tab->webview->get_DocumentTitle(
                            &title
                        )
                    ) &&
                    title
                )
                {
                    tab->title = title;

                    CoTaskMemFree(title);
                }

                UpdateTabButtons();

                return S_OK;
            }
        ).Get(),
        nullptr
    );

    tab->webview->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](
                ICoreWebView2*,
                ICoreWebView2NewWindowRequestedEventArgs* args
            )
            {
                HandleNewWindowRequest(args);

                return S_OK;
            }
        ).Get(),
        nullptr
    );

    tab->webview->add_PermissionRequested(
        Callback<ICoreWebView2PermissionRequestedEventHandler>(
            [](
                ICoreWebView2*,
                ICoreWebView2PermissionRequestedEventArgs* args
            )
            {
                COREWEBVIEW2_PERMISSION_KIND kind;

                args->get_PermissionKind(
                    &kind
                );

                switch (kind)
                {
                case COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ:
                case COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS:
                    break;

                default:
                    break;
                }

                return S_OK;
            }
        ).Get(),
        nullptr
    );
}

static void CreateTabWebView(
    Tab* tab
)
{
    if (!g_environment || !tab)
        return;

    g_environment->CreateCoreWebView2Controller(
        g_mainWindow,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [tab](
                HRESULT result,
                ICoreWebView2Controller* controller
            )
            {
                if (FAILED(result) || !controller)
                    return result;

                tab->controller = controller;

                HRESULT hr =
                    controller->get_CoreWebView2(
                        &tab->webview
                    );

                if (FAILED(hr))
                    return hr;

                controller->put_IsVisible(FALSE);

                controller->put_ZoomFactor(
                    1.0
                );

                ConfigureWebView(tab);

                ResizeTab();

                std::wstring url = tab->url;

                if (url.empty())
                    url = g_homePage;

                tab->url = url;

                tab->webview->Navigate(
                    url.c_str()
                );

                int index = -1;

                for (
                    size_t i = 0;
                    i < g_tabs.size();
                    ++i
                )
                {
                    if (g_tabs[i].get() == tab)
                    {
                        index =
                            static_cast<int>(i);
                        break;
                    }
                }

                if (index >= 0)
                    ActivateTab(index);

                return S_OK;
            }
        ).Get()
    );
}

static void OpenNewTab(
    const std::wstring& url
)
{
    auto tab =
        std::make_unique<Tab>();

    tab->url =
        url.empty()
            ? g_homePage
            : url;

    tab->title =
        L"New Tab";

    g_tabs.push_back(
        std::move(tab)
    );

    int index =
        static_cast<int>(g_tabs.size()) - 1;

    AddTabButton(index);

    ActivateTab(index);

    CreateTabWebView(
        g_tabs[index].get()
    );
}

static void CloseActiveTab()
{
    if (g_activeTab >= 0)
        CloseTab(g_activeTab);
}

static void ShowBookmarks()
{
    if (!g_bookmarksPanel)
        return;

    bool visible =
        IsWindowVisible(
            g_bookmarksPanel
        ) != FALSE;

    ShowWindow(
        g_bookmarksPanel,
        visible
            ? SW_HIDE
            : SW_SHOW
    );

    if (!visible)
    {
        SetWindowTextW(
            g_bookmarksPanel,
            L"BOOKMARKS"
        );
    }
}

static void ShowHistory()
{
    if (!g_historyPanel)
        return;

    bool visible =
        IsWindowVisible(
            g_historyPanel
        ) != FALSE;

    ShowWindow(
        g_historyPanel,
        visible
            ? SW_HIDE
            : SW_SHOW
    );

    if (!visible)
    {
        SetWindowTextW(
            g_historyPanel,
            L"HISTORY"
        );
    }
}

static void CreateToolbar()
{
    g_toolbar = CreateWindowExW(
        0,
        L"STATIC",
        nullptr,
        WS_CHILD |
        WS_VISIBLE,
        0,
        0,
        100,
        55,
        g_mainWindow,
        nullptr,
        g_instance,
        nullptr
    );

    g_backButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"←",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        5,
        8,
        38,
        34,
        g_toolbar,
        reinterpret_cast<HMENU>(1),
        g_instance,
        nullptr
    );

    g_forwardButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"→",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        47,
        8,
        38,
        34,
        g_toolbar,
        reinterpret_cast<HMENU>(2),
        g_instance,
        nullptr
    );

    g_reloadButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"↻",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        89,
        8,
        38,
        34,
        g_toolbar,
        reinterpret_cast<HMENU>(3),
        g_instance,
        nullptr
    );

    g_homeButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"⌂",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        131,
        8,
        38,
        34,
        g_toolbar,
        reinterpret_cast<HMENU>(4),
        g_instance,
        nullptr
    );

    g_addressBar = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD |
        WS_VISIBLE |
        ES_AUTOHSCROLL,
        175,
        8,
        500,
        34,
        g_toolbar,
        reinterpret_cast<HMENU>(5),
        g_instance,
        nullptr
    );

    g_bookmarkButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"☆",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        680,
        8,
        42,
        34,
        g_toolbar,
        reinterpret_cast<HMENU>(6),
        g_instance,
        nullptr
    );

    g_historyButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"History",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        726,
        8,
        70,
        34,
        g_toolbar,
        reinterpret_cast<HMENU>(7),
        g_instance,
        nullptr
    );

    g_newTabButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"+",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        800,
        8,
        42,
        34,
        g_toolbar,
        reinterpret_cast<HMENU>(8),
        g_instance,
        nullptr
    );
}

static void CreatePanels()
{
    g_bookmarksPanel = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"BOOKMARKS",
        WS_CHILD |
        ES_MULTILINE |
        ES_READONLY |
        WS_VSCROLL,
        0,
        0,
        260,
        300,
        g_mainWindow,
        nullptr,
        g_instance,
        nullptr
    );

    g_historyPanel = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"HISTORY",
        WS_CHILD |
        ES_MULTILINE |
        ES_READONLY |
        WS_VSCROLL,
        0,
        0,
        260,
        300,
        g_mainWindow,
        nullptr,
        g_instance,
        nullptr
    );
}

static void UpdatePanelContents()
{
    if (g_bookmarksPanel)
    {
        auto bookmarks =
            LoadLines(
                BookmarkFile()
            );

        std::wstring text;

        for (const auto& item : bookmarks)
        {
            text += item;
            text += L"\r\n";
        }

        SetWindowTextW(
            g_bookmarksPanel,
            text.c_str()
        );
    }

    if (g_historyPanel)
    {
        auto history =
            LoadLines(
                HistoryFile()
            );

        std::wstring text;

        int start =
            history.size() > 100
                ? static_cast<int>(
                    history.size() - 100
                  )
                : 0;

        for (
            int i = start;
            i < static_cast<int>(history.size());
            ++i
        )
        {
            text += history[i];
            text += L"\r\n";
        }

        SetWindowTextW(
            g_historyPanel,
            text.c_str()
        );
    }
}

static void UpdateLayout()
{
    if (!g_mainWindow)
        return;

    RECT rc{};

    GetClientRect(
        g_mainWindow,
        &rc
    );

    const int tabsHeight = 34;
    const int toolbarHeight = 55;

    if (g_tabsBar)
    {
        SetWindowPos(
            g_tabsBar,
            nullptr,
            0,
            0,
            rc.right,
            tabsHeight,
            SWP_NOZORDER
        );
    }

    if (g_toolbar)
    {
        SetWindowPos(
            g_toolbar,
            nullptr,
            0,
            tabsHeight,
            rc.right,
            toolbarHeight,
            SWP_NOZORDER
        );

        int width =
            rc.right - 175 - 170;

        if (width < 150)
            width = 150;

        SetWindowPos(
            g_addressBar,
            nullptr,
            175,
            8,
            width,
            34,
            SWP_NOZORDER
        );

        int x =
            175 + width + 5;

        SetWindowPos(
            g_bookmarkButton,
            nullptr,
            x,
            8,
            42,
            34,
            SWP_NOZORDER
        );

        x += 47;

        SetWindowPos(
            g_historyButton,
            nullptr,
            x,
            8,
            70,
            34,
            SWP_NOZORDER
        );

        x += 75;

        SetWindowPos(
            g_newTabButton,
            nullptr,
            x,
            8,
            42,
            34,
            SWP_NOZORDER
        );
    }

    if (g_bookmarksPanel)
    {
        SetWindowPos(
            g_bookmarksPanel,
            nullptr,
            0,
            tabsHeight + toolbarHeight,
            300,
            rc.bottom -
                tabsHeight -
                toolbarHeight,
            SWP_NOZORDER
        );
    }

    if (g_historyPanel)
    {
        SetWindowPos(
            g_historyPanel,
            nullptr,
            0,
            tabsHeight + toolbarHeight,
            300,
            rc.bottom -
                tabsHeight -
                toolbarHeight,
            SWP_NOZORDER
        );
    }

    ResizeTab();
    UpdateTabButtons();
}

static void InitializeWebView2()
{
    std::wstring userDataFolder =
        AppDataPath() + L"\\WebView2";

    CreateDirectoryW(
        userDataFolder.c_str(),
        nullptr
    );

    HRESULT hr =
        CreateCoreWebView2EnvironmentWithOptions(
            nullptr,
            userDataFolder.c_str(),
            nullptr,
            Callback<
                ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler
            >(
                [](
                    HRESULT result,
                    ICoreWebView2Environment* environment
                )
                {
                    if (FAILED(result) ||
                        !environment)
                    {
                        MessageBoxW(
                            g_mainWindow,
                            L"Microsoft Edge WebView2 Runtime could not be initialized.",
                            APP_NAME,
                            MB_ICONERROR
                        );

                        return result;
                    }

                    g_environment =
                        environment;

                    OpenNewTab(
                        g_homePage
                    );

                    return S_OK;
                }
            ).Get()
        );

    if (FAILED(hr))
    {
        MessageBoxW(
            g_mainWindow,
            L"Failed to start WebView2.",
            APP_NAME,
            MB_ICONERROR
        );
    }
}

static void ExecuteCommand(
    int command
)
{
    switch (command)
    {
    case 1:
        GoBack();
        break;

    case 2:
        GoForward();
        break;

    case 3:
        Reload();
        break;

    case 4:
        NavigateHome();
        break;

    case 6:
        ToggleBookmark();
        UpdateBookmarkButton();
        UpdatePanelContents();
        break;

    case 7:
        UpdatePanelContents();
        ShowHistory();
        break;

    case 8:
        OpenNewTab();
        break;

    default:
        break;
    }
}

static LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
)
{
    switch (message)
    {
    case WM_CREATE:
    {
        CreateToolbar();
        CreatePanels();

        g_tabsBar = CreateWindowExW(
            0,
            L"STATIC",
            nullptr,
            WS_CHILD |
            WS_VISIBLE,
            0,
            0,
            100,
            34,
            hwnd,
            nullptr,
            g_instance,
            nullptr
        );

        InitializeWebView2();

        return 0;
    }

    case WM_SIZE:
    {
        UpdateLayout();
        return 0;
    }

    case WM_SETFOCUS:
    {
        if (g_addressBar)
            SetFocus(g_addressBar);

        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int notification = HIWORD(wParam);

        if (
            id >= 1 &&
            id <= 8 &&
            notification == BN_CLICKED
        )
        {
            ExecuteCommand(id);
            return 0;
        }

        if (
            id >= 10000 &&
            id < 20000 &&
            notification == BN_CLICKED
        )
        {
            int tabIndex =
                id - 10000;

            ActivateTab(tabIndex);

            return 0;
        }

        if (
            id == 5 &&
            notification == EN_SETFOCUS
        )
        {
            SendMessageW(
                g_addressBar,
                EM_SETSEL,
                0,
                -1
            );

            return 0;
        }

        if (
            id == 5 &&
            notification == EN_UPDATE
        )
        {
            return 0;
        }

        return 0;
    }

    case WM_KEYDOWN:
    {
        bool ctrl =
            (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        bool shift =
            (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (ctrl)
        {
            switch (wParam)
            {
            case 'L':
                SetFocus(g_addressBar);

                SendMessageW(
                    g_addressBar,
                    EM_SETSEL,
                    0,
                    -1
                );

                return 0;

            case 'T':
                OpenNewTab();
                return 0;

            case 'W':
                CloseActiveTab();
                return 0;

            case 'R':
                Reload();
                return 0;

            case 'D':
                ToggleBookmark();
                UpdateBookmarkButton();
                return 0;

            case 'H':
                UpdatePanelContents();
                ShowHistory();
                return 0;

            case 'N':
                OpenNewTab();
                return 0;

            case 'F':
                return 0;

            default:
                break;
            }
        }

        if (wParam == VK_F5)
        {
            Reload();
            return 0;
        }

        if (wParam == VK_ESCAPE)
        {
            Stop();
            return 0;
        }

        if (wParam == VK_F11)
        {
            static bool fullscreen = false;

            fullscreen = !fullscreen;

            if (fullscreen)
            {
                SetWindowLongW(
                    hwnd,
                    GWL_STYLE,
                    WS_POPUP |
                    WS_VISIBLE
                );

                ShowWindow(
                    hwnd,
                    SW_MAXIMIZE
                );
            }
            else
            {
                SetWindowLongW(
                    hwnd,
                    GWL_STYLE,
                    WS_OVERLAPPEDWINDOW |
                    WS_VISIBLE
                );

                ShowWindow(
                    hwnd,
                    SW_RESTORE
                );
            }

            UpdateLayout();

            return 0;
        }

        if (
            shift &&
            wParam == VK_F5
        )
        {
            Reload();
            return 0;
        }

        break;
    }

    case WM_CHAR:
    {
        if (
            wParam == VK_RETURN &&
            GetFocus() == g_addressBar
        )
        {
            wchar_t buffer[8192]{};

            GetWindowTextW(
                g_addressBar,
                buffer,
                static_cast<int>(
                    std::size(buffer)
                )
            );

            Navigate(buffer);

            return 0;
        }

        break;
    }

    case WM_CLOSE:
    {
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY:
    {
        g_tabs.clear();

        PostQuitMessage(0);

        return 0;
    }

    default:
        break;
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam
    );
}

static bool RegisterBrowserClass()
{
    WNDCLASSEXW wc{};

    wc.cbSize =
        sizeof(WNDCLASSEXW);

    wc.style =
        CS_HREDRAW |
        CS_VREDRAW;

    wc.lpfnWndProc =
        WindowProc;

    wc.hInstance =
        g_instance;

    wc.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW
        );

    wc.hbrBackground =
        reinterpret_cast<HBRUSH>(
            COLOR_WINDOW + 1
        );

    wc.lpszClassName =
        L"LightBrowserWindow";

    return RegisterClassExW(&wc) != 0;
}

int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE,
    PWSTR,
    int nCmdShow
)
{
    g_instance = hInstance;

    HRESULT comResult =
        CoInitializeEx(
            nullptr,
            COINIT_APARTMENTTHREADED
        );

    if (
        FAILED(comResult) &&
        comResult != RPC_E_CHANGED_MODE
    )
    {
        MessageBoxW(
            nullptr,
            L"COM initialization failed.",
            APP_NAME,
            MB_ICONERROR
        );

        return 1;
    }

    INITCOMMONCONTROLSEX controls{};

    controls.dwSize =
        sizeof(INITCOMMONCONTROLSEX);

    controls.dwICC =
        ICC_STANDARD_CLASSES;

    InitCommonControlsEx(
        &controls
    );

    if (!RegisterBrowserClass())
    {
        MessageBoxW(
            nullptr,
            L"Could not register browser window.",
            APP_NAME,
            MB_ICONERROR
        );

        if (SUCCEEDED(comResult))
            CoUninitialize();

        return 1;
    }

    g_mainWindow = CreateWindowExW(
        0,
        L"LightBrowserWindow",
        L"LightBrowser",
        WS_OVERLAPPEDWINDOW |
        WS_CLIPCHILDREN |
        WS_CLIPSIBLINGS,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1400,
        900,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!g_mainWindow)
    {
        MessageBoxW(
            nullptr,
            L"Could not create browser window.",
            APP_NAME,
            MB_ICONERROR
        );

        if (SUCCEEDED(comResult))
            CoUninitialize();

        return 1;
    }

    ShowWindow(
        g_mainWindow,
        nCmdShow
    );

    UpdateWindow(
        g_mainWindow
    );

    MSG msg{};

    while (
        GetMessageW(
            &msg,
            nullptr,
            0,
            0
        ) > 0
    )
    {
        if (
            msg.message == WM_KEYDOWN &&
            GetFocus() == g_addressBar
        )
        {
            if (msg.wParam == VK_RETURN)
            {
                wchar_t buffer[8192]{};

                GetWindowTextW(
                    g_addressBar,
                    buffer,
                    static_cast<int>(
                        std::size(buffer)
                    )
                );

                Navigate(buffer);

                continue;
            }
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (SUCCEEDED(comResult))
        CoUninitialize();

    return static_cast<int>(
        msg.wParam
    );
}
