using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using SqlTracker.Core.Protocol;
using SqlTracker.Core.Tracing;

namespace SqlTracker.Core.Transport;

public sealed class NamedPipeTraceReceiver
{
    private readonly string _pipeName;
    private readonly TraceEventAssembler _assembler = new();
    private readonly JsonSerializerOptions _jsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    public NamedPipeTraceReceiver(string pipeName)
    {
        _pipeName = pipeName;
    }

    public async Task RunAsync(Func<SqlTraceBlock, CancellationToken, ValueTask> onBlock, CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            await using var pipe = new NamedPipeServerStream(
                _pipeName,
                PipeDirection.In,
                maxNumberOfServerInstances: 1,
                PipeTransmissionMode.Byte,
                PipeOptions.Asynchronous);

            await pipe.WaitForConnectionAsync(cancellationToken).ConfigureAwait(false);
            using var reader = new StreamReader(pipe, Encoding.UTF8, detectEncodingFromByteOrderMarks: true, leaveOpen: true);

            while (!cancellationToken.IsCancellationRequested && pipe.IsConnected)
            {
                var line = await reader.ReadLineAsync(cancellationToken).ConfigureAwait(false);
                if (line is null)
                {
                    break;
                }

                foreach (var block in ParseLine(line))
                {
                    await onBlock(block, cancellationToken).ConfigureAwait(false);
                }
            }
        }
    }

    private IReadOnlyList<SqlTraceBlock> ParseLine(string line)
    {
        try
        {
            var envelope = JsonSerializer.Deserialize<AgentEventEnvelope>(line, _jsonOptions);
            if (envelope is null)
            {
                return new[] { SqlTraceBlock.Info("[protocol error] empty event") };
            }

            return _assembler.Apply(envelope);
        }
        catch (JsonException ex)
        {
            return new[] { SqlTraceBlock.Info($"[protocol error] invalid json: {ex.Message}") };
        }
    }
}

