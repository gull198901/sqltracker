using System.IO;

namespace SqlTracker.Windows.Services;

public static class AgentPathResolver
{
    private const string StructuredOciAgentName = "sqltracker_oci_agent_win32.dll";
    private const string LegacyOciAgentName = "ociagent.dll";

    public static string? FindStructuredOciAgent()
    {
        return FindAgent(StructuredOciAgentName);
    }

    public static string? FindLegacyOciAgent()
    {
        return FindAgent(LegacyOciAgentName);
    }

    private static string? FindAgent(string fileName)
    {
        var candidates = new List<string>
        {
            Path.Combine(AppContext.BaseDirectory, fileName),
            Path.Combine(Environment.CurrentDirectory, fileName)
        };

        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        for (var i = 0; i < 8 && directory is not null; i++, directory = directory.Parent)
        {
            candidates.Add(Path.Combine(directory.FullName, fileName));
            candidates.Add(Path.Combine(directory.FullName, "agents", "win-x86", fileName));
            candidates.Add(Path.Combine(directory.FullName, "artifacts", "native", "win-x86", "Debug", fileName));
            candidates.Add(Path.Combine(directory.FullName, "artifacts", "native", "win-x86", "Release", fileName));
        }

        return candidates
            .Select(Path.GetFullPath)
            .FirstOrDefault(File.Exists);
    }
}
