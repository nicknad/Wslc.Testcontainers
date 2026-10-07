#include "internal/tcp_http.hpp"

#include "internal/util.hpp"
#include "wslc/exceptions.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <wil/resource.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>
#include <utility>

#pragma comment(lib, "ws2_32.lib")

namespace wslc::internal
{

void EnsureWinsock()
{
    static std::once_flag flag;
    std::call_once(flag,
                   []
                   {
                       WSADATA data{};
                       WSAStartup(MAKEWORD(2, 2), &data);
                   });
}

namespace
{

constexpr std::chrono::milliseconds c_connectTimeout{2000};
constexpr std::chrono::milliseconds c_ioTimeout{5000};
constexpr std::chrono::milliseconds c_selectSlice{50};

/// <summary>Connects to a numeric Host; nullopt socket and false on connect failure/Timeout.</summary>
bool ConnectSocket(const std::string& Host, int port, std::chrono::milliseconds Timeout, std::stop_token token,
                   wil::unique_socket& out_socket)
{
    EnsureWinsock();
    ThrowIfStopped(token);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

    addrinfo* addresses = nullptr;
    const std::string portText = std::to_string(port);
    if (getaddrinfo(Host.c_str(), portText.c_str(), &hints, &addresses) != 0 || addresses == nullptr)
    {
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + Timeout;
    bool connected = false;

    for (addrinfo* address = addresses; address != nullptr && !connected; address = address->ai_next)
    {
        wil::unique_socket candidate(::socket(address->ai_family, address->ai_socktype, address->ai_protocol));
        if (!candidate)
        {
            continue;
        }

        u_long non_blocking = 1;
        ioctlsocket(candidate.get(), FIONBIO, &non_blocking);

        const int result = connect(candidate.get(), address->ai_addr, static_cast<int>(address->ai_addrlen));
        if (result == 0)
        {
            connected = true;
        }
        else
        {
            const int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEINVAL)
            {
                for (;;)
                {
                    ThrowIfStopped(token);
                    const auto now = std::chrono::steady_clock::now();
                    if (now >= deadline)
                    {
                        break;
                    }

                    const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::min(std::chrono::duration_cast<std::chrono::microseconds>(deadline - now),
                                 std::chrono::duration_cast<std::chrono::microseconds>(c_selectSlice)));

                    fd_set write_set;
                    fd_set error_set;
                    FD_ZERO(&write_set);
                    FD_ZERO(&error_set);
                    FD_SET(candidate.get(), &write_set);
                    FD_SET(candidate.get(), &error_set);
                    timeval timeout_value{};
                    timeout_value.tv_sec = static_cast<long>(remaining.count() / 1'000'000);
                    timeout_value.tv_usec = static_cast<long>(remaining.count() % 1'000'000);

                    const int selected = select(0, nullptr, &write_set, &error_set, &timeout_value);
                    if (selected > 0)
                    {
                        int socket_error = 0;
                        int length = sizeof(socket_error);
                        if (getsockopt(candidate.get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error),
                                       &length) == 0 &&
                            socket_error == 0)
                        {
                            connected = true;
                        }

                        break;
                    }
                }
            }
        }

        if (connected)
        {
            out_socket = std::move(candidate);
        }
    }

    freeaddrinfo(addresses);
    return connected;
}

bool WaitReadable(SOCKET socket, std::chrono::steady_clock::time_point deadline, std::stop_token token)
{
    for (;;)
    {
        ThrowIfStopped(token);
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
        {
            return false;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
            std::min(std::chrono::duration_cast<std::chrono::microseconds>(deadline - now),
                     std::chrono::duration_cast<std::chrono::microseconds>(c_selectSlice)));
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(socket, &read_set);
        timeval timeout_value{};
        timeout_value.tv_sec = static_cast<long>(remaining.count() / 1'000'000);
        timeout_value.tv_usec = static_cast<long>(remaining.count() % 1'000'000);
        const int selected = select(0, &read_set, nullptr, nullptr, &timeout_value);
        if (selected > 0)
        {
            return true;
        }

        if (selected < 0)
        {
            return false;
        }
    }
}

} // namespace

bool TcpProbe(const std::string& Host, int port, std::chrono::milliseconds Timeout, std::stop_token token)
{
    wil::unique_socket socket;
    return ConnectSocket(Host, port, Timeout, token, socket);
}

bool HttpGetSucceeds(const std::string& Host, int port, const std::string& pathAndQuery, std::stop_token token)
{
    wil::unique_socket socket;
    if (!ConnectSocket(Host, port, c_connectTimeout, token, socket))
    {
        return false;
    }

    const bool ipv6 = Host.contains(':');
    const std::string authority = ipv6 ? "[" + Host + "]" : Host;
    const std::string request = "GET " + pathAndQuery + " HTTP/1.1\r\nHost: " + authority + ":" + std::to_string(port) +
                                "\r\nConnection: close\r\nAccept: */*\r\n\r\n";

    const char* send_data = request.data();
    std::size_t remaining = request.size();
    const auto deadline = std::chrono::steady_clock::now() + c_ioTimeout;
    while (remaining > 0)
    {
        const int written = send(socket.get(), send_data, static_cast<int>(remaining), 0);
        if (written <= 0)
        {
            return false;
        }

        send_data += written;
        remaining -= static_cast<std::size_t>(written);
    }

    std::string response;
    std::array<char, 1024> buffer{};
    while (response.find("\r\n") == std::string::npos && response.size() < 64 * 1024)
    {
        if (!WaitReadable(socket.get(), deadline, token))
        {
            return false;
        }

        const int received = recv(socket.get(), buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received <= 0)
        {
            break;
        }

        response.append(buffer.data(), static_cast<std::size_t>(received));
    }

    const std::size_t end = response.find("\r\n");
    if (end == std::string::npos || response.compare(0, 5, "HTTP/") != 0)
    {
        return false;
    }

    const std::size_t first_space = response.find(' ');
    if (first_space == std::string::npos)
    {
        return false;
    }

    int status = 0;
    for (std::size_t i = first_space + 1;
         i < response.size() && std::isdigit(static_cast<unsigned char>(response[i])) != 0; i++)
    {
        status = status * 10 + (response[i] - '0');
    }

    return status >= 100 && status < 500;
}

} // namespace wslc::internal
