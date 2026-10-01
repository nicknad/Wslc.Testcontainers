using System.Text;
using Microsoft.WSL.Containers;
using Wslc.Testcontainers.Internal;

namespace Wslc.Testcontainers.Runtime;

/// <summary>
/// Adapts an official <see cref="Microsoft.WSL.Containers.Process"/> to <see cref="IWslProcess"/>,
/// capturing stdout/stderr and exposing the exit state.
/// </summary>
internal sealed class ContainerProcess : IWslProcess
{
    private readonly Microsoft.WSL.Containers.Process _process;
    private readonly bool _captureOutput;
    private readonly LogBroadcaster _logs = new();
    private readonly Action<LogLine>? _observer;
    private readonly CaptureBuffer _stdout = new();
    private readonly CaptureBuffer _stderr = new();
    private readonly LineAssembler _stdoutLines;
    private readonly LineAssembler _stderrLines;
    private readonly TaskCompletionSource<int> _exit = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private int _started;
    private int _disposed;

    public ContainerProcess(Microsoft.WSL.Containers.Process process, bool captureOutput, Action<LogLine>? observer)
    {
        _process = process;
        _captureOutput = captureOutput;
        _observer = observer;
        _stdoutLines = new LineAssembler(text => Publish(LogSource.Stdout, text));
        _stderrLines = new LineAssembler(text => Publish(LogSource.Stderr, text));

        if (captureOutput)
        {
            process.OutputReceived += OnOutputReceived;
            process.ErrorReceived += OnErrorReceived;
        }

        process.Exited += OnExited;
    }

    public Microsoft.WSL.Containers.Process NativeProcess => _process;

    public int? Id => _process.Pid == 0 ? null : (int)_process.Pid;

    public bool HasExited => _exit.Task.IsCompleted || _process.State is ProcessState.Exited or ProcessState.Signalled;

    public int ExitCode
    {
        get
        {
            if (_exit.Task.IsCompleted)
            {
                return _exit.Task.Result;
            }

            if (_process.State is ProcessState.Exited or ProcessState.Signalled)
            {
                return _process.ExitCode;
            }

            throw new InvalidOperationException("The process has not exited yet.");
        }
    }

    public IAsyncEnumerable<string> Stdout => _logs.StreamAsync().TextLines(LogSource.Stdout);

    public IAsyncEnumerable<string> Stderr => _logs.StreamAsync().TextLines(LogSource.Stderr);

    public IAsyncEnumerable<LogLine> LogsAsync(CancellationToken cancellationToken = default) =>
        _logs.StreamAsync(cancellationToken);

    internal string StdoutText => _stdout.Decode();

    internal string StderrText => _stderr.Decode();

    public void Start()
    {
        if (Interlocked.Exchange(ref _started, 1) != 0)
        {
            throw new InvalidOperationException("The process has already been started.");
        }

        _process.Start();
    }

    public async Task<int> WaitForExitAsync(CancellationToken cancellationToken = default) =>
        await _exit.Task.WaitAsync(cancellationToken).ConfigureAwait(false);

    public async Task KillAsync(CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(_disposed != 0, this);
        if (HasExited)
        {
            return;
        }

        TrySignal(Signal.SIGTERM);
        if (await WaitForExitAsync(TimeSpan.FromSeconds(5), cancellationToken).ConfigureAwait(false))
        {
            return;
        }

        TrySignal(Signal.SIGKILL);
        await WaitForExitAsync(TimeSpan.FromSeconds(5), cancellationToken).ConfigureAwait(false);
    }

    internal async Task WriteStandardInputAsync(byte[] data, CancellationToken cancellationToken)
    {
        await using var stream = _process.GetInputStream().AsStreamForWrite();
        await stream.WriteAsync(data, cancellationToken).ConfigureAwait(false);
        await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
    }

    internal async Task<bool> WaitForExitAsync(TimeSpan timeout, CancellationToken cancellationToken)
    {
        try
        {
            await _exit.Task.WaitAsync(timeout, cancellationToken).ConfigureAwait(false);
            return true;
        }
        catch (TimeoutException)
        {
            return false;
        }
    }

    public ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return default;
        }

        try
        {
            _process.OutputReceived -= OnOutputReceived;
            _process.ErrorReceived -= OnErrorReceived;
            _process.Exited -= OnExited;
        }
        catch
        {
        }

        if (!HasExited)
        {
            TrySignal(Signal.SIGKILL);
        }

        _stdoutLines.Flush();
        _stderrLines.Flush();
        _stdout.Dispose();
        _stderr.Dispose();
        _logs.Complete();
        return default;
    }

    private void TrySignal(Signal signal)
    {
        try
        {
            _process.Signal(signal);
        }
        catch
        {
        }
    }

    private void OnOutputReceived(byte[] data) => OnData(_stdout, _stdoutLines, data);

    private void OnErrorReceived(byte[] data) => OnData(_stderr, _stderrLines, data);

    private static void OnData(CaptureBuffer buffer, LineAssembler assembler, byte[] data)
    {
        buffer.Append(data);
        assembler.Append(data);
    }

    private void OnExited(int exitCode)
    {
        _stdoutLines.Flush();
        _stderrLines.Flush();
        Publish(LogSource.System, $"process exited with code {exitCode}");
        _exit.TrySetResult(exitCode);
    }

    private void Publish(LogSource source, string text)
    {
        var line = new LogLine(source, text, DateTimeOffset.UtcNow);
        _logs.Publish(line);
        try
        {
            _observer?.Invoke(line);
        }
        catch
        {
        }
    }

    /// <summary>Incrementally decodes UTF-8 byte chunks into complete lines.</summary>
    internal sealed class LineAssembler
    {
        private readonly Decoder _decoder = Encoding.UTF8.GetDecoder();
        private readonly StringBuilder _pending = new(256);
        private readonly Action<string> _onLine;

        public LineAssembler(Action<string> onLine) => _onLine = onLine;

        public void Append(byte[] data)
        {
            var count = _decoder.GetCharCount(data, 0, data.Length, flush: false);
            if (count == 0)
            {
                return;
            }

            var chars = new char[count];
            _decoder.GetChars(data, 0, data.Length, chars, 0, flush: false);
            _pending.Append(chars);
            EmitLines(flush: false);
        }

        public void Flush() => EmitLines(flush: true);

        private void EmitLines(bool flush)
        {
            int index;
            while ((index = IndexOfNewline(_pending)) >= 0)
            {
                var line = _pending.ToString(0, index).TrimEnd('\r');
                _pending.Remove(0, index + 1);
                _onLine(line);
            }

            if (flush && _pending.Length > 0)
            {
                _onLine(_pending.ToString());
                _pending.Clear();
            }
        }

        private static int IndexOfNewline(StringBuilder builder)
        {
            for (var i = 0; i < builder.Length; i++)
            {
                if (builder[i] == '\n')
                {
                    return i;
                }
            }

            return -1;
        }
    }

    /// <summary>Thread-safe capture buffer that keeps only the newest bytes.</summary>
    private sealed class CaptureBuffer : IDisposable
    {
        // Long-running processes can emit unbounded output; keep the newest 1 MiB per
        // stream so captured diagnostics stay bounded. On overflow trim to half the cap,
        // which amortizes the copy over the next 512 KiB instead of every chunk.
        private const int MaxBytes = 1024 * 1024;
        private const int TrimToBytes = MaxBytes / 2;

        private readonly MemoryStream _buffer = new();

        public void Append(byte[] data)
        {
            lock (_buffer)
            {
                _buffer.Write(data, 0, data.Length);
                if (_buffer.Length > MaxBytes)
                {
                    Trim();
                }
            }
        }

        public string Decode()
        {
            lock (_buffer)
            {
                return Encoding.UTF8.GetString(_buffer.ToArray());
            }
        }

        public void Dispose()
        {
            lock (_buffer)
            {
                _buffer.Dispose();
            }
        }

        private void Trim()
        {
            var bytes = _buffer.ToArray();
            var start = bytes.Length - TrimToBytes;
            _buffer.SetLength(0);
            _buffer.Write(bytes, start, TrimToBytes);
        }
    }
}
