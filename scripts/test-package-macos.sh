#!/usr/bin/env bash
set -euo pipefail

image="$1"
architecture="${2:-$(uname -m)}"
version="$3"
stage="$(mktemp -d "${TMPDIR:-/tmp}/orbite-test.XXXXXX")"
mount="$stage/mount"
cleanup() {
    if [[ -d "$mount/Orbite.app" ]]; then
        hdiutil detach -quiet "$mount"
    fi
    rm -rf "$stage"
}
trap cleanup EXIT
mkdir "$mount"
hdiutil attach -quiet -readonly -nobrowse -mountpoint "$mount" "$image"
app="$mount/Orbite.app"
codesign --verify --deep --strict "$app"
lipo "$app/Contents/MacOS/Orbite" -verify_arch "$architecture"
report="$stage/result.json"
env -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM -u QT_QPA_PLATFORM_PLUGIN_PATH \
    -u DYLD_LIBRARY_PATH -u DYLD_FRAMEWORK_PATH \
    /usr/bin/arch "-$architecture" "$app/Contents/MacOS/Orbite" --verify-package "$report"
python3 - "$report" "$architecture" "$version" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as report_file:
    report = json.load(report_file)
expected = {
    "platform": "cocoa",
    "architecture": sys.argv[2],
    "version": sys.argv[3],
    "databaseWritable": True,
    "windowVisible": True,
}
if report != expected:
    raise SystemExit(f"Packaged application failed verification: {report}")
print(f"Packaged application verified: {report}")
PY
