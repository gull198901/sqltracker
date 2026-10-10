using System.ComponentModel;
using System.Diagnostics;
using SqlTracker.Core.Rendering;
using SqlTracker.Core.Tracing;
using SqlTracker.Core.Transport;
using SqlTracker.Windows.Services;

const string StructuredPipeNamePrefix = "sqltracker-oci";

var options = TestOptions.Parse(args);
if (options.ShowHelp)
{
    PrintUsage();
    return 0;
}

var scanner = new OracleProcessScanner();
var processes = scanner.FindOracleProcesses();

Console.WriteLine("Oracle OCI processes:");
if (processes.Count == 0)
{
    Console.WriteLine("  <none>");
    return 2;
}

foreach (var process in processes)
{
    Console.WriteLine($"  PID={process.ProcessId} {process.ProcessName} {process.Bitness} OCI={process.OciPath}");
}

if (options.ListOnly)
{
    return 0;
}

var target = SelectTarget(processes, options.ProcessId);
if (target is null)
{
    Console.Error.WriteLine(options.ProcessId is null
        ? "No x86 Oracle OCI process was found."
        : $"PID {options.ProcessId.Value} was not found in the Oracle OCI process list.");
    return 3;
}

if (!string.Equals(target.Bitness, "x86", StringComparison.OrdinalIgnoreCase))
{
    Console.Error.WriteLine($"PID {target.ProcessId} is {target.Bitness}; sqltracker_oci_agent_win32.dll is x86 only.");
    return 4;
}

var agentPath = AgentPathResolver.FindStructuredOciAgent();
if (agentPath is null)
{
    Console.Error.WriteLine("sqltracker_oci_agent_win32.dll was not found. Build the native agent first.");
    return 5;
}

Console.WriteLine($"Target: PID={target.ProcessId} {target.ProcessName}");
Console.WriteLine($"Agent : {agentPath}");
var pipeName = $"{StructuredPipeNamePrefix}-{target.ProcessId}";
Console.WriteLine($"Pipe  : {pipeName}");
Console.WriteLine($"Wait  : {options.WaitSeconds}s");

using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(options.WaitSeconds));
using var stop = CancellationTokenSource.CreateLinkedTokenSource(timeout.Token);

var renderer = new LegacyTraceRenderer();
var receivedBlocks = 0;
var receiver = new NamedPipeTraceReceiver(pipeName);
var receiverTask = Task.Run(async () =>
{
    try
    {
        await receiver.RunAsync((block, _) =>
        {
            if (block.Kind == TraceBlockKind.Info)
            {
                Console.WriteLine($"[info] {block.Sql}");
            }
            else
            {
                Interlocked.Increment(ref receivedBlocks);
                Console.Write(renderer.Render(block));
            }

            return ValueTask.CompletedTask;
        }, stop.Token).ConfigureAwait(false);
    }
    catch (OperationCanceledException)
    {
    }
}, CancellationToken.None);

Console.CancelKeyPress += (_, eventArgs) =>
{
    eventArgs.Cancel = true;
    stop.Cancel();
};

RemoteInjectionHandle? injection = null;
try
{
    await Task.Delay(250, CancellationToken.None).ConfigureAwait(false);

    var injector = new WindowsDllInjector();
    injection = injector.Inject(target.ProcessId, agentPath);
    Console.WriteLine($"Injected. Remote module handle: 0x{injection.RemoteModuleHandle.ToInt64():X}");
    Console.WriteLine("Execute SQL in the target process now. Waiting for structured SQL trace...");

    while (!stop.IsCancellationRequested)
    {
        if (Volatile.Read(ref receivedBlocks) > 0)
        {
            await Task.Delay(TimeSpan.FromSeconds(2), CancellationToken.None).ConfigureAwait(false);
            break;
        }

        if (!IsProcessRunning(target.ProcessId))
        {
            Console.Error.WriteLine("Target process exited.");
            return 6;
        }

        await Task.Delay(500, CancellationToken.None).ConfigureAwait(false);
    }

    if (receivedBlocks == 0)
    {
        Console.Error.WriteLine("No structured SQL trace was received before timeout.");
        return 7;
    }

    Console.WriteLine($"Received {receivedBlocks} trace block(s).");
    return 0;
}
catch (Win32Exception ex)
{
    Console.Error.WriteLine($"{ex.Message} Win32Error={ex.NativeErrorCode}");
    return 8;
}
finally
{
    stop.Cancel();

    if (injection is not null && !options.KeepInjected)
    {
        try
        {
            new WindowsDllInjector().Uninject(injection);
            Console.WriteLine("Uninjected.");
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Uninject failed: {ex.Message}");
        }
    }

    try
    {
        await receiverTask.ConfigureAwait(false);
    }
    catch
    {
    }
}

static OracleProcessInfo? SelectTarget(IReadOnlyList<OracleProcessInfo> processes, int? processId)
{
    if (processId is not null)
    {
        return processes.FirstOrDefault(process => process.ProcessId == processId.Value);
    }

    return processes.FirstOrDefault(process =>
        string.Equals(process.Bitness, "x86", StringComparison.OrdinalIgnoreCase));
}

static bool IsProcessRunning(int processId)
{
    try
    {
        using var process = Process.GetProcessById(processId);
        return !process.HasExited;
    }
    catch
    {
        return false;
    }
}

static void PrintUsage()
{
    Console.WriteLine("Usage:");
    Console.WriteLine("  SqlTracker.Tools.RealInjectTest.exe --list");
    Console.WriteLine("  SqlTracker.Tools.RealInjectTest.exe [--pid <pid>] [--wait <seconds>] [--keep-injected]");
}

internal sealed record TestOptions(
    int? ProcessId,
    int WaitSeconds,
    bool KeepInjected,
    bool ListOnly,
    bool ShowHelp)
{
    public static TestOptions Parse(string[] args)
    {
        int? processId = null;
        var waitSeconds = 90;
        var keepInjected = false;
        var listOnly = false;
        var showHelp = false;

        for (var i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "-h":
                case "--help":
                    showHelp = true;
                    break;
                case "--pid" when i + 1 < args.Length && int.TryParse(args[i + 1], out var parsedPid):
                    processId = parsedPid;
                    i++;
                    break;
                case "--wait" when i + 1 < args.Length && int.TryParse(args[i + 1], out var parsedWait):
                    waitSeconds = Math.Max(1, parsedWait);
                    i++;
                    break;
                case "--keep-injected":
                    keepInjected = true;
                    break;
                case "--list":
                    listOnly = true;
                    break;
                default:
                    throw new ArgumentException($"Unknown or incomplete argument: {args[i]}");
            }
        }

        return new TestOptions(processId, waitSeconds, keepInjected, listOnly, showHelp);
    }
}
