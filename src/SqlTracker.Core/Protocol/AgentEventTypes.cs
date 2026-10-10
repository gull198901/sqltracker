namespace SqlTracker.Core.Protocol;

public static class AgentEventTypes
{
    public const string AgentHello = "agent_hello";
    public const string AgentInfo = "agent_info";
    public const string Prepare = "prepare";
    public const string Bind = "bind";
    public const string ExecuteCompleted = "execute_completed";
    public const string CommitCompleted = "commit_completed";
    public const string RollbackCompleted = "rollback_completed";
}

