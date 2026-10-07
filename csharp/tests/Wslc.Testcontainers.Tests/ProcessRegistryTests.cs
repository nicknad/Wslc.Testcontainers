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
        _ = registry.TakeAll();

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

    [Fact]
    public void TakeAll_returns_live_processes_and_empties_the_registry()
    {
        var registry = new ProcessRegistry();
        var first = new FakeProcess();
        var second = new FakeProcess();
        var exited = new FakeProcess { HasExited = true };

        registry.Add(first);
        registry.Add(second);

        Assert.Equal(new IWslProcess[] { first, second }, registry.TakeAll());
        Assert.Empty(registry.Snapshot());

        registry.Add(exited);
        Assert.Empty(registry.TakeAll());
    }

    [Fact]
    public void TakeAll_keeps_processes_added_afterward()
    {
        var registry = new ProcessRegistry();
        var first = new FakeProcess();
        var second = new FakeProcess();

        registry.Add(first);
        Assert.Equal(new IWslProcess[] { first }, registry.TakeAll());

        registry.Add(second);

        Assert.Equal(new IWslProcess[] { second }, registry.TakeAll());
        Assert.Empty(registry.Snapshot());
    }

    [Fact]
    public async Task TakeAll_does_not_drop_processes_added_concurrently()
    {
        const int processCount = 2_000;
        var cancellationToken = TestContext.Current.CancellationToken;
        var registry = new ProcessRegistry();
        var added = new HashSet<IWslProcess>(ReferenceEqualityComparer.Instance);
        var sentinel = new FakeProcess();

        var producer = Task.Run(
            () =>
            {
                for (var i = 0; i < processCount; i++)
                {
                    var process = new FakeProcess();
                    added.Add(process);
                    registry.Add(process);
                }

                added.Add(sentinel);
                registry.Add(sentinel);
            },
            cancellationToken);

        var drained = new HashSet<IWslProcess>(ReferenceEqualityComparer.Instance);
        var drain = Task.Run(
            () =>
            {
                var sawSentinel = false;
                while (!sawSentinel)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    foreach (var process in registry.TakeAll())
                    {
                        Assert.True(drained.Add(process), "TakeAll returned a process that had already been drained.");
                        sawSentinel = ReferenceEquals(process, sentinel);
                    }
                }
            },
            cancellationToken);

        await Task.WhenAll(producer, drain);

        Assert.Equal(processCount + 1, added.Count);
        Assert.Equal(processCount + 1, drained.Count);
        Assert.True(added.SetEquals(drained));
        Assert.Empty(registry.TakeAll());
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
