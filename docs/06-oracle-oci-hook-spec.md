# Oracle OCI Hook Spec

## First Hook Set

Required hooks:

- `OCIStmtPrepare`
- `OCIStmtPrepare2`
- `OCIBindByName`
- `OCIBindByName2`
- `OCIBindByPos`
- `OCIBindByPos2`
- `OCIStmtExecute`
- `OCITransCommit`
- `OCITransRollback`

Useful helper calls:

- `OCIErrorGet`
- `OCINumberToReal`
- `OCINumberToInt`
- `OCINumberSign`

## Statement Context

The agent should keep a map keyed by statement handle.

Stored data:

- statement handle
- SQL text
- bind list
- last prepare time, optional
- last thread id, optional

## Execute Timing

The user-facing output should not include start time.

Implementation:

- On `OCIStmtExecute` entry: record monotonic start tick internally.
- Call original OCI function.
- On return: compute elapsed milliseconds.
- Capture local end timestamp.
- Emit `execute_completed`.

Rendered output:

```text
costs:[25.0] ms  end:[2026-05-09 10:32:18.148]
```

## Bind Capture

Capture at minimum:

- bind index or position;
- bind name when available;
- OCI data type;
- display value where safely readable.

Unsupported types should render as:

```text
[dty:N]
```

## Safety Requirements

- Do not block the target process on slow UI reads.
- Use bounded queues.
- Avoid heap-heavy work inside hot OCI paths where possible.
- If transport is disconnected, continue original OCI calls and drop trace output.
- Hook uninstall must restore original bytes exactly.
- Avoid unsafe thread termination during unload.

## Windows Implementation Notes

The first rewritten x86 agent uses import address table patching. This covers common
clients that directly import `OCI.dll`, including SQLPlus. A later x86/x64 hardening
pass should add a maintained trampoline layer or `GetProcAddress` interception for
clients that resolve OCI dynamically.

The agent must be built separately for x86 and x64.
