#pragma once

#include "internal/container_process.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace wslc::internal
{

/// <summary>
/// Tracks live child processes so Stop/Dispose can terminate them. Processes must be added
/// before they Start, otherwise a concurrent Stop could Snapshot the registry before the
/// process is registered and leak it. Exited entries are pruned on add, so callers that forget
/// per-process disposal cannot grow the registry without bound.
/// </summary>
class ProcessRegistry
{
public:
    void Add(std::shared_ptr<ContainerProcessState> process)
    {
        std::lock_guard lock(m_gate);
        PruneLocked();
        // weak_ptr construction only takes the control block; there is nothing to move.
        m_processes.emplace_back(process);
    }

    void Remove(const std::shared_ptr<ContainerProcessState>& process)
    {
        std::lock_guard lock(m_gate);
        for (auto it = m_processes.begin(); it != m_processes.end(); ++it)
        {
            if (it->lock().get() == process.get())
            {
                m_processes.erase(it);
                break;
            }
        }
    }

    std::vector<std::shared_ptr<ContainerProcessState>> Snapshot()
    {
        std::lock_guard lock(m_gate);
        return SnapshotLocked();
    }

    /// <summary>
    /// Atomically removes and returns the live processes. Returning and clearing under one lock
    /// guarantees a process added concurrently is kept for the next Stop instead of being dropped.
    /// This is the only drain path; tests assert emptiness through Snapshot().
    /// </summary>
    std::vector<std::shared_ptr<ContainerProcessState>> TakeAll()
    {
        std::lock_guard lock(m_gate);
        PruneLocked();
        auto result = SnapshotLocked();
        m_processes.clear();
        return result;
    }

private:
    std::vector<std::shared_ptr<ContainerProcessState>> SnapshotLocked()
    {
        std::vector<std::shared_ptr<ContainerProcessState>> result;
        result.reserve(m_processes.size());
        for (const auto& weak : m_processes)
        {
            if (auto process = weak.lock())
            {
                result.push_back(std::move(process));
            }
        }

        return result;
    }

    void PruneLocked()
    {
        for (auto it = m_processes.begin(); it != m_processes.end();)
        {
            bool remove = false;
            try
            {
                const auto process = it->lock();
                remove = process == nullptr || process->HasExited();
            }
            catch (...)
            {
                // A faulted liveness probe (e.g. a torn-down native Handle) is treated as gone.
                remove = true;
            }

            if (remove)
            {
                it = m_processes.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::mutex m_gate;
    std::vector<std::weak_ptr<ContainerProcessState>> m_processes;
};

} // namespace wslc::internal
