#include "internal/pipe_reader.hpp"

#include "internal/util.hpp"
#include "wslc/exceptions.hpp"

#include <chrono>

namespace wslc::internal
{

namespace
{

// Bound on each idle wait; SleepFor wakes early when the token fires.
constexpr std::chrono::milliseconds c_waitSlice{100};

} // namespace

std::size_t ReadPipeAvailable(const wil::unique_handle& handle, std::span<char> buffer, std::stop_token token)
{
    for (;;)
    {
        ThrowIfStopped(token);

        DWORD available = 0;
        if (PeekNamedPipe(handle.get(), nullptr, 0, nullptr, &available, nullptr) == 0)
        {
            const DWORD last_error = GetLastError();
            if (last_error == ERROR_BROKEN_PIPE || last_error == ERROR_HANDLE_EOF)
            {
                return 0;
            }

            throw WslProcessException("Failed to read the process standard output.");
        }

        if (available > 0)
        {
            DWORD read = 0;
            if (ReadFile(handle.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) == 0)
            {
                const DWORD last_error = GetLastError();
                if (last_error == ERROR_BROKEN_PIPE || last_error == ERROR_HANDLE_EOF)
                {
                    return 0;
                }

                throw WslProcessException("Failed to read the process standard output.");
            }

            return read;
        }

        if (!SleepFor(c_waitSlice, token))
        {
            throw OperationCanceledException();
        }
    }
}

} // namespace wslc::internal
