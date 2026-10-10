using System.ComponentModel;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
using SqlTracker.Windows.ViewModels;

namespace SqlTracker.Windows;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        Closing += OnClosing;

        if (DataContext is MainViewModel viewModel)
        {
            viewModel.PropertyChanged += OnViewModelPropertyChanged;
            viewModel.TraceTextAppended += OnTraceTextAppended;
        }
    }

    private void OnClosing(object? sender, CancelEventArgs e)
    {
        if (DataContext is MainViewModel viewModel)
        {
            viewModel.TraceTextAppended -= OnTraceTextAppended;
            viewModel.PropertyChanged -= OnViewModelPropertyChanged;
            viewModel.Shutdown();
        }
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(MainViewModel.StreamText))
        {
            if (sender is MainViewModel viewModel)
            {
                RenderTraceOutput(viewModel.StreamText);
            }

            TraceOutput.ScrollToEnd();
        }
    }

    private void RenderTraceOutput(string text)
    {
        var document = CreateTraceDocument();
        AddTraceText(document, text);
        TraceOutput.Document = document;
    }

    private void OnTraceTextAppended(object? sender, string text)
    {
        AppendTraceOutput(text);
        TraceOutput.ScrollToEnd();
    }

    private void AppendTraceOutput(string text)
    {
        var document = TraceOutput.Document;
        AddTraceText(document, text);
    }

    private static FlowDocument CreateTraceDocument()
    {
        return new FlowDocument
        {
            PagePadding = new Thickness(10),
            FontFamily = new FontFamily("Consolas"),
            FontSize = 13
        };
    }

    private static void AddTraceText(FlowDocument document, string text)
    {
        var lines = text.Replace("\r\n", "\n").Split('\n');
        var lineCount = lines.Length;
        if (lineCount > 0 && lines[lineCount - 1].Length == 0)
        {
            --lineCount;
        }

        for (var index = 0; index < lineCount; ++index)
        {
            var line = lines[index].TrimEnd('\r');
            var paragraph = new Paragraph
            {
                Margin = new Thickness(0),
                LineHeight = 17
            };

            var run = new Run(line);
            ApplyLineStyle(line, run);
            paragraph.Inlines.Add(run);
            document.Blocks.Add(paragraph);
        }
    }

    private static void ApplyLineStyle(string line, Run run)
    {
        if (IsSeparatorLine(line))
        {
            run.Foreground = new SolidColorBrush(Color.FromRgb(107, 114, 128));
            return;
        }

        if (line.StartsWith("costs:", StringComparison.OrdinalIgnoreCase))
        {
            run.Foreground = new SolidColorBrush(Color.FromRgb(124, 58, 237));
            return;
        }

        if (line.StartsWith("ORA-", StringComparison.OrdinalIgnoreCase))
        {
            run.Foreground = new SolidColorBrush(Color.FromRgb(185, 28, 28));
            run.FontWeight = FontWeights.SemiBold;
            return;
        }

        if (line.StartsWith("[", StringComparison.Ordinal))
        {
            run.Foreground = new SolidColorBrush(Color.FromRgb(4, 120, 87));
            return;
        }

        if (line.Equals("Commit;", StringComparison.OrdinalIgnoreCase) ||
            line.Equals("Rollback;", StringComparison.OrdinalIgnoreCase))
        {
            run.Foreground = new SolidColorBrush(Color.FromRgb(30, 64, 175));
            run.FontWeight = FontWeights.SemiBold;
            return;
        }

        if (!string.IsNullOrWhiteSpace(line))
        {
            run.Foreground = new SolidColorBrush(Color.FromRgb(29, 78, 216));
            run.FontWeight = FontWeights.SemiBold;
        }
    }

    private static bool IsSeparatorLine(string line)
    {
        if (line.StartsWith("-----", StringComparison.Ordinal))
        {
            return true;
        }

        var index = 0;
        while (index < line.Length && char.IsDigit(line[index]))
        {
            ++index;
        }

        return index > 0 &&
            index < line.Length &&
            line[index..].StartsWith("-----", StringComparison.Ordinal);
    }
}
