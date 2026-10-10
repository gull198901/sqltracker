using System.Globalization;
using System.Text;
using SqlTracker.Core.Tracing;

namespace SqlTracker.Core.Rendering;

public sealed class LegacyTraceRenderer
{
    private const string Separator = "-----------------------------------------------------";

    public string Render(SqlTraceBlock block, int? sequenceNumber = null)
    {
        var builder = new StringBuilder();
        if (sequenceNumber is { } value)
        {
            builder.Append(value.ToString(CultureInfo.InvariantCulture));
        }

        builder.AppendLine(Separator);

        switch (block.Kind)
        {
            case TraceBlockKind.Commit:
                builder.AppendLine("Commit;");
                break;
            case TraceBlockKind.Rollback:
                builder.AppendLine("Rollback;");
                break;
            case TraceBlockKind.Info:
                builder.AppendLine(block.Sql ?? string.Empty);
                break;
            default:
                builder.AppendLine(string.IsNullOrWhiteSpace(block.Sql) ? "[SQL unavailable]" : block.Sql);
                AppendBinds(builder, block.Binds);
                AppendError(builder, block);
                break;
        }

        AppendCostLine(builder, block);

        return builder.ToString();
    }

    private static void AppendBinds(StringBuilder builder, IReadOnlyList<BindValue> binds)
    {
        foreach (var bind in binds)
        {
            var name = string.IsNullOrWhiteSpace(bind.Name) ? bind.Index.ToString(CultureInfo.InvariantCulture) : bind.Name;
            builder.Append('[');
            builder.Append(bind.Index.ToString(CultureInfo.InvariantCulture));
            builder.Append("] ");
            builder.Append(name);
            builder.Append('=');
            builder.AppendLine(FormatBindDisplayValue(bind));
        }
    }

    private static string FormatBindDisplayValue(BindValue bind)
    {
        if (!IsCharacterDataType(bind.DataType))
        {
            return bind.DisplayValue;
        }

        if (string.Equals(bind.DisplayValue, "null", StringComparison.OrdinalIgnoreCase) ||
            bind.DisplayValue.StartsWith("[dty:", StringComparison.OrdinalIgnoreCase))
        {
            return bind.DisplayValue;
        }

        return "'" + bind.DisplayValue.Replace("'", "''", StringComparison.Ordinal) + "'";
    }

    private static bool IsCharacterDataType(int dataType)
    {
        return dataType is
            1 or  // SQLT_CHR
            5 or  // SQLT_STR
            9 or  // SQLT_VCS
            96 or // SQLT_AFC
            97;   // SQLT_AVC
    }

    private static void AppendError(StringBuilder builder, SqlTraceBlock block)
    {
        if (!string.IsNullOrWhiteSpace(block.ErrorMessage))
        {
            builder.AppendLine(block.ErrorMessage.Trim());
        }
    }

    private static void AppendCostLine(StringBuilder builder, SqlTraceBlock block)
    {
        if (block.ElapsedMilliseconds is null && block.EndTime is null)
        {
            return;
        }

        if (block.ElapsedMilliseconds is { } elapsed)
        {
            builder.Append("costs:[");
            builder.Append(elapsed.ToString("0.0", CultureInfo.InvariantCulture));
            builder.Append("] ms");
        }

        if (block.EndTime is { } endTime)
        {
            if (block.ElapsedMilliseconds is not null)
            {
                builder.Append("  ");
            }

            builder.Append("end:[");
            builder.Append(endTime.LocalDateTime.ToString("yyyy-MM-dd HH:mm:ss.fff", CultureInfo.InvariantCulture));
            builder.Append(']');
        }

        builder.AppendLine();
    }
}
