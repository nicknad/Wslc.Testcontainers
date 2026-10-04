namespace Wslc.Testcontainers.Internal;

/// <summary>
/// Tracks live child processes so Stop/Dispose can terminate them. Processes must be added
/// before they start, otherwise a concurrent stop could snapshot the registry before the
/// process is registered and leak it. Exited entries are pruned on add, so callers that
/// forget per-process disposal cannot grow the registry without bound.
/// </summary>
internal sealed class ProcessRegistry
{
    private readonly object _gate = new();
    private readonly List<IWslProcess> _processes = new();

    public void Add(IWslProcess process)
    {
        lock (_gate)
        {
            PruneLocked();
            _processes.Add(process);
        }
    }

    public void Remove(IWslProcess process)
    {
        lock (_gate)
        {
            _processes.Remove(process);
        }
    }

    public IWslProcess[] Snapshot()
    {
        lock (_gate)
        {
            return _processes.ToArray();
        }
    }

    /// <summary>
    /// Atomically removes and returns the live processes. Returning and clearing under one lock
    /// guarantees a process added concurrently is kept for the next stop instead of being dropped.
    /// </summary>
    public IWslProcess[] TakeAll()
    {
        lock (_gate)
        {
            PruneLocked();
            var processes = _processes.ToArray();
            _processes.Clear();
            return processes;
        }
    }

    public void Clear()
    {
        lock (_gate)
        {
            _processes.Clear();
        }
    }

    private void PruneLocked()
    {
        for (var i = _processes.Count - 1; i >= 0; i--)
        {
            try
            {
                if (_processes[i].HasExited)
                {
                    _processes.RemoveAt(i);
                }
            }
            catch
            {
                // A faulted HasExited probe (e.g. a torn-down native handle) is treated as gone.
                _processes.RemoveAt(i);
            }
        }
    }
}
