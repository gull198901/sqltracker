namespace SqlTracker.Windows.Services;

public sealed record OracleProcessInfo(
    int ProcessId,
    string ProcessName,
    string Bitness,
    string OciPath,
    string ExecutablePath);
