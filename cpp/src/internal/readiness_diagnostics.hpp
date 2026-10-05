#pragma once

#include "wslc/exceptions.hpp"

#include <optional>
#include <string>
#include <utility>

namespace wslc::internal
{

/// <summary>Attaches container diagnostics to the readiness failure rethrown to consumers.</summary>
/// <remarks>
/// This is library-internal enrichment: failures are created without diagnostics by the wait
/// strategy and filled in by the container, so the thrown instance is fully populated.
/// </remarks>
struct ReadinessDiagnostics
{
    static WslReadinessException Enrich(const WslReadinessException& exception, std::optional<std::string> Image,
                                        std::optional<std::string> command, std::optional<int> ExitCode,
                                        std::optional<std::string> Stdout, std::optional<std::string> Stderr)
    {
        return exception.WithDiagnostics(std::move(Image), std::move(command), ExitCode, std::move(Stdout),
                                         std::move(Stderr));
    }
};

} // namespace wslc::internal
