using SqlTracker.Core.Tracing;

namespace SqlTracker.Core.Protocol;

public sealed class TraceEventAssembler
{
    private readonly Dictionary<string, StatementContext> _statements = new(StringComparer.OrdinalIgnoreCase);

    public IReadOnlyList<SqlTraceBlock> Apply(AgentEventEnvelope envelope)
    {
        return envelope.Type switch
        {
            AgentEventTypes.AgentHello => Single(AgentInfo(envelope)),
            AgentEventTypes.AgentInfo => Single(AgentInfo(envelope)),
            AgentEventTypes.Prepare => ApplyPrepare(envelope),
            AgentEventTypes.Bind => ApplyBind(envelope),
            AgentEventTypes.ExecuteCompleted => ApplyExecuteCompleted(envelope),
            AgentEventTypes.CommitCompleted => Single(ApplyTransaction(envelope, TraceBlockKind.Commit)),
            AgentEventTypes.RollbackCompleted => Single(ApplyTransaction(envelope, TraceBlockKind.Rollback)),
            _ => Single(SqlTraceBlock.Info($"[unknown event] {envelope.Type}"))
        };
    }

    private static IReadOnlyList<SqlTraceBlock> Single(SqlTraceBlock block)
    {
        return new[] { block };
    }

    private IReadOnlyList<SqlTraceBlock> ApplyPrepare(AgentEventEnvelope envelope)
    {
        var statementId = envelope.Payload.RequiredString("statementId");
        if (string.IsNullOrWhiteSpace(statementId))
        {
            return Single(SqlTraceBlock.Info("[prepare ignored] missing statementId"));
        }

        var context = GetStatement(statementId);
        context.Sql = envelope.Payload.OptionalString("sql");
        context.Binds.Clear();
        return Array.Empty<SqlTraceBlock>();
    }

    private IReadOnlyList<SqlTraceBlock> ApplyBind(AgentEventEnvelope envelope)
    {
        var statementId = envelope.Payload.RequiredString("statementId");
        if (string.IsNullOrWhiteSpace(statementId))
        {
            return Single(SqlTraceBlock.Info("[bind ignored] missing statementId"));
        }

        var index = envelope.Payload.OptionalInt32("index");
        var bind = new BindValue(
            index,
            envelope.Payload.OptionalString("name"),
            envelope.Payload.OptionalInt32("dataType"),
            envelope.Payload.OptionalString("displayValue") ?? $"[dty:{envelope.Payload.OptionalInt32("dataType")}]");

        var context = GetStatement(statementId);
        context.UpsertBind(bind);
        return Array.Empty<SqlTraceBlock>();
    }

    private IReadOnlyList<SqlTraceBlock> ApplyExecuteCompleted(AgentEventEnvelope envelope)
    {
        var statementId = envelope.Payload.RequiredString("statementId");
        _statements.TryGetValue(statementId, out var context);

        var block = new SqlTraceBlock
        {
            Kind = TraceBlockKind.Sql,
            ProcessId = envelope.ProcessId,
            ThreadId = envelope.ThreadId,
            Sql = context?.Sql,
            Binds = context?.Binds.ToArray() ?? Array.Empty<BindValue>(),
            ElapsedMilliseconds = envelope.Payload.OptionalDouble("elapsedMilliseconds"),
            EndTime = envelope.Payload.OptionalDateTimeOffset("endTime") ?? envelope.Timestamp,
            Status = envelope.Payload.OptionalInt32("status"),
            ErrorMessage = envelope.Payload.OptionalString("errorMessage")
        };

        return Single(block);
    }

    private static SqlTraceBlock ApplyTransaction(AgentEventEnvelope envelope, TraceBlockKind kind)
    {
        return new SqlTraceBlock
        {
            Kind = kind,
            ProcessId = envelope.ProcessId,
            ThreadId = envelope.ThreadId,
            ElapsedMilliseconds = envelope.Payload.OptionalDouble("elapsedMilliseconds"),
            EndTime = envelope.Payload.OptionalDateTimeOffset("endTime") ?? envelope.Timestamp,
            Status = envelope.Payload.OptionalInt32("status"),
            ErrorMessage = envelope.Payload.OptionalString("errorMessage")
        };
    }

    private static SqlTraceBlock AgentInfo(AgentEventEnvelope envelope)
    {
        var message = envelope.Payload.OptionalString("message");
        if (!string.IsNullOrWhiteSpace(message))
        {
            return SqlTraceBlock.Info(message);
        }

        if (envelope.Type == AgentEventTypes.AgentHello)
        {
            var kind = envelope.Payload.OptionalString("agentKind") ?? "agent";
            var version = envelope.Payload.OptionalString("agentVersion") ?? "unknown";
            var bitness = envelope.Payload.OptionalInt32("bitness");
            return SqlTraceBlock.Info($"# {kind} v{version} attached, pid:{envelope.ProcessId}, bitness:{bitness} #");
        }

        return SqlTraceBlock.Info($"# {envelope.Type} #");
    }

    private StatementContext GetStatement(string statementId)
    {
        if (!_statements.TryGetValue(statementId, out var context))
        {
            context = new StatementContext();
            _statements.Add(statementId, context);
        }

        return context;
    }

    private sealed class StatementContext
    {
        public string? Sql { get; set; }

        public List<BindValue> Binds { get; } = new();

        public void UpsertBind(BindValue bind)
        {
            var existingIndex = Binds.FindIndex(existing =>
                existing.Index == bind.Index ||
                (!string.IsNullOrWhiteSpace(existing.Name) &&
                 !string.IsNullOrWhiteSpace(bind.Name) &&
                 string.Equals(existing.Name, bind.Name, StringComparison.OrdinalIgnoreCase)));

            if (existingIndex >= 0)
            {
                Binds[existingIndex] = bind;
            }
            else
            {
                Binds.Add(bind);
            }
        }
    }
}

