using System.Text;
using Microsoft.WSL.Containers;

namespace Wslc.Testcontainers.Runtime;

/// <summary>
/// Adapts an official <see cref="Microsoft.WSL.Containers.Process"/> to <see cref="IWslProcess"/>,
/// capturing stdout/stderr and exposing the exit state.
/// </summary>
internal sealed class ContainerProcess : IWslProcess
{
    private readonly Microsoft.WSL.Containers.Process _process;
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

    /// <summary>Grace period used when aborting a process from cleanup paths.</summary>
    internal static readonly TimeSpan AbortGracePeriod = TimeSpan.FromSeconds(2);

    private static readonly TimeSpan DefaultGracePeriod = TimeSpan.FromSeconds(5);

    public Task KillAsync(CancellationToken cancellationToken = default) =>
        KillAsync(DefaultGracePeriod, cancellationToken);

    internal async Task KillAsync(TimeSpan gracePeriod, CancellationToken cancellationToken)
    {
        ObjectDisposedException.ThrowIf(_disposed != 0, this);
        if (HasExited)
        {
            return;
        }

        TrySignal(Signal.SIGTERM);
        if (await WaitForExitAsync(gracePeriod, cancellationToken).ConfigureAwait(false))
        {
            return;
        }

        TrySignal(Signal.SIGKILL);
        await WaitForExitAsync(gracePeriod, cancellationToken).ConfigureAwait(false);
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
        try
        {
            _observer?.Invoke(line);
        }
        catch
        {
        }
    }

    /// <summary>Incrementally decodes UTF-8 byte chunks into complete lines (pending capped at 256 KiB).</summary>
    internal sealed class LineAssembler
    {
        private const int MaxPendingChars = 256 * 1024;
        private readonly Decoder _decoder = Encoding.UTF8.GetDecoder();
        private readonly Action<string> _onLine;
        private char[] _pending = new char[256];
        private int _count;
        private int _scanFrom;

        public LineAssembler(Action<string> onLine) => _onLine = onLine;

        public void Append(byte[] data)
        {
            EnsureCapacity(_count + data.Length + 1);
            _decoder.Convert(data, 0, data.Length, _pending, _count, _pending.Length - _count, flush: false, out _, out var charsUsed, out _);
            _count += charsUsed;
            EmitCompleteLines();

            // A process that emits a giant single line without '\n' would otherwise grow
            // the buffer without bound. Flush early to keep memory capped; the line is split
            // but no data is lost beyond the normal history cap downstream.
            if (_count > MaxPendingChars)
            {
                _onLine(new string(_pending, 0, _count));
                _count = 0;
                _scanFrom = 0;
            }
        }

        public void Flush()
        {
            EnsureCapacity(_count + 1);
            _decoder.Convert(Array.Empty<byte>(), 0, 0, _pending, _count, _pending.Length - _count, flush: true, out _, out var charsUsed, out _);
            _count += charsUsed;
            _decoder.Reset();

            EmitCompleteLines();
            if (_count > 0)
            {
                _onLine(new string(_pending, 0, _count));
                _count = 0;
            }

            _scanFrom = 0;
        }

        private void EnsureCapacity(int required)
        {
            if (required <= _pending.Length)
            {
                return;
            }

            var size = _pending.Length;
            while (size < required)
            {
                size *= 2;
            }

            Array.Resize(ref _pending, size);
        }

        private void EmitCompleteLines()
        {
            // Scanning resumes where the previous call stopped: the earlier characters are
            // known to contain no newline, so a long line spanning many chunks is scanned once.
            var lineStart = 0;
            for (var i = _scanFrom; i < _count; i++)
            {
                if (_pending[i] != '\n')
                {
                    continue;
                }

                var end = i > 0 && _pending[i - 1] == '\r' ? i - 1 : i;
                _onLine(new string(_pending, lineStart, end - lineStart));
                lineStart = i + 1;
            }

            // Complete lines are removed with a single compaction instead of one memmove per line.
            if (lineStart > 0)
            {
                _count -= lineStart;
                if (_count > 0)
                {
                    Array.Copy(_pending, lineStart, _pending, 0, _count);
                }
            }

            _scanFrom = _count;
        }
    }

    /// <summary>Thread-safe capture buffer that keeps only the newest bytes.</summary>
    internal sealed class CaptureBuffer : IDisposable
    {
        // Long-running processes can emit unbounded output; keep the newest 1 MiB per
        // stream so captured diagnostics stay bounded. The buffer is a ring: appending
        // past the cap drops the oldest bytes in place, so no whole-buffer copies occur.
        private const int MaxBytes = 1024 * 1024;

        private readonly byte[] _buffer = new byte[MaxBytes];
        private readonly object _gate = new();
        private int _start;
        private int _count;
        private bool _disposed;

        public void Append(byte[] data)
        {
            if (data.Length == 0)
            {
                return;
            }

            lock (_gate)
            {
                if (_disposed)
                {
                    return;
                }

                if (data.Length >= _buffer.Length)
                {
                    Buffer.BlockCopy(data, data.Length - _buffer.Length, _buffer, 0, _buffer.Length);
                    _start = 0;
                    _count = _buffer.Length;
                    return;
                }

                var overflow = _count + data.Length - _buffer.Length;
                if (overflow > 0)
                {
                    _count -= overflow;
                    _start = (_start + overflow) % _buffer.Length;
                }

                var tail = (_start + _count) % _buffer.Length;
                var first = Math.Min(data.Length, _buffer.Length - tail);
                Buffer.BlockCopy(data, 0, _buffer, tail, first);
                if (first < data.Length)
                {
                    Buffer.BlockCopy(data, first, _buffer, 0, data.Length - first);
                }

                _count += data.Length;
            }
        }

        public string Decode()
        {
            lock (_gate)
            {
                if (_count == 0)
                {
                    return string.Empty;
                }

                if (_start + _count <= _buffer.Length)
                {
                    return Encoding.UTF8.GetString(_buffer, _start, _count);
                }

                var wrapped = new byte[_count];
                var first = _buffer.Length - _start;
                Buffer.BlockCopy(_buffer, _start, wrapped, 0, first);
                Buffer.BlockCopy(_buffer, 0, wrapped, first, _count - first);
                return Encoding.UTF8.GetString(wrapped);
            }
        }

        public void Dispose()
        {
            lock (_gate)
            {
                _disposed = true;
                _start = 0;
                _count = 0;
            }
        }
    }
}
