#include "wslc/platform.hpp"

#include <windows.h>

#include <string>

namespace wslc
{

namespace
{

struct RtlOsVersionInfo
{
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    wchar_t szCSDVersion[128];
};

bool WindowsVersionAtLeast(ULONG Major, ULONG Minor, ULONG Build)
{
    using RtlGetVersionFn = LONG(WINAPI*)(RtlOsVersionInfo*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        return false;
    }

    // GetProcAddress returns FARPROC (INT_PTR (*)()) while RtlGetVersion returns LONG, so route
    // the conversion through void* to keep clang's cast-function-type-mismatch diagnostic quiet.
    const auto proc = reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion"));
    const auto rtl_get_version = reinterpret_cast<RtlGetVersionFn>(proc);
    if (rtl_get_version == nullptr)
    {
        return false;
    }

    RtlOsVersionInfo info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtl_get_version(&info) != 0)
    {
        return false;
    }

    if (info.dwMajorVersion != Major)
    {
        return info.dwMajorVersion > Major;
    }

    if (info.dwMinorVersion != Minor)
    {
        return info.dwMinorVersion > Minor;
    }

    return info.dwBuildNumber >= Build;
}

std::string architecture()
{
#if defined(_M_ARM64)
    return "Arm64";
#elif defined(_M_X64)
    return "X64";
#elif defined(_M_IX86)
    return "X86";
#else
    return "Unknown";
#endif
}

} // namespace

bool WslPlatform::IsSupported()
{
#if defined(_M_X64) || defined(_M_ARM64)
    return WindowsVersionAtLeast(10, 0, 19041);
#else
    return false;
#endif
}

std::string WslPlatform::DescribeCurrent()
{
    const std::string arch = architecture();
    std::string os = "Windows";
    if (WindowsVersionAtLeast(10, 0, 0))
    {
        os = "Windows 10+";
    }

    return os + " (" + arch + ")";
}

void WslPlatform::ThrowIfUnsupported()
{
    if (IsSupported())
    {
        return;
    }

    throw PlatformNotSupportedException(
        "Wslc.Testcontainers requires Windows 10 Build 19041+ (x64/ARM64) with WSL " +
        std::to_string(MinimumWslVersion.Major) + "." + std::to_string(MinimumWslVersion.Minor) + "." +
        std::to_string(MinimumWslVersion.Revision) + " or newer. Current: " + DescribeCurrent() +
        ". Run 'wsl --install' (or 'wsl --update') on a supported Windows Host and retry.");
}

bool WslPlatform::IsWslVersionSupported(std::uint32_t Major, std::uint32_t Minor, std::uint32_t patch) noexcept
{
    if (Major != MinimumWslVersion.Major)
    {
        return Major > MinimumWslVersion.Major;
    }

    if (Minor != MinimumWslVersion.Minor)
    {
        return Minor > MinimumWslVersion.Minor;
    }

    return patch >= MinimumWslVersion.Revision;
}

} // namespace wslc
