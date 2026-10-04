#include "internal/api.hpp"

#include "internal/util.hpp"
#include "wslc/exceptions.hpp"

#include <cstdio>
#include <memory>

namespace wslc::internal
{

void EnsureComInitialized()
{
    thread_local const bool initialized = []
    {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        // RPC_E_CHANGED_MODE means the thread already has COM in another apartment; keep going.
        return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
    }();
    static_cast<void>(initialized);
}

std::string HresultHex(HRESULT hr)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(hr));
    return buffer;
}

namespace
{

std::unique_ptr<WslcException> CreateException(ErrorKind Kind, std::string message)
{
    switch (Kind)
    {
    case ErrorKind::Runtime:
        return std::make_unique<WslRuntimeException>(std::move(message));
    case ErrorKind::Provisioning:
        return std::make_unique<WslProvisioningException>(std::move(message));
    case ErrorKind::Network:
        return std::make_unique<WslNetworkException>(std::move(message));
    case ErrorKind::Process:
        return std::make_unique<WslProcessException>(std::move(message));
    case ErrorKind::Cleanup:
        return std::make_unique<WslCleanupException>(std::move(message));
    }

    return std::make_unique<WslcException>(std::move(message));
}

} // namespace

void ThrowNative(ErrorKind Kind, HRESULT hr, std::string_view context, PWSTR message)
{
    std::string detail = ConsumeCoTaskString(message);
    std::string Text(context);
    Text += " (HRESULT ";
    Text += HresultHex(hr);
    if (!detail.empty())
    {
        Text += ": ";
        Text += detail;
    }

    Text += ")";
    throw *CreateException(Kind, std::move(Text));
}

void check(HRESULT hr, ErrorKind Kind, std::string_view context, PWSTR* message)
{
    if (SUCCEEDED(hr))
    {
        if (message != nullptr && *message != nullptr)
        {
            CoTaskMemFree(*message);
            *message = nullptr;
        }

        return;
    }

    PWSTR native_message = message != nullptr ? *message : nullptr;
    if (message != nullptr)
    {
        *message = nullptr;
    }

    ThrowNative(Kind, hr, context, native_message);
}

std::string ConsumeCoTaskString(PWSTR value)
{
    CoTaskMemPtr<wchar_t> Owner(value);
    return value == nullptr ? std::string() : ToUtf8(value);
}

bool IsBenignRuntimeError(HRESULT hr) noexcept
{
    return hr == WSLC_E_CONTAINER_NOT_RUNNING || hr == WSLC_E_CONTAINER_NOT_FOUND || hr == WSLC_E_CONTAINER_DELETED ||
           hr == WSLC_E_SESSION_RESERVED;
}

} // namespace wslc::internal
