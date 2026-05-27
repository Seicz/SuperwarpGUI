// net.cpp - see net.h for the contract.
//
// The worker thread runs a tiny state machine:
//   Disconnected -> (try connect, timeout-guarded) -> Connected -> drain queue
//   any send/connect failure -> back to Disconnected, retry after a short wait.
//
// Connect uses a non-blocking socket + select() so a missing hub (FFXI not
// running, addon not loaded) never hangs the worker. Commands queued while
// disconnected are held until a connection is established.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com

#include "net.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// <inaddr.h> defines BSD-compat macros that hijack innocent identifiers
// (e.g. `s_host` expands to `S_un.S_un_b.s_b2`). Kill them here so any future
// variable named s_host / s_net / s_imp / s_impno / s_lh compiles cleanly.
#ifdef s_host
#  undef s_host
#endif
#ifdef s_net
#  undef s_net
#endif
#ifdef s_imp
#  undef s_imp
#endif
#ifdef s_impno
#  undef s_impno
#endif
#ifdef s_lh
#  undef s_lh
#endif

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <deque>
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace
{
    std::thread              s_worker;
    std::mutex               s_mtx;
    std::condition_variable  s_cv;
    std::deque<std::string>  s_queue;
    std::atomic<bool>        s_running{ false };
    std::atomic<net::Status> s_status{ net::Status::Disconnected };

    std::string   s_remote_host = "127.0.0.1";
    unsigned short s_port = 0;

    const int  kConnectTimeoutMs = 800;   // per attempt
    const int  kReconnectWaitMs  = 1500;  // between failed attempts

    // Attempt a single non-blocking connect. Returns a connected blocking
    // socket on success, or INVALID_SOCKET on failure/timeout.
    SOCKET TryConnect()
    {
        SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET)
            return INVALID_SOCKET;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(s_port);
        if (::inet_pton(AF_INET, s_remote_host.c_str(), &addr.sin_addr) != 1)
        {
            ::closesocket(sock);
            return INVALID_SOCKET;
        }

        u_long nb = 1;
        ::ioctlsocket(sock, FIONBIO, &nb);

        int r = ::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (r == SOCKET_ERROR && ::WSAGetLastError() != WSAEWOULDBLOCK)
        {
            ::closesocket(sock);
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
            return INVALID_SOCKET;
        }

        // Back to blocking for straightforward sends.
        nb = 0;
        ::ioctlsocket(sock, FIONBIO, &nb);
        return sock;
    }

    // Send the whole buffer; returns false on any socket error.
    bool SendAll(SOCKET sock, const char* data, int len)
    {
        int sent = 0;
        while (sent < len)
        {
            int n = ::send(sock, data + sent, len - sent, 0);
            if (n == SOCKET_ERROR)
                return false;
            sent += n;
        }
        return true;
    }

    void WorkerLoop()
    {
        SOCKET sock = INVALID_SOCKET;

        while (s_running.load())
        {
            if (sock == INVALID_SOCKET)
            {
                s_status.store(net::Status::Connecting);
                sock = TryConnect();
                if (sock == INVALID_SOCKET)
                {
                    s_status.store(net::Status::Disconnected);
                    std::unique_lock<std::mutex> lk(s_mtx);
                    s_cv.wait_for(lk, std::chrono::milliseconds(kReconnectWaitMs),
                                  [] { return !s_running.load(); });
                    continue;
                }
                s_status.store(net::Status::Connected);
            }

            // Wait for work (or shutdown).
            std::string line;
            {
                std::unique_lock<std::mutex> lk(s_mtx);
                s_cv.wait(lk, [] { return !s_queue.empty() || !s_running.load(); });
                if (!s_running.load())
                    break;
                line = std::move(s_queue.front());
                s_queue.pop_front();
            }

            if (!SendAll(sock, line.c_str(), static_cast<int>(line.size())))
            {
                // Lost the connection: requeue the line, drop the socket, retry.
                ::closesocket(sock);
                sock = INVALID_SOCKET;
                s_status.store(net::Status::Disconnected);
                std::lock_guard<std::mutex> lk(s_mtx);
                s_queue.push_front(std::move(line));
            }
        }

        if (sock != INVALID_SOCKET)
            ::closesocket(sock);
    }
}

void net::Start(const char* host, unsigned short port)
{
    if (s_running.load())
        return;

    WSADATA wsa{};
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return;

    s_remote_host = host ? host : "127.0.0.1";
    s_port = port;
    s_running.store(true);
    s_status.store(net::Status::Disconnected);
    s_worker = std::thread(WorkerLoop);
}

void net::Stop()
{
    if (!s_running.load())
        return;

    s_running.store(false);
    s_cv.notify_all();
    if (s_worker.joinable())
        s_worker.join();

    s_status.store(net::Status::Disconnected);
    ::WSACleanup();
}

void net::SendLine(const std::string& line)
{
    std::string framed = line;
    if (framed.empty() || framed.back() != '\n')
        framed.push_back('\n');

    {
        std::lock_guard<std::mutex> lk(s_mtx);
        s_queue.push_back(std::move(framed));
    }
    s_cv.notify_all();
}

net::Status net::GetStatus()       { return s_status.load(); }
const char* net::Host()            { return s_remote_host.c_str(); }
unsigned short net::Port()         { return s_port; }

const char* net::StatusText()
{
    switch (s_status.load())
    {
        case net::Status::Connected:  return "Connected";
        case net::Status::Connecting: return "Connecting...";
        default:                      return "Disconnected";
    }
}
