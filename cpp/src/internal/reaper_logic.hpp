#pragma once

#include "internal/instance_store.hpp"

#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace wslc::internal
{

/// <summary>Decides whether WSLC-owned storage may be deleted automatically.</summary>
bool ShouldCleanup(const std::optional<InstanceMetadata>& metadata, bool owner_alive, bool include_reuse);

/// <summary>Returns true when a process with the pid is still running (access denied counts as alive).</summary>
bool IsOwnerAlive(int process_id);

/// <summary>Owner liveness with a PID-recycling guard based on the metadata creation time.</summary>
bool IsOwnerAlive(const InstanceMetadata& metadata);

/// <summary>Returns true when another process currently holds the instance Reuse lock.</summary>
bool IsReuseInstanceInUse(const std::filesystem::path& instanceDirectory);

std::vector<std::string> CleanupCore(std::stop_token token, bool include_reuse);
std::vector<std::string> PurgeReuseCore(std::stop_token token);

} // namespace wslc::internal
