using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text;
using System.Windows;
using System.Windows.Threading;
using Microsoft.Win32;
using SqlTracker.Core.Rendering;
using SqlTracker.Core.Tracing;
using SqlTracker.Core.Transport;
using SqlTracker.Windows.Services;
using SqlTracker.Windows.ViewModels.Support;

namespace SqlTracker.Windows.ViewModels;

public sealed class MainViewModel : INotifyPropertyChanged
{
    private const string StructuredPipeNamePrefix = "sqltracker-oci";
    private const int MaxBlocks = 100000;

    private readonly List<CapturedTraceBlock> _blocks = new();
    private readonly List<SqlTraceBlock> _pendingBlocks = new();
    private readonly object _pendingLock = new();
    private readonly StringBuilder _streamBuilder = new();
    private readonly Dispatcher _dispatcher = Dispatcher.CurrentDispatcher;
    private readonly LegacyTraceRenderer _renderer = new();
    private readonly OracleProcessScanner _processScanner = new();
    private readonly WindowsDllInjector _injector = new();
    private CancellationTokenSource? _receiverCancellation;
    private Task? _receiverTask;
    private RemoteInjectionHandle? _activeInjection;
    private bool _isRunning;
    private bool _pendingDrainScheduled;
    private string _statusText = "Ready";
    private string _minElapsedText = string.Empty;
    private string _sqlKeyword = string.Empty;
    private int _nextTraceNumber = 1;
    private TargetProcessViewModel? _selectedProcess;

    public MainViewModel()
    {
        Processes = new ObservableCollection<TargetProcessViewModel>();

        RefreshCommand = new RelayCommand(_ => RefreshProcesses(), _ => !_isRunning);
        StartCommand = new RelayCommand(_ => Start(), _ => !_isRunning && SelectedProcess is not null);
        PauseCommand = new RelayCommand(_ => Pause(), _ => _isRunning);
        StopCommand = new RelayCommand(_ => Stop(), _ => _isRunning);
        ClearCommand = new RelayCommand(_ => Clear(), _ => !string.IsNullOrEmpty(StreamText));
        SaveCommand = new RelayCommand(_ => SaveCurrentTrace(), _ => !string.IsNullOrWhiteSpace(StreamText));
        HelpCommand = new RelayCommand(_ => ShowHelp());

        RefreshProcesses();
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public event EventHandler<string>? TraceTextAppended;

    public ObservableCollection<TargetProcessViewModel> Processes { get; }

    public RelayCommand RefreshCommand { get; }

    public RelayCommand StartCommand { get; }

    public RelayCommand PauseCommand { get; }

    public RelayCommand StopCommand { get; }

    public RelayCommand ClearCommand { get; }

    public RelayCommand SaveCommand { get; }

    public RelayCommand HelpCommand { get; }

    public void Shutdown()
    {
        StopMonitoring("Monitoring stopped.");
    }

    public TargetProcessViewModel? SelectedProcess
    {
        get => _selectedProcess;
        set
        {
            if (SetField(ref _selectedProcess, value))
            {
                RaiseCommandStates();
            }
        }
    }

    public string StreamText => _streamBuilder.ToString();

    public string StatusText
    {
        get => _statusText;
        private set => SetField(ref _statusText, value);
    }

    public string MinElapsedText
    {
        get => _minElapsedText;
        set
        {
            if (SetField(ref _minElapsedText, value))
            {
                RebuildVisibleOutput();
            }
        }
    }

    public string SqlKeyword
    {
        get => _sqlKeyword;
        set
        {
            if (SetField(ref _sqlKeyword, value))
            {
                RebuildVisibleOutput();
            }
        }
    }

    private void RefreshProcesses()
    {
        Processes.Clear();

        foreach (var process in _processScanner.FindOracleProcesses())
        {
            Processes.Add(new TargetProcessViewModel(
                process.ProcessId,
                process.ProcessName,
                process.Bitness,
                process.ExecutablePath));
        }

        if (Processes.Count == 0)
        {
            StatusText = "No running Oracle OCI process was found.";
        }
        else
        {
            StatusText = $"Found {Processes.Count} Oracle OCI process(es).";
        }

        SelectedProcess = Processes.FirstOrDefault();
    }

    private void Start()
    {
        if (_isRunning)
        {
            return;
        }

        SetRunning(true);
        StartRealOracleMonitoring();
    }

    private void StartRealOracleMonitoring()
    {
        if (SelectedProcess is null)
        {
            SetRunning(false);
            StatusText = "Start failed: no real Oracle process selected.";
            return;
        }

        if (!string.Equals(SelectedProcess.Bitness, "x86", StringComparison.OrdinalIgnoreCase))
        {
            SetRunning(false);
            StatusText = "Start failed: current Windows agent supports x86 Oracle processes only.";
            return;
        }

        var agentPath = AgentPathResolver.FindStructuredOciAgent();
        if (agentPath is null)
        {
            SetRunning(false);
            StatusText = "Start failed: sqltracker_oci_agent_win32.dll not found.";
            return;
        }

        StartTracePipeReceiver(SelectedProcess.ProcessId);

        try
        {
            _activeInjection = _injector.Inject(SelectedProcess.ProcessId, agentPath);
            StatusText = $"Injected {Path.GetFileName(agentPath)} into pid {SelectedProcess.ProcessId}.";
        }
        catch (Exception ex)
        {
            StopPipeReceiver();
            SetRunning(false);
            StatusText = $"Injection failed: {ex.Message}";
        }
    }

    private void Pause()
    {
        StopMonitoring("Monitoring paused.");
    }

    private void Stop()
    {
        StopMonitoring("Monitoring stopped.");
    }

    private void StopMonitoring(string finalStatus)
    {
        if (!_isRunning && _activeInjection is null && _receiverCancellation is null)
        {
            return;
        }

        if (_activeInjection is not null)
        {
            try
            {
                _injector.Uninject(_activeInjection);
                StatusText = $"Uninjected pid {_activeInjection.ProcessId}.";
            }
            catch (Exception ex)
            {
                StatusText = $"Uninject failed: {ex.Message}";
            }
            finally
            {
                _activeInjection = null;
            }
        }

        StopPipeReceiver();
        SetRunning(false);
        StatusText = finalStatus;
    }

    private void Clear()
    {
        _blocks.Clear();
        _nextTraceNumber = 1;
        ReplaceStreamText(string.Empty);
        StatusText = "Output cleared.";
    }

    private void SaveCurrentTrace()
    {
        var dialog = new SaveFileDialog
        {
            Title = "Save current trace",
            Filter = "Text files (*.txt)|*.txt|Log files (*.log)|*.log|All files (*.*)|*.*",
            FileName = $"sqltracker-{DateTime.Now:yyyyMMdd-HHmmss}.txt",
            InitialDirectory = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments)
        };

        if (dialog.ShowDialog() != true)
        {
            return;
        }

        File.WriteAllText(dialog.FileName, StreamText, Encoding.UTF8);
        StatusText = $"Saved trace to {dialog.FileName}.";
    }

    private static void ShowHelp()
    {
        MessageBox.Show(
            "使用说明：\n\n" +
            "1. 先启动 32 位 Oracle 客户端并完成连接。\n" +
            "2. 点击 Refresh，选择目标进程。\n" +
            "3. 点击 Start 开始跟踪。\n" +
            "4. 在目标程序中执行 SQL，右侧实时显示 SQL、绑定变量、耗时和结束时间。\n" +
            "5. Pause 或 Stop 会停止当前跟踪，Start 可重新开始。\n" +
            "6. Save 保存当前右侧跟踪内容到本地文档。",
            "SqlTracker 使用说明",
            MessageBoxButton.OK,
            MessageBoxImage.Information);
    }

    private void StartTracePipeReceiver(int processId)
    {
        var pipeName = $"{StructuredPipeNamePrefix}-{processId}";
        var receiver = new NamedPipeTraceReceiver(pipeName);
        _receiverCancellation = new CancellationTokenSource();
        var cancellationToken = _receiverCancellation.Token;

        _receiverTask = Task.Run(async () =>
        {
            try
            {
                await receiver.RunAsync(OnTraceBlockAsync, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
            catch (Exception ex)
            {
                await _dispatcher.InvokeAsync(() =>
                {
                    StatusText = $"Trace receiver stopped with an error: {ex.Message}";
                }).Task.ConfigureAwait(false);
            }
        }, cancellationToken);

        StatusText = $"Waiting for agent pipe client: {pipeName}.";
    }

    private void StopPipeReceiver()
    {
        _receiverCancellation?.Cancel();
        _receiverCancellation?.Dispose();
        _receiverCancellation = null;
        _receiverTask = null;
    }

    private ValueTask OnTraceBlockAsync(SqlTraceBlock block, CancellationToken cancellationToken)
    {
        if (cancellationToken.IsCancellationRequested)
        {
            return ValueTask.CompletedTask;
        }

        if (block.Kind == TraceBlockKind.Info)
        {
            _dispatcher.BeginInvoke(
                new Action(() => StatusText = block.Sql ?? StatusText),
                DispatcherPriority.Background);
            return ValueTask.CompletedTask;
        }

        lock (_pendingLock)
        {
            _pendingBlocks.Add(block);
            if (_pendingDrainScheduled)
            {
                return ValueTask.CompletedTask;
            }

            _pendingDrainScheduled = true;
        }

        _dispatcher.BeginInvoke(new Action(DrainPendingBlocks), DispatcherPriority.Background);
        return ValueTask.CompletedTask;
    }

    private void DrainPendingBlocks()
    {
        List<SqlTraceBlock> blocks;
        lock (_pendingLock)
        {
            blocks = new List<SqlTraceBlock>(_pendingBlocks);
            _pendingBlocks.Clear();
            _pendingDrainScheduled = false;
        }

        if (blocks.Count == 0)
        {
            return;
        }

        AddBlocks(blocks);
    }

    private void AddBlocks(IReadOnlyList<SqlTraceBlock> blocks)
    {
        var filter = CurrentFilter();
        var visibleText = new StringBuilder();

        foreach (var block in blocks)
        {
            var captured = new CapturedTraceBlock(_nextTraceNumber++, block);
            _blocks.Add(captured);

            if (_blocks.Count > MaxBlocks)
            {
                _blocks.RemoveAt(0);
            }

            if (filter.Matches(captured.Block))
            {
                visibleText.Append(_renderer.Render(captured.Block, captured.Number));
            }
        }

        if (visibleText.Length > 0)
        {
            AppendStreamText(visibleText.ToString());
        }
    }

    private void RebuildVisibleOutput()
    {
        var filter = CurrentFilter();
        var builder = new StringBuilder();

        foreach (var captured in _blocks)
        {
            if (filter.Matches(captured.Block))
            {
                builder.Append(_renderer.Render(captured.Block, captured.Number));
            }
        }

        ReplaceStreamText(builder.ToString());
    }

    private void AppendStreamText(string text)
    {
        _streamBuilder.Append(text);
        TraceTextAppended?.Invoke(this, text);
        SaveCommand.RaiseCanExecuteChanged();
        ClearCommand.RaiseCanExecuteChanged();
    }

    private void ReplaceStreamText(string text)
    {
        _streamBuilder.Clear();
        _streamBuilder.Append(text);
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(StreamText)));
        SaveCommand.RaiseCanExecuteChanged();
        ClearCommand.RaiseCanExecuteChanged();
    }

    private void SetRunning(bool isRunning)
    {
        if (_isRunning == isRunning)
        {
            return;
        }

        _isRunning = isRunning;
        RaiseCommandStates();
    }

    private void RaiseCommandStates()
    {
        RefreshCommand.RaiseCanExecuteChanged();
        StartCommand.RaiseCanExecuteChanged();
        PauseCommand.RaiseCanExecuteChanged();
        StopCommand.RaiseCanExecuteChanged();
    }

    private TraceFilter CurrentFilter()
    {
        double? minElapsed = null;
        if (double.TryParse(MinElapsedText, out var parsed))
        {
            minElapsed = parsed;
        }

        return new TraceFilter
        {
            MinElapsedMilliseconds = minElapsed,
            SqlKeyword = SqlKeyword
        };
    }

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        return true;
    }

    private sealed record CapturedTraceBlock(int Number, SqlTraceBlock Block);
}
