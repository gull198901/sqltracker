namespace SqlTracker.Windows.Services;

public sealed record RemoteInjectionHandle(
    int ProcessId,
    string DllPath,
    nint RemoteModuleHandle);

