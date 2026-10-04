#pragma once

#include "wslc/wsl_container.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace wslc
{

/// <summary>
/// Base class for typed module containers. Forwards lifecycle, Exec, copy and log operations so
/// module users are not blocked when they need ad-hoc initialization (e.g. seeding a database).
/// </summary>
class WslModuleContainer
{
public:
    WslModuleContainer(const WslModuleContainer&) = delete;
    WslModuleContainer& operator=(const WslModuleContainer&) = delete;
    WslModuleContainer(WslModuleContainer&&) noexcept = default;
    WslModuleContainer& operator=(WslModuleContainer&&) noexcept = default;
    virtual ~WslModuleContainer() = default;

    /// <summary>Gets the unique Name of the underlying WSLC instance.</summary>
    const std::string& Name() const { return m_inner.Name(); }

    /// <summary>Gets the container Image reference, when one was configured.</summary>
    std::optional<std::string> Image() const { return m_inner.Image(); }

    /// <summary>Gets a value indicating whether the container has been started.</summary>
    bool IsStarted() const { return m_inner.IsStarted(); }

    /// <summary>Gets the Host address that exposes mapped ports.</summary>
    std::string Host() const { return m_inner.Host(); }

    /// <summary>Creates and provisions the Environment.</summary>
    void Start(std::stop_token token = {}) { m_inner.Start(token); }

    /// <summary>Stops the Environment and its processes (storage preserved for restart).</summary>
    void Stop(std::stop_token token = {}) { m_inner.Stop(token); }

    /// <summary>Executes a command and captures its exit code, stdout and stderr.</summary>
    ExecResult Exec(std::string command, std::vector<std::string> arguments = {}, ExecOptions options = {},
                    std::stop_token token = {})
    {
        return m_inner.Exec(std::move(command), std::move(arguments), std::move(options), token);
    }

    /// <summary>Starts a long-running process inside the Environment.</summary>
    std::unique_ptr<IWslProcess> StartProcess(std::string command, std::vector<std::string> arguments = {},
                                              ExecOptions options = {}, std::stop_token token = {})
    {
        return m_inner.StartProcess(std::move(command), std::move(arguments), std::move(options), token);
    }

    /// <summary>Copies a Windows file into the Environment.</summary>
    void CopyTo(const std::filesystem::path& HostPath, std::string ContainerPath, std::stop_token token = {})
    {
        m_inner.CopyTo(HostPath, std::move(ContainerPath), token);
    }

    /// <summary>Copies a Linux file out of the Environment.</summary>
    void CopyFrom(std::string ContainerPath, const std::filesystem::path& HostPath, std::stop_token token = {})
    {
        m_inner.CopyFrom(std::move(ContainerPath), HostPath, token);
    }

    /// <summary>Streams all Logs captured by the Environment.</summary>
    LogStream Logs() { return m_inner.Logs(); }

    /// <summary>Returns a bounded Snapshot of the most recent log lines, oldest first.</summary>
    std::vector<LogLine> GetRecentLogs(int maxLines = 50) const { return m_inner.GetRecentLogs(maxLines); }

    /// <summary>Stops and releases the container, deleting ephemeral storage. Idempotent.</summary>
    void Dispose() { m_inner.Dispose(); }

    /// <summary>Gets the Windows port mapped to a Linux TCP service port.</summary>
    int GetMappedPort(int port) const { return m_inner.GetMappedPort(port); }

    /// <summary>Gets the Windows address to connect to for a mapped Linux TCP port.</summary>
    std::string GetMappedHost(int port) const { return m_inner.GetMappedHost(port); }

protected:
    /// <summary>Initializes a wrapper around the given container.</summary>
    explicit WslModuleContainer(WslContainer inner) : m_inner(std::move(inner)) {}

    /// <summary>Gets the underlying container for advanced scenarios.</summary>
    WslContainer& Inner() noexcept { return m_inner; }
    const WslContainer& Inner() const noexcept { return m_inner; }

private:
    WslContainer m_inner;
};

} // namespace wslc
