#include "internal/container_process.hpp"

#include "internal/util.hpp"
#include "wslc/exceptions.hpp"

namespace wslc::internal
{

namespace
{

constexpr std::chrono::milliseconds c_defaultGracePeriod{5000};
constexpr std::chrono::milliseconds c_waitSlice{100};

} // namespace

ContainerProcessState::ContainerProcessState(bool capture_output, std::function<void(LogLine)> observer)
    : m_captureOutput(capture_output), m_observer(std::move(observer)),
      m_stdoutLines([this](std::string Text) { Publish(LogSource::Stdout, std::move(Text)); }),
      m_stderrLines([this](std::string Text) { Publish(LogSource::Stderr, std::move(Text)); })
{
    if (m_captureOutput)
    {
        m_callbacks.onStdOut = &stdout_thunk;
        m_callbacks.onStdErr = &stderr_thunk;
        m_callbacks.onExit = &exit_thunk;
    }
}

ContainerProcessState::~ContainerProcessState()
{
    Dispose();
}

void ContainerProcessState::SetHandle(WslcProcess Handle)
{
    std::lock_guard lock(m_mutex);
    m_handle = ProcessHandle(Handle);
}

WslcProcess ContainerProcessState::Handle() const
{
    std::lock_guard lock(m_mutex);
    return m_handle.get();
}

std::optional<int> ContainerProcessState::pid() const
{
    std::lock_guard lock(m_mutex);
    if (m_handle.get() == nullptr)
    {
        return std::nullopt;
    }

    std::uint32_t pid = 0;
    if (FAILED(WslcGetProcessPid(m_handle.get(), &pid)) || pid == 0)
    {
        return std::nullopt;
    }

    return static_cast<int>(pid);
}

bool ContainerProcessState::HasExited() const
{
    {
        std::lock_guard lock(m_mutex);
        if (m_exited)
        {
            return true;
        }

        if (m_handle.get() == nullptr)
        {
            return false;
        }

        WslcProcessState State = WSLC_PROCESS_STATE_UNKNOWN;
        if (FAILED(WslcGetProcessState(m_handle.get(), &State)))
        {
            return false;
        }

        if (State == WSLC_PROCESS_STATE_EXITED || State == WSLC_PROCESS_STATE_SIGNALLED)
        {
            return true;
        }
    }

    return false;
}

int ContainerProcessState::ExitCode() const
{
    {
        std::lock_guard lock(m_mutex);
        if (m_exited)
        {
            return m_exitCode;
        }

        if (m_handle.get() != nullptr)
        {
            WslcProcessState State = WSLC_PROCESS_STATE_UNKNOWN;
            if (SUCCEEDED(WslcGetProcessState(m_handle.get(), &State)) &&
                (State == WSLC_PROCESS_STATE_EXITED || State == WSLC_PROCESS_STATE_SIGNALLED))
            {
                INT32 code = 0;
                if (SUCCEEDED(WslcGetProcessExitCode(m_handle.get(), &code)))
                {
                    return code;
                }
            }
        }
    }

    throw WslProcessException("The process has not exited yet.");
}

int ContainerProcessState::WaitForExit(std::stop_token token)
{
    if (m_captureOutput)
    {
        std::unique_lock lock(m_mutex);
        if (!m_exited && m_handle.get() == nullptr)
        {
            throw WslProcessException("The process has not been started.");
        }

        if (!m_exited)
        {
            std::stop_callback callback(token, [this] { m_condition.notify_all(); });
            m_condition.wait(lock, [this, &token] { return m_exited || token.stop_requested(); });
        }

        if (!m_exited)
        {
            throw OperationCanceledException();
        }

        return m_exitCode;
    }

    // Without callbacks there is no exit notification: Wait on the native exit event and
    // query the code afterwards.
    HANDLE exit_event = nullptr;
    WslcProcess Handle = nullptr;
    {
        std::lock_guard lock(m_mutex);
        if (m_exited)
        {
            return m_exitCode;
        }

        Handle = m_handle.get();
        if (Handle == nullptr)
        {
            throw WslProcessException("The process has not been started.");
        }
    }

    if (SUCCEEDED(WslcGetProcessExitEvent(Handle, &exit_event)) && exit_event != nullptr)
    {
        for (;;)
        {
            ThrowIfStopped(token);
            const DWORD result = WaitForSingleObject(exit_event, static_cast<DWORD>(c_waitSlice.count()));
            if (result == WAIT_OBJECT_0)
            {
                break;
            }

            if (result == WAIT_FAILED)
            {
                break;
            }
        }
    }
    else
    {
        for (;;)
        {
            ThrowIfStopped(token);
            if (HasExited())
            {
                break;
            }

            if (!SleepFor(c_waitSlice, token))
            {
                throw OperationCanceledException();
            }
        }
    }

    mark_exited_from_native();
    std::lock_guard lock(m_mutex);
    return m_exitCode;
}

bool ContainerProcessState::WaitForExitFor(std::chrono::milliseconds Timeout, std::stop_token token)
{
    const auto deadline = std::chrono::steady_clock::now() + Timeout;
    if (m_captureOutput)
    {
        std::unique_lock lock(m_mutex);
        if (m_exited)
        {
            return true;
        }

        std::stop_callback callback(token, [this] { m_condition.notify_all(); });
        // wait_for (not wait) so the deadline itself wakes the wait; the predicate alone would
        // never be re-evaluated without a notification.
        m_condition.wait_for(lock, Timeout, [this, &token] { return m_exited || token.stop_requested(); });

        if (m_exited)
        {
            return true;
        }

        if (token.stop_requested())
        {
            throw OperationCanceledException();
        }

        return false;
    }

    HANDLE exit_event = nullptr;
    WslcProcess Handle = nullptr;
    {
        std::lock_guard lock(m_mutex);
        if (m_exited)
        {
            return true;
        }

        Handle = m_handle.get();
        if (Handle == nullptr)
        {
            throw WslProcessException("The process has not been started.");
        }
    }

    if (SUCCEEDED(WslcGetProcessExitEvent(Handle, &exit_event)) && exit_event != nullptr)
    {
        for (;;)
        {
            ThrowIfStopped(token);
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                return false;
            }

            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            const DWORD slice = static_cast<DWORD>(std::min(remaining.count(), c_waitSlice.count()));
            const DWORD result = WaitForSingleObject(exit_event, slice);
            if (result == WAIT_OBJECT_0)
            {
                break;
            }

            if (result == WAIT_FAILED)
            {
                return false;
            }
        }
    }
    else
    {
        for (;;)
        {
            ThrowIfStopped(token);
            if (HasExited())
            {
                break;
            }

            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }

            if (!SleepFor(c_waitSlice, token))
            {
                throw OperationCanceledException();
            }
        }
    }

    mark_exited_from_native();
    return true;
}

void ContainerProcessState::Kill(std::chrono::milliseconds grace_period, std::stop_token token)
{
    if (HasExited())
    {
        return;
    }

    TrySignal(WSLC_SIGNAL_SIGTERM);
    if (WaitForExitFor(grace_period, token))
    {
        return;
    }

    TrySignal(WSLC_SIGNAL_SIGKILL);
    WaitForExitFor(grace_period, token);
}

void ContainerProcessState::Dispose()
{
    {
        std::lock_guard lock(m_mutex);
        if (m_disposed)
        {
            return;
        }

        m_disposed = true;
    }

    if (!HasExited())
    {
        TrySignal(WSLC_SIGNAL_SIGKILL);
    }

    Flush();
    m_stdoutBuffer.Dispose();
    m_stderrBuffer.Dispose();
    {
        std::lock_guard lock(m_mutex);
        m_handle.reset();
    }
}

void ContainerProcessState::TrySignal(WslcSignal signal) noexcept
{
    std::lock_guard lock(m_mutex);
    if (m_handle.get() != nullptr)
    {
        WslcSignalProcess(m_handle.get(), signal);
    }
}

void CALLBACK ContainerProcessState::stdout_thunk(WslcProcessIOHandle, const BYTE* data, std::uint32_t data_bytes,
                                                  PVOID context)
{
    if (context != nullptr)
    {
        static_cast<ContainerProcessState*>(context)->on_output(LogSource::Stdout, data, data_bytes);
    }
}

void CALLBACK ContainerProcessState::stderr_thunk(WslcProcessIOHandle, const BYTE* data, std::uint32_t data_bytes,
                                                  PVOID context)
{
    if (context != nullptr)
    {
        static_cast<ContainerProcessState*>(context)->on_output(LogSource::Stderr, data, data_bytes);
    }
}

void CALLBACK ContainerProcessState::exit_thunk(INT32 ExitCode, PVOID context)
{
    if (context != nullptr)
    {
        static_cast<ContainerProcessState*>(context)->on_exit(ExitCode);
    }
}

void ContainerProcessState::on_output(LogSource Source, const std::uint8_t* data, std::uint32_t data_bytes)
{
    try
    {
        const std::span<const std::uint8_t> bytes(data, data_bytes);
        if (Source == LogSource::Stdout)
        {
            m_stdoutBuffer.Append(bytes);
            m_stdoutLines.Append(bytes);
        }
        else
        {
            m_stderrBuffer.Append(bytes);
            m_stderrLines.Append(bytes);
        }
    }
    catch (...)
    {
        // Capture must never propagate into the native callback.
    }
}

void ContainerProcessState::on_exit(std::int32_t ExitCode)
{
    m_stdoutLines.Flush();
    m_stderrLines.Flush();
    Publish(LogSource::System, "process exited with code " + std::to_string(ExitCode));
    {
        std::lock_guard lock(m_mutex);
        m_exitCode = ExitCode;
        m_exited = true;
    }

    m_condition.notify_all();
}

void ContainerProcessState::Flush()
{
    m_stdoutLines.Flush();
    m_stderrLines.Flush();
}

void ContainerProcessState::Publish(LogSource Source, std::string Text)
{
    if (!m_observer)
    {
        return;
    }

    try
    {
        m_observer(LogLine{Source, std::move(Text), std::chrono::system_clock::now()});
    }
    catch (...)
    {
        // Observers must never propagate into the native callback.
    }
}

void ContainerProcessState::mark_exited_from_native()
{
    INT32 code = 0;
    WslcProcess Handle = nullptr;
    {
        std::lock_guard lock(m_mutex);
        if (m_exited)
        {
            return;
        }

        Handle = m_handle.get();
    }

    if (Handle != nullptr && SUCCEEDED(WslcGetProcessExitCode(Handle, &code)))
    {
        std::lock_guard lock(m_mutex);
        m_exitCode = code;
        m_exited = true;
    }
}

ContainerProcess::ContainerProcess(std::shared_ptr<ContainerProcessState> State) : m_state(std::move(State)) {}

ContainerProcess::~ContainerProcess()
{
    Dispose();
}

std::optional<int> ContainerProcess::Id() const
{
    return m_state->pid();
}

bool ContainerProcess::HasExited() const
{
    return m_state->HasExited();
}

int ContainerProcess::ExitCode() const
{
    return m_state->ExitCode();
}

int ContainerProcess::WaitForExit(std::stop_token token)
{
    return m_state->WaitForExit(token);
}

void ContainerProcess::Kill(std::stop_token token)
{
    m_state->Kill(c_defaultGracePeriod, token);
}

void ContainerProcess::Dispose()
{
    if (m_state)
    {
        m_state->Dispose();
    }
}

} // namespace wslc::internal
