#pragma once

#include <string>

namespace wslc
{

/// <summary>Windows endpoint to connect to for a mapped Linux TCP port.</summary>
struct WslEndpoint
{
    /// <summary>Windows address the mapping is actually bound to; wildcard/default bindings
    /// resolve to loopback.</summary>
    std::string Host;

    /// <summary>Runtime-assigned Windows host port.</summary>
    int Port = 0;
};

} // namespace wslc
