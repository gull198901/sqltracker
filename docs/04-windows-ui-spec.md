# Windows UI Spec

## Technology

First version:

- WPF
- .NET 8
- MVVM-style structure
- In-memory session store

The initial skeleton uses built-in WPF controls to avoid external dependencies. A later
UI pass should replace the output text box with AvalonEdit for better large-text
performance and SQL highlighting.

## Layout

```text
Toolbar:
  Refresh | Start | Pause Output | Stop | Clear | Copy | Export
  costs >= [    ] ms
  SQL keyword [                 ]

Left:
  Target process list

Right:
  Realtime stream output
```

## Process List

Columns or list item text should include:

- pid
- process name
- bitness
- detected client library path
- monitor state

First implementation can start with a simple list and later move to a richer grid.

## Realtime Output

The output control appends completed blocks in capture order.

Required behavior:

- Auto-scroll to bottom by default.
- Pause output stops UI appending but does not stop collection.
- Resume output rebuilds visible output from retained in-memory blocks.
- Clear removes retained blocks and visible output.
- Filtering always renders complete blocks.

## Filter Behavior

`costs >= N ms`:

- Empty value means no elapsed filter.
- Invalid value should not crash the UI.
- Filter changes rebuild the visible stream from retained blocks.

`SQL keyword`:

- Case-insensitive.
- Matches SQL text.
- Does not need regex in first version.

## Export

First version exports the currently visible text stream.

Later export formats:

- full session text;
- filtered text;
- JSON trace blocks;
- CSV summary.

## Visual Tone

This is an operational diagnostic tool. It should be compact, readable, and optimized
for scanning live output. Avoid marketing-style layout.

