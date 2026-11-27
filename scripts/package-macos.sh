#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    printf 'Usage: %s <Qt prefix> <CMake build directory> [output directory]\n' "$0" >&2
    exit 2
fi

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
qt_prefix="$1"
bundle="$2/Orbite.app"
output_directory="${3:-dist}"
macdeployqt="$qt_prefix/bin/macdeployqt"
identity="${ORBITE_SIGN_IDENTITY:--}"

if [[ ! -x "$macdeployqt" || ! -x "$bundle/Contents/MacOS/Orbite" ]]; then
    printf 'Build Orbite and supply the Qt prefix that contains macdeployqt.\n' >&2
    exit 1
fi
if [[ -n "${ORBITE_NOTARY_PROFILE:-}" && "$identity" == "-" ]]; then
    printf 'Notarization requires a Developer ID Application signing identity.\n' >&2
    exit 1
fi

mkdir -p "$output_directory"
output_directory="$(cd "$output_directory" && pwd -P)"
stage="$(mktemp -d "${TMPDIR:-/tmp}/orbite-package.XXXXXX")"
trap 'rm -rf "$stage"' EXIT
mkdir "$stage/disk"

app="$stage/disk/Orbite.app"
ditto "$bundle" "$app"
architectures="$(lipo -archs "$app/Contents/MacOS/Orbite")"
case "$architectures" in
    arm64|x86_64) arch="$architectures" ;;
    'x86_64 arm64'|'arm64 x86_64') arch=universal ;;
    *) printf 'Unexpected architecture %s\n' "$architectures" >&2; exit 1 ;;
esac

platform_plugin="$app/Contents/PlugIns/platforms/libqcocoa.dylib"
sqlite_plugin="$app/Contents/PlugIns/sqldrivers/libqsqlite.dylib"
mkdir -p "$(dirname "$platform_plugin")" "$(dirname "$sqlite_plugin")"
install -m 755 "$qt_prefix/plugins/platforms/libqcocoa.dylib" "$platform_plugin"
install -m 755 "$qt_prefix/plugins/sqldrivers/libqsqlite.dylib" "$sqlite_plugin"
resources="$app/Contents/Resources"
printf '[Paths]\nPlugins=PlugIns\n' > "$resources/qt.conf"
mkdir "$resources/Licenses"
install -m 644 "$repo/LICENSE" "$resources/Licenses/Orbite-MIT.txt"
install -m 644 "$repo/licenses/"Qt-*.txt "$resources/Licenses/"
qt_version="$("$qt_prefix/bin/qtpaths" --qt-version)"
cat > "$resources/Licenses/Third-Party-Notices.txt" <<NOTICE
Orbite dynamically links to Qt Base $qt_version.
Qt is copyright The Qt Company Ltd. and other contributors.
Qt Base is provided here under LGPL-3.0-only. Refer to Qt-LGPL-3.0.txt and Qt-GPL-3.0.txt.

Corresponding Qt Base source, build files and upstream license notices
https://download.qt.io/archive/qt/${qt_version%.*}/$qt_version/submodules/qtbase-everywhere-src-$qt_version.tar.xz
Qt module and third-party attributions
https://doc.qt.io/qt-${qt_version%.*}/licenses-used-in-qt.html

Qt frameworks and plugins are in Orbite.app/Contents/Frameworks and PlugIns.
You can rebuild Orbite from its tagged source with a compatible Qt SDK, or
replace the dynamic Qt components in a local copy and sign that modified copy.
NOTICE
install -m 644 "$resources/Licenses/Third-Party-Notices.txt" "$stage/disk/Third-Party-Notices.txt"
deploy_args=(-no-plugins "-executable=$platform_plugin" "-executable=$sqlite_plugin" "-codesign=$identity")
if [[ "$identity" != "-" ]]; then
    deploy_args+=(-hardened-runtime -timestamp)
fi
"$macdeployqt" "$app" "${deploy_args[@]}"
test -f "$app/Contents/PlugIns/platforms/libqcocoa.dylib"
test -f "$app/Contents/PlugIns/sqldrivers/libqsqlite.dylib"
codesign --verify --deep --strict "$app"

version="$(cat "$resources/release-version.txt")"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[a-z0-9][a-z0-9.-]*)?$ ]]; then
    printf 'Invalid release version %s\n' "$version" >&2
    exit 1
fi
stem="Orbite-${version}-macOS-${arch}"
ln -s /Applications "$stage/disk/Applications"
hdiutil create -quiet -fs HFS+ -format UDZO -volname Orbite -srcfolder "$stage/disk" "$stage/$stem.dmg"
if [[ "$identity" != "-" ]]; then
    codesign --force --timestamp --sign "$identity" "$stage/$stem.dmg"
fi
if [[ -n "${ORBITE_NOTARY_PROFILE:-}" ]]; then
    xcrun notarytool submit "$stage/$stem.dmg" --keychain-profile "$ORBITE_NOTARY_PROFILE" --wait
    xcrun stapler staple "$stage/$stem.dmg"
fi
hdiutil verify -quiet "$stage/$stem.dmg"
mv -f "$stage/$stem.dmg" "$output_directory/$stem.dmg"
(
    cd "$output_directory"
    shasum -a 256 "$stem.dmg" > "$stem.dmg.sha256"
)
printf 'Wrote %s/%s.dmg\n' "$output_directory" "$stem"
