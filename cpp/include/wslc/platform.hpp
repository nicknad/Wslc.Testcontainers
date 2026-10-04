#pragma once

#include "wslc/exceptions.hpp"

#include <cstdint>
#include <string>

namespace wslc
{

/// <summary>A WSL runtime version (Major.Minor.Revision).</summary>
struct WslVersion
{
    std::uint32_t Major = 0;
    std::uint32_t Minor = 0;
    std::uint32_t Revision = 0;
};

/// <summary>
/// Windows-only platform guard for WSLC. The library runs exclusively on
/// Windows 10 Build 19041+ (x64/ARM64) with WSL container support; every public entry point
/// fails fast off-platform instead of surfacing obscure loader or COM errors.
/// </summary>
class WslPlatform
{
public:
    /// <summary>
    /// Minimum supported WSL runtime version with container support (2.9.3, the release where
    /// WSLC shipped). This is the runtime feature floor, independent of the SDK package version.
    /// </summary>
    static constexpr WslVersion MinimumWslVersion{2, 9, 3};

    /// <summary>Gets a value indicating whether the current machine can Run WSLC.</summary>
    static bool IsSupported();

    /// <summary>Gets a short description of the current OS and architecture for diagnostics.</summary>
    static std::string DescribeCurrent();

    /// <summary>Throws std::runtime_error when the current machine cannot Run WSLC.</summary>
    static void ThrowIfUnsupported();

    /// <summary>Returns true when a WSL (Major.Minor.patch) version meets the minimum requirement.</summary>
    static bool IsWslVersionSupported(std::uint32_t Major, std::uint32_t Minor, std::uint32_t patch) noexcept;
};

} // namespace wslc
