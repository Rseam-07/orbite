Orbite Qt 0.8 Windows x64 | CI test build, not a public release

1. Extract the Windows ZIP completely. Keep the DLL and plugin folders
   beside Orbite.exe. Do not run it directly from a ZIP viewer.
2. If the Microsoft Visual C++ v14 runtime is missing, use Microsoft's
   official x64 Redistributable:
   https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/#latest-supported-redistributable-version
3. To keep test data separate from your normal Orbite installation, open
   PowerShell in the extracted folder and run:
   .\Orbite.exe --profile (Join-Path $env:TEMP 'Orbite-Qt-08-QA')
4. For model chat, install the official Antigravity CLI and make sure
   agy --version works. Orbite uses the CLI's Google sign-in:
   https://antigravity.google/docs/cli-install

This build is unsigned. It was checked on a Windows Server 2022 CI runner
with the Microsoft runtime installed, not independently on a fresh Windows
desktop. Do not disable OS protections to run it.

Qt Base 6.8.3 is dynamically linked and distributed under LGPL-3.0;
see Qt-LGPL-3.0.txt and Qt-GPL-3.0.txt. The same Actions artifact contains
the corresponding Qt Base source archive, including upstream build files
and third-party notices (also see https://doc.qt.io/qt-6.8/licenses-used-in-qt.html).
To use a compatible modified Qt, rebuild the shared Qt Base DLLs and plugins
and replace those distributed components in a separate copy, preserving
their relative paths. Orbite itself may be rebuilt from the source commit
identified by the Actions run.
