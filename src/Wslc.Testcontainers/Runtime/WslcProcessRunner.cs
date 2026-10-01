using System.Text;
using Microsoft.WSL.Containers;
using Windows.Storage.Streams;

namespace Wslc.Testcontainers.Runtime;

/// <summary>
/// Executes processes inside an official WSL container and streams files through process
/// stdio. All operations use <see cref="Microsoft.WSL.Containers"/> only.
/// </summary>
internal static class WslcProcessRunner
{
    public static ProcessSettings CreateSettings(
        IReadOnlyList<string> commandLine,
        string? workingDirectory,
        IReadOnlyDictionary<string, string>? environment,
        bool enableStandardInput = false,
        ProcessOutputMode outputMode = ProcessOutputMode.Event)
    {
        if (commandLine.Count == 0)
        {
            throw new ArgumentException("A command line is required.", nameof(commandLine));
        }

        // When the caller passes a freshly built List<string> (WslContainer path) take
        // ownership to avoid a second copy; otherwise copy once with known capacity.
        List<string> owned;
        if (commandLine is List<string> list)
        {
            owned = list;
        }
        else
        {
            owned = new List<string>(commandLine.Count);
            for (var i = 0; i < commandLine.Count; i++)
            {
                owned.Add(commandLine[i]);
            }
        }

        var settings = new ProcessSettings
        {
            CommandLine = owned,
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

    public static ContainerProcess Start(
        Microsoft.WSL.Containers.Container container,
        ProcessSettings settings,
        Action<LogLine>? observer)
    {
        var native = container.CreateProcess(settings);
        var capturesOutputEvents = settings.OutputMode == ProcessOutputMode.Event;
        var process = new ContainerProcess(native, captureOutput: capturesOutputEvents, observer);
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
                    await process.KillAsync(CancellationToken.None).ConfigureAwait(false);
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
            await process.KillAsync(CancellationToken.None).ConfigureAwait(false);
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
        const long MaxCopyBytes = 1024L * 1024L * 1024L;
        var fullSource = Path.GetFullPath(source);
        var sourceLength = new FileInfo(fullSource).Length;
        if (sourceLength > MaxCopyBytes)
        {
            throw new WslProcessException($"Copying '{source}' to '{destination}' exceeds 1 GiB limit ({sourceLength} bytes).");
        }

        var settings = CreateSettings(
            new List<string>(5) { "/bin/sh", "-c", "mkdir -p \"$(dirname \"$1\")\" && cat > \"$1\"", "sh", destination },
            workingDirectory: null,
            environment: null,
            enableStandardInput: true);

        return ExecuteCopyAsync(container, settings, observer, source, destination, cancellationToken, async process =>
        {
            await using var sourceStream = File.OpenRead(fullSource);
            await using var stdin = process.NativeProcess.GetInputStream().AsStreamForWrite();
            await sourceStream.CopyToAsync(stdin, cancellationToken).ConfigureAwait(false);
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
        var settings = CreateSettings(
            new List<string>(5) { "/bin/sh", "-c", "cat -- \"$1\"", "sh", source },
            workingDirectory: null,
            environment: null,
            outputMode: ProcessOutputMode.Stream);

        return ExecuteCopyAsync(container, settings, observer, source, destination, cancellationToken, async process =>
        {
            var destinationPath = Path.GetFullPath(destination);
            var destinationDirectory = Path.GetDirectoryName(destinationPath);
            if (!string.IsNullOrEmpty(destinationDirectory))
            {
                Directory.CreateDirectory(destinationDirectory);
            }

            await using var output = File.Create(destinationPath);

            // The WinRT stream adapter rejects InputStreamOptions::Partial, so read
            // explicitly through a DataReader instead of AsStreamForRead().
            using IInputStream stdout = process.NativeProcess.GetOutputStream(ProcessOutputHandle.StandardOutput);
            using var reader = new DataReader(stdout) { InputStreamOptions = InputStreamOptions.None };

            // Per ADR bounded loops we don't allow while(true): copy the container stdout
            // in fixed 64 KiB chunks up to 1 GiB, then fail. The cap bounds both bytes and iterations.
            const ulong MaxBytes = 1024u * 1024u * 1024u;
            const uint ChunkSize = 64 * 1024;
            const int MaxChunks = (int)(MaxBytes / ChunkSize) + 1;
            ulong total = 0;
            for (var chunk = 0; chunk < MaxChunks; chunk++)
            {
                var loaded = await reader.LoadAsync(ChunkSize);
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
        });
    }

    private static async Task ExecuteCopyAsync(
        Microsoft.WSL.Containers.Container container,
        ProcessSettings settings,
        Action<LogLine>? observer,
        string source,
        string destination,
        CancellationToken cancellationToken,
        Func<ContainerProcess, Task> transfer)
    {
        var process = Start(container, settings, observer);

        try
        {
            await transfer(process).ConfigureAwait(false);
            await process.WaitForExitAsync(cancellationToken).ConfigureAwait(false);
            ThrowIfCopyFailed(process, source, destination);
        }
        catch
        {
            await process.KillAsync(CancellationToken.None).ConfigureAwait(false);
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
