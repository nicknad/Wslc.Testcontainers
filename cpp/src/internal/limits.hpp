#pragma once

#include "internal/util.hpp"
#include "wslc/exceptions.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace wslc::internal
{

/// <summary>
/// Central caps for builder-provided values. Single-value caps fail fast in the
/// <c>with...</c> setters; aggregate caps are enforced by <c>Build</c> because those values
/// accumulate across calls. The caps bound the resource sizes the WSLC runtime accepts and
/// the startup work a container requests.
/// </summary>
inline constexpr std::uint32_t c_maxCpuCount = 64;
inline constexpr std::uint32_t c_maxMemoryMb = 1024u * 1024u;
inline constexpr std::size_t c_maxCommandArguments = 1000;
inline constexpr std::size_t c_maxEnvironmentVariables = 1000;
inline constexpr std::size_t c_maxEnvironmentValueBytes = 128u * 1024u;
inline constexpr std::size_t c_maxFileCopies = 64;
inline constexpr std::size_t c_maxVolumeMounts = 64;
inline constexpr std::size_t c_maxSessionVolumes = 64;
inline constexpr std::size_t c_maxWaitStrategies = 16;
inline constexpr std::uint64_t c_maxSessionVolumeBytes = 1ull << 40;
inline constexpr std::uint64_t c_maxTarballBytes = c_maxSessionVolumeBytes;
inline constexpr std::chrono::milliseconds c_maxStartupTimeout = std::chrono::hours(24);
inline constexpr std::chrono::milliseconds c_maxExecTimeout = std::chrono::hours(24);

inline void RequireCpuCount(std::uint32_t cpuCount)
{
    if (cpuCount == 0)
    {
        throw WslException("CPU count must be positive.");
    }

    if (cpuCount > c_maxCpuCount)
    {
        throw WslException("CPU count " + std::to_string(cpuCount) + " exceeds the maximum of " +
                           std::to_string(c_maxCpuCount) + ".");
    }
}

inline void RequireMemoryMb(std::uint32_t megabytes)
{
    if (megabytes == 0)
    {
        throw WslException("Memory limit must be positive.");
    }

    if (megabytes > c_maxMemoryMb)
    {
        throw WslException("Memory limit " + std::to_string(megabytes) + " MB exceeds the maximum of " +
                           std::to_string(c_maxMemoryMb) + " MB (1 TiB).");
    }
}

inline void RequireSessionVolumeSize(std::uint64_t sizeBytes)
{
    if (sizeBytes == 0)
    {
        throw WslException("Session volume size must be positive.");
    }

    if (sizeBytes > c_maxSessionVolumeBytes)
    {
        throw WslException("Session volume size " + std::to_string(sizeBytes) + " bytes exceeds the maximum of " +
                           std::to_string(c_maxSessionVolumeBytes) + " bytes (1 TiB).");
    }
}

inline void RequireStartupTimeout(std::chrono::milliseconds timeout)
{
    if (timeout <= std::chrono::milliseconds::zero())
    {
        throw WslException("Timeout must be positive.");
    }

    if (timeout > c_maxStartupTimeout)
    {
        throw WslException("Startup timeout " + FormatMilliseconds(timeout) + "s exceeds the maximum of " +
                           FormatMilliseconds(c_maxStartupTimeout) + "s (24 h).");
    }
}

inline void RequireExecTimeout(std::chrono::milliseconds timeout)
{
    if (timeout <= std::chrono::milliseconds::zero())
    {
        throw WslException("Exec Timeout must be positive.");
    }

    if (timeout > c_maxExecTimeout)
    {
        throw WslException("Exec timeout " + FormatMilliseconds(timeout) + "s exceeds the maximum of " +
                           FormatMilliseconds(c_maxExecTimeout) + "s (24 h).");
    }
}

inline void RequireEnvironmentValue(std::string_view name, std::string_view value)
{
    if (value.size() > c_maxEnvironmentValueBytes)
    {
        throw WslException("Environment variable '" + std::string(name) + "' value is " + std::to_string(value.size()) +
                           " bytes; the maximum is " + std::to_string(c_maxEnvironmentValueBytes) +
                           " bytes (128 KiB).");
    }
}

inline void RequireTarballSize(const std::string& pathText, std::uintmax_t length)
{
    if (length > c_maxTarballBytes)
    {
        throw WslException("Tarball '" + pathText + "' is " + std::to_string(length) + " bytes; the maximum is " +
                           std::to_string(c_maxTarballBytes) + " bytes (1 TiB).");
    }
}

inline void RequireCount(std::size_t count, std::size_t maximum, std::string_view what)
{
    if (count > maximum)
    {
        throw WslException("Too many " + std::string(what) + ": " + std::to_string(count) +
                           " configured, but the maximum is " + std::to_string(maximum) + ".");
    }
}

inline void RequireWaitStrategyCount(std::size_t count)
{
    if (count > c_maxWaitStrategies)
    {
        throw WslException("Too many wait strategies: " + std::to_string(count) +
                           " configured or combined, but the maximum is " + std::to_string(c_maxWaitStrategies) + ".");
    }
}

} // namespace wslc::internal
