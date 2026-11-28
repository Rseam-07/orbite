# Qt 0.8 distribution checklist

Use this for the first Qt release with macOS, Windows and Linux downloads. The existing macOS-only beta and its release notes are unchanged. A green PR build is not a public release.

## Candidate evidence

- Build every asset from the same tagged source and record the exact Qt patch version, toolchain and target architecture per platform. Publish checksums of the actual downloads, not of intermediate CI archives.
- Use the Windows and Ubuntu CI package inventories and the Ubuntu ELF version requirements as starting evidence. Review every extra DLL, shared library and plugin before distribution; remove unused items only after rebuilding and rerunning package tests.
- Record each shipped dependency's version, origin, license, notices and applicable corresponding source. Include both Qt license texts and the appropriate notices for the shipped Qt modules and third-party code, including separately bundled dependencies such as ICU. Keep the exact corresponding Qt source (and any patches/build instructions needed to reproduce it) available with the distribution and document how recipients can rebuild or replace the dynamic libraries. Upstream references: [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations) and [Qt third-party attributions](https://doc.qt.io/qt-6.8/licenses-used-in-qt.html).
- Check the [upstream Qt 6.8 source and security update archive](https://download.qt.io/archive/qt/6.8/) before selecting a public runtime. A successful build using a CI SDK does not show that the SDK is suitable for public distribution. Verify the final dependency versions again from the actual release package.

## Clean desktop acceptance

| Target | Current CI smoke check | Still required for the release candidate |
| --- | --- | --- |
| Windows x64 | ZIP opens with the Qt SDK off PATH on Windows Server 2022, which already has the MSVC runtime. | Test on a clean Windows desktop without Visual Studio or Qt. The QA ZIP omits the compiler runtime; if the chosen format leaves it as a prerequisite, document the [official x64 Microsoft Visual C++ v14 Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/#latest-supported-redistributable-version) and verify the normal first-install path. |
| Ubuntu 24.04 x64 | Relocated tar.gz opens under Xvfb using Qt's xcb plugin and the runner's system libraries. | Test on a clean Ubuntu desktop, declare the required system packages, and record X11 and XWayland separately if both are offered. Do not advertise native Wayland until it is packaged and verified. ELF symbol versions alone are not a supported-distribution list. [Qt X11 requirements](https://doc.qt.io/qt-6.8/linux-requirements.html). |
| macOS arm64 and x86_64 | CI makes separate ad-hoc signed DMGs. | Verify both slices of the final universal DMG, ideally on native Apple Silicon and Intel; record signing and notarization status and the actual minimum OS tested. |

On each accepted desktop, open the final download with no Qt SDK installed, initialize a new temporary profile, and restart it. Drag one file and one nested folder from the OS file manager; repeat using the picker, confirm nothing sends automatically, and confirm the draft survives switching conversations. Test the official Antigravity CLI and first Google sign-in with an authorized test account; distinguish it from reusing an existing CLI session. Report Computer Use separately from startup and do not claim a platform or action was verified unless it was exercised.

Record the download name and checksum, OS version, desktop session, result and any release-blocking failures in the candidate review. Do not promote QA artifacts to release assets.
