param(
    [string]$Configuration = "Debug",
    [string]$CoreTargetFramework = "",
    [string]$WindowsTargetFramework = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$dotnet = Join-Path $env:ProgramFiles "dotnet\dotnet.exe"

if (-not (Test-Path $dotnet)) {
    throw "dotnet.exe was not found at $dotnet. Install .NET 8 SDK and add dotnet to PATH."
}

$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = "1"
$env:DOTNET_CLI_TELEMETRY_OPTOUT = "1"
$env:DOTNET_CLI_HOME = Join-Path $repoRoot ".dotnet-home"
$env:NUGET_PACKAGES = Join-Path $repoRoot ".nuget-packages"
$env:APPDATA = Join-Path $repoRoot ".appdata"
$env:LOCALAPPDATA = Join-Path $repoRoot ".localappdata"

$properties = @()
if ($CoreTargetFramework) {
    $properties += "-p:SqlTrackerCoreTargetFramework=$CoreTargetFramework"
}

if ($WindowsTargetFramework) {
    $properties += "-p:SqlTrackerWindowsTargetFramework=$WindowsTargetFramework"
}

$projects = @(
    "src\SqlTracker.Windows\SqlTracker.Windows.csproj",
    "src\SqlTracker.Tools.MockAgent\SqlTracker.Tools.MockAgent.csproj",
    "src\SqlTracker.Tools.PipeSmokeTest\SqlTracker.Tools.PipeSmokeTest.csproj",
    "src\SqlTracker.Tools.RealInjectTest\SqlTracker.Tools.RealInjectTest.csproj"
)

foreach ($project in $projects) {
    & $dotnet build (Join-Path $repoRoot $project) `
        --configuration $Configuration `
        --configfile (Join-Path $repoRoot "NuGet.Config") `
        --artifacts-path (Join-Path $repoRoot "artifacts") `
        @properties

    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$msbuildCandidates = @(
    "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
)

$msbuild = $msbuildCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($msbuild) {
    & $msbuild (Join-Path $repoRoot "src\SqlTracker.Agent.Oci.Win32\SqlTracker.Agent.Oci.Win32.vcxproj") `
        /p:Configuration=$Configuration `
        /p:Platform=Win32 `
        /m

    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
else {
    Write-Warning "MSBuild.exe was not found. Skipping native Windows OCI agent build."
}
