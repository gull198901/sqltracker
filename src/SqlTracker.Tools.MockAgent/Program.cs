using System.Globalization;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;

var options = MockAgentOptions.Parse(args);
Console.WriteLine($"Connecting to pipe '{options.PipeName}'...");

using var pipe = new NamedPipeClientStream(".", options.PipeName, PipeDirection.Out);
await pipe.ConnectAsync(options.ConnectTimeoutMilliseconds);

await using var writer = new StreamWriter(pipe, new UTF8Encoding(false))
{
    AutoFlush = true
};

var sequence = 0L;
await SendAsync("agent_hello", new
{
    agentKind = "mock-agent",
    agentVersion = "0.1.0",
    bitness = Environment.Is64BitProcess ? 64 : 32
});

for (var i = 1; i <= options.Count; i++)
{
    var statementId = $"0x{i:x8}";
    var elapsed = Math.Round(8 + Random.Shared.NextDouble() * 260, 1);
    var end = DateTimeOffset.Now;

    await SendAsync("prepare", new
    {
        statementId,
        sql = "select * from users where id = :id and status = :status"
    });

    await SendAsync("bind", new
    {
        statementId,
        index = 1,
        name = "id",
        dataType = 2,
        displayValue = (1000 + i).ToString(CultureInfo.InvariantCulture)
    });

    await SendAsync("bind", new
    {
        statementId,
        index = 2,
        name = "status",
        dataType = 1,
        displayValue = "'ACTIVE'"
    });

    await SendAsync("execute_completed", new
    {
        statementId,
        elapsedMilliseconds = elapsed,
        endTime = end
    });

    if (i % 5 == 0)
    {
        await SendAsync("commit_completed", new
        {
            elapsedMilliseconds = Math.Round(1 + Random.Shared.NextDouble() * 5, 1),
            endTime = DateTimeOffset.Now
        });
    }

    await Task.Delay(options.DelayMilliseconds);
}

await SendAsync("agent_info", new
{
    message = "mock agent completed"
});

async Task SendAsync(string type, object payload)
{
    sequence++;
    var envelope = new
    {
        version = 1,
        type,
        processId = options.ProcessId,
        threadId = Environment.CurrentManagedThreadId,
        sequence,
        timestamp = DateTimeOffset.Now,
        payload
    };

    await writer.WriteLineAsync(JsonSerializer.Serialize(envelope));
}

internal sealed record MockAgentOptions(
    string PipeName,
    int ProcessId,
    int Count,
    int DelayMilliseconds,
    int ConnectTimeoutMilliseconds)
{
    public static MockAgentOptions Parse(string[] args)
    {
        var processId = 1234;
        var count = 20;
        var delayMilliseconds = 700;
        var connectTimeoutMilliseconds = 5000;
        string? pipeName = null;

        for (var i = 0; i < args.Length; i++)
        {
            var arg = args[i];
            if (arg is "--pipe" && i + 1 < args.Length)
            {
                pipeName = args[++i];
            }
            else if (arg is "--pid" && i + 1 < args.Length && int.TryParse(args[++i], out var parsedPid))
            {
                processId = parsedPid;
            }
            else if (arg is "--count" && i + 1 < args.Length && int.TryParse(args[++i], out var parsedCount))
            {
                count = parsedCount;
            }
            else if (arg is "--delay" && i + 1 < args.Length && int.TryParse(args[++i], out var parsedDelay))
            {
                delayMilliseconds = parsedDelay;
            }
            else if (arg is "--timeout" && i + 1 < args.Length && int.TryParse(args[++i], out var parsedTimeout))
            {
                connectTimeoutMilliseconds = parsedTimeout;
            }
        }

        pipeName ??= $"global_kingsqltracker-{processId}";
        return new MockAgentOptions(pipeName, processId, count, delayMilliseconds, connectTimeoutMilliseconds);
    }
}

