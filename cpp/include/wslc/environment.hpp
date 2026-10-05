#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace wslc
{

/// <summary>
/// Process-wide WSLC configuration resolved from Environment variables. Builder
/// configuration always takes precedence over these defaults.
/// Truthy values are <c>1/true/yes/on</c>; falsy are <c>0/false/no/off</c> (case-insensitive).
/// Any other non-empty value is treated as unset (falls back to defaults).
/// Data directory and session Id are snapshotted on first use; changing the Environment
/// afterwards has no effect on them.
/// </summary>
class WslEnvironment
{
public:
    /// <summary>Default readiness Timeout, in seconds or as a clock string.</summary>
    static constexpr const char* TimeoutVariable = "WSLC_TIMEOUT";

    /// <summary>Directory used to store WSLC instances and caches. Snapshotted on first store access.</summary>
    static constexpr const char* DataDirectoryVariable = "WSLC_DATA_DIRECTORY";

    /// <summary>Container Image used when a builder does not specify one.</summary>
    static constexpr const char* DefaultImageVariable = "WSLC_DEFAULT_IMAGE";

    /// <summary>Enables Reuse by default when set to a truthy value.</summary>
    static constexpr const char* ReuseVariable = "WSLC_REUSE";

    /// <summary>Set to a falsy value to disable automatic Cleanup and the orphan reaper.</summary>
    static constexpr const char* CleanupVariable = "WSLC_CLEANUP";

    /// <summary>Set to a truthy value to allow Reuse while running under CI.</summary>
    static constexpr const char* ReuseInCiVariable = "WSLC_REUSE_IN_CI";

    /// <summary>Overrides the session identifier used to Name WSLC instances. Snapshotted once per process.</summary>
    static constexpr const char* SessionIdVariable = "WSLC_SESSION_ID";

    /// <summary>Gets the configured default Image, when any.</summary>
    static std::optional<std::string> DefaultImage();

    /// <summary>
    /// Gets the data directory used for instance metadata and caches. Relative values are made
    /// absolute; UNC/device paths are rejected with WslException. Snapshotted on first use.
    /// </summary>
    static const std::filesystem::path& DataDirectory();

    /// <summary>Gets a value indicating whether Reuse is enabled by default.</summary>
    static bool ReuseByDefault();

    /// <summary>Gets a value indicating whether automatic Cleanup (including the orphan reaper) is enabled.</summary>
    static bool CleanupEnabled();

    /// <summary>
    /// Gets the session identifier ([A-Za-z0-9_-], 1-64 characters), generating a sanitized
    /// process-unique value when unset. Invalid configured values throw WslException.
    /// </summary>
    static const std::string& SessionId();

    /// <summary>Gets the default Wait Timeout (WSLC_TIMEOUT, default 60s).</summary>
    static std::chrono::milliseconds DefaultWaitTimeout();

    /// <summary>Gets a value indicating whether Reuse is allowed (disabled under CI unless WSLC_REUSE_IN_CI is
    /// set).</summary>
    static bool ReuseAllowed();

    /// <summary>Reads an Environment variable; returns nullopt when unset or blank.</summary>
    static std::optional<std::string> GetNonEmpty(const char* Name);

    /// <summary>Parses a truthy/falsy value; nullopt when the value is unset or unrecognized.</summary>
    static std::optional<bool> ParseBool(const std::optional<std::string>& value);

    /// <summary>Parses a Timeout (plain seconds or [-]d.hh:mm:ss[.fff] clock string); nullopt when invalid.</summary>
    static std::optional<std::chrono::milliseconds> ParseTimeout(const std::optional<std::string>& value);
};

} // namespace wslc
