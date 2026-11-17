param([string]$Generator = '', [switch]$Run, [switch]$SkipInstall, [switch]$Package)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$buildDirectory = Join-Path $projectRoot 'build'
$executable = Join-Path $buildDirectory 'bin/Release/dr-music.exe'

Push-Location $projectRoot
try {
    if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
        throw 'Node.js is required. Install Node.js 22.12 or newer and reopen PowerShell.'
    }
    if (-not (Get-Command npm.cmd -ErrorAction SilentlyContinue)) {
        throw 'npm is required. Install Node.js with npm and reopen PowerShell.'
    }
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw 'CMake is required. Install CMake 3.25+ and Visual Studio with Desktop development with C++.'
    }
    if (-not (Get-Command vcpkg -ErrorAction SilentlyContinue)) {
        throw 'A bootstrapped vcpkg must be on PATH to build the pinned native audio libraries.'
    }
    if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
        throw 'FFmpeg CLI must be on PATH for generated audio tests. It is not used or shipped at runtime.'
    }

    if (-not $SkipInstall) {
        Write-Host 'Installing locked dependencies...' -ForegroundColor Cyan
        & npm.cmd ci
        if ($LASTEXITCODE) { throw 'Dependency installation failed' }
    }

    Write-Host 'Building DR Music...' -ForegroundColor Cyan
    $configureArguments = @('-S', $projectRoot, '-B', $buildDirectory)
    if ($Generator) { $configureArguments += @('-G', $Generator) }
    if ($Generator -match 'Ninja|Makefiles') { $configureArguments += @('-DCMAKE_BUILD_TYPE=Release') }
    if ($Generator -notmatch 'Ninja|Makefiles') { $configureArguments += @('-A', 'x64') }
    & cmake @configureArguments
    if ($LASTEXITCODE) { throw 'CMake configure failed' }
    & cmake --build $buildDirectory --config Release --parallel
    if ($LASTEXITCODE) { throw 'Build failed' }

    Write-Host 'Running tests...' -ForegroundColor Cyan
    & ctest --test-dir $buildDirectory -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed' }
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Build output not found: $executable"
    }

    Write-Host "Build succeeded: $executable" -ForegroundColor Green
    if ($Package) {
        $packageDirectory = Join-Path $projectRoot 'release/DR-Music-native'
        & cmake --install $buildDirectory --config Release --component Runtime --prefix $packageDirectory
        if ($LASTEXITCODE) { throw 'Packaging failed' }
        Compress-Archive -Path (Join-Path $packageDirectory '*') -DestinationPath (Join-Path $projectRoot 'release/DR-Music-0.3.0-Windows-x64.zip') -Force
    }
    if ($Run) {
        Start-Process -FilePath $executable -WorkingDirectory (Split-Path $executable -Parent) -WindowStyle Normal
    }
} finally {
    Pop-Location
}
