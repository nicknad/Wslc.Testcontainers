#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <objbase.h>

#include <wslcsdk.h>

#include <string>
#include <string_view>

namespace wslc::internal
{

/// <summary>
/// Initializes COM (multithreaded apartment) once per thread. The native SDK activates WinRT
/// classes through COM, so every thread that calls the SDK must be initialized; already
/// initialized threads (including RPC_E_CHANGED_MODE) are left alone.
/// </summary>
void EnsureComInitialized();

/// <summary>Selects the exception family a native failure is reported as.</summary>
enum class ErrorKind
{
    Provisioning,
    Process,
};

/// <summary>Formats an HRESULT as 0xXXXXXXXX.</summary>
std::string HresultHex(HRESULT hr);

/// <summary>Throws the exception matching <paramref Name="Kind"/> and frees the native message.</summary>
[[noreturn]] void ThrowNative(ErrorKind Kind, HRESULT hr, std::string_view context, PWSTR message);

/// <summary>Throws when the HRESULT is a failure; the optional message is owned by the caller.</summary>
void check(HRESULT hr, ErrorKind Kind, std::string_view context, PWSTR* message);

/// <summary>Frees a CoTaskMemAlloc-allocated string and returns it as UTF-8.</summary>
std::string ConsumeCoTaskString(PWSTR value);

/// <summary>Returns true for runtime errors that mean the target is already gone.</summary>
bool IsBenignRuntimeError(HRESULT hr) noexcept;

/// <summary>RAII Owner for a WslcSession Handle.</summary>
class SessionHandle
{
public:
    SessionHandle() = default;
    explicit SessionHandle(WslcSession Handle) : m_handle(Handle) {}
    ~SessionHandle() { reset(); }
    SessionHandle(const SessionHandle&) = delete;
    SessionHandle& operator=(const SessionHandle&) = delete;
    SessionHandle(SessionHandle&& other) noexcept : m_handle(other.m_handle) { other.m_handle = nullptr; }
    SessionHandle& operator=(SessionHandle&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    WslcSession get() const noexcept { return m_handle; }
    explicit operator bool() const noexcept { return m_handle != nullptr; }

    void reset() noexcept
    {
        if (m_handle != nullptr)
        {
            WslcReleaseSession(m_handle);
            m_handle = nullptr;
        }
    }

private:
    WslcSession m_handle = nullptr;
};

/// <summary>RAII Owner for a WslcContainer Handle.</summary>
class ContainerHandle
{
public:
    ContainerHandle() = default;
    explicit ContainerHandle(WslcContainer Handle) : m_handle(Handle) {}
    ~ContainerHandle() { reset(); }
    ContainerHandle(const ContainerHandle&) = delete;
    ContainerHandle& operator=(const ContainerHandle&) = delete;
    ContainerHandle(ContainerHandle&& other) noexcept : m_handle(other.m_handle) { other.m_handle = nullptr; }
    ContainerHandle& operator=(ContainerHandle&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    WslcContainer get() const noexcept { return m_handle; }
    explicit operator bool() const noexcept { return m_handle != nullptr; }

    void reset() noexcept
    {
        if (m_handle != nullptr)
        {
            WslcReleaseContainer(m_handle);
            m_handle = nullptr;
        }
    }

private:
    WslcContainer m_handle = nullptr;
};

/// <summary>RAII Owner for a WslcProcess Handle.</summary>
class ProcessHandle
{
public:
    ProcessHandle() = default;
    explicit ProcessHandle(WslcProcess Handle) : m_handle(Handle) {}
    ~ProcessHandle() { reset(); }
    ProcessHandle(const ProcessHandle&) = delete;
    ProcessHandle& operator=(const ProcessHandle&) = delete;
    ProcessHandle(ProcessHandle&& other) noexcept : m_handle(other.m_handle) { other.m_handle = nullptr; }
    ProcessHandle& operator=(ProcessHandle&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    WslcProcess get() const noexcept { return m_handle; }
    explicit operator bool() const noexcept { return m_handle != nullptr; }

    void reset() noexcept
    {
        if (m_handle != nullptr)
        {
            WslcReleaseProcess(m_handle);
            m_handle = nullptr;
        }
    }

private:
    WslcProcess m_handle = nullptr;
};

/// <summary>RAII Owner for a CoTaskMemAlloc-allocated string.</summary>
template <typename T> class CoTaskMemPtr
{
public:
    CoTaskMemPtr() = default;
    explicit CoTaskMemPtr(T* value) : m_value(value) {}
    ~CoTaskMemPtr() { reset(); }
    CoTaskMemPtr(const CoTaskMemPtr&) = delete;
    CoTaskMemPtr& operator=(const CoTaskMemPtr&) = delete;
    CoTaskMemPtr(CoTaskMemPtr&& other) noexcept : m_value(other.m_value) { other.m_value = nullptr; }
    CoTaskMemPtr& operator=(CoTaskMemPtr&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_value = other.m_value;
            other.m_value = nullptr;
        }
        return *this;
    }

    void reset()
    {
        if (m_value != nullptr)
        {
            CoTaskMemFree(m_value);
            m_value = nullptr;
        }
    }

private:
    T* m_value = nullptr;
};

} // namespace wslc::internal
