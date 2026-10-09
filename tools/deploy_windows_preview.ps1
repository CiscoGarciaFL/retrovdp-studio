param(
    [switch]$SkipBuild,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$cmake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$deployQt = 'C:\Qt\6.10.3\mingw_64\bin\windeployqt.exe'
$env:PATH = 'C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.10.3\mingw_64\bin;' `
    + $env:PATH
$preset = "windows-mingw-$($Configuration.ToLowerInvariant())"
$buildDirectory = Join-Path $projectRoot "build/$preset"
$executable = Join-Path $projectRoot "build/$preset/bin/RetroVDPStudio.exe"
$qmlDirectory = Join-Path $projectRoot 'app/qml'

function Get-NormalizedPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return '' }
    return [IO.Path]::GetFullPath($Path).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
}

function Get-CMakeCacheValue([string]$CachePath, [string]$Key) {
    $prefix = "$Key="
    $entry = Get-Content -LiteralPath $CachePath | Where-Object {
        $_.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
    } | Select-Object -First 1
    if ($null -eq $entry) { return '' }
    return $entry.Substring($prefix.Length)
}

foreach ($requiredTool in @($cmake, $deployQt)) {
    if (-not (Test-Path -LiteralPath $requiredTool -PathType Leaf)) {
        throw "Required Qt development tool was not found: $requiredTool"
    }
}

if (-not $SkipBuild) {
    $configureArguments = @('--preset', $preset)
    $cachePath = Join-Path $buildDirectory 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
        $cachedSource = Get-CMakeCacheValue `
            $cachePath 'CMAKE_HOME_DIRECTORY:INTERNAL'
        $cachedBuild = Get-CMakeCacheValue `
            $cachePath 'CMAKE_CACHEFILE_DIR:INTERNAL'
        $sourceWasRelocated = (Get-NormalizedPath $cachedSource) -ne `
            (Get-NormalizedPath $projectRoot)
        $buildWasRelocated = (Get-NormalizedPath $cachedBuild) -ne `
            (Get-NormalizedPath $buildDirectory)
        if ($sourceWasRelocated -or $buildWasRelocated) {
            Write-Host 'Refreshing CMake cache after repository relocation.'
            $configureArguments = @('--fresh', '--preset', $preset)
        }
    }

    & $cmake @configureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "The Windows preview configure failed with exit code $LASTEXITCODE."
    }

    & $cmake --build --preset $preset --target RetroVDPStudio
    if ($LASTEXITCODE -ne 0) {
        throw "The Windows preview build failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "RetroVDPStudio.exe was not built. Configure the $preset preset first."
}

# The Qt online installer supplies release runtime DLLs for this MinGW kit even
# when our application has Debug symbols, so force the matching release runtime
# names (Qt6Core.dll, qwindows.dll, and friends).
& $deployQt `
    --release `
    --compiler-runtime `
    --qmldir $qmlDirectory `
    $executable
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE."
}

$runtime = Join-Path (Split-Path -Parent $executable) 'Qt6Core.dll'
if (-not (Test-Path -LiteralPath $runtime -PathType Leaf)) {
    throw 'Deployment completed without producing Qt6Core.dll.'
}

Write-Host "Runnable Windows folder: $(Split-Path -Parent $executable)"
