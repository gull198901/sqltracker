# Event Protocol

## Purpose

Agents should send structured events to the collector. The UI may render them in the
legacy stream style, but protocol payloads should not be pre-rendered text.

## Message Envelope

Initial transport can use newline-delimited JSON for readability:

```json
{
  "version": 1,
  "type": "execute_completed",
  "processId": 1234,
  "threadId": 5678,
  "sequence": 42,
  "timestamp": "2026-05-09T10:32:18.148+08:00",
  "payload": {}
}
```

Later versions can switch to MessagePack or protobuf without changing the UI model.

## Windows Pipe Name

The collector creates one server pipe per target process:

```text
sqltracker-oci-{pid}
```

The agent connects as a client and writes UTF-8 newline-delimited JSON. UI status
messages such as injection success stay outside the trace output stream.

## Event Types

### agent_hello

Sent when the agent starts.

```json
{
  "agentKind": "oracle-oci",
  "agentVersion": "0.1.0",
  "processId": 1234,
  "bitness": 64
}
```

### prepare

Captures SQL associated with a statement handle.

```json
{
  "statementId": "0x12345678",
  "sql": "select * from users where id = :id"
}
```

### bind

Captures a bind value associated with a statement handle.

```json
{
  "statementId": "0x12345678",
  "index": 1,
  "name": "id",
  "dataType": 2,
  "displayValue": "1001"
}
```

### execute_completed

Completes a SQL block.

```json
{
  "statementId": "0x12345678",
  "elapsedMilliseconds": 25.0,
  "endTime": "2026-05-09T10:32:18.148+08:00",
  "status": 0,
  "errorCode": null,
  "errorMessage": null
}
```

### commit_completed

```json
{
  "elapsedMilliseconds": 2.0,
  "endTime": "2026-05-09T10:33:01.004+08:00",
  "status": 0
}
```

### rollback_completed

```json
{
  "elapsedMilliseconds": 2.0,
  "endTime": "2026-05-09T10:33:01.004+08:00",
  "status": 0
}
```

### agent_info

For informational messages such as disconnect or hook install status.

## Trace Block Assembly

The collector assembles events by `statementId`.

Rules:

- `prepare` stores or replaces SQL text for the statement.
- `bind` stores bind values for the statement.
- `execute_completed` renders a completed SQL block.
- `commit_completed` and `rollback_completed` render standalone blocks.
- If SQL is missing, render `[SQL unavailable]`.
- If bind type is unsupported, render `[dty:N]`.

## Ordering

Each agent must include a monotonically increasing `sequence` per process.

The collector should preserve per-process sequence order where possible. Cross-process
ordering can use event timestamp.
