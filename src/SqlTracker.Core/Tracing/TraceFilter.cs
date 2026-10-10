namespace SqlTracker.Core.Tracing;

public sealed record TraceFilter
{
    public double? MinElapsedMilliseconds { get; init; }

    public string? SqlKeyword { get; init; }

    public bool Matches(SqlTraceBlock block)
    {
        if (MinElapsedMilliseconds is { } minElapsed)
        {
            if (block.ElapsedMilliseconds is null || block.ElapsedMilliseconds < minElapsed)
            {
                return false;
            }
        }

        if (!string.IsNullOrWhiteSpace(SqlKeyword))
        {
            var sql = block.Sql ?? string.Empty;
            if (sql.IndexOf(SqlKeyword, StringComparison.OrdinalIgnoreCase) < 0)
            {
                return false;
            }
        }

        return true;
    }
}

