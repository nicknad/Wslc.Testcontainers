#include "internal/process_runner.hpp"

#include "internal/util.hpp"
#include "wslc/exceptions.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <fstream>

namespace wslc::internal
{

namespace
{

constexpr std::uint64_t c_maxCopyBytes = 1024ull * 1024ull * 1024ull;
constexpr std::size_t c_copyChunkSize = 64 * 1024;
constexpr std::chrono::milliseconds c_abortGracePeriod{2000};

class IoHandle
{
public:
    IoHandle() = default;
    explicit IoHandle(HANDLE Handle) : m_handle(Handle) {}
    ~IoHandle() { reset(); }
    IoHandle(const IoHandle&) = delete;
    IoHandle& operator=(const IoHandle&) = delete;
    IoHandle(IoHandle&& other) noexcept : m_handle(other.m_handle) { other.m_handle = INVALID_HANDLE_VALUE; }
    IoHandle& operator=(IoHandle&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_handle = other.m_handle;
            other.m_handle = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    HANDLE get() const noexcept { return m_handle; }
    explicit operator bool() const noexcept { return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE; }
    HANDLE release() noexcept
    {
        HANDLE result = m_handle;
        m_handle = INVALID_HANDLE_VALUE;
        return result;
    }

    void reset() noexcept
    {
        if (operator bool())
        {
            CloseHandle(m_handle);
        }

        m_handle = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

std::vector<PCSTR> build_argv(const std::vector<std::string>& CommandLine)
{
    std::vector<PCSTR> argv;
    argv.reserve(CommandLine.size());
    for (const auto& argument : CommandLine)
    {
        argv.push_back(argument.c_str());
    }

    return argv;
}

std::vector<std::string> BuildEnvironment(const std::map<std::string, std::string>& Environment)
{
    std::vector<std::string> values;
    values.reserve(Environment.size());
    for (const auto& pair : Environment)
    {
        values.push_back(pair.first + "=" + pair.second);
    }

    return values;
}

std::vector<PCSTR> build_environment_values(const std::vector<std::string>& Environment)
{
    std::vector<PCSTR> values;
    values.reserve(Environment.size());
    for (const auto& entry : Environment)
    {
        values.push_back(entry.c_str());
    }

    return values;
}

std::string first_argument(const ProcessSettings& settings)
{
    return settings.CommandLine.empty() ? std::string() : settings.CommandLine.front();
}

} // namespace

std::shared_ptr<ContainerProcessState> ProcessRunner::Prepare(std::function<void(LogLine)> observer,
                                                              bool capture_output)
{
    return std::make_shared<ContainerProcessState>(capture_output, std::move(observer));
}

void ProcessRunner::CreateNative(WslcContainer container, const ProcessSettings& settings, ContainerProcessState& State,
                                 ErrorKind error_kind)
{
    if (settings.CommandLine.empty())
    {
        throw WslcException("A command line is required.");
    }

    WslcProcessSettings nativeSettings{};
    HRESULT result = WslcInitProcessSettings(&nativeSettings);
    check(result, error_kind, "Failed to initialize process settings", nullptr);

    if (settings.WorkingDirectory && !settings.WorkingDirectory->empty())
    {
        result = WslcSetProcessSettingsWorkingDirectory(&nativeSettings, settings.WorkingDirectory->c_str());
        check(result, error_kind, "Failed to set the process working directory", nullptr);
    }

    const std::vector<PCSTR> argv = build_argv(settings.CommandLine);
    result = WslcSetProcessSettingsCmdLine(&nativeSettings, argv.data(), argv.size());
    check(result, error_kind, "Failed to set the process command line", nullptr);

    // The SDK takes an array of "KEY=VALUE" strings.
    const std::vector<std::string> Environment = BuildEnvironment(settings.Environment);
    const std::vector<PCSTR> environmentValues = build_environment_values(Environment);
    if (!environmentValues.empty())
    {
        result =
            WslcSetProcessSettingsEnvVariables(&nativeSettings, environmentValues.data(), environmentValues.size());
        check(result, error_kind, "Failed to set the process environment", nullptr);
    }

    if (settings.EnableStandardInput)
    {
        result = WslcSetProcessSettingsFlags(&nativeSettings, WSLC_PROCESS_FLAG_STDIN);
        check(result, error_kind, "Failed to enable standard input", nullptr);
    }

    if (State.NativeCallbacks()->onStdOut != nullptr)
    {
        result = WslcSetProcessSettingsCallbacks(&nativeSettings, State.NativeCallbacks(), State.NativeContext());
        check(result, error_kind, "Failed to set process callbacks", nullptr);
    }

    WslcProcess process = nullptr;
    PWSTR error = nullptr;
    result = WslcCreateContainerProcess(container, &nativeSettings, &process, &error);
    check(result, error_kind, "Failed to create the process", &error);
    State.SetHandle(process);
}

ExecResult ProcessRunner::Run(WslcContainer container, const ProcessSettings& settings,
                              const std::optional<std::string>& StandardInput,
                              std::optional<std::chrono::milliseconds> Timeout, std::stop_token token,
                              std::function<void(LogLine)> observer)
{
    auto State = Prepare(std::move(observer), true);
    CreateNative(container, settings, *State, ErrorKind::Process);

    try
    {
        if (StandardInput)
        {
            IoHandle stdinHandle;
            HANDLE Handle = nullptr;
            const HRESULT result = WslcGetProcessIOHandle(State->Handle(), WSLC_PROCESS_IO_HANDLE_STDIN, &Handle);
            check(result, ErrorKind::Process, "Failed to open the process standard input", nullptr);
            stdinHandle = IoHandle(Handle);

            std::size_t offset = 0;
            while (offset < StandardInput->size())
            {
                ThrowIfStopped(token);
                const std::size_t chunk = std::min(c_copyChunkSize, StandardInput->size() - offset);
                DWORD written = 0;
                if (WriteFile(stdinHandle.get(), StandardInput->data() + offset, static_cast<DWORD>(chunk), &written,
                              nullptr) == 0)
                {
                    throw WslProcessException("Failed to write to the process standard input.");
                }

                offset += written;
            }
        }

        if (Timeout)
        {
            if (!State->WaitForExitFor(*Timeout, token))
            {
                State->Kill(c_abortGracePeriod, std::stop_token{});
                throw WslTimeoutException("Command '" + first_argument(settings) + "' timed out after " +
                                          FormatMilliseconds(*Timeout) + "ms.");
            }
        }
        else
        {
            State->WaitForExit(token);
        }

        ExecResult result{State->ExitCode(), State->StdoutText(), State->StderrText()};
        State->Dispose();
        return result;
    }
    catch (const WslTimeoutException&)
    {
        State->Dispose();
        throw;
    }
    catch (...)
    {
        State->Kill(c_abortGracePeriod, std::stop_token{});
        State->Dispose();
        throw;
    }
}

void ProcessRunner::CopyTo(WslcContainer container, const std::filesystem::path& Source, const std::string& Destination,
                           std::stop_token token, std::function<void(LogLine)> observer)
{
    std::error_code error;
    const std::uintmax_t length = std::filesystem::file_size(Source, error);
    if (error)
    {
        throw WslProcessException("Host file '" + ToUtf8(Source.wstring()) + "' does not exist.");
    }

    if (length > c_maxCopyBytes)
    {
        throw WslProcessException("Copying '" + ToUtf8(Source.wstring()) + "' to '" + Destination +
                                  "' exceeds 1 GiB limit (" + std::to_string(length) + " bytes).");
    }

    ProcessSettings settings;
    settings.CommandLine = {"/bin/sh", "-c", "mkdir -p \"$(dirname \"$1\")\" && cat > \"$1\"", "sh", Destination};
    settings.EnableStandardInput = true;

    auto State = Prepare(std::move(observer), true);
    CreateNative(container, settings, *State, ErrorKind::Process);

    try
    {
        IoHandle stdinHandle;
        HANDLE Handle = nullptr;
        HRESULT result = WslcGetProcessIOHandle(State->Handle(), WSLC_PROCESS_IO_HANDLE_STDIN, &Handle);
        check(result, ErrorKind::Process, "Failed to open the process standard input", nullptr);
        stdinHandle = IoHandle(Handle);

        std::ifstream input(Source, std::ios::binary);
        if (!input)
        {
            throw WslProcessException("Failed to open Host file '" + ToUtf8(Source.wstring()) + "'.");
        }

        std::array<char, c_copyChunkSize> buffer{};
        while (input)
        {
            ThrowIfStopped(token);
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize read = input.gcount();
            if (read <= 0)
            {
                break;
            }

            std::streamsize offset = 0;
            while (offset < read)
            {
                DWORD written = 0;
                if (WriteFile(stdinHandle.get(), buffer.data() + offset, static_cast<DWORD>(read - offset), &written,
                              nullptr) == 0)
                {
                    throw WslProcessException("Failed to write to the process standard input.");
                }

                offset += written;
            }
        }

        stdinHandle.reset();
        State->WaitForExit(token);

        if (State->ExitCode() != 0)
        {
            throw WslProcessException("Copying '" + ToUtf8(Source.wstring()) + "' to '" + Destination +
                                      "' failed: " + trim(State->StderrText()));
        }

        State->Dispose();
    }
    catch (...)
    {
        State->Kill(c_abortGracePeriod, std::stop_token{});
        State->Dispose();
        throw;
    }
}

void ProcessRunner::CopyFrom(WslcContainer container, const std::string& Source,
                             const std::filesystem::path& Destination, std::stop_token token,
                             std::function<void(LogLine)> observer)
{
    ProcessSettings settings;
    settings.CommandLine = {"/bin/sh", "-c", "cat -- \"$1\"", "sh", Source};

    auto State = Prepare(std::move(observer), false);
    CreateNative(container, settings, *State, ErrorKind::Process);

    std::filesystem::path tempFile;
    try
    {
        IoHandle stdoutHandle;
        HANDLE Handle = nullptr;
        const HRESULT result = WslcGetProcessIOHandle(State->Handle(), WSLC_PROCESS_IO_HANDLE_STDOUT, &Handle);
        check(result, ErrorKind::Process, "Failed to open the process standard output", nullptr);
        stdoutHandle = IoHandle(Handle);

        const std::filesystem::path fullDestination = std::filesystem::absolute(Destination);
        EnsureReplaceableDestination(fullDestination);
        if (fullDestination.has_parent_path())
        {
            std::error_code error;
            std::filesystem::create_directories(fullDestination.parent_path(), error);
        }

        // Write to a sibling temp file; the destination is only replaced after the container
        // command exits successfully, so failures cannot truncate existing host data.
        tempFile = CreateAdjacentTempFile(fullDestination);

        {
            std::ofstream output(tempFile, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                throw WslProcessException("Failed to create temporary file '" + ToUtf8(tempFile.wstring()) + "'.");
            }

            std::array<char, c_copyChunkSize> buffer{};
            std::uint64_t total = 0;
            for (;;)
            {
                ThrowIfStopped(token);
                DWORD read = 0;
                if (ReadFile(stdoutHandle.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) == 0)
                {
                    const DWORD last_error = GetLastError();
                    if (last_error == ERROR_BROKEN_PIPE || last_error == ERROR_HANDLE_EOF)
                    {
                        break;
                    }

                    throw WslProcessException("Failed to read the process standard output.");
                }

                if (read == 0)
                {
                    break;
                }

                total += read;
                if (total > c_maxCopyBytes)
                {
                    throw WslProcessException("Copying '" + Source + "' to '" + ToUtf8(fullDestination.wstring()) +
                                              "' exceeded 1 GiB limit.");
                }

                output.write(buffer.data(), static_cast<std::streamsize>(read));
            }

            output.close();
        }

        stdoutHandle.reset();
        State->WaitForExit(token);

        if (State->ExitCode() != 0)
        {
            throw WslProcessException("Copying '" + Source + "' to '" + ToUtf8(fullDestination.wstring()) +
                                      "' failed: " + trim(State->StderrText()));
        }

        CommitFileReplace(tempFile, fullDestination);
        State->Dispose();
    }
    catch (...)
    {
        BestEffortDeleteFile(tempFile);
        State->Kill(c_abortGracePeriod, std::stop_token{});
        State->Dispose();
        throw;
    }
}

} // namespace wslc::internal
