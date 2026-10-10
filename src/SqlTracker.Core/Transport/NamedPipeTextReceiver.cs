using System.IO.Pipes;
using System.Text;

namespace SqlTracker.Core.Transport;

public sealed class NamedPipeTextReceiver
{
    private readonly string _pipeName;
    private readonly Encoding _encoding;

    public NamedPipeTextReceiver(string pipeName)
        : this(pipeName, Encoding.UTF8)
    {
    }

    public NamedPipeTextReceiver(string pipeName, Encoding encoding)
    {
        _pipeName = pipeName;
        _encoding = encoding;
    }

    public async Task RunAsync(Func<string, CancellationToken, ValueTask> onLine, CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            await using var pipe = new NamedPipeServerStream(
                _pipeName,
                PipeDirection.InOut,
                maxNumberOfServerInstances: 1,
                PipeTransmissionMode.Byte,
                PipeOptions.Asynchronous);

            await pipe.WaitForConnectionAsync(cancellationToken).ConfigureAwait(false);
            using var reader = new StreamReader(pipe, _encoding, detectEncodingFromByteOrderMarks: true, leaveOpen: true);

            while (!cancellationToken.IsCancellationRequested && pipe.IsConnected)
            {
                var line = await reader.ReadLineAsync(cancellationToken).ConfigureAwait(false);
                if (line is null)
                {
                    break;
                }

                await onLine(line, cancellationToken).ConfigureAwait(false);
            }
        }
    }
}
