#pragma once

#include <windows.h>

#include <cstddef>
#include <span>
#include <stop_token>

namespace wslc::internal
{

/// <summary>RAII owner for a Win32 kernel handle.</summary>
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

/// <summary>
/// Reads the next available chunk from a pipe. The pipe is polled with PeekNamedPipe and the
/// wait between polls is sliced, so a fired stop token is observed promptly instead of blocking
/// until the next byte arrives. Returns the number of bytes read, or 0 when the write end is
/// closed (end-of-file). Throws OperationCanceledException when the token fired, and
/// WslProcessException when the pipe cannot be read.
/// </summary>
std::size_t ReadPipeAvailable(const IoHandle& handle, std::span<char> buffer, std::stop_token token);

} // namespace wslc::internal
