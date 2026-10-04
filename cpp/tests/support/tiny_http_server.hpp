#pragma once

#include <atomic>
#include <thread>

namespace wslc::test
{

/// <summary>Minimal loopback HTTP server used to exercise the HTTP wait strategy.</summary>
class TinyHttpServer
{
public:
    explicit TinyHttpServer(int statusCode = 200);
    ~TinyHttpServer();
    TinyHttpServer(const TinyHttpServer&) = delete;
    TinyHttpServer& operator=(const TinyHttpServer&) = delete;

    int Port() const noexcept { return m_port; }

private:
    void AcceptLoop();

    std::atomic<bool> m_stopped{false};
    unsigned long long m_listener = ~0ull;
    int m_statusCode = 200;
    int m_port = 0;
    std::thread m_thread;
};

} // namespace wslc::test
