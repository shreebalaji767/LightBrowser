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
static constexpr double DEFAULT_ZOOM = 1.0;
static constexpr double MIN_ZOOM = 0.25;
static constexpr double MAX_ZOOM = 5.0;

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
    int bytesNeeded = WideCharToMultiByte(
        CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()),
        nullptr, 0, nullptr, nullptr);

    if (bytesNeeded <= 0)
        return L"";

    std::string utf8(static_cast<size_t>(bytesNeeded), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()),
        utf8.data(), bytesNeeded, nullptr, nullptr);

    std::wstring output;
    const wchar_t hex[] = L"0123456789ABCDEF";

    for (unsigned char c : utf8)
    {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            output += static_cast<wchar_t>(c);
        }
        else if (c == ' ')
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

static void SetZoom(double factor)
{
    Tab* tab = ActiveTab();
    if (!tab || !tab->controller)
        return;

    factor = max(MIN_ZOOM, min(MAX_ZOOM, factor));
    tab->controller->put_ZoomFactor(factor);
}

static void ZoomBy(double delta)
{
    Tab* tab = ActiveTab();
    if (!tab || !tab->controller)
        return;

    double zoom = DEFAULT_ZOOM;
    if (SUCCEEDED(tab->controller->get_ZoomFactor(&zoom)))
        SetZoom(zoom + delta);
}

static void OpenDevTools()
{
    Tab* tab = ActiveTab();
    if (tab && tab->webview)
        tab->webview->OpenDevToolsWindow();
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

static void OpenDownloadsFolder()
{
    std::wstring path = DownloadsPath();
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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

        const bool narrow = rc.right < 760;
        const int left = 175;
        const int rightButtons = narrow ? 94 : 169;

        int width = rc.right - left - rightButtons;
        if (width < 120)
            width = 120;

        SetWindowPos(g_addressBar, nullptr, left, 8, width, 34, SWP_NOZORDER);

        int x = left + width + 5;
        SetWindowPos(g_bookmarkButton, nullptr, x, 8, 42, 34, SWP_NOZORDER);
        x += 47;

        SetWindowPos(g_historyButton, nullptr, x, 8, narrow ? 42 : 70, 34, SWP_NOZORDER);
        SetWindowTextW(g_historyButton, narrow ? L"H" : L"History");
        x += narrow ? 47 : 75;

        SetWindowPos(g_downloadButton, nullptr, x, 8, 42, 34, SWP_NOZORDER);
        x += 47;

        SetWindowPos(g_newTabButton, nullptr, x, 8, 42, 34, SWP_NOZORDER);
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

    case 9:
        OpenDownloadsFolder();
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
            id <= 9 &&
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
                FindOnPage();
                return 0;

            case '0':
                SetZoom(DEFAULT_ZOOM);
                return 0;

            case VK_OEM_PLUS:
            case VK_ADD:
                ZoomBy(0.10);
                return 0;

            case VK_OEM_MINUS:
            case VK_SUBTRACT:
                ZoomBy(-0.10);
                return 0;

            default:
                break;
            }
        }

        if (wParam == VK_F12)
        {
            OpenDevTools();
            return 0;
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
