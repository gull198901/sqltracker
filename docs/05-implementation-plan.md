# Implementation Plan

## Phase 0 - Repository Setup

Status: started.

- Create product and architecture documents.
- Create core model skeleton.
- Create WPF UI skeleton.
- Preserve legacy binaries.

## Phase 1 - Windows UI Prototype

Deliverable:

- WPF app can show mock realtime SQL blocks.
- Pause, clear, elapsed filter, and keyword filter work against in-memory blocks.
- Output line includes final end time after `costs`.

Validation:

```powershell
dotnet build .\src\SqlTracker.Windows\SqlTracker.Windows.csproj
dotnet run --project .\src\SqlTracker.Windows\SqlTracker.Windows.csproj
```

## Phase 2 - Windows Collector Transport

Status: skeleton started.

Deliverable:

- Named pipe server. Done.
- JSON line protocol reader. Done.
- Structured events assembled into trace blocks. Done.
- Mock sender or test console for transport testing. Done.

## Phase 3 - Oracle OCI Agent Prototype

Deliverable:

- Windows x64 native OCI agent first.
- Hook `OCIStmtPrepare`, `OCIStmtPrepare2`, `OCIBindByName`, `OCIStmtExecute`,
  `OCITransCommit`, `OCITransRollback`.
- Emit structured events.
- Keep target process stable if UI disconnects.

## Phase 4 - Injector

Deliverable:

- Enumerate target processes.
- Detect Oracle client library and bitness.
- Inject matching agent architecture.
- Stop/unload agent safely.

## Phase 5 - Hardening

Deliverable:

- Multi-threaded statement correlation.
- Bounded agent-side queues.
- Hook install/uninstall safety.
- Error reporting.
- Large-output UI performance pass.

## Phase 6 - Linux Planning

Deliverable:

- Decide `LD_PRELOAD` vs uprobes for first Linux mode.
- Define Linux transport.
- Reuse the same event protocol and UI core model.
