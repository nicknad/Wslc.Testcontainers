#include "internal/process_runner.hpp"

#include "internal/pipe_reader.hpp"
#include "internal/util.hpp"
#include "wslc/exceptions.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <utility>

namespace wslc::internal
{

namespace
{

constexpr std::uint64_t c_maxCopyBytes = 1024ull * 1024ull * 1024ull;
constexpr std::size_t c_copyChunkSize = 64 * 1024;
constexpr std::chrono::milliseconds c_abortGracePeriod{2000};

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

void ProcessRunner::CreateNative(WslcContainer container, const ProcessSettings& settings, ContainerProcessState& State)
{
    if (settings.CommandLine.empty())
    {
        throw WslException("A command line is required.");
    }

    WslcProcessSettings nativeSettings{};
    HRESULT result = WslcInitProcessSettings(&nativeSettings);
    check(result, ErrorKind::Process, "Failed to initialize process settings", nullptr);

    if (settings.WorkingDirectory && !settings.WorkingDirectory->empty())
    {
        result = WslcSetProcessSettingsWorkingDirectory(&nativeSettings, settings.WorkingDirectory->c_str());
        check(result, ErrorKind::Process, "Failed to set the process working directory", nullptr);
    }

    const std::vector<PCSTR> argv = build_argv(settings.CommandLine);
    result = WslcSetProcessSettingsCmdLine(&nativeSettings, argv.data(), argv.size());
    check(result, ErrorKind::Process, "Failed to set the process command line", nullptr);

    // The SDK takes an array of "KEY=VALUE" strings.
    const std::vector<std::string> Environment = BuildEnvironment(settings.Environment);
    const std::vector<PCSTR> environmentValues = build_environment_values(Environment);
    if (!environmentValues.empty())
    {
        result =
            WslcSetProcessSettingsEnvVariables(&nativeSettings, environmentValues.data(), environmentValues.size());
        check(result, ErrorKind::Process, "Failed to set the process environment", nullptr);
    }

    if (settings.EnableStandardInput)
    {
        result = WslcSetProcessSettingsFlags(&nativeSettings, WSLC_PROCESS_FLAG_STDIN);
        check(result, ErrorKind::Process, "Failed to enable standard input", nullptr);
    }

    if (State.NativeCallbacks()->onStdOut != nullptr)
    {
        result = WslcSetProcessSettingsCallbacks(&nativeSettings, State.NativeCallbacks(), State.NativeContext());
        check(result, ErrorKind::Process, "Failed to set process callbacks", nullptr);
    }

    WslcProcess process = nullptr;
    PWSTR error = nullptr;
    result = WslcCreateContainerProcess(container, &nativeSettings, &process, &error);
    check(result, ErrorKind::Process, "Failed to create the process", &error);
    State.SetHandle(process);
}

ExecResult ProcessRunner::Run(WslcContainer container, const ProcessSettings& settings,
                              const std::optional<std::string>& StandardInput,
                              std::optional<std::chrono::milliseconds> Timeout, std::stop_token token,
                              std::function<void(LogLine)> observer)
{
    if (StandardInput)
    {
        ValidateStandardInputSize(StandardInput->size());
    }

    auto State = Prepare(std::move(observer), true);
    CreateNative(container, settings, *State);

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

                // A successful WriteFile that reports zero bytes would otherwise spin forever.
                if (written == 0)
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
                                          FormatMilliseconds(*Timeout) + "s.");
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
    ProcessSettings settings;
    settings.CommandLine = {"/bin/sh", "-c", "mkdir -p \"$(dirname \"$1\")\" && cat > \"$1\"", "sh", Destination};
    settings.EnableStandardInput = true;

    auto State = Prepare(std::move(observer), true);
    CreateNative(container, settings, *State);

    try
    {
        IoHandle stdinHandle;
        HANDLE Handle = nullptr;
        HRESULT result = WslcGetProcessIOHandle(State->Handle(), WSLC_PROCESS_IO_HANDLE_STDIN, &Handle);
        check(result, ErrorKind::Process, "Failed to open the process standard input", nullptr);
        stdinHandle = IoHandle(Handle);

        // Open without following reparse points, then read attributes and size from the same
        // handle: a path swapped after a pre-check cannot bypass these guards.
        IoHandle fileHandle(CreateFileW(
            Source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (!fileHandle)
        {
            const DWORD last_error = GetLastError();
            if (last_error == ERROR_FILE_NOT_FOUND || last_error == ERROR_PATH_NOT_FOUND)
            {
                throw WslProcessException("Host file '" + ToUtf8(Source.wstring()) + "' does not exist.");
            }

            throw WslProcessException("Failed to open Host file '" + ToUtf8(Source.wstring()) +
                                      "': " + FormatWindowsError(last_error));
        }

        BY_HANDLE_FILE_INFORMATION information{};
        if (GetFileInformationByHandle(fileHandle.get(), &information) == 0)
        {
            throw WslProcessException("Failed to inspect Host file '" + ToUtf8(Source.wstring()) +
                                      "': " + FormatWindowsError(GetLastError()));
        }

        if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            throw WslProcessException("Host file '" + ToUtf8(Source.wstring()) +
                                      "' is a reparse point (symlink or junction); refusing to follow it.");
        }

        if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            throw WslProcessException("Host path '" + ToUtf8(Source.wstring()) +
                                      "' is a directory; only files can be copied.");
        }

        LARGE_INTEGER size{};
        if (GetFileSizeEx(fileHandle.get(), &size) == 0)
        {
            throw WslProcessException("Failed to inspect Host file '" + ToUtf8(Source.wstring()) +
                                      "': " + FormatWindowsError(GetLastError()));
        }

        if (std::cmp_greater(size.QuadPart, c_maxCopyBytes))
        {
            throw WslProcessException("Copying '" + ToUtf8(Source.wstring()) + "' to '" + Destination +
                                      "' exceeds 1 GiB limit (" + std::to_string(size.QuadPart) + " bytes).");
        }

        std::array<char, c_copyChunkSize> buffer{};
        std::uint64_t total = 0;
        for (;;)
        {
            ThrowIfStopped(token);
            DWORD read = 0;
            if (ReadFile(fileHandle.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) == 0)
            {
                throw WslProcessException("Failed to read Host file '" + ToUtf8(Source.wstring()) +
                                          "': " + FormatWindowsError(GetLastError()));
            }

            if (read == 0)
            {
                break;
            }

            // Accumulate while streaming so a file that grows during the copy is aborted
            // even though the handle size was within the cap at open time.
            total += read;
            if (total > c_maxCopyBytes)
            {
                throw WslProcessException("Copying '" + ToUtf8(Source.wstring()) + "' to '" + Destination +
                                          "' exceeded 1 GiB limit.");
            }

            std::size_t offset = 0;
            while (offset < read)
            {
                DWORD written = 0;
                if (WriteFile(stdinHandle.get(), buffer.data() + offset, static_cast<DWORD>(read - offset), &written,
                              nullptr) == 0)
                {
                    throw WslProcessException("Failed to write to the process standard input.");
                }

                // A successful WriteFile that reports zero bytes would otherwise spin forever.
                if (written == 0)
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
                                      "' failed: " + Trim(State->StderrText()));
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
    CreateNative(container, settings, *State);

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
                // Poll instead of blocking in ReadFile so a cancelled copy is observed between
                // chunks; ReadPipeAvailable returns 0 once the stdout pipe reaches end-of-file.
                const std::size_t read = ReadPipeAvailable(stdoutHandle, buffer, token);
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
                                      "' failed: " + Trim(State->StderrText()));
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
