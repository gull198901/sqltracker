# Product Spec

## Goal

Build a new non-invasive SQL tracking tool that monitors database client calls and
shows SQL text, bind variables, elapsed time, and execution end time in realtime.

The first implementation targets Oracle OCI clients on Windows. The design must allow
Linux support and other database client libraries later.

## Legacy Behavior To Preserve

The old tool is useful because SQL output appears immediately as a live text stream.
The new UI must keep that behavior as the primary workflow.

The main screen is not a table where each SQL execution is a row. The main screen is a
streaming text output window that appends SQL blocks as they are captured.

## First-Version Features

- Select a target process that has loaded Oracle `oci.dll`.
- Start monitoring the selected process.
- Display realtime SQL trace output in legacy-style text blocks.
- Show SQL text, bind values, elapsed time, and execution end timestamp.
- Pause UI output without stopping underlying collection.
- Clear the current session output.
- Filter/search by elapsed time.
- Search by SQL keyword.
- Export current visible output as text.
- Keep data in memory only.

## Non-Goals For First Version

- No historical database cache.
- No persistent trace store.
- No dashboard-style table as the main view.
- No cross-database implementation in the first milestone.
- No Linux implementation in the first milestone, but the architecture must leave room
  for it.

## Output Format

Normal SQL:

```text
-----------------------------------------------------
select * from users where id = :id
[1] id=1001
costs:[25.0] ms  end:[2026-05-09 10:32:18.148]
```

Commit:

```text
-----------------------------------------------------
Commit;
costs:[2.0] ms  end:[2026-05-09 10:33:01.004]
```

Rollback:

```text
-----------------------------------------------------
Rollback;
costs:[2.0] ms  end:[2026-05-09 10:33:01.004]
```

Unknown bind type:

```text
[3] amount=[dty:187]
```

## Filtering

The first filter set is intentionally small:

- `costs >= N ms`
- SQL keyword contains text
- bind value contains text, later milestone

Filtering must show complete SQL blocks. It must never show only the `costs` line.

## Memory Policy

The UI stores trace blocks in memory for the current session only.

Default limits:

- Maximum retained blocks: 100000
- When the limit is reached, discard the oldest blocks.
- `Clear` removes retained blocks and visible output.

## Acceptance Criteria

- User can select a target process and start monitoring.
- SQL appears as realtime text blocks.
- Completed SQL blocks include `costs` and `end`.
- `costs >= N ms` filters complete blocks.
- `Clear` removes current output immediately.
- Closing the app does not persist history.

