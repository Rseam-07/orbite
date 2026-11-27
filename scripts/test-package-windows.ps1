param(
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][string]$Version
)

$ErrorActionPreference = 'Stop'
$output = (Resolve-Path -LiteralPath $OutputDirectory).Path
$stem = "Orbite-$Version-Windows-x64"
$stage = (New-Item -ItemType Directory -Path (Join-Path ([IO.Path]::GetTempPath()) "orbite-test-$([guid]::NewGuid())")).FullName
$install = Join-Path $stage 'Installed Orbite'
$oldPath = $env:Path
$qtEnvironment = Get-ChildItem Env: | Where-Object Name -Like 'QT_*'

function Invoke-CheckedProcess($Executable, $ArgumentList) {
    $process = Start-Process -FilePath $Executable -ArgumentList $ArgumentList -WorkingDirectory $stage -PassThru
    if (-not $process.WaitForExit(60000)) {
        Stop-Process -Id $process.Id -Force
        throw "Timed out: $Executable"
    }
    if ($process.ExitCode -ne 0) { throw "$Executable exited with $($process.ExitCode)." }
}

function Test-Orbite($Directory, $Name) {
    foreach ($required in 'platforms\qwindows.dll', 'sqldrivers\qsqlite.dll', 'vcruntime140_1.dll') {
        if (-not (Test-Path -LiteralPath (Join-Path $Directory $required))) {
            throw "$Name is missing $required."
        }
    }
    $report = Join-Path $stage "$Name.json"
    Invoke-CheckedProcess (Join-Path $Directory 'Orbite.exe') @('--verify-package', "`"$report`"")
    $result = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    if ($result.platform -ne 'windows' -or $result.architecture -ne 'x86_64' -or
        $result.version -ne $Version -or -not $result.databaseWritable -or -not $result.windowVisible) {
        throw "The $Name application failed verification: $($result | ConvertTo-Json -Compress)"
    }
    Write-Host "$Name verified: $($result | ConvertTo-Json -Compress)"
}

try {
    $env:Path = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($variable in $qtEnvironment) { Remove-Item "Env:$($variable.Name)" }
    $zip = Join-Path $output "$stem.zip"
    $setup = Join-Path $output "$stem-Setup.exe"
    foreach ($file in $zip, $setup) {
        $expected = ((Get-Content -LiteralPath "$file.sha256" -Raw) -split '\s+')[0]
        if ((Get-FileHash -LiteralPath $file).Hash -ine $expected) {
            throw "Checksum failed for $file."
        }
    }
    Expand-Archive -LiteralPath $zip -DestinationPath $stage
    Test-Orbite (Join-Path $stage 'Orbite') 'portable'
    Invoke-CheckedProcess $setup @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/DIR=`"$install`"")
    Test-Orbite $install 'installed'
} finally {
    try {
        $uninstaller = Join-Path $install 'unins000.exe'
        if (Test-Path -LiteralPath $uninstaller) {
            Invoke-CheckedProcess $uninstaller @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
        }
    } finally {
        $env:Path = $oldPath
        foreach ($variable in $qtEnvironment) { Set-Item "Env:$($variable.Name)" $variable.Value }
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
