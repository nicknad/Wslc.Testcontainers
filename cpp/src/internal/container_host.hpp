#pragma once

#include "wslc/platform.hpp"

#include <functional>
#include <stop_token>
#include <string>

namespace wslc::internal
{

/// <summary>
/// Checks that the machine can Run WSL containers using the official SDK entry points.
/// </summary>
class WslcHost
{
public:
    /// <summary>Throws WslRuntimeException when WSL or a required component is missing.</summary>
    static void EnsureAvailable();

    /// <summary>Returns the runtime version as "Major.Minor.Revision" or "unknown".</summary>
    static std::string GetVersion();
};

/// <summary>
/// Process-wide WSLC initialization: verifies the runtime, runs storage Cleanup once, and
/// guarantees best-effort Cleanup of live containers when the Host process exits unexpectedly.
/// </summary>
class ContainerHost
{
public:
    static void EnsureInitialized(std::stop_token token);

    /// <summary>Registers a best-effort Cleanup callback invoked at process exit.</summary>
    static void RegisterCleanup(std::string Name, std::function<void()> Cleanup);

    static void UnregisterCleanup(const std::string& Name);
};

} // namespace wslc::internal
