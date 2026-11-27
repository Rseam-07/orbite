param(
    [Parameter(Mandatory = $true)][string]$QtPrefix,
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [string]$OutputDirectory = 'dist',
    [string]$RedistDirectory = $env:VCToolsRedistDir
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$qt = (Resolve-Path -LiteralPath $QtPrefix).Path
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$output = (New-Item -ItemType Directory -Path $OutputDirectory -Force).FullName
$exe = Join-Path $build 'Orbite.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    $exe = Join-Path $build 'Release\Orbite.exe'
}
if (-not (Test-Path -LiteralPath $exe)) {
    throw 'Build Orbite in Release mode before packaging.'
}
$version = (Get-Content -LiteralPath (Join-Path $build 'release-version.txt') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+(?:-[a-z0-9][a-z0-9.-]*)?$') {
    throw "Unexpected Orbite version $version"
}

$stem = "Orbite-$version-Windows-x64"
$stage = (New-Item -ItemType Directory -Path (Join-Path ([IO.Path]::GetTempPath()) "orbite-package-$([guid]::NewGuid())")).FullName
$oldPath = $env:Path
try {
    $portable = (New-Item -ItemType Directory -Path (Join-Path $stage 'Orbite')).FullName
    $target = Join-Path $portable 'Orbite.exe'
    Copy-Item -LiteralPath $exe -Destination $target
    $env:Path = "$(Join-Path $qt 'bin');$oldPath"
    & (Join-Path $qt 'bin\windeployqt.exe') --release --no-translations --no-opengl-sw --no-system-d3d-compiler --no-compiler-runtime $target
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed ($LASTEXITCODE)." }
    $sql = (New-Item -ItemType Directory -Path (Join-Path $portable 'sqldrivers') -Force).FullName
    Copy-Item -LiteralPath (Join-Path $qt 'plugins\sqldrivers\qsqlite.dll') -Destination $sql
    if (-not (Test-Path -LiteralPath (Join-Path $portable 'platforms\qwindows.dll'))) {
        throw 'Missing Qt Windows platform plugin.'
    }
    Set-Content -LiteralPath (Join-Path $portable 'qt.conf') -Value "[Paths]`nPlugins=." -Encoding ascii

    if (-not $RedistDirectory) {
        throw 'Run from a Visual Studio x64 developer shell or supply -RedistDirectory.'
    }
    $crt = Get-ChildItem -LiteralPath (Join-Path $RedistDirectory 'x64') -Directory -Filter 'Microsoft.VC*.CRT' |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $crt) { throw 'Missing Microsoft Visual C++ x64 redistributable runtime.' }
    Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll' | Copy-Item -Destination $portable
    foreach ($runtime in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
        if (-not (Test-Path -LiteralPath (Join-Path $portable $runtime))) {
            throw "Missing $runtime in the application runtime."
        }
    }

    $licenses = (New-Item -ItemType Directory -Path (Join-Path $portable 'Licenses')).FullName
    Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination (Join-Path $licenses 'Orbite-MIT.txt')
    Copy-Item -Path (Join-Path $repo 'licenses\Qt-*.txt') -Destination $licenses
    $qtVersion = (& (Join-Path $qt 'bin\qtpaths.exe') --qt-version).Trim()
    if ($LASTEXITCODE -ne 0 -or $qtVersion -notmatch '^\d+\.\d+\.\d+$') {
        throw 'Could not determine the Qt Base version for the notices.'
    }
    $qtMinor = $qtVersion -replace '\.\d+$', ''
    @"
Orbite dynamically links to Qt Base $qtVersion under LGPL-3.0-only.
Qt is copyright The Qt Company Ltd. and other contributors.
See Licenses/Qt-LGPL-3.0.txt and Licenses/Qt-GPL-3.0.txt.

Corresponding Qt Base source, build files and upstream license notices
https://download.qt.io/archive/qt/$qtMinor/$qtVersion/submodules/qtbase-everywhere-src-$qtVersion.tar.xz
Qt module and third-party attributions
https://doc.qt.io/qt-$qtMinor/licenses-used-in-qt.html

Qt DLLs and plugins are stored alongside Orbite.exe. You can replace them
with compatible builds or rebuild Orbite from source with a compatible Qt SDK.
The Microsoft Visual C++ runtime files are redistributed with the application.
"@ | Set-Content -LiteralPath (Join-Path $portable 'Third-Party-Notices.txt') -Encoding utf8

    $zip = Join-Path $output "$stem.zip"
    Compress-Archive -LiteralPath $portable -DestinationPath $zip -Force
    $inno = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    $iscc = if ($inno) { $inno.Source } else { Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' }
    if (-not (Test-Path -LiteralPath $iscc)) { throw 'Inno Setup 6 was not found.' }
    & $iscc "/DAppVersion=$version" "/DStageDir=$portable" "/O$output" (Join-Path $PSScriptRoot 'orbite-windows.iss')
    $setup = Join-Path $output "$stem-Setup.exe"
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $setup)) {
        throw "Inno Setup build failed ($LASTEXITCODE)."
    }
    foreach ($file in $zip, $setup) {
        $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
        Set-Content -LiteralPath "$file.sha256" -Value "$hash  $(Split-Path -Leaf $file)" -Encoding ascii
    }
} finally {
    $env:Path = $oldPath
    Remove-Item -LiteralPath $stage -Recurse -Force
}
