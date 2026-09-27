# LightBrowser

LightBrowser is an independent native Windows browser built with:

- C++
- Win32 API
- Microsoft Edge WebView2
- CMake
- GitHub Actions

## Features

- Chromium-based web rendering through WebView2
- HTTPS websites
- JavaScript
- HTML
- CSS
- Browser tabs
- Address bar and web search
- Back / Forward / Reload / Stop / Home
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
- Reproducible GitHub Actions build workflow
- Release integrity checks with SHA-256

## Keyboard Shortcuts

- Ctrl + L — Focus address bar
- Ctrl + T — New tab
- Ctrl + W — Close tab
- Ctrl + R — Reload
- Ctrl + D — Bookmark
- Ctrl + H — History
- F5 — Reload
- Esc — Stop loading
- F11 — Fullscreen

## Requirements

- Windows 10 or Windows 11
- Microsoft Edge WebView2 Runtime

## Build

GitHub Actions builds LightBrowser on a GitHub-hosted Windows runner. The build downloads a pinned WebView2 SDK, configures CMake, builds the Release executable with Windows security hardening, creates the Windows x64 package, generates SHA-256 hashes, and uploads the release artifacts.

## Code Signing Policy

LightBrowser is intended to use **free code signing provided by SignPath.io, certificate by SignPath Foundation** once the project is accepted into the SignPath Foundation program.

The signing process is designed so that signed release artifacts originate from this public GitHub repository and its GitHub Actions build. Signing requests require manual approval under the SignPath Foundation rules.

See [CODE_SIGNING_POLICY.md](CODE_SIGNING_POLICY.md).

### Team roles

- Committers and reviewers: repository maintainers with write access to this GitHub repository.
- Approvers: the repository owner/maintainer responsible for approving release signing requests.

### Privacy

LightBrowser does not intentionally transfer application data to a server operated by the project. The browser connects to websites chosen by the user through Microsoft Edge WebView2. Third-party website privacy policies apply to websites the user visits.

## Release integrity

Release packages include SHA-256 checksums. Users should obtain releases from the official repository/release page and verify the checksum before execution.

## Security

The Windows build enables ASLR, DEP/NX compatibility, Control Flow Guard, CET compatibility where supported by the toolchain, Visual C++ SDL compiler checks, an `asInvoker` execution level, a pinned WebView2 SDK version, no embedded application secrets, GitHub-hosted build agents, and SHA-256 release hashes.

Security hardening does not guarantee that Windows SmartScreen will immediately establish reputation for a new application. Code signing and SmartScreen reputation are separate mechanisms.

## License

LightBrowser source code is released under the MIT License. See [LICENSE](LICENSE).

This project is an independent browser implementation. It does not contain Opera proprietary source code, Opera proprietary services, or Opera trademarks.
