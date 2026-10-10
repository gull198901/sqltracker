namespace SqlTracker.Windows.ViewModels;

public sealed record TargetProcessViewModel(
    int ProcessId,
    string ProcessName,
    string Bitness,
    string ProgramPath)
{
    public string Header => $"{ProcessId}  {ProcessName}  {Bitness}";

    public string DisplayName => $"{Header}  {ProgramPath}";
}
