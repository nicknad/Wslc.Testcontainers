#pragma once

#include "internal/configuration.hpp"
#include "wslc/log_line.hpp"

#include <wslcsdk.h>

#include <functional>
#include <stop_token>
#include <string>
#include <string_view>

namespace wslc::internal
{

/// <summary>Pulls or imports the Image required by a container configuration.</summary>
class ImageResolver
{
public:
    ImageResolver(WslcSession session, const Configuration& configuration, std::function<void(LogLine)> publish);

    std::string Resolve(const std::string& fallback_image_name, std::stop_token token);

    /// <summary>
    /// Compares two Image references after Docker-style canonicalization: an implicit docker.io
    /// registry, an implicit library/ namespace and an implicit :latest tag.
    /// </summary>
    static bool MatchesImage(std::string_view candidate, std::string_view Image);

private:
    bool ImageExists(const std::string& Image, std::stop_token token);

    WslcSession m_session;
    const Configuration& m_configuration;
    std::function<void(LogLine)> m_publish;
};

} // namespace wslc::internal
