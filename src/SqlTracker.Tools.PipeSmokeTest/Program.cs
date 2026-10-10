using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using SqlTracker.Core.Rendering;
using SqlTracker.Core.Tracing;
using SqlTracker.Core.Transport;

var pipeName = $"sqltracker-smoke-{Guid.NewGuid():N}";
using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
using var receiverStop = new CancellationTokenSource();
using var linked = CancellationTokenSource.CreateLinkedTokenSource(timeout.Token, receiverStop.Token);

var blocks = new List<SqlTraceBlock>();
var completed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
var receiver = new NamedPipeTraceReceiver(pipeName);

var receiverTask = Task.Run(async () =>
{
    try
    {
        await receiver.RunAsync((block, _) =>
        {
            blocks.Add(block);
            if (block.Kind == TraceBlockKind.Sql)
            {
                completed.TrySetResult();
            }

            return ValueTask.CompletedTask;
        }, linked.Token);
    }
    catch (OperationCanceledException)
    {
    }
});

await Task.Delay(250, timeout.Token);

await using (var client = new NamedPipeClientStream(".", pipeName, PipeDirection.Out))
{
    await client.ConnectAsync(timeout.Token);
    await using var writer = new StreamWriter(client, new UTF8Encoding(false))
    {
        AutoFlush = true
    };

    await SendAsync(writer, "prepare", 1, new
    {
        statementId = "0x1",
        sql = "select * from users where id = :id"
    });
    await SendAsync(writer, "bind", 2, new
    {
        statementId = "0x1",
        index = 1,
        name = "id",
        dataType = 2,
        displayValue = "1001"
    });
    await SendAsync(writer, "execute_completed", 3, new
    {
        statementId = "0x1",
        elapsedMilliseconds = 25.0,
        endTime = DateTimeOffset.Now
    });

    await completed.Task.WaitAsync(timeout.Token);
}

receiverStop.Cancel();
await receiverTask.WaitAsync(TimeSpan.FromSeconds(3));

var sqlBlock = blocks.FirstOrDefault(block => block.Kind == TraceBlockKind.Sql);
if (sqlBlock is null || sqlBlock.Binds.Count != 1 || sqlBlock.ElapsedMilliseconds != 25.0)
{
    Console.Error.WriteLine("Pipe smoke test failed.");
    Environment.ExitCode = 1;
    return;
}

Console.WriteLine(new LegacyTraceRenderer().Render(sqlBlock));

static async Task SendAsync(StreamWriter writer, string type, long sequence, object payload)
{
    var envelope = new
    {
        version = 1,
        type,
        processId = 1234,
        threadId = Environment.CurrentManagedThreadId,
        sequence,
        timestamp = DateTimeOffset.Now,
        payload
    };

    await writer.WriteLineAsync(JsonSerializer.Serialize(envelope));
}
