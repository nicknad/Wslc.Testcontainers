#pragma once

#include <windows.h>

#include <wil/resource.h>

#include <cstddef>
#include <span>
#include <stop_token>

namespace wslc::internal
{

// Kernel handles are WIL RAII owners: wil::unique_handle (nullptr-invalid) for pipe and process
// IO handles, wil::unique_hfile (INVALID_HANDLE_VALUE-invalid) for CreateFileW results.

/// <summary>
/// Reads the next available chunk from a pipe. The pipe is polled with PeekNamedPipe and the
/// wait between polls is sliced, so a fired stop token is observed promptly instead of blocking
/// until the next byte arrives. Returns the number of bytes read, or 0 when the write end is
/// closed (end-of-file). Throws OperationCanceledException when the token fired, and
/// WslProcessException when the pipe cannot be read.
/// </summary>
std::size_t ReadPipeAvailable(const wil::unique_handle& handle, std::span<char> buffer, std::stop_token token);

} // namespace wslc::internal
