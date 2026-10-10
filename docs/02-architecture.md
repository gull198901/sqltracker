# Architecture

## New Design

The new design keeps the user-facing realtime stream, but separates internal concerns:

```text
Target process
  |
  | database client calls
  v
Agent plugin
  |
  | structured trace events
  v
Collector transport
  |
  | structured trace blocks
  v
Windows UI
  |
  | legacy stream renderer
  v
Realtime output window
```

## Components

### Agent

Runs inside or beside the target process and observes database client calls.

First agent:

- Windows Oracle OCI x86 agent: `sqltracker_oci_agent_win32.dll`.
- Current hook strategy: patch target module import address tables for OCI calls.
- x64 is planned as a separate native build.

Future agents:

- Linux Oracle OCI via `LD_PRELOAD` or uprobes.
- MySQL client library.
- PostgreSQL libpq.
- ODBC.

### Transport

Transfers structured events to the UI process.

Initial Windows transport:

- Named pipe per target process.
- Pipe name: `sqltracker-oci-{pid}`.
- Payload: newline-delimited JSON using the event protocol.

Future Linux transport:

- Unix domain socket.
- Optional local TCP for remote collector scenarios.

### Core Model

Normalizes agent events into trace blocks:

- SQL text.
- bind list.
- elapsed milliseconds.
- end timestamp.
- status/error.

### UI

The UI renders trace blocks into legacy stream text. It also owns filtering, pause,
clear, and export behavior.

## Why Structured Internals

The UI must look like the old realtime stream, but raw text is not enough for filtering
and future database support. Structured internals allow:

- complete-block elapsed-time filtering;
- keyword filtering without parsing rendered text;
- database-specific agents behind one UI;
- later export to JSON if needed.

## Threading Model

- Transport reader runs in a background task.
- Incoming events are added to a bounded in-memory session.
- UI refresh is batched.
- The output window is updated on the UI thread only.

## Reliability Requirements

- Agent failure must not crash the target application.
- UI failure must not leave a permanent hook in the target process.
- Hook install/uninstall must be idempotent.
- Agent must avoid blocking OCI calls on slow UI rendering.
- Transport writes must be bounded or buffered.
