using Wslc.Testcontainers;

// Minimal Wslc round-trip: start alpine, exec, stream logs, dispose.
// Requires Windows + WSL 2.9.3 with container support. Run with:
//   dotnet run --project examples/Quickstart

await using var container = new WslContainerBuilder()
    .WithImage("docker.io/library/alpine:latest")
    .WithCommand("/bin/sh", "-c", "while true; do sleep 3600; done")
    .WithEnvironment("HELLO", "wslc")
    .WithReadinessTimeout(TimeSpan.FromMinutes(2))
    .Build();

await container.StartAsync();

var whoami = await container.ExecAsync("sh", new[] { "-c", "echo $HELLO && uname -a" }, null, CancellationToken.None);
whoami.EnsureSuccess();
Console.WriteLine($"exit={whoami.ExitCode} stdout={whoami.Stdout.Trim()}");

using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(3));
try
{
    await foreach (var line in container.SubscribeLogs(cts.Token))
    {
        Console.WriteLine(line);
    }
}
catch (OperationCanceledException)
{
    // Expected: log stream is infinite until the timeout above.
}

Console.WriteLine($"container {container.Name} ready. Press Ctrl+C to dispose.");
