#pragma once

#include <chrono>
#include <stop_token>
#include <string>

namespace wslc::internal
{

/// <summary>Initializes Winsock once per process; safe to call from any networking path.</summary>
void EnsureWinsock();

/// <summary>Returns true when the host accepts a TCP connection before the timeout.</summary>
bool TcpProbe(const std::string& host, int port, std::chrono::milliseconds timeout, std::stop_token token);

/// <summary>
/// Performs an HTTP GET and returns true when the response status is below 500. Connection or
/// protocol failures return false; caller cancellation throws OperationCanceledException.
/// </summary>
bool HttpGetSucceeds(const std::string& host, int port, const std::string& pathAndQuery, std::stop_token token);

} // namespace wslc::internal
