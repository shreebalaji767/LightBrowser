# Code Signing Policy

LightBrowser uses **free code signing provided by SignPath.io, certificate by SignPath Foundation** when the project is accepted and configured for the SignPath Foundation program.

## Source and build

Signed binaries must be produced from the LightBrowser source repository:

https://github.com/shreebalaji767/LightBrowser

The release build is performed by GitHub Actions on a GitHub-hosted Windows runner. SignPath origin verification is used for signing requests when the project is configured.

## Roles

- **Committers / reviewers:** maintainers with write access to the repository.
- **Approver:** the repository owner/maintainer responsible for deciding whether a release should be signed.

## Release process

1. Source changes are committed to the repository.
2. GitHub Actions builds the Windows x64 Release binary.
3. SHA-256 checksums are generated.
4. The unsigned release artifact is uploaded to GitHub Actions.
5. A SignPath signing request is submitted for configured releases.
6. The signing request is manually approved according to the SignPath Foundation rules.
7. The signed release artifact is published separately from the unsigned build.

## Privacy

The LightBrowser project does not intentionally send application data to project-operated servers. The browser communicates with websites selected by the user through Microsoft Edge WebView2. Website operators and other third-party services have their own privacy policies.

## Verification

Users should download releases from the official LightBrowser GitHub repository and verify the published SHA-256 checksum. A valid SignPath Foundation signature also provides a cryptographic link between the signed artifact and the verified build source when SignPath origin verification is enabled.

## Scope

This policy applies to LightBrowser releases signed through SignPath Foundation. Third-party dependencies remain subject to their own licenses and signing policies.
