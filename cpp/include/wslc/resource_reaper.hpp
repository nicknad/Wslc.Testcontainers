#pragma once

#include <stop_token>
#include <string>
#include <vector>

namespace wslc
{

/// <summary>
/// Removes storage left behind by WSLC sessions whose owning process no longer exists.
/// Cleanup is limited to WSLC-owned storage directories (<c>wslc-*</c>) because the runtime
/// does not expose session enumeration.
/// </summary>
/// <remarks>
/// Ephemeral instances are deleted when the Owner is gone. Reusable instances are preserved by
/// default (they survive Owner exit by design); use <c>PurgeReuse</c> or
/// <c>CleanupIncludingReuse</c> to reclaim them. Directories with missing/corrupt metadata
/// are deleted only after a 7-day grace period.
/// </remarks>
class WslResourceReaper
{
public:
    /// <summary>Deletes storage of abandoned ephemeral instances. Reusable instances are preserved.</summary>
    static std::vector<std::string> Cleanup(std::stop_token token = {});

    /// <summary>
    /// Deletes storage of abandoned instances including reusable ones whose Owner is gone,
    /// skipping Reuse instances another live process currently holds via the instance lock.
    /// </summary>
    static std::vector<std::string> CleanupIncludingReuse(std::stop_token token = {});

    /// <summary>
    /// Force-deletes all reusable instances regardless of Owner liveness and without checking
    /// the instance lock; do not call while another process may be using one.
    /// </summary>
    static std::vector<std::string> PurgeReuse(std::stop_token token = {});
};

} // namespace wslc
