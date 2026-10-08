#pragma once

#include "internal/api.hpp"
#include "internal/container_process.hpp"
#include "wslc/exec.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace wslc::internal
{

/// <summary>Views a command line as the PCSTR array the SDK expects; the strings must outlive the array.</summary>
std::vector<PCSTR> ToNativeArgv(const std::vector<std::string>& CommandLine);

/// <summary>Renders one "KEY=VALUE" string per environment entry.</summary>
std::vector<std::string> ToEnvironmentStrings(const std::map<std::string, std::string>& Environment);

/// <summary>Views "KEY=VALUE" strings as the PCSTR array the SDK expects; the strings must outlive the array.</summary>
std::vector<PCSTR> ToNativeEnvironment(const std::vector<std::string>& Environment);

/// <summary>Description of a process to create inside a container.</summary>
struct ProcessSettings
{
    std::vector<std::string> CommandLine;
    std::optional<std::string> WorkingDirectory;
    std::map<std::string, std::string> Environment;
    bool EnableStandardInput = false;
};

/// <summary>
/// Executes processes inside a WSL container and streams Files through process stdio.
/// All operations use the official wslcsdk API only.
/// </summary>
class ProcessRunner
{
public:
    /// <summary>Creates the shared capture/exit State without starting anything.</summary>
    static std::shared_ptr<ContainerProcessState> Prepare(std::function<void(LogLine)> observer, bool capture_output);

    /// <summary>
    /// Creates (and starts) the native process. Callers register the State before calling this
    /// so a concurrent Stop cannot miss it.
    /// </summary>
    static void CreateNative(WslcContainer container, const ProcessSettings& settings, ContainerProcessState& State);

    /// <summary>Runs a command to completion and captures its exit code, stdout and stderr.</summary>
    static ExecResult Run(WslcContainer container, const ProcessSettings& settings,
                          const std::optional<std::string>& StandardInput,
                          std::optional<std::chrono::milliseconds> Timeout, std::stop_token token,
                          std::function<void(LogLine)> observer);

    /// <summary>Copies a Windows file into the container through the process stdin.</summary>
    static void CopyTo(WslcContainer container, const std::filesystem::path& Source, const std::string& Destination,
                       std::stop_token token, std::function<void(LogLine)> observer);

    /// <summary>Copies a Linux file out of the container through the process stdout.</summary>
    static void CopyFrom(WslcContainer container, const std::string& Source, const std::filesystem::path& Destination,
                         std::stop_token token, std::function<void(LogLine)> observer);
};

} // namespace wslc::internal
