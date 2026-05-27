// net.cpp - see net.h for the contract.
//
// Worker state machine:
//   Disconnected -> (try connect with 800ms timeout) -> Connected ->
//     loop: select() up to ~50ms; if readable, drain into line buffer and
//     parse complete lines; if outgoing queue non-empty and writable, send.
//   Any I/O failure -> Disconnected, retry after ~3s backoff.
//
// Outgoing: deque<string> guarded by mutex + cv.
// Incoming: parsed ServerMsg list drained by the UI thread.

#include "net.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// Defend against <inaddr.h>'s BSD-compat macros - learned the hard way.
#ifdef s_host
#  undef s_host
#endif
#ifdef s_net
#  undef s_net
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace
{
    std::thread              s_worker;
    std::atomic<bool>        s_running{ false };
    std::atomic<net::Status> s_status{ net::Status::Disconnected };

    std::mutex              s_mtx;
    std::condition_variable s_cv;
    std::deque<std::string> s_outq;
    std::string             s_remote_host = "127.0.0.1";
    uint16_t                s_remote_port = 19519;
    bool                    s_endpoint_changed = false;
    std::string             s_error;

    std::mutex                       s_inmtx;
    std::vector<proto::ServerMsg>    s_inq;
    std::atomic<bool>                s_connected_event{ false };

    const int kConnectTimeoutMs = 800;
    const int kReconnectWaitMs  = 3000;
    const int kSelectWaitMs     = 50;

    void set_error(const std::string& e)
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        s_error = e;
    }

    SOCKET try_connect(const std::string& host, uint16_t port)
    {
        SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) return INVALID_SOCKET;

        // Resolve via getaddrinfo so "localhost" / hostnames work (not just IPs).
        addrinfo hints{};
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        char portbuf[16];
        std::snprintf(portbuf, sizeof(portbuf), "%u", static_cast<unsigned>(port));
        if (::getaddrinfo(host.c_str(), portbuf, &hints, &res) != 0 || !res)
        {
            ::closesocket(sock);
            set_error("dns: cannot resolve " + host);
            return INVALID_SOCKET;
        }

        u_long nb = 1;
        ::ioctlsocket(sock, FIONBIO, &nb);
        int r = ::connect(sock, res->ai_addr, static_cast<int>(res->ai_addrlen));
        ::freeaddrinfo(res);
        if (r == SOCKET_ERROR && ::WSAGetLastError() != WSAEWOULDBLOCK)
        {
            ::closesocket(sock);
            set_error("connect: immediate failure");
            return INVALID_SOCKET;
        }

        fd_set wfds; FD_ZERO(&wfds); FD_SET(sock, &wfds);
        fd_set efds; FD_ZERO(&efds); FD_SET(sock, &efds);
        timeval tv{};
        tv.tv_sec  = kConnectTimeoutMs / 1000;
        tv.tv_usec = (kConnectTimeoutMs % 1000) * 1000;
        r = ::select(0, nullptr, &wfds, &efds, &tv);
        if (r <= 0 || FD_ISSET(sock, &efds))
        {
            ::closesocket(sock);
            set_error("connect: timeout");
            return INVALID_SOCKET;
        }
        // stays non-blocking; we use select() in the main loop.
        return sock;
    }

    bool send_all(SOCKET sock, const char* data, int len)
    {
        int sent = 0;
        while (sent < len)
        {
            int n = ::send(sock, data + sent, len - sent, 0);
            if (n == SOCKET_ERROR)
            {
                if (::WSAGetLastError() == WSAEWOULDBLOCK)
                {
                    // wait briefly until writable
                    fd_set w; FD_ZERO(&w); FD_SET(sock, &w);
                    timeval tv{ 0, 50 * 1000 };
                    int r = ::select(0, nullptr, &w, nullptr, &tv);
                    if (r <= 0) return false;
                    continue;
                }
                return false;
            }
            sent += n;
        }
        return true;
    }

    void push_incoming(proto::ServerMsg&& m)
    {
        std::lock_guard<std::mutex> lk(s_inmtx);
        s_inq.push_back(std::move(m));
    }

    void worker_loop()
    {
        SOCKET sock = INVALID_SOCKET;
        std::string inbuf;

        while (s_running.load())
        {
            // ---- connect phase ----
            if (sock == INVALID_SOCKET)
            {
                std::string host;
                uint16_t    port;
                {
                    std::lock_guard<std::mutex> lk(s_mtx);
                    host = s_remote_host;
                    port = s_remote_port;
                    s_endpoint_changed = false;
                }
                s_status.store(net::Status::Connecting);
                sock = try_connect(host, port);
                if (sock == INVALID_SOCKET)
                {
                    s_status.store(net::Status::Disconnected);
                    std::unique_lock<std::mutex> lk(s_mtx);
                    s_cv.wait_for(lk,
                        std::chrono::milliseconds(kReconnectWaitMs),
                        [] { return !s_running.load() || s_endpoint_changed; });
                    continue;
                }
                inbuf.clear();
                set_error("");
                s_status.store(net::Status::Connected);
                s_connected_event.store(true);
            }

            // Endpoint changed mid-connection? Drop and reconnect.
            {
                std::lock_guard<std::mutex> lk(s_mtx);
                if (s_endpoint_changed)
                {
                    ::closesocket(sock);
                    sock = INVALID_SOCKET;
                    s_status.store(net::Status::Disconnected);
                    continue;
                }
            }

            // ---- run phase: read + write ----
            fd_set rfds; FD_ZERO(&rfds); FD_SET(sock, &rfds);
            timeval tv{ 0, kSelectWaitMs * 1000 };
            int sel = ::select(0, &rfds, nullptr, nullptr, &tv);
            if (sel == SOCKET_ERROR)
            {
                set_error("select error");
                ::closesocket(sock);
                sock = INVALID_SOCKET;
                s_status.store(net::Status::Disconnected);
                continue;
            }

            if (sel > 0 && FD_ISSET(sock, &rfds))
            {
                char buf[4096];
                int n = ::recv(sock, buf, sizeof(buf), 0);
                if (n == 0)
                {
                    set_error("disconnected by peer");
                    ::closesocket(sock);
                    sock = INVALID_SOCKET;
                    s_status.store(net::Status::Disconnected);
                    continue;
                }
                if (n == SOCKET_ERROR)
                {
                    if (::WSAGetLastError() != WSAEWOULDBLOCK)
                    {
                        set_error("recv error");
                        ::closesocket(sock);
                        sock = INVALID_SOCKET;
                        s_status.store(net::Status::Disconnected);
                        continue;
                    }
                }
                else
                {
                    inbuf.append(buf, buf + n);
                    // Pull out complete lines.
                    for (;;)
                    {
                        size_t nl = inbuf.find('\n');
                        if (nl == std::string::npos) break;
                        std::string line = inbuf.substr(0, nl);
                        inbuf.erase(0, nl + 1);
                        if (line.empty()) continue;
                        // strip optional \r
                        if (line.back() == '\r') line.pop_back();
                        if (line.size() > 1 << 18) continue; // 256 KB sanity cap
                        proto::ServerMsg msg;
                        if (proto::parse(line, msg) && msg.type != proto::ServerMsg::Type::Unknown)
                            push_incoming(std::move(msg));
                    }
                }
            }

            // Drain outgoing queue (cheap, no blocking).
            std::deque<std::string> to_send;
            {
                std::lock_guard<std::mutex> lk(s_mtx);
                to_send.swap(s_outq);
            }
            for (auto& line : to_send)
            {
                if (line.empty()) continue;
                if (line.back() != '\n') line.push_back('\n');
                if (!send_all(sock, line.data(), static_cast<int>(line.size())))
                {
                    set_error("send error");
                    ::closesocket(sock);
                    sock = INVALID_SOCKET;
                    s_status.store(net::Status::Disconnected);
                    // unsent rest is lost; UI can reissue on reconnect.
                    break;
                }
            }
        }

        if (sock != INVALID_SOCKET) ::closesocket(sock);
    }
}

// ---- public API ---------------------------------------------------------

void net::Start(const std::string& host, uint16_t port)
{
    if (s_running.load()) return;
    WSADATA wsa{};
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        s_remote_host = host.empty() ? "127.0.0.1" : host;
        s_remote_port = (port == 0) ? 19519 : port;
        s_endpoint_changed = false;
        s_error.clear();
        s_outq.clear();
    }
    s_running.store(true);
    s_status.store(net::Status::Disconnected);
    s_connected_event.store(false);
    s_worker = std::thread(worker_loop);
}

void net::Stop()
{
    if (!s_running.load()) return;
    s_running.store(false);
    s_cv.notify_all();
    if (s_worker.joinable()) s_worker.join();
    s_status.store(net::Status::Disconnected);
    ::WSACleanup();
}

void net::SetEndpoint(const std::string& host, uint16_t port)
{
    std::lock_guard<std::mutex> lk(s_mtx);
    s_remote_host = host.empty() ? "127.0.0.1" : host;
    s_remote_port = (port == 0) ? 19519 : port;
    s_endpoint_changed = true;
    s_cv.notify_all();
}

net::Status net::GetStatus()    { return s_status.load(); }
std::string net::Host()         { std::lock_guard<std::mutex> lk(s_mtx); return s_remote_host; }
uint16_t    net::Port()         { std::lock_guard<std::mutex> lk(s_mtx); return s_remote_port; }
std::string net::GetError()     { std::lock_guard<std::mutex> lk(s_mtx); return s_error; }

const char* net::StatusText()
{
    switch (s_status.load())
    {
        case Status::Connected:  return "connected";
        case Status::Connecting: return "connecting...";
        default:                 return "offline";
    }
}

void net::SendLine(const std::string& line)
{
    std::lock_guard<std::mutex> lk(s_mtx);
    s_outq.push_back(line);
}

std::vector<proto::ServerMsg> net::DrainIncoming()
{
    std::vector<proto::ServerMsg> out;
    std::lock_guard<std::mutex> lk(s_inmtx);
    out.swap(s_inq);
    return out;
}

bool net::ConsumeConnectedEvent()
{
    bool expected = true;
    return s_connected_event.compare_exchange_strong(expected, false);
}
