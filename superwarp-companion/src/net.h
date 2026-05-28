// net.h - TCP client for superwarp's JSON bridge (default 127.0.0.1:19519).
//
// One worker thread owns the socket and the protocol I/O. The UI thread
// enqueues outgoing JSON command lines (fire-and-forget) and drains parsed
// ServerMsg objects each frame. Auto-reconnects with a ~3s backoff. Endpoint
// is changeable at runtime (Advanced view "apply + reconnect").
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "proto.h"

namespace net
{
    enum class Status { Disconnected, Connecting, Connected };

    void Start(const std::string& host, uint16_t port);
    void Stop();

    // Change the endpoint and drop the current connection (worker reconnects).
    void SetEndpoint(const std::string& host, uint16_t port);

    Status      GetStatus();
    const char* StatusText();
    std::string GetError();       // last connect/read error message (may be empty)
    std::string Host();
    uint16_t    Port();

    // Queue a JSON command line. A trailing '\n' is appended if absent.
    // Discarded silently if not connected.
    void SendLine(const std::string& json_line);

    // Drain all parsed incoming messages received since the last call.
    std::vector<proto::ServerMsg> DrainIncoming();

    // True once per (re)connection event. The app uses this to re-issue
    // get_state / get_lists / get_unlocks.
    bool ConsumeConnectedEvent();
}
