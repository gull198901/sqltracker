# Windows x86 Agents

The main Windows x86 Oracle agent is now built from source:

```text
src\SqlTracker.Agent.Oci.Win32
```

Build output:

```text
artifacts\native\win-x86\Debug\sqltracker_oci_agent_win32.dll
```

The old `ociagent.dll` is no longer the primary runtime path. Keep it only as a
legacy comparison artifact if needed.
