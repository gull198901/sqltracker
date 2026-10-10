param(
    [string]$Configuration = "Release",
    [string]$CoreTargetFramework = "net10.0",
    [string]$WindowsTargetFramework = "net10.0-windows",
    [switch]$FrameworkDependent,
    [switch]$NoZip
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$dotnet = Join-Path $env:ProgramFiles "dotnet\dotnet.exe"

if (-not (Test-Path $dotnet)) {
    throw "dotnet.exe was not found at $dotnet. Install the .NET SDK on the build machine."
}

$msbuildCandidates = @(
    "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
)

$msbuild = $msbuildCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $msbuild) {
    throw "MSBuild.exe was not found. Install Visual Studio Build Tools with C++ desktop workload."
}

$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = "1"
$env:DOTNET_CLI_TELEMETRY_OPTOUT = "1"
$env:DOTNET_CLI_HOME = Join-Path $repoRoot ".dotnet-home"
$env:NUGET_PACKAGES = Join-Path $repoRoot ".nuget-packages"
$env:APPDATA = Join-Path $repoRoot ".appdata"
$env:LOCALAPPDATA = Join-Path $repoRoot ".localappdata"

$distRoot = Join-Path $repoRoot "dist"
$packageName = if ($FrameworkDependent) { "SqlTracker.Windows-win-x86-framework-dependent" } else { "SqlTracker.Windows-win-x86-self-contained" }
$publishDir = Join-Path $distRoot $packageName
$zipPath = "$publishDir.zip"

New-Item -ItemType Directory -Force -Path $distRoot | Out-Null

$fullDistRoot = [System.IO.Path]::GetFullPath($distRoot)
$fullPublishDir = [System.IO.Path]::GetFullPath($publishDir)
if (-not $fullPublishDir.StartsWith($fullDistRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to publish outside dist: $fullPublishDir"
}

if (Test-Path $fullPublishDir) {
    Remove-Item -LiteralPath $fullPublishDir -Recurse -Force
}

if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

& $msbuild (Join-Path $repoRoot "src\SqlTracker.Agent.Oci.Win32\SqlTracker.Agent.Oci.Win32.vcxproj") `
    /p:Configuration=$Configuration `
    /p:Platform=Win32 `
    /m

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$selfContainedValue = if ($FrameworkDependent) { "false" } else { "true" }

& $dotnet publish (Join-Path $repoRoot "src\SqlTracker.Windows\SqlTracker.Windows.csproj") `
    --configuration $Configuration `
    --runtime win-x86 `
    --self-contained $selfContainedValue `
    --configfile (Join-Path $repoRoot "NuGet.Publish.Config") `
    --artifacts-path (Join-Path $repoRoot "artifacts-publish") `
    --output $fullPublishDir `
    -p:SqlTrackerCoreTargetFramework=$CoreTargetFramework `
    -p:SqlTrackerWindowsTargetFramework=$WindowsTargetFramework `
    -p:PublishSingleFile=false `
    -p:PublishTrimmed=false `
    -p:DebugType=none `
    -p:DebugSymbols=false

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$agentPath = Join-Path $repoRoot "artifacts\native\win-x86\$Configuration\sqltracker_oci_agent_win32.dll"
if (-not (Test-Path $agentPath)) {
    throw "Native agent was not found: $agentPath"
}

Copy-Item -LiteralPath $agentPath -Destination (Join-Path $fullPublishDir "sqltracker_oci_agent_win32.dll") -Force

$runtimeRequirement = if ($FrameworkDependent) {
    ".NET Desktop Runtime x86 matching $WindowsTargetFramework is required on the target computer."
} else {
    ".NET Runtime is included in this package. The target computer does not need a separate .NET install."
}

$readme = @"
SqlTracker Windows x86
======================

Run:
  SqlTracker.Windows.exe

Target computer requirements:
  - Windows 10/11.
  - 32-bit Oracle client process with oci.dll loaded.
  - Run SqlTracker as Administrator if the target process is elevated.
  - $runtimeRequirement

Current limitation:
  - This package tracks 32-bit Oracle OCI processes only.
  - 64-bit Oracle clients, Linux, and other database agents are later milestones.

Main files:
  - SqlTracker.Windows.exe
  - SqlTracker.Core.dll
  - sqltracker_oci_agent_win32.dll
"@

Set-Content -LiteralPath (Join-Path $fullPublishDir "README-RUN.txt") -Value $readme -Encoding UTF8

if (-not $NoZip) {
    Compress-Archive -Path (Join-Path $fullPublishDir "*") -DestinationPath $zipPath -Force
}

Write-Host "Publish directory: $fullPublishDir"
if (-not $NoZip) {
    Write-Host "Zip package: $zipPath"
}
