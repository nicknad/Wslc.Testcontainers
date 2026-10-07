using System.Text;
using Microsoft.WSL.Containers;
using Windows.Storage.Streams;
using Wslc.Testcontainers.Internal;

namespace Wslc.Testcontainers.Runtime;

/// <summary>
/// Executes processes inside an official WSL container and streams files through process
/// stdio. All operations use <see cref="Microsoft.WSL.Containers"/> only.
/// </summary>
internal static class WslcProcessRunner
{
    /// <summary>Maximum UTF-8 size of a single exec standard input payload.</summary>
    internal const long MaxStandardInputBytes = 64L * 1024L * 1024L;

    /// <summary>Throws <see cref="WslProcessException"/> when the payload exceeds the cap.</summary>
    internal static void ValidateStandardInputSize(long byteCount)
    {
        if (byteCount > MaxStandardInputBytes)
        {
            throw new WslProcessException($"Standard input exceeds the 64 MiB limit ({byteCount} bytes).");
        }
    }

    public static ProcessSettings CreateSettings(
        List<string> commandLine,
        string? workingDirectory,
        IReadOnlyDictionary<string, string>? environment,
        bool enableStandardInput = false,
        ProcessOutputMode outputMode = ProcessOutputMode.Event)
    {
        if (commandLine.Count == 0)
        {
            throw new ArgumentException("A command line is required.", nameof(commandLine));
        }

        // Callers pass the list they just built (BuildCommandLine or a fresh inline list) and
        // never retain it; the SDK stores this reference, so callers must not mutate it later.
        var settings = new ProcessSettings
        {
            CommandLine = commandLine,
            OutputMode = outputMode,
        };

        if (!string.IsNullOrEmpty(workingDirectory))
        {
            settings.WorkingDirectory = workingDirectory;
        }

        if (environment is { Count: > 0 })
        {
            settings.EnvironmentVariables = new Dictionary<string, string>(environment.Count, StringComparer.Ordinal);
            foreach (var pair in environment)
            {
                settings.EnvironmentVariables[pair.Key] = pair.Value;
            }
        }

        if (enableStandardInput)
        {
            settings.EnableStandardInput = true;
        }

        return settings;
    }

    /// <summary>
    /// Creates a process without starting it, so callers can register it before <see cref="ContainerProcess.Start"/>
    /// and a concurrent stop cannot miss it.
    /// </summary>
    public static ContainerProcess Create(
        Microsoft.WSL.Containers.Container container,
        ProcessSettings settings,
        Action<LogLine>? observer)
    {
        var native = container.CreateProcess(settings);
        var capturesOutputEvents = settings.OutputMode == ProcessOutputMode.Event;
        return new ContainerProcess(native, captureOutput: capturesOutputEvents, observer);
    }

    public static ContainerProcess Start(
        Microsoft.WSL.Containers.Container container,
        ProcessSettings settings,
        Action<LogLine>? observer)
    {
        var process = Create(container, settings, observer);
        process.Start();
        return process;
    }

    public static async Task<ExecResult> RunAsync(
        Microsoft.WSL.Containers.Container container,
        ProcessSettings settings,
        string? standardInput,
        TimeSpan? timeout,
        CancellationToken cancellationToken,
        Action<LogLine>? observer)
    {
        if (standardInput is not null)
        {
            // Count first so an oversized payload fails before a process is started or a
            // second copy is allocated; the cap bounds the array built below.
            ValidateStandardInputSize(Encoding.UTF8.GetByteCount(standardInput));
        }

        var process = Start(container, settings, observer);

        try
        {
            if (standardInput is not null)
            {
                await process.WriteStandardInputAsync(Encoding.UTF8.GetBytes(standardInput), cancellationToken).ConfigureAwait(false);
            }

            if (timeout is { } timeoutValue)
            {
                if (!await process.WaitForExitAsync(timeoutValue, cancellationToken).ConfigureAwait(false))
                {
                    await process.KillAsync(ContainerProcess.AbortGracePeriod, CancellationToken.None).ConfigureAwait(false);
                    throw new WslTimeoutException($"Command '{settings.CommandLine[0]}' timed out after {timeoutValue}.");
                }
            }
            else
            {
                await process.WaitForExitAsync(cancellationToken).ConfigureAwait(false);
            }

            return new ExecResult(process.ExitCode, process.StdoutText, process.StderrText);
        }
        catch (WslTimeoutException)
        {
            throw;
        }
        catch
        {
            await process.KillAsync(ContainerProcess.AbortGracePeriod, CancellationToken.None).ConfigureAwait(false);
            throw;
        }
        finally
        {
            await process.DisposeAsync().ConfigureAwait(false);
        }
    }

    public static Task CopyToAsync(
        Microsoft.WSL.Containers.Container container,
        string source,
        string destination,
        CancellationToken cancellationToken,
        Action<LogLine>? observer)
    {
        var settings = CreateSettings(
            new List<string>(5) { "/bin/sh", "-c", "mkdir -p \"$(dirname \"$1\")\" && cat > \"$1\"", "sh", destination },
            workingDirectory: null,
            environment: null,
            enableStandardInput: true);

        return ExecuteCopyAsync(container, settings, observer, source, destination, cancellationToken, async process =>
        {
            // Validation and streaming share one handle, so a path swapped to a reparse point
            // after the builder checks cannot redirect the transfer.
            await using var sourceStream = HostFile.OpenRead(source);

            await using var stdin = process.NativeProcess.GetInputStream().AsStreamForWrite();
            var buffer = new byte[64 * 1024];
            long total = 0;
            for (;;)
            {
                var read = await sourceStream.ReadAsync(buffer, cancellationToken).ConfigureAwait(false);
                if (read == 0)
                {
                    break;
                }

                // Accumulate while streaming so a file that grows during the copy is aborted
                // even though the handle size was within the cap at open time.
                total += read;
                if (total > HostFile.MaxCopyBytes)
                {
                    throw new WslProcessException($"Copying '{source}' to '{destination}' exceeded 1 GiB limit.");
                }

                await stdin.WriteAsync(buffer.AsMemory(0, read), cancellationToken).ConfigureAwait(false);
            }

            await stdin.FlushAsync(cancellationToken).ConfigureAwait(false);
        });
    }

    public static Task CopyFromAsync(
        Microsoft.WSL.Containers.Container container,
        string source,
        string destination,
        CancellationToken cancellationToken,
        Action<LogLine>? observer)
    {
        var destinationPath = Path.GetFullPath(destination);
        AtomicFile.EnsureReplaceable(destinationPath);
        var destinationDirectory = Path.GetDirectoryName(destinationPath);
        if (!string.IsNullOrEmpty(destinationDirectory))
        {
            Directory.CreateDirectory(destinationDirectory);
        }

        // The transfer writes a sibling temp file; the destination is only replaced after the
        // container command exits successfully, so failures cannot truncate existing host data.
        var tempPath = AtomicFile.CreateTempPath(destinationPath);

        var settings = CreateSettings(
            new List<string>(5) { "/bin/sh", "-c", "cat -- \"$1\"", "sh", source },
            workingDirectory: null,
            environment: null,
            outputMode: ProcessOutputMode.Stream);

        return ExecuteCopyAsync(
            container,
            settings,
            observer,
            source,
            destination,
            cancellationToken,
            async process =>
            {
                await using var output = new FileStream(
                    tempPath, FileMode.CreateNew, FileAccess.Write, FileShare.None, 64 * 1024, FileOptions.Asynchronous);
                using IInputStream stdout = process.NativeProcess.GetOutputStream(ProcessOutputHandle.StandardOutput);
                await CopyStdoutAsync(stdout, output, source, destination, cancellationToken).ConfigureAwait(false);
            },
            commit: () => AtomicFile.Commit(tempPath, destinationPath),
            rollback: () => AtomicFile.Discard(tempPath));
    }

    /// <summary>
    /// Streams a container stdout pipe into <paramref name="output"/> in bounded chunks while
    /// honoring <paramref name="cancellationToken"/> even when the pipe is idle.
    /// </summary>
    internal static async Task CopyStdoutAsync(
        IInputStream stdout,
        Stream output,
        string source,
        string destination,
        CancellationToken cancellationToken)
    {
        // The WinRT stream adapter rejects InputStreamOptions::Partial, so read explicitly
        // through a DataReader instead of AsStreamForRead().
        using var reader = new DataReader(stdout) { InputStreamOptions = InputStreamOptions.None };

        // Per ADR bounded loops we don't allow while(true): copy the container stdout
        // in fixed 64 KiB chunks up to 1 GiB, then fail. The cap bounds both bytes and iterations.
        const ulong MaxBytes = 1024u * 1024u * 1024u;
        const uint ChunkSize = 64 * 1024;
        const int MaxChunks = (int)(MaxBytes / ChunkSize) + 1;
        ulong total = 0;
        for (var chunk = 0; chunk < MaxChunks; chunk++)
        {
            // Await with the token so a cancelled copy does not wait for the next chunk (or
            // forever on a silent container); the caller kills the process, unblocking the read.
            var loaded = await reader.LoadAsync(ChunkSize).AsTask(cancellationToken).ConfigureAwait(false);
            if (loaded == 0)
            {
                break;
            }

            total += loaded;
            if (total > MaxBytes)
            {
                throw new WslProcessException($"Copying '{source}' to '{destination}' exceeded 1 GiB limit.");
            }

            // DataReader.ReadBytes(byte[]) requires an exact-length array, so each
            // chunk allocates exactly the loaded size instead of sharing a buffer.
            var buffer = new byte[loaded];
            reader.ReadBytes(buffer);
            await output.WriteAsync(buffer.AsMemory(0, (int)loaded), cancellationToken).ConfigureAwait(false);
        }
    }

    private static async Task ExecuteCopyAsync(
        Microsoft.WSL.Containers.Container container,
        ProcessSettings settings,
        Action<LogLine>? observer,
        string source,
        string destination,
        CancellationToken cancellationToken,
        Func<ContainerProcess, Task> transfer,
        Action? commit = null,
        Action? rollback = null)
    {
        var process = Start(container, settings, observer);

        try
        {
            await transfer(process).ConfigureAwait(false);
            await process.WaitForExitAsync(cancellationToken).ConfigureAwait(false);
            ThrowIfCopyFailed(process, source, destination);
            commit?.Invoke();
        }
        catch
        {
            rollback?.Invoke();
            await process.KillAsync(ContainerProcess.AbortGracePeriod, CancellationToken.None).ConfigureAwait(false);
            throw;
        }
        finally
        {
            await process.DisposeAsync().ConfigureAwait(false);
        }
    }

    private static void ThrowIfCopyFailed(ContainerProcess process, string source, string destination)
    {
        if (process.ExitCode != 0)
        {
            throw new WslProcessException($"Copying '{source}' to '{destination}' failed: {process.StderrText.Trim()}");
        }
    }
}
