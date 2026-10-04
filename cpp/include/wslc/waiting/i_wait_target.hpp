#pragma once

#include "wslc/exec.hpp"
#include "wslc/log_line.hpp"

#include <stop_token>
#include <string>
#include <vector>

namespace wslc::waiting
{

/// <summary>
/// The Environment a Wait strategy observes. Implementations are provided by
/// <see cref="wslc::WslContainer"/>; custom Strategies can be written against this abstraction.
/// </summary>
class IWaitTarget
{
public:
    virtual ~IWaitTarget() = default;

    /// <summary>Gets the WSLC instance Name.</summary>
    virtual const std::string& Name() const = 0;

    /// <summary>Gets the Host exposing mapped ports.</summary>
    virtual std::string Host() const = 0;

    /// <summary>Gets the mapped Host port for a Linux port.</summary>
    virtual int GetMappedPort(int ContainerPort) const = 0;

    /// <summary>Gets the Host address a probe should connect to for a mapped Linux TCP port.</summary>
    virtual std::string GetProbeHost(int ContainerPort) const = 0;

    /// <summary>Executes a command inside the Environment.</summary>
    virtual ExecResult Exec(std::string command, std::vector<std::string> arguments, std::stop_token token) = 0;

    /// <summary>Probes a Linux TCP port.</summary>
    virtual bool IsTcpPortOpen(int ContainerPort, std::stop_token token) = 0;

    /// <summary>Probes whether a Linux process is running.</summary>
    virtual bool IsProcessRunning(std::string processName, std::stop_token token) = 0;

    /// <summary>Returns a Snapshot of the recent Logs.</summary>
    virtual std::vector<LogLine> GetRecentLogs() const = 0;
};

} // namespace wslc::waiting
