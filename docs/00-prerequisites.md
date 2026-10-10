# Prerequisites

## Required Now

Install .NET 8 SDK for the current project target:

- .NET SDK 8.x
- Windows Desktop runtime/targeting pack for .NET 8

The current machine has .NET SDK 10.0.203 installed, but this project targets
`net8.0` and `net8.0-windows`. Without the .NET 8 reference packs, restore fails with
missing `Microsoft.NETCore.App.Ref (= 8.0.x)` and `Microsoft.WindowsDesktop.App.Ref`.

For temporary validation on this machine, the project can be built with the installed
.NET 10 SDK by overriding the target frameworks:

```powershell
.\scripts\build-windows.ps1 -CoreTargetFramework net10.0 -WindowsTargetFramework net10.0-windows
```

## Required For Native Agent Work

Already detected on the current machine:

- Visual Studio Community 18
- MSVC compiler
- MSBuild
- CMake
- Windows SDK 10.0.26100.0

No additional native C++ build tools are currently required.

## PATH

The tools are installed, but the current shell does not have these paths:

```text
C:\Program Files\dotnet\
C:\Program Files\Git\cmd\
```

Add them to the system PATH or start a new terminal after installation.

For native command-line builds, use the Visual Studio developer environment:

```text
C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat
```

## Verified Tool Locations

```text
C:\Program Files\dotnet\dotnet.exe
C:\Program Files\Git\cmd\git.exe
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe
C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64\cl.exe
C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\rc.exe
```
