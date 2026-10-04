using Wslc.Testcontainers.Internal;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class ProcessRegistryTests
{
    [Fact]
    public void Adding_prunes_processes_that_have_exited()
    {
        var registry = new ProcessRegistry();
        var exited = new FakeProcess { HasExited = true };
        var live = new FakeProcess();

        registry.Add(exited);
        registry.Add(live);

        Assert.Equal(new IWslProcess[] { live }, registry.Snapshot());
    }

    [Fact]
    public void Removing_a_process_takes_it_out_of_the_snapshot()
    {
        var registry = new ProcessRegistry();
        var process = new FakeProcess();
        registry.Add(process);

        registry.Remove(process);

        Assert.Empty(registry.Snapshot());
    }

    [Fact]
    public void Snapshot_is_a_copy()
    {
        var registry = new ProcessRegistry();
        registry.Add(new FakeProcess());

        var snapshot = registry.Snapshot();
        registry.Clear();

        Assert.Single(snapshot);
        Assert.Empty(registry.Snapshot());
    }

    [Fact]
    public void Faulted_liveness_probe_is_pruned()
    {
        var registry = new ProcessRegistry();
        var faulty = new FakeProcess { ThrowOnHasExited = true };
        var live = new FakeProcess();

        registry.Add(faulty);
        registry.Add(live);

        Assert.Equal(new IWslProcess[] { live }, registry.Snapshot());
    }

    private sealed class FakeProcess : IWslProcess
    {
        private bool _hasExited;

        public bool HasExited
        {
            get => ThrowOnHasExited ? throw new InvalidOperationException("gone") : _hasExited;
            set => _hasExited = value;
        }

        public bool ThrowOnHasExited { get; set; }

        public int? Id => null;

        public int ExitCode => 0;

        public Task<int> WaitForExitAsync(CancellationToken cancellationToken = default) => Task.FromResult(0);

        public Task KillAsync(CancellationToken cancellationToken = default) => Task.CompletedTask;

        public ValueTask DisposeAsync() => default;
    }
}
