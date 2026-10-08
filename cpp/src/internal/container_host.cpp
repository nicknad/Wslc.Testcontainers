#include "internal/container_host.hpp"

#include "internal/api.hpp"
#include "internal/util.hpp"
#include "wslc/environment.hpp"
#include "wslc/exceptions.hpp"
#include "wslc/resource_reaper.hpp"

#include <map>
#include <mutex>

namespace wslc::internal
{

namespace
{

std::string ComponentNames(WslcComponentFlags flags)
{
    std::vector<std::string> names;
    if ((flags & WSLC_COMPONENT_FLAG_VIRTUAL_MACHINE_PLATFORM) != 0)
    {
        names.emplace_back("VirtualMachinePlatform");
    }

    if ((flags & WSLC_COMPONENT_FLAG_WSL_PACKAGE) != 0)
    {
        names.emplace_back("WslPackage");
    }

    if ((flags & WSLC_COMPONENT_FLAG_SDK_NEEDS_UPDATE) != 0)
    {
        names.emplace_back("SdkNeedsUpdate");
    }

    return join(names, ", ");
}

struct CleanupRegistry
{
    std::mutex gate;
    std::map<std::string, std::function<void()>, std::less<>> callbacks;
};

CleanupRegistry& cleanup_registry()
{
    static CleanupRegistry registry;
    return registry;
}

void CleanupAll()
{
    std::vector<std::function<void()>> callbacks;
    {
        CleanupRegistry& registry = cleanup_registry();
        std::lock_guard lock(registry.gate);
        for (auto& pair : registry.callbacks)
        {
            callbacks.push_back(pair.second);
        }
    }

    for (auto& callback : callbacks)
    {
        try
        {
            callback();
        }
        catch (...)
        {
            // Best effort only.
        }
    }
}

void HookProcessExit()
{
    static std::once_flag flag;
    std::call_once(flag,
                   []
                   {
                       // Construct the registry before registering the exit hook: atexit handlers
                       // and static destructors run in reverse registration order, so CleanupAll
                       // must be registered after the registry exists or it would iterate a
                       // destroyed registry at process exit (use-after-free).
                       cleanup_registry();
                       std::atexit(&CleanupAll);
                   });
}

} // namespace

void WslcHost::EnsureAvailable()
{
    EnsureComInitialized();
    WslPlatform::ThrowIfUnsupported();

    WslVersion version{};
    WslcVersion nativeVersion{};
    const HRESULT version_result = WslcGetVersion(&nativeVersion);
    if (FAILED(version_result))
    {
        throw WslRuntimeException("The WSL container runtime is not available. Install WSL 2.9.3 or newer with 'wsl "
                                  "--install' and retry. (HRESULT " +
                                  HresultHex(version_result) + ")");
    }

    version = WslVersion{nativeVersion.major, nativeVersion.minor, nativeVersion.revision};
    if (!WslPlatform::IsWslVersionSupported(version.Major, version.Minor, version.Revision))
    {
        throw WslRuntimeException("WSL " + std::to_string(version.Major) + "." + std::to_string(version.Minor) + "." +
                                  std::to_string(version.Revision) +
                                  " is too old. Wslc.Testcontainers requires WSL 2.9.3 or newer. " +
                                  "Run 'wsl --update' and retry.");
    }

    WslcComponentFlags missing = WSLC_COMPONENT_FLAG_NONE;
    const HRESULT components_result = WslcGetMissingComponents(&missing);
    if (FAILED(components_result))
    {
        throw WslRuntimeException("The WSL container runtime is not available. Install WSL 2.9.3 or newer with 'wsl "
                                  "--install' and retry. (HRESULT " +
                                  HresultHex(components_result) + ")");
    }

    if (missing != WSLC_COMPONENT_FLAG_NONE)
    {
        throw WslRuntimeException("The WSL container runtime is missing required components (" +
                                  ComponentNames(missing) + "). Run 'wsl --install' (or 'wsl --update') and retry.");
    }
}

std::string WslcHost::GetVersion()
{
    EnsureComInitialized();
    WslcVersion version{};
    if (FAILED(WslcGetVersion(&version)))
    {
        return "unknown";
    }

    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.revision);
}

void ContainerHost::EnsureInitialized(std::stop_token token)
{
    EnsureComInitialized();
    static std::once_flag flag;
    static std::exception_ptr error;

    std::call_once(flag,
                   []
                   {
                       try
                       {
                           HookProcessExit();
                           WslcHost::EnsureAvailable();

                           if (WslEnvironment::CleanupEnabled())
                           {
                               try
                               {
                                   WslResourceReaper::Cleanup();
                               }
                               catch (...)
                               {
                                   // Reaping is best effort and must never block container startup.
                               }
                           }
                       }
                       catch (...)
                       {
                           error = std::current_exception();
                       }
                   });

    if (error)
    {
        std::rethrow_exception(error);
    }

    ThrowIfStopped(token);
}

void ContainerHost::RegisterCleanup(std::string Name, std::function<void()> Cleanup)
{
    CleanupRegistry& registry = cleanup_registry();
    std::lock_guard lock(registry.gate);
    registry.callbacks[std::move(Name)] = std::move(Cleanup);
}

void ContainerHost::UnregisterCleanup(const std::string& Name)
{
    CleanupRegistry& registry = cleanup_registry();
    std::lock_guard lock(registry.gate);
    registry.callbacks.erase(Name);
}

} // namespace wslc::internal
