#include "wslc/wsl_container.hpp"

#include "internal/api.hpp"
#include "internal/configuration.hpp"
#include "internal/container_host.hpp"
#include "internal/container_process.hpp"
#include "internal/image_resolver.hpp"
#include "internal/instance_store.hpp"
#include "internal/limits.hpp"
#include "internal/log_broadcaster.hpp"
#include "internal/port_mapping.hpp"
#include "internal/process_registry.hpp"
#include "internal/process_runner.hpp"
#include "internal/readiness_diagnostics.hpp"
#include "internal/util.hpp"
#include "wslc/environment.hpp"
#include "wslc/exceptions.hpp"
#include "wslc/platform.hpp"

#include <windows.h>

#include <wil/resource.h>

#include <atomic>
#include <condition_variable>
#include <format>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace wslc
{

namespace
{

constexpr std::chrono::milliseconds c_abortGracePeriod{2000};
constexpr std::chrono::milliseconds c_mainProcessGracePeriod{2000};
constexpr std::chrono::milliseconds c_containerStopTimeout{10000};
constexpr int c_maxReuseLockAttempts = 100;
constexpr std::chrono::milliseconds c_reuseLockRetryDelay{300};
constexpr int c_maxPortResolveAttempts = 200;
constexpr std::chrono::milliseconds c_portResolveDelay{50};
constexpr std::size_t c_maxRecentLogs = 50;
constexpr int c_maxStartAttempts = 200;
constexpr std::chrono::milliseconds c_startPollDelay{50};
constexpr std::chrono::milliseconds c_startThreadJoinTimeout{5000};

void ValidateProcessOptions(const ProcessOptions& options)
{
    internal::RequireCount(options.Environment.size(), internal::c_maxEnvironmentVariables,
                           "exec environment variables");
    for (const auto& pair : options.Environment)
    {
        internal::RequireEnvironmentName(pair.first);
        internal::RequireEnvironmentValue(pair.first, pair.second);
    }

    if (options.WorkingDirectory)
    {
        internal::ValidateContainerPath(*options.WorkingDirectory);
    }
}

void ValidateExecOptions(const ExecOptions& options)
{
    if (options.Timeout)
    {
        internal::RequireExecTimeout(*options.Timeout);
    }

    ValidateProcessOptions(options);
}

std::string DescribeException(const std::exception_ptr& error)
{
    try
    {
        std::rethrow_exception(error);
    }
    catch (const std::exception& exception)
    {
        return exception.what();
    }
    catch (...)
    {
        return "unknown error";
    }
}

} // namespace

struct WslContainer::Impl
{
    internal::Configuration configuration;
    internal::InstanceStore& store;
    bool Reuse = false;
    std::string Name;
    std::string owner_pid;

    std::shared_ptr<internal::LogBroadcaster> Logs = std::make_shared<internal::LogBroadcaster>();
    std::mutex lifecycle;
    mutable std::mutex state_gate;
    std::atomic<bool> disposed{false};
    std::atomic<bool> started{false};
    std::atomic<bool> storage_created{false};
    std::atomic<bool> dispose_requested{false};

    std::shared_ptr<internal::SessionHandle> session;
    std::shared_ptr<internal::ContainerHandle> container;
    std::shared_ptr<internal::PortMapping> network;
    std::shared_ptr<internal::ContainerProcessState> main_process;
    std::optional<internal::InstanceMetadata> metadata;
    wil::unique_hfile reuse_lock;
    internal::ProcessRegistry processes;

    // WslcStartContainer with ATTACH streams the init process IO and blocks until that process
    // exits, so the attached call runs on a worker thread while Start waits for the running
    // state and Stop joins the thread after tearing the container down. The completion state is
    // shared so a detached thread can never touch a destroyed Impl.
    struct StartState
    {
        std::mutex gate;
        std::condition_variable condition;
        bool finished = true;
        bool succeeded = false;
        std::string error;
    };

    std::shared_ptr<StartState> start_state = std::make_shared<StartState>();
    std::thread start_thread;

    explicit Impl(internal::Configuration config)
        : configuration(std::move(config)), store(internal::InstanceStore::DefaultStore()),
          Reuse(internal::IsReuseEffective(configuration.Reuse, WslEnvironment::ReuseByDefault(),
                                           WslEnvironment::ReuseAllowed())),
          Name(Reuse ? internal::WslNaming::CreateReuseName(internal::WslConfigHasher::Compute(configuration))
                     : internal::WslNaming::CreateInstanceName(store.SessionId())),
          owner_pid(std::to_string(internal::CurrentProcessId()))
    {
    }

    ~Impl()
    {
        Dispose();
        JoinStartThread();
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void Publish(LogLine line) { Logs->Publish(line); }
    void publish_diagnostic(std::string Text) { Logs->Publish(LogLine::Diagnostic(std::move(Text))); }

    void Start(WslContainer& target, std::stop_token caller);
    void Stop(std::stop_token token);
    void Dispose();

    ExecResult Exec(std::string command, std::vector<std::string> arguments, ExecOptions options,
                    std::stop_token token);

    std::unique_ptr<IWslProcess> StartProcess(std::string command, std::vector<std::string> arguments,
                                              const ProcessOptions& options, std::stop_token token);

    void CopyTo(const std::filesystem::path& HostPath, std::string ContainerPath, std::stop_token token);
    void CopyFrom(std::string ContainerPath, const std::filesystem::path& HostPath, std::stop_token token);
    std::vector<LogLine> RecentLogs(int maxLines) const;
    WslEndpoint connect_endpoint(int containerPort) const;
    bool tcp_port_open(int containerPort, std::stop_token token) const;
    bool process_running(std::string processName, std::stop_token token);

    std::optional<std::string> Image() const
    {
        if (configuration.Image)
        {
            return configuration.Image;
        }

        return configuration.TarballImageName;
    }

    std::shared_ptr<internal::ContainerHandle> get_container() const
    {
        std::lock_guard lock(state_gate);
        return container;
    }

    std::shared_ptr<internal::SessionHandle> get_session() const
    {
        std::lock_guard lock(state_gate);
        return session;
    }

    std::shared_ptr<internal::PortMapping> get_network() const
    {
        std::lock_guard lock(state_gate);
        return network;
    }

    std::shared_ptr<internal::ContainerProcessState> get_main_process() const
    {
        std::lock_guard lock(state_gate);
        return main_process;
    }

    std::shared_ptr<internal::ContainerHandle> RequireContainer() const
    {
        auto Handle = get_container();
        if (!Handle)
        {
            throw WslException("Container '" + Name + "' has not been started. Call Start() first.");
        }

        return Handle;
    }

private:
    std::string CreateSessionAndContainer(std::stop_token token);
    std::filesystem::path EnsureStorageAndMetadata();
    void StartSession(const std::filesystem::path& storagePath);
    void CreateScratchVolumes();
    void DeleteVolumeIfPresent(const std::string& volume_name);
    void CreateAndStartContainer(const std::string& Image);
    void StartContainerAttached(std::shared_ptr<internal::ContainerHandle> handle, const std::string& image);
    bool JoinStartThread();
    void ResolveMappedPortsIfNeeded(std::stop_token token);
    std::string InspectContainer(internal::ContainerHandle& Handle);
    void CopyConfiguredFiles(std::stop_token token);
    void WaitForReadiness(WslContainer& target, std::stop_token token);
    std::string FormatMappedPorts();
    void AcquireReuseLock(std::stop_token token);
    void ReleaseReuseLock();
    void StopLocked(std::stop_token token, bool throw_on_error);
    void CleanupLocked(bool throw_on_error);
    void CleanupSynchronously();
    void UpdateState(const std::string& State);
    void UpdateImageMetadata(const std::string& Image);
    void StopAndDeleteContainer(std::vector<std::string>& failures);
    void TerminateSession();
    void DisposeMainProcess(std::vector<std::string>& failures);
    void TranslateAndThrow(std::stop_token caller, const std::stop_source& startup_source);
    WslReadinessException enrich(const WslReadinessException& readiness);
    std::map<std::string, std::string> BuildEnvironment(const std::map<std::string, std::string>& overrides);
    internal::ProcessSettings BuildProcessSettings(const std::string& command,
                                                   const std::vector<std::string>& arguments,
                                                   const ProcessOptions& options, bool EnableStandardInput);
};

void WslContainer::Impl::Start(WslContainer& target, std::stop_token caller)
{
    internal::EnsureComInitialized();
    WslPlatform::ThrowIfUnsupported();
    std::unique_lock lifecycle_lock(lifecycle);
    if (disposed)
    {
        throw WslException("Container '" + Name + "' has been disposed.");
    }

    if (started)
    {
        return;
    }

    std::stop_source startup_source;
    std::stop_callback caller_callback(caller, [&startup_source] { startup_source.request_stop(); });
    std::jthread timer(
        [this, &startup_source](std::stop_token timer_token)
        {
            if (internal::SleepFor(configuration.StartupTimeout, timer_token))
            {
                startup_source.request_stop();
            }
        });
    const std::stop_token token = startup_source.get_token();

    try
    {
        internal::ContainerHost::EnsureInitialized(token);
        internal::ThrowIfStopped(token);

        if (configuration.Reuse == true && !Reuse)
        {
            publish_diagnostic("Reuse was requested but is disabled (default off, or blocked under CI without "
                               "WSLC_REUSE_IN_CI); using ephemeral instance");
        }

        publish_diagnostic(
            std::format("creating WSL container '{}' (runtime {})", Name, internal::WslcHost::GetVersion()));
        const std::string Image = CreateSessionAndContainer(token);

        CopyConfiguredFiles(token);
        WaitForReadiness(target, token);

        started = true;
        UpdateState("Running");
        internal::ContainerHost::RegisterCleanup(Name, [this] { CleanupSynchronously(); });
        publish_diagnostic(std::format("container '{}' is ready (Image '{}')", Name, Image));
    }
    catch (...)
    {
        const auto error = std::current_exception();
        publish_diagnostic(std::format("startup failed: {}", DescribeException(error)));
        CleanupLocked(false);
        TranslateAndThrow(caller, startup_source);
    }
}

std::string WslContainer::Impl::CreateSessionAndContainer(std::stop_token token)
{
    AcquireReuseLock(token);
    const std::filesystem::path storagePath = EnsureStorageAndMetadata();
    StartSession(storagePath);
    CreateScratchVolumes();

    internal::ImageResolver resolver(get_session()->get(), configuration,
                                     [this](LogLine line) { Logs->Publish(line); });
    const std::string Image = resolver.Resolve(Name + ":local", token);
    internal::ThrowIfStopped(token);
    UpdateImageMetadata(Image);

    CreateAndStartContainer(Image);
    ResolveMappedPortsIfNeeded(token);
    return Image;
}

std::filesystem::path WslContainer::Impl::EnsureStorageAndMetadata()
{
    const std::filesystem::path storagePath = store.GetSessionStorageDirectory(Name);
    if (!Reuse && std::filesystem::exists(storagePath))
    {
        internal::InstanceStore::BestEffortDeleteDirectory(storagePath);
    }

    std::error_code error;
    std::filesystem::create_directories(storagePath, error);
    storage_created = true;

    internal::InstanceMetadata instance;
    instance.SessionId = store.SessionId();
    instance.InstanceId = Name;
    instance.OwnerProcessId = static_cast<int>(internal::CurrentProcessId());
    instance.CreatedAt = std::chrono::system_clock::now();
    instance.State = "Creating";
    instance.Owner = internal::CurrentUserName();
    instance.Image = configuration.Image;
    instance.Reuse = Reuse;
    store.WriteMetadata(instance);
    {
        std::lock_guard lock(state_gate);
        metadata = instance;
    }

    return storagePath;
}

void WslContainer::Impl::StartSession(const std::filesystem::path& storagePath)
{
    WslcSessionSettings settings{};
    const std::wstring wideName = internal::ToUtf16(Name);
    const std::wstring wideStorage = storagePath.wstring();
    internal::check(WslcInitSessionSettings(wideName.c_str(), wideStorage.c_str(), &settings),
                    internal::ErrorKind::Provisioning, "Failed to create WSL container session '" + Name + "'",
                    nullptr);

    if (configuration.CpuCount)
    {
        internal::check(WslcSetSessionSettingsCpuCount(&settings, *configuration.CpuCount),
                        internal::ErrorKind::Provisioning, "Failed to set the session CPU count", nullptr);
    }

    if (configuration.MemoryMb)
    {
        internal::check(WslcSetSessionSettingsMemory(&settings, *configuration.MemoryMb),
                        internal::ErrorKind::Provisioning, "Failed to set the session memory", nullptr);
    }

    WslcSession Handle = nullptr;
    PWSTR error = nullptr;
    internal::check(WslcCreateSession(&settings, &Handle, &error), internal::ErrorKind::Provisioning,
                    "Failed to Start WSL container session '" + Name + "'", &error);
    std::lock_guard lock(state_gate);
    session = std::make_shared<internal::SessionHandle>(Handle);
}

void WslContainer::Impl::CreateScratchVolumes()
{
    if (configuration.ScratchVolumes.empty())
    {
        return;
    }

    auto sessionHandle = get_session();
    for (const auto& volume : configuration.ScratchVolumes)
    {
        try
        {
            if (Reuse)
            {
                // The previous Run's storage VHD survives for Reuse, so its scratch volume VHDs
                // do too. CreateVhdVolume rejects an existing Name, and scratch Volumes are
                // documented as recreated empty on every Start.
                DeleteVolumeIfPresent(volume.Name);
            }

            WslcVhdRequirements requirements{};
            requirements.name = volume.Name.c_str();
            requirements.sizeBytes = volume.SizeBytes;
            requirements.type = volume.type == VhdAllocationType::Fixed ? WSLC_VHD_TYPE_FIXED : WSLC_VHD_TYPE_DYNAMIC;
            requirements.flags = WSLC_VHD_REQ_FLAG_NONE;
            requirements.uid = 0;
            requirements.gid = 0;
            PWSTR error = nullptr;
            internal::check(WslcCreateSessionVhdVolume(sessionHandle->get(), &requirements, &error),
                            internal::ErrorKind::Provisioning, "Failed to create scratch volume '" + volume.Name + "'",
                            &error);
            publish_diagnostic(std::format("created scratch volume '{}' ({} bytes)", volume.Name, volume.SizeBytes));
        }
        catch (const WslException& exception)
        {
            throw WslProvisioningException("Failed to create scratch volume '" + volume.Name +
                                           "': " + exception.what());
        }
    }
}

void WslContainer::Impl::DeleteVolumeIfPresent(const std::string& volume_name)
{
    auto sessionHandle = get_session();
    PWSTR error = nullptr;
    const HRESULT result = WslcDeleteSessionVhdVolume(sessionHandle->get(), volume_name.c_str(), &error);
    if (error != nullptr)
    {
        CoTaskMemFree(error);
    }

    if (SUCCEEDED(result))
    {
        publish_diagnostic(std::format("recreated scratch volume '{}': deleted the previous VHD", volume_name));
    }
}

void WslContainer::Impl::CreateAndStartContainer(const std::string& Image)
{
    {
        std::lock_guard lock(state_gate);
        network = std::make_shared<internal::PortMapping>(configuration.PortMappings);
    }

    WslcContainerSettings settings{};
    internal::check(WslcInitContainerSettings(Image.c_str(), &settings), internal::ErrorKind::Provisioning,
                    "Failed to initialize container settings", nullptr);
    internal::check(WslcSetContainerSettingsName(&settings, Name.c_str()), internal::ErrorKind::Provisioning,
                    "Failed to set the container Name", nullptr);

    const WslcContainerNetworkingMode NetworkingMode = configuration.NetworkingMode == ContainerNetworkMode::Isolated
                                                           ? WSLC_CONTAINER_NETWORKING_MODE_NONE
                                                           : WSLC_CONTAINER_NETWORKING_MODE_BRIDGED;
    internal::check(WslcSetContainerSettingsNetworkingMode(&settings, NetworkingMode),
                    internal::ErrorKind::Provisioning, "Failed to set the container networking mode", nullptr);

    // Init process: declare the service command, or keep the Environment alive so Exec works.
    // WSLC never runs the Image ENTRYPOINT/CMD automatically.
    WslcProcessSettings initSettings{};
    internal::check(WslcInitProcessSettings(&initSettings), internal::ErrorKind::Provisioning,
                    "Failed to initialize the init process settings", nullptr);

    std::vector<std::string> CommandLine;
    if (configuration.Command)
    {
        CommandLine.push_back(*configuration.Command);
        CommandLine.insert(CommandLine.end(), configuration.CommandArguments.begin(),
                           configuration.CommandArguments.end());
    }
    else
    {
        CommandLine = {"/bin/sh", "-c", "while true; do sleep 3600; done"};
    }

    const std::vector<PCSTR> argv = internal::ToNativeArgv(CommandLine);

    internal::check(WslcSetProcessSettingsCmdLine(&initSettings, argv.data(), argv.size()),
                    internal::ErrorKind::Provisioning, "Failed to set the init process command line", nullptr);

    if (configuration.WorkingDirectory)
    {
        internal::check(WslcSetProcessSettingsWorkingDirectory(&initSettings, configuration.WorkingDirectory->c_str()),
                        internal::ErrorKind::Provisioning, "Failed to set the init process working directory", nullptr);
    }

    // The SDK takes an array of "KEY=VALUE" strings.
    const std::map<std::string, std::string> Environment = BuildEnvironment({});
    const std::vector<std::string> environmentStrings = internal::ToEnvironmentStrings(Environment);
    const std::vector<PCSTR> environmentValues = internal::ToNativeEnvironment(environmentStrings);

    if (!environmentValues.empty())
    {
        internal::check(
            WslcSetProcessSettingsEnvVariables(&initSettings, environmentValues.data(), environmentValues.size()),
            internal::ErrorKind::Provisioning, "Failed to set the init process Environment", nullptr);
    }

    auto mainProcessState = internal::ProcessRunner::Prepare([this](LogLine line) { Logs->Publish(line); }, true);
    internal::check(WslcSetProcessSettingsCallbacks(&initSettings, mainProcessState->NativeCallbacks(),
                                                    mainProcessState->NativeContext()),
                    internal::ErrorKind::Provisioning, "Failed to set the init process callbacks", nullptr);

    internal::check(WslcSetContainerSettingsInitProcess(&settings, &initSettings), internal::ErrorKind::Provisioning,
                    "Failed to set the init process", nullptr);

    std::vector<sockaddr_storage> addressStorage;
    const std::vector<WslcContainerPortMapping> PortMappings = network->ToNativeMappings(addressStorage);
    if (!PortMappings.empty())
    {
        internal::check(WslcSetContainerSettingsPortMappings(&settings, PortMappings.data(),
                                                             static_cast<std::uint32_t>(PortMappings.size())),
                        internal::ErrorKind::Provisioning, "Failed to set the container port mappings", nullptr);
    }

    std::vector<std::wstring> volumeHosts;
    std::vector<WslcContainerVolume> Volumes;
    volumeHosts.reserve(configuration.Volumes.size());
    Volumes.reserve(configuration.Volumes.size());
    for (const auto& volume : configuration.Volumes)
    {
        volumeHosts.push_back(volume.HostPath.wstring());
        WslcContainerVolume native{};
        native.windowsPath = volumeHosts.back().c_str();
        native.containerPath = volume.ContainerPath.c_str();
        native.readOnly = volume.ReadOnly ? TRUE : FALSE;
        Volumes.push_back(native);
    }

    if (!Volumes.empty())
    {
        internal::check(
            WslcSetContainerSettingsVolumes(&settings, Volumes.data(), static_cast<std::uint32_t>(Volumes.size())),
            internal::ErrorKind::Provisioning, "Failed to set the container Volumes", nullptr);
    }

    std::vector<WslcContainerNamedVolume> namedVolumes;
    namedVolumes.reserve(configuration.ScratchVolumes.size());
    for (const auto& volume : configuration.ScratchVolumes)
    {
        WslcContainerNamedVolume native{};
        native.name = volume.Name.c_str();
        native.containerPath = volume.ContainerPath.c_str();
        native.readOnly = volume.ReadOnly ? TRUE : FALSE;
        namedVolumes.push_back(native);
    }

    if (!namedVolumes.empty())
    {
        internal::check(WslcSetContainerSettingsNamedVolumes(&settings, namedVolumes.data(),
                                                             static_cast<std::uint32_t>(namedVolumes.size())),
                        internal::ErrorKind::Provisioning, "Failed to set the container named Volumes", nullptr);
    }

    WslcContainer Handle = nullptr;
    PWSTR error = nullptr;
    internal::check(WslcCreateContainer(get_session()->get(), &settings, &Handle, &error),
                    internal::ErrorKind::Provisioning, "Failed to create the container from Image '" + Image + "'",
                    &error);
    std::shared_ptr<internal::ContainerHandle> containerHandle;
    {
        std::lock_guard lock(state_gate);
        containerHandle = std::make_shared<internal::ContainerHandle>(Handle);
        container = containerHandle;
    }

    StartContainerAttached(std::move(containerHandle), Image);

    WslcProcess initProcess = nullptr;
    if (SUCCEEDED(WslcGetContainerInitProcess(Handle, &initProcess)))
    {
        mainProcessState->SetHandle(initProcess);
    }

    {
        std::lock_guard lock(state_gate);
        main_process = std::move(mainProcessState);
    }

    CHAR containerId[WSLC_CONTAINER_ID_BUFFER_SIZE] = {};
    std::string idText = "unknown";
    if (SUCCEEDED(WslcGetContainerID(Handle, containerId)))
    {
        idText = containerId;
    }

    publish_diagnostic(std::format("container started (Id {})", idText));
}

void WslContainer::Impl::StartContainerAttached(std::shared_ptr<internal::ContainerHandle> handle,
                                                const std::string& image)
{
    const std::shared_ptr<StartState> state = start_state;
    {
        std::lock_guard lock(state->gate);
        state->finished = false;
        state->succeeded = false;
        state->error.clear();
    }

    // Capture the handle wrapper so a detached thread keeps the native handle alive.
    start_thread = std::thread(
        [state, handle]
        {
            PWSTR error = nullptr;
            const HRESULT result = WslcStartContainer(handle->get(), WSLC_CONTAINER_START_FLAG_ATTACH, &error);
            std::string message;
            if (FAILED(result))
            {
                message = internal::ConsumeCoTaskString(error);
            }
            else if (error != nullptr)
            {
                CoTaskMemFree(error);
            }

            {
                std::lock_guard lock(state->gate);
                state->succeeded = SUCCEEDED(result);
                state->error = std::move(message);
                state->finished = true;
            }

            state->condition.notify_all();
        });

    // The attached call blocks until the init process exits; the container itself is running
    // while it blocks, so wait for the running state instead of the call.
    WslcContainerState containerState = WSLC_CONTAINER_STATE_INVALID;
    for (int attempt = 0; attempt < c_maxStartAttempts; attempt++)
    {
        const HRESULT stateResult = WslcGetContainerState(handle->get(), &containerState);
        if (SUCCEEDED(stateResult) && containerState == WSLC_CONTAINER_STATE_RUNNING)
        {
            return;
        }

        {
            std::lock_guard lock(state->gate);
            if (state->finished && !state->succeeded)
            {
                break;
            }
        }

        internal::SleepFor(c_startPollDelay, std::stop_token{});
    }

    std::string detail;
    {
        std::lock_guard lock(state->gate);
        detail = state->error;
    }

    throw WslProvisioningException("Failed to start the container from image '" + image + "'" +
                                   (detail.empty() ? "." : ": " + detail));
}

bool WslContainer::Impl::JoinStartThread()
{
    if (!start_thread.joinable())
    {
        return true;
    }

    const std::shared_ptr<StartState> state = start_state;
    std::unique_lock lock(state->gate);
    if (state->condition.wait_for(lock, c_startThreadJoinTimeout, [&state] { return state->finished; }))
    {
        lock.unlock();
        start_thread.join();
        return true;
    }

    // The attached call did not return after the container was torn down; detach so it can
    // never outlive the process. It touches only the shared StartState and its captured
    // handle wrapper, so the caller must not delete the runtime container under it.
    start_thread.detach();
    return false;
}

void WslContainer::Impl::ResolveMappedPortsIfNeeded(std::stop_token token)
{
    if (configuration.PortMappings.empty())
    {
        return;
    }

    for (int attempt = 0; attempt < c_maxPortResolveAttempts; attempt++)
    {
        auto containerHandle = RequireContainer();
        network->ResolveFromInspect(InspectContainer(*containerHandle));
        if (network->UnresolvedCount() == 0)
        {
            publish_diagnostic(std::format("mapped ports: {}", FormatMappedPorts()));
            return;
        }

        if (!internal::SleepFor(c_portResolveDelay, token))
        {
            throw OperationCanceledException();
        }
    }

    throw WslNetworkException("The WSL runtime did not assign Host ports for container port(s): " +
                              internal::join(network->UnresolvedPorts(), ", ") + ".");
}

std::string WslContainer::Impl::InspectContainer(internal::ContainerHandle& Handle)
{
    PSTR data = nullptr;
    internal::check(WslcInspectContainer(Handle.get(), &data), internal::ErrorKind::Provisioning,
                    "Failed to inspect the container", nullptr);
    std::string result = data != nullptr ? data : "";
    if (data != nullptr)
    {
        CoTaskMemFree(data);
    }

    return result;
}

void WslContainer::Impl::CopyConfiguredFiles(std::stop_token token)
{
    for (const auto& file : configuration.Files)
    {
        publish_diagnostic(
            std::format("copying '{}' to '{}'", internal::ToUtf8(file.Source.wstring()), file.Destination));
        internal::ProcessRunner::CopyTo(RequireContainer()->get(), file.Source, file.Destination, token,
                                        [this](LogLine line) { Logs->Publish(line); });
    }
}

void WslContainer::Impl::WaitForReadiness(WslContainer& target, std::stop_token token)
{
    for (const auto& strategy : configuration.WaitStrategies)
    {
        publish_diagnostic(std::format("waiting for {} (Timeout {}s)", strategy->Name(),
                                       internal::FormatMilliseconds(strategy->Timeout())));
        strategy->Wait(target, token);
    }
}

std::string WslContainer::Impl::FormatMappedPorts()
{
    std::vector<std::string> parts;
    parts.reserve(configuration.PortMappings.size());
    for (const auto& mapping : configuration.PortMappings)
    {
        parts.push_back(std::to_string(mapping.ContainerPort) + "->" +
                        std::to_string(network->GetMappedPort(mapping.ContainerPort)));
    }

    return internal::join(parts, ", ");
}

void WslContainer::Impl::AcquireReuseLock(std::stop_token token)
{
    if (!Reuse)
    {
        return;
    }

    const std::filesystem::path instanceDirectory = store.GetInstanceDirectory(Name);
    std::error_code error;
    std::filesystem::create_directories(instanceDirectory, error);
    const std::filesystem::path lockPath = instanceDirectory / L"wslc.lock";

    for (int attempt = 0; attempt < c_maxReuseLockAttempts; attempt++)
    {
        internal::ThrowIfStopped(token);
        const HANDLE Handle = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                          FILE_ATTRIBUTE_NORMAL, nullptr);
        if (Handle != INVALID_HANDLE_VALUE)
        {
            reuse_lock.reset(Handle);
            return;
        }

        if (!internal::SleepFor(c_reuseLockRetryDelay, token))
        {
            throw OperationCanceledException();
        }
    }

    throw WslProvisioningException("Reuse instance '" + Name +
                                   "' is locked by another process and was not released within 30s.");
}

void WslContainer::Impl::ReleaseReuseLock()
{
    reuse_lock.reset();
}

void WslContainer::Impl::Stop(std::stop_token token)
{
    internal::EnsureComInitialized();
    std::unique_lock lifecycle_lock(lifecycle);
    if (disposed)
    {
        throw WslException("Container '" + Name + "' has been disposed.");
    }

    StopLocked(token, true);
}

void WslContainer::Impl::StopLocked([[maybe_unused]] std::stop_token token, bool throw_on_error)
{
    std::vector<std::string> failures;

    for (const auto& process : processes.TakeAll())
    {
        try
        {
            process->Kill(c_abortGracePeriod, std::stop_token{});
            process->Dispose();
        }
        catch (const std::exception& exception)
        {
            failures.push_back(exception.what());
        }
    }

    DisposeMainProcess(failures);
    StopAndDeleteContainer(failures);
    TerminateSession();

    // The instance is torn down, so a later Start must be able to re-acquire the reuse lock.
    ReleaseReuseLock();

    {
        std::lock_guard lock(state_gate);
        network.reset();
    }

    started = false;
    UpdateState("Stopped");
    internal::ContainerHost::UnregisterCleanup(Name);

    if (!failures.empty() && throw_on_error)
    {
        throw WslCleanupException("Failed to Stop container '" + Name + "': " + failures.front());
    }
}

void WslContainer::Impl::DisposeMainProcess(std::vector<std::string>& failures)
{
    auto process = get_main_process();
    if (!process)
    {
        return;
    }

    try
    {
        process->Kill(c_mainProcessGracePeriod, std::stop_token{});
        process->Dispose();
    }
    catch (const std::exception& exception)
    {
        failures.push_back(exception.what());
    }

    std::lock_guard lock(state_gate);
    main_process.reset();
}

void WslContainer::Impl::StopAndDeleteContainer(std::vector<std::string>& failures)
{
    auto containerHandle = get_container();
    if (!containerHandle)
    {
        return;
    }

    PWSTR error = nullptr;
    HRESULT result = WslcStopContainer(containerHandle->get(), WSLC_SIGNAL_SIGTERM,
                                       static_cast<std::uint32_t>(c_containerStopTimeout.count() / 1000), &error);
    if (error != nullptr)
    {
        CoTaskMemFree(error);
        error = nullptr;
    }

    if (FAILED(result) && !internal::IsBenignRuntimeError(result))
    {
        failures.push_back("Failed to Stop the container (HRESULT " + internal::HresultHex(result) + ").");
    }

    // Stopping the init process releases the attached WslcStartContainer call.
    if (!JoinStartThread())
    {
        // Deleting the runtime container under the still-running attached call would be a
        // use-after-free; the session teardown after this reclaims it instead.
        publish_diagnostic("attached start call did not finish; leaving the container to session teardown");
        std::lock_guard lock(state_gate);
        container.reset();
        return;
    }

    result = WslcDeleteContainer(containerHandle->get(), WSLC_DELETE_CONTAINER_FLAG_FORCE, &error);
    if (error != nullptr)
    {
        CoTaskMemFree(error);
    }

    if (FAILED(result) && !internal::IsBenignRuntimeError(result))
    {
        failures.push_back("Failed to delete the container (HRESULT " + internal::HresultHex(result) + ").");
    }

    std::lock_guard lock(state_gate);
    container.reset();
}

void WslContainer::Impl::TerminateSession()
{
    auto sessionHandle = get_session();
    if (!sessionHandle)
    {
        return;
    }

    WslcTerminateSession(sessionHandle->get());
    std::lock_guard lock(state_gate);
    session.reset();
}

void WslContainer::Impl::CleanupLocked(bool throw_on_error)
{
    std::string failure;
    try
    {
        StopLocked(std::stop_token{}, false);
    }
    catch (const std::exception& exception)
    {
        failure = exception.what();
    }

    ReleaseReuseLock();

    if (!Reuse && storage_created.exchange(false))
    {
        store.DeleteInstanceDirectory(Name);
    }

    if (!failure.empty() && throw_on_error)
    {
        throw WslCleanupException("Failed to clean up container '" + Name + "': " + failure);
    }
}

void WslContainer::Impl::Dispose()
{
    internal::EnsureComInitialized();
    if (dispose_requested.exchange(true))
    {
        Logs->Complete();
        return;
    }

    disposed = true;
    {
        std::unique_lock lifecycle_lock(lifecycle);
        CleanupLocked(false);
    }

    Logs->Complete();
}

void WslContainer::Impl::CleanupSynchronously()
{
    internal::EnsureComInitialized();
    // The exit hook can race an in-flight Dispose; only the first caller tears down.
    if (dispose_requested.exchange(true))
    {
        return;
    }

    disposed = true;
    started = false;

    if (auto process = get_main_process())
    {
        process->TrySignal(WSLC_SIGNAL_SIGKILL);
    }

    if (auto containerHandle = get_container())
    {
        WslcStopContainer(containerHandle->get(), WSLC_SIGNAL_SIGTERM, 2, nullptr);
        if (JoinStartThread())
        {
            WslcDeleteContainer(containerHandle->get(), WSLC_DELETE_CONTAINER_FLAG_FORCE, nullptr);
        }
    }

    if (auto sessionHandle = get_session())
    {
        WslcTerminateSession(sessionHandle->get());
    }

    {
        std::lock_guard lock(state_gate);
        container.reset();
        session.reset();
        network.reset();
        main_process.reset();
    }

    UpdateState("Stopped");
    ReleaseReuseLock();

    if (!Reuse && storage_created.exchange(false))
    {
        store.DeleteInstanceDirectory(Name);
    }

    internal::ContainerHost::UnregisterCleanup(Name);
    Logs->Complete();
}

void WslContainer::Impl::UpdateState(const std::string& State)
{
    std::lock_guard lock(state_gate);
    if (!metadata)
    {
        return;
    }

    metadata->State = State;
    try
    {
        store.WriteMetadata(*metadata);
    }
    catch (const std::exception& exception)
    {
        // Best-effort per ADR-0004, but a stale wslc.json must be visible in the logs.
        publish_diagnostic(std::format("failed to persist instance metadata: {}", exception.what()));
    }
}

void WslContainer::Impl::UpdateImageMetadata(const std::string& Image)
{
    // In-memory only: the running/stopped State transition persists this with the Next write.
    std::lock_guard lock(state_gate);
    if (metadata)
    {
        metadata->Image = Image;
    }
}

void WslContainer::Impl::TranslateAndThrow(std::stop_token caller, const std::stop_source& startup_source)
{
    const auto error = std::current_exception();
    try
    {
        std::rethrow_exception(error);
    }
    catch (const OperationCanceledException&)
    {
        if (caller.stop_requested())
        {
            throw;
        }

        if (startup_source.stop_requested())
        {
            throw WslTimeoutException(
                "Container '" + Name + "' did not Complete startup within " +
                internal::FormatMilliseconds(configuration.StartupTimeout) + "s." +
                (configuration.Command
                     ? std::string()
                     : " No init command was configured, so only a keep-alive shell is running; WSLC never runs the "
                       "Image's ENTRYPOINT/CMD automatically. Call WithCommand(...) or use a module builder."));
        }

        throw;
    }
    catch (const WslReadinessException& readiness)
    {
        throw enrich(readiness);
    }
    catch (const WslException&)
    {
        throw;
    }
    catch (const std::exception& exception)
    {
        throw WslProvisioningException("Failed to Start container '" + Name + "': " + exception.what());
    }
}

WslReadinessException WslContainer::Impl::enrich(const WslReadinessException& readiness)
{
    std::optional<int> ExitCode = readiness.ExitCode();
    if (!ExitCode)
    {
        if (auto process = get_main_process(); process && process->HasExited())
        {
            ExitCode = process->ExitCode();
        }
    }

    const auto logs_snapshot = Logs->Snapshot();
    const std::optional<std::string> Image =
        readiness.Image()
            ? readiness.Image()
            : (configuration.Image
                   ? configuration.Image
                   : (configuration.TarballPath
                          ? std::optional<std::string>(internal::ToUtf8(configuration.TarballPath->wstring()))
                          : std::nullopt));
    const std::optional<std::string> Command = readiness.Command() ? readiness.Command() : configuration.Command;
    const std::optional<std::string> Stdout =
        readiness.Stdout() ? readiness.Stdout()
                           : internal::JoinLast(*logs_snapshot, LogSource::Stdout, c_maxRecentLogs);
    const std::optional<std::string> Stderr =
        readiness.Stderr() ? readiness.Stderr()
                           : internal::JoinLast(*logs_snapshot, LogSource::Stderr, c_maxRecentLogs);
    return internal::ReadinessDiagnostics::Enrich(readiness, Image, Command, ExitCode, Stdout, Stderr);
}

std::map<std::string, std::string> WslContainer::Impl::BuildEnvironment(
    const std::map<std::string, std::string>& overrides)
{
    for (const auto& pair : overrides)
    {
        internal::RequireEnvironmentName(pair.first);
    }

    std::map<std::string, std::string> Environment = configuration.Environment;
    Environment["WSLC_SESSION_ID"] = store.SessionId();
    Environment["WSLC_INSTANCE_ID"] = Name;
    Environment["WSLC_OWNER_PID"] = owner_pid;

    std::chrono::system_clock::time_point CreatedAt = std::chrono::system_clock::now();
    {
        std::lock_guard lock(state_gate);
        if (metadata && metadata->CreatedAt)
        {
            CreatedAt = *metadata->CreatedAt;
        }
    }

    Environment["WSLC_CREATED_AT"] = internal::FormatIso8601(CreatedAt);
    for (const auto& pair : overrides)
    {
        Environment[pair.first] = pair.second;
    }

    return Environment;
}

internal::ProcessSettings WslContainer::Impl::BuildProcessSettings(const std::string& command,
                                                                   const std::vector<std::string>& arguments,
                                                                   const ProcessOptions& options,
                                                                   bool EnableStandardInput)
{
    internal::ProcessSettings settings;
    settings.CommandLine.reserve(arguments.size() + 1);
    settings.CommandLine.push_back(command);
    settings.CommandLine.insert(settings.CommandLine.end(), arguments.begin(), arguments.end());
    if (options.WorkingDirectory)
    {
        settings.WorkingDirectory = options.WorkingDirectory;
    }
    else if (configuration.WorkingDirectory)
    {
        settings.WorkingDirectory = configuration.WorkingDirectory;
    }

    settings.Environment = BuildEnvironment(options.Environment);
    settings.EnableStandardInput = EnableStandardInput;
    return settings;
}

ExecResult WslContainer::Impl::Exec(std::string command, std::vector<std::string> arguments, ExecOptions options,
                                    std::stop_token token)
{
    internal::EnsureComInitialized();
    if (internal::IsBlank(command))
    {
        throw WslException("Command must not be empty.");
    }

    internal::RequireCount(arguments.size(), internal::c_maxCommandArguments, "command arguments");
    ValidateExecOptions(options);
    auto containerHandle = RequireContainer();
    const internal::ProcessSettings settings =
        BuildProcessSettings(command, arguments, options, options.StandardInput.has_value());

    try
    {
        return internal::ProcessRunner::Run(containerHandle->get(), settings, options.StandardInput, options.Timeout,
                                            token, [this](LogLine line) { Logs->Publish(line); });
    }
    catch (const WslException&)
    {
        throw;
    }
    catch (const std::exception& exception)
    {
        throw WslProcessException("Command '" + command + "' failed: " + exception.what());
    }
}

std::unique_ptr<IWslProcess> WslContainer::Impl::StartProcess(std::string command, std::vector<std::string> arguments,
                                                              const ProcessOptions& options, std::stop_token token)
{
    internal::EnsureComInitialized();
    internal::ThrowIfStopped(token);
    if (internal::IsBlank(command))
    {
        throw WslException("Command must not be empty.");
    }

    internal::RequireCount(arguments.size(), internal::c_maxCommandArguments, "command arguments");

    // options is a ProcessOptions so it also accepts ExecOptions; silently dropping the derived
    // StandardInput/Timeout members would mislead callers, so reject them before any container
    // or process work. RTTI is enabled by default with /EHsc (the build never passes /GR-).
    if (const auto* exec = dynamic_cast<const ExecOptions*>(&options);
        exec != nullptr && (exec->StandardInput.has_value() || exec->Timeout.has_value()))
    {
        throw WslException("StandardInput and Timeout apply only to Exec, not to long-running StartProcess. Use "
                           "Exec for stdin or kill the IWslProcess when done.");
    }

    ValidateProcessOptions(options);
    auto containerHandle = RequireContainer();
    const internal::ProcessSettings settings = BuildProcessSettings(command, arguments, options, false);

    // Register before starting: a concurrent Stop snapshots this list and would otherwise miss
    // (and leak) a process that was started but not yet added.
    auto State = internal::ProcessRunner::Prepare([this](LogLine line) { Logs->Publish(line); }, true);
    processes.Add(State);
    try
    {
        internal::ProcessRunner::CreateNative(containerHandle->get(), settings, *State);
    }
    catch (...)
    {
        processes.Remove(State);
        State->Dispose();
        throw;
    }

    return std::make_unique<internal::ContainerProcess>(std::move(State));
}

void WslContainer::Impl::CopyTo(const std::filesystem::path& HostPath, std::string ContainerPath, std::stop_token token)
{
    internal::EnsureComInitialized();
    const std::string hostText = internal::ToUtf8(HostPath.wstring());
    if (internal::IsBlank(hostText))
    {
        throw WslException("Host path must not be empty.");
    }

    internal::ValidateContainerPath(ContainerPath);
    auto containerHandle = RequireContainer();

    std::error_code error;
    if (!std::filesystem::exists(HostPath, error) || std::filesystem::is_directory(HostPath, error))
    {
        throw WslException("Host file '" + hostText + "' does not exist. Only file copies are supported.");
    }

    internal::ProcessRunner::CopyTo(containerHandle->get(), std::filesystem::absolute(HostPath), ContainerPath, token,
                                    [this](LogLine line) { Logs->Publish(line); });
}

void WslContainer::Impl::CopyFrom(std::string ContainerPath, const std::filesystem::path& HostPath,
                                  std::stop_token token)
{
    internal::EnsureComInitialized();
    internal::ValidateContainerPath(ContainerPath);
    const std::string hostText = internal::ToUtf8(HostPath.wstring());
    if (internal::IsBlank(hostText))
    {
        throw WslException("Host path must not be empty.");
    }

    auto containerHandle = RequireContainer();
    internal::ProcessRunner::CopyFrom(containerHandle->get(), ContainerPath, HostPath, token,
                                      [this](LogLine line) { Logs->Publish(line); });
}

std::vector<LogLine> WslContainer::Impl::RecentLogs(int maxLines) const
{
    if (maxLines <= 0)
    {
        throw WslException("maxLines must be positive.");
    }

    return internal::TakeLast(*Logs->Snapshot(), static_cast<std::size_t>(maxLines));
}

WslEndpoint WslContainer::Impl::connect_endpoint(int containerPort) const
{
    const auto networkSnapshot = get_network();
    if (!networkSnapshot)
    {
        throw WslException("Container '" + Name + "' has not been started, so port " + std::to_string(containerPort) +
                           " is not mapped yet. Call Start() first.");
    }

    return networkSnapshot->GetConnectEndpoint(containerPort);
}

bool WslContainer::Impl::tcp_port_open(int containerPort, std::stop_token token) const
{
    const auto networkSnapshot = get_network();
    return networkSnapshot ? networkSnapshot->IsPortOpen(containerPort, token) : false;
}

bool WslContainer::Impl::process_running(std::string processName, std::stop_token token)
{
    const ExecResult result =
        Exec("sh",
             {"-c", "for p in /proc/[0-9]*; do [ \"$(cat \"$p/comm\" 2>/dev/null)\" = \"$1\" ] && exit 0; done; exit 1",
              "sh", processName},
             ExecOptions{}, token);
    return result.ExitCode == 0;
}

// ---------------------------------------------------------------------------
// WslContainer facade
// ---------------------------------------------------------------------------

WslContainer WslContainer::Create(internal::Configuration configuration)
{
    return WslContainer(std::make_unique<Impl>(std::move(configuration)));
}

WslContainer::WslContainer(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

WslContainer::~WslContainer()
{
    if (m_impl)
    {
        m_impl->Dispose();
    }
}

WslContainer::WslContainer(WslContainer&& other) noexcept = default;

WslContainer& WslContainer::operator=(WslContainer&& other) noexcept
{
    if (this != &other)
    {
        if (m_impl)
        {
            m_impl->Dispose();
        }

        m_impl = std::move(other.m_impl);
    }

    return *this;
}

const std::string& WslContainer::Name() const
{
    return m_impl->Name;
}

std::optional<std::string> WslContainer::Image() const
{
    return m_impl->Image();
}

bool WslContainer::IsStarted() const
{
    return m_impl->started;
}

bool WslContainer::IsReuseEffective() const
{
    return m_impl->Reuse;
}

WslEndpoint WslContainer::GetConnectEndpoint(int containerPort) const
{
    return m_impl->connect_endpoint(containerPort);
}

void WslContainer::Start(std::stop_token token)
{
    m_impl->Start(*this, token);
}

void WslContainer::Stop(std::stop_token token)
{
    m_impl->Stop(token);
}

ExecResult WslContainer::Exec(std::string command, std::vector<std::string> arguments, ExecOptions options,
                              std::stop_token token)
{
    return m_impl->Exec(std::move(command), std::move(arguments), std::move(options), token);
}

ExecResult WslContainer::Exec(std::string command, std::vector<std::string> arguments, std::stop_token token)
{
    return m_impl->Exec(std::move(command), std::move(arguments), ExecOptions{}, token);
}

ExecResult WslContainer::ExecShell(std::string script, ExecOptions options, std::stop_token token)
{
    if (script.find_first_not_of(" \t\r\n") == std::string::npos)
    {
        throw WslException("Shell script must not be empty.");
    }

    return m_impl->Exec("/bin/sh", {"-c", std::move(script)}, std::move(options), token);
}

std::unique_ptr<IWslProcess> WslContainer::StartProcess(std::string command, std::vector<std::string> arguments,
                                                        const ProcessOptions& options, std::stop_token token)
{
    return m_impl->StartProcess(std::move(command), std::move(arguments), options, token);
}

void WslContainer::CopyTo(const std::filesystem::path& HostPath, std::string ContainerPath, std::stop_token token)
{
    m_impl->CopyTo(HostPath, std::move(ContainerPath), token);
}

void WslContainer::CopyFrom(std::string ContainerPath, const std::filesystem::path& HostPath, std::stop_token token)
{
    m_impl->CopyFrom(std::move(ContainerPath), HostPath, token);
}

LogStream WslContainer::SubscribeLogs()
{
    return LogStream::FromBroadcaster(m_impl->Logs);
}

std::vector<LogLine> WslContainer::GetRecentLogs() const
{
    return GetRecentLogs(static_cast<int>(c_maxRecentLogs));
}

std::vector<LogLine> WslContainer::GetRecentLogs(int maxLines) const
{
    return m_impl->RecentLogs(maxLines);
}

void WslContainer::Dispose()
{
    if (m_impl)
    {
        m_impl->Dispose();
    }
}

bool WslContainer::IsTcpPortOpen(int containerPort, std::stop_token token)
{
    return m_impl->tcp_port_open(containerPort, token);
}

bool WslContainer::IsProcessRunning(std::string processName, std::stop_token token)
{
    return m_impl->process_running(std::move(processName), token);
}

} // namespace wslc
