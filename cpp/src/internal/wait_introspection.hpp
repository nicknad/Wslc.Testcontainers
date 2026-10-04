#pragma once

#include "wslc/waiting/i_wait_strategy.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace wslc::waiting::detail
{

/// <summary>
/// Returns the Name of the first built-in network Wait strategy (TCP/HTTP) in the list,
/// recursing into composites. Custom Strategies are intentionally not detected.
/// </summary>
std::optional<std::string> FindNetworkWait(const std::vector<std::shared_ptr<IWaitStrategy>>& Strategies);

} // namespace wslc::waiting::detail
