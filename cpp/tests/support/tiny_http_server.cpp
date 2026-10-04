#include "support/tiny_http_server.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <array>
#include <cstring>
#include <mutex>
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace wslc::test
{

namespace
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

} // namespace

TinyHttpServer::TinyHttpServer(int statusCode) : m_statusCode(statusCode)
{
    EnsureWinsock();

    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET)
    {
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(listener, 8) != 0)
    {
        closesocket(listener);
        return;
    }

    int length = sizeof(address);
    getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
    m_port = ntohs(address.sin_port);
    m_listener = static_cast<unsigned long long>(listener);
    m_thread = std::thread([this] { AcceptLoop(); });
}

TinyHttpServer::~TinyHttpServer()
{
    m_stopped = true;
    const SOCKET listener = static_cast<SOCKET>(m_listener);
    if (listener != INVALID_SOCKET)
    {
        closesocket(listener);
    }

    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

void TinyHttpServer::AcceptLoop()
{
    const SOCKET listener = static_cast<SOCKET>(m_listener);
    while (!m_stopped)
    {
        const SOCKET client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET)
        {
            return;
        }

        std::array<char, 4096> buffer{};
        recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
        const std::string body = "ok";
        const std::string response = "HTTP/1.1 " + std::to_string(m_statusCode) +
                                     " Status\r\nContent-Length: " + std::to_string(body.size()) +
                                     "\r\nConnection: close\r\n\r\n" + body;
        send(client, response.data(), static_cast<int>(response.size()), 0);
        closesocket(client);
    }
}

} // namespace wslc::test
