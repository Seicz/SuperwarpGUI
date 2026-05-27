// net.h - minimal localhost TCP client to the superwarp_hub Windower addon.
//
// Design: one background worker thread owns the socket. The UI thread only
// enqueues command lines (lock-free from the caller's perspective via a mutex-
// guarded queue) and reads an atomic status. No asio, no boost - just Winsock.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com

#pragma once
#include <string>

namespace net
{
    enum class Status { Disconnected, Connecting, Connected };

    // Spawn the worker thread and begin connecting to host:port (loopback only).
    void Start(const char* host, unsigned short port);

    // Signal the worker to stop, close the socket, and join. Safe to call once.
    void Stop();

    // Queue a single command line for delivery. A trailing '\n' is added if
    // absent. Thread-safe; returns immediately (fire-and-forget).
    void SendLine(const std::string& line);

    // Current connection state (atomic snapshot).
    Status GetStatus();

    // Human-readable status ("Connected" / "Connecting..." / "Disconnected").
    const char* StatusText();

    // Connection target (for display).
    const char* Host();
    unsigned short Port();
}
