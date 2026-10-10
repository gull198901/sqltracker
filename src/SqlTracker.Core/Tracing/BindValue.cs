namespace SqlTracker.Core.Tracing;

public sealed record BindValue(
    int Index,
    string? Name,
    int DataType,
    string DisplayValue);

