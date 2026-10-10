using System.Text.Json;

namespace SqlTracker.Core.Protocol;

public sealed record AgentEventEnvelope
{
    public int Version { get; init; }

    public string Type { get; init; } = string.Empty;

    public int ProcessId { get; init; }

    public int ThreadId { get; init; }

    public long Sequence { get; init; }

    public DateTimeOffset Timestamp { get; init; }

    public JsonElement Payload { get; init; }
}

