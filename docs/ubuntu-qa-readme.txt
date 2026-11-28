Orbite Qt 0.8 | Ubuntu 24.04 x64 CI test, not a public release

Extract the GitHub Actions download. Verify the inner archive in a terminal:
  sha256sum -c Orbite-ubuntu-x64-qa.tar.gz.sha256

Extract the inner archive into a fresh folder:
  mkdir -p Orbite-Qt-08-QA
  tar -xzf Orbite-ubuntu-x64-qa.tar.gz -C Orbite-Qt-08-QA
  cd Orbite-Qt-08-QA
  QT_QPA_PLATFORM=xcb ./Orbite --profile "$(mktemp -d)"

Use the top-level Orbite launcher, not bin/Orbite. It uses the bundled Qt
and ICU shared libraries. This command makes a fresh profile each time;
reuse the path after --profile if you want to keep a test conversation.

Use an X11 desktop. The current CI check used Xvfb with Qt's xcb backend.
Native Wayland is not included, and XWayland has not been verified.
If a library is missing on Ubuntu 24.04, these runtime packages cover
the X11/OpenGL dependencies checked here:
  sudo apt install libegl1 libgl1 libopengl0 libx11-xcb1 libxcb-cursor0 \
    libxkbcommon-x11-0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
    libxcb-render-util0 libxcb-shape0 libxcb-randr0 libxcb-render0 \
    libxcb-shm0 libxcb-sync1 libxcb-xfixes0 libxcb-xkb1
No Qt SDK is needed. Model chat uses the official Antigravity CLI and
its Google sign-in: https://antigravity.google/docs/cli-install

The same Actions artifact contains the matching Qt Base 6.8.3 and ICU4C
73.2 source archives. See Qt-LGPL-3.0.txt, Qt-GPL-3.0.txt and ICU-LICENSE.txt
and the Qt third-party notices at https://doc.qt.io/qt-6.8/licenses-used-in-qt.html.
Compatible rebuilt Qt/ICU shared libraries and Qt plugins can replace the
bundled versions in a separate copy, preserving paths and filenames.
Orbite can be rebuilt from the source commit named on the Actions run.
