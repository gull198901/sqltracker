# SqlTracker Next

SqlTracker Next is a rewrite of the existing Windows Oracle OCI SQL tracker. The
legacy binaries are reference artifacts only:

- `sqltracker2.exe`: Windows 32-bit .NET Framework UI and injector.
- `ociagent.dll`: Windows 32-bit native OCI hook agent.

The new design keeps the legacy realtime streaming output style, while separating the
collector UI, structured trace model, Windows agent, and future Linux/database-specific
agents.

## First Target

- Windows desktop UI: WPF on .NET 8, session-only in-memory data.
- Main view: realtime streaming SQL output, not one row per SQL.
- Output format: legacy-style SQL block with elapsed time and final end timestamp.
- Filter: minimum elapsed time, SQL keyword, pause output, clear output.
- Agent direction: Oracle OCI hook first, Windows x86/x64 first, Linux later.

## Documents

- [Prerequisites](docs/00-prerequisites.md)
- [Product Spec](docs/01-product-spec.md)
- [Architecture](docs/02-architecture.md)
- [Event Protocol](docs/03-event-protocol.md)
- [Windows UI Spec](docs/04-windows-ui-spec.md)
- [Implementation Plan](docs/05-implementation-plan.md)
- [Oracle OCI Hook Spec](docs/06-oracle-oci-hook-spec.md)
- [中文功能说明](docs/07-user-guide-zh.md)

## Current Status

This repository contains the WPF collector, shared trace model, test tools, and a
first native Windows x86 OCI agent.

```powershell
.\scripts\build-windows.ps1
```

The build output is written to:

```text
artifacts\bin\SqlTracker.Windows\debug\SqlTracker.Windows.exe
```

If only .NET 10 SDK is installed, use this temporary validation command:

```powershell
.\scripts\build-windows.ps1 -CoreTargetFramework net10.0 -WindowsTargetFramework net10.0-windows
```

## Run UI

```powershell
.\artifacts\bin\SqlTracker.Windows\debug\SqlTracker.Windows.exe
```

## Publish Portable Windows x86 Package

For copying to another computer, build a self-contained Windows x86 package:

```powershell
.\scripts\publish-windows-x86.ps1
```

Output:

```text
dist\SqlTracker.Windows-win-x86-self-contained\
dist\SqlTracker.Windows-win-x86-self-contained.zip
```

The self-contained package includes the .NET runtime and automatically copies
`sqltracker_oci_agent_win32.dll` next to `SqlTracker.Windows.exe`. The target
computer still needs a 32-bit Oracle client process with `oci.dll` loaded.

If you prefer a smaller package and can install .NET Desktop Runtime x86 on the
target computer, use:

```powershell
.\scripts\publish-windows-x86.ps1 -FrameworkDependent
```

## Real Oracle Injection

The current real-process path uses the rewritten structured 32-bit agent:

```text
artifacts\native\win-x86\Debug\sqltracker_oci_agent_win32.dll
```

Requirements:

- Target Oracle client process must be 32-bit.
- Target process must have loaded `oci.dll`.
- Run the UI with enough permission to open the target process.

Steps:

1. Start the Oracle client application.
2. Open `SqlTracker.Windows.exe`.
3. Click `Refresh`.
4. Select a real process from the left list.
5. Click `Start`.

The UI creates `sqltracker-oci-{pid}`, injects the structured agent DLL, receives
newline-delimited JSON events, and renders SQL blocks with `costs` and `end`.

64-bit Oracle clients are intentionally rejected until a new x64 agent is built.

## Real Injection Smoke Test

```powershell
.\artifacts-net10\bin\SqlTracker.Tools.RealInjectTest\debug\SqlTracker.Tools.RealInjectTest.exe --list
.\artifacts-net10\bin\SqlTracker.Tools.RealInjectTest\debug\SqlTracker.Tools.RealInjectTest.exe --pid <pid> --wait 120
```

## Protocol Smoke Test

The JSON-line receiver/protocol chain can be tested without the UI:

```powershell
.\artifacts\bin\SqlTracker.Tools.PipeSmokeTest\debug\SqlTracker.Tools.PipeSmokeTest.exe
```

The standalone mock JSON sender remains available for transport experiments:

```powershell
.\artifacts\bin\SqlTracker.Tools.MockAgent\debug\SqlTracker.Tools.MockAgent.exe --pid 1234
```

Expected output is a complete legacy-style SQL block with bind values, `costs`, and
`end`.
