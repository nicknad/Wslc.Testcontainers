#pragma once

#include "internal/api.hpp"
#include "internal/capture_buffer.hpp"
#include "internal/line_assembler.hpp"
#include "wslc/log_line.hpp"
#include "wslc/process.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>

namespace wslc::internal
{

/// <summary>
/// Native Handle plus capture/exit State for one WSLC process. The State is shared so a
/// container's registry can observe and terminate processes without owning the caller's Handle.
/// </summary>
class ContainerProcessState
{
public:
    ContainerProcessState(bool capture_output, std::function<void(LogLine)> observer);
    ~ContainerProcessState();
    ContainerProcessState(const ContainerProcessState&) = delete;
    ContainerProcessState& operator=(const ContainerProcessState&) = delete;

    void SetHandle(WslcProcess Handle);

    /// <summary>Returns the native process Handle (null before creation).</summary>
    WslcProcess Handle() const;

    /// <summary>Native callbacks to register on the settings before the process is created.</summary>
    const WslcProcessCallbacks* NativeCallbacks() const noexcept { return &m_callbacks; }

    /// <summary>Context pointer for the native callbacks.</summary>
    void* NativeContext() noexcept { return this; }

    std::optional<int> pid() const;
    bool HasExited() const;
    int ExitCode() const;
    int WaitForExit(std::stop_token token);
    bool WaitForExitFor(std::chrono::milliseconds Timeout, std::stop_token token);
    void Kill(std::chrono::milliseconds grace_period, std::stop_token token);
    void Dispose();

    std::string StdoutText() const { return m_stdoutBuffer.Decode(); }
    std::string StderrText() const { return m_stderrBuffer.Decode(); }

    /// <summary>Signals the process without waiting; used by best-effort Cleanup paths.</summary>
    void TrySignal(WslcSignal signal) noexcept;

private:
    static void CALLBACK stdout_thunk(WslcProcessIOHandle Handle, const BYTE* data, std::uint32_t data_bytes,
                                      PVOID context);
    static void CALLBACK stderr_thunk(WslcProcessIOHandle Handle, const BYTE* data, std::uint32_t data_bytes,
                                      PVOID context);
    static void CALLBACK exit_thunk(INT32 ExitCode, PVOID context);

    void on_output(LogSource Source, const std::uint8_t* data, std::uint32_t data_bytes);
    void on_exit(std::int32_t ExitCode);
    void Flush();
    void Publish(LogSource Source, std::string Text);
    void mark_exited_from_native();

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_exited = false;
    int m_exitCode = 0;
    bool m_captureOutput = false;
    std::function<void(LogLine)> m_observer;
    ProcessHandle m_handle;
    CaptureBuffer m_stdoutBuffer;
    CaptureBuffer m_stderrBuffer;
    LineAssembler m_stdoutLines;
    LineAssembler m_stderrLines;
    WslcProcessCallbacks m_callbacks{};
    bool m_disposed = false;
};

/// <summary>Adapts <see cref="ContainerProcessState"/> to the public process Handle.</summary>
class ContainerProcess final : public IWslProcess
{
public:
    explicit ContainerProcess(std::shared_ptr<ContainerProcessState> State);
    ~ContainerProcess() override;

    std::optional<int> Id() const override;
    bool HasExited() const override;
    int ExitCode() const override;
    int WaitForExit(std::stop_token token = {}) override;
    void Kill(std::stop_token token = {}) override;
    void Dispose() override;

private:
    std::shared_ptr<ContainerProcessState> m_state;
};

} // namespace wslc::internal
