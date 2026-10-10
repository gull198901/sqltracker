namespace SqlTracker.Core.Tracing;

public sealed record SqlTraceBlock
{
    public TraceBlockKind Kind { get; init; } = TraceBlockKind.Sql;

    public int ProcessId { get; init; }

    public int ThreadId { get; init; }

    public string? Sql { get; init; }

    public IReadOnlyList<BindValue> Binds { get; init; } = Array.Empty<BindValue>();

    public double? ElapsedMilliseconds { get; init; }

    public DateTimeOffset? EndTime { get; init; }

    public int? Status { get; init; }

    public string? ErrorMessage { get; init; }

    public static SqlTraceBlock Info(string message)
    {
        return new SqlTraceBlock
        {
            Kind = TraceBlockKind.Info,
            Sql = message,
            EndTime = DateTimeOffset.Now
        };
    }
}

