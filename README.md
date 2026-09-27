# LightBrowser

LightBrowser is a native Windows browser built with:

- C++
- Win32 API
- Microsoft Edge WebView2
- CMake
- GitHub Actions

## Features

- Real Chromium-based web rendering
- HTTPS websites
- JavaScript
- HTML
- CSS
- Browser tabs
- Address bar
- Search
- Back
- Forward
- Reload
- Stop
- Home
- Bookmarks
- History
- Downloads
- New-window handling
- Web permissions
- Browser zoom
- Full-screen mode
- Keyboard shortcuts
- Persistent WebView2 profile
- Persistent history
- Persistent bookmarks
- Windows x64 build
- GitHub Actions build
- No local compiler required

## Keyboard Shortcuts

Ctrl + L
Focus address bar

Ctrl + T
New tab

Ctrl + W
Close tab

Ctrl + R
Reload

Ctrl + D
Bookmark

Ctrl + H
History

F5
Reload

Esc
Stop loading

F11
Fullscreen

## Local Storage

LightBrowser stores browser data under:

%LOCALAPPDATA%\LightBrowser

Downloads are saved to:

%USERPROFILE%\Downloads

## Build

GitHub Actions automatically:

1. Installs the WebView2 SDK.
2. Configures CMake.
3. Builds the native Windows application.
4. Copies WebView2Loader.dll.
5. Creates LightBrowser-Windows-x64.zip.
6. Uploads the ZIP as a GitHub Actions artifact.

## Requirements

Windows 10 or Windows 11.

Microsoft Edge WebView2 Runtime must be installed.

## License

This project is an independent browser implementation.

It does not contain Opera proprietary source code, Opera proprietary services, or Opera trademarks.
