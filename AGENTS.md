# Agent Handoff

## Scope

This repository contains legacy SqlTracker binaries and the new SqlTracker Next rewrite
plan. Do not modify or replace the legacy binaries unless the user explicitly asks.

## Project Layout

- `docs/`: product, architecture, protocol, UI, and hook specifications.
- `src/SqlTracker.Core/`: shared trace model, filter, and legacy stream renderer.
- `src/SqlTracker.Windows/`: WPF desktop UI skeleton.
- `sqltracker2.exe`, `ociagent.dll`: legacy reference binaries.

## Engineering Rules

- Keep the UI as realtime streaming text output by default.
- Keep all trace data session-only in memory unless the user later asks for persistence.
- Preserve the final output line shape:

```text
costs:[25.0] ms  end:[2026-05-09 10:32:18.148]
```

- Internally prefer structured events and trace blocks even when rendering legacy text.
- Keep Oracle OCI support isolated so Linux and other database agents can be added later.

## Validation Commands

When .NET SDK is available:

```powershell
.\scripts\build-windows.ps1
```

The current workspace may not have .NET SDK installed, so document any skipped validation.
