// util.cpp - small shared helpers.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com

#include "util.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>

std::string util::destkey(const std::string& zone, const std::optional<std::string>& sub)
{
    if (sub) { std::string r = zone; r.push_back('\x1e'); r += *sub; return r; }
    return zone;
}

int64_t util::now_unix()
{
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string util::time_ago(int64_t updated)
{
    int64_t secs = now_unix() - updated;
    if (secs < 0) secs = 0;
    char buf[32];
    if (secs < 10)        std::snprintf(buf, sizeof(buf), "just now");
    else if (secs < 60)   std::snprintf(buf, sizeof(buf), "%llds ago",  (long long)secs);
    else if (secs < 3600) std::snprintf(buf, sizeof(buf), "%lldm ago",  (long long)(secs / 60));
    else if (secs < 86400)std::snprintf(buf, sizeof(buf), "%lldh ago",  (long long)(secs / 3600));
    else                  std::snprintf(buf, sizeof(buf), "%lldd ago",  (long long)(secs / 86400));
    return std::string(buf);
}

std::string util::fmt_thousands(int64_t n)
{
    bool neg = n < 0;
    unsigned long long mag = neg ? (unsigned long long)(-(n + 1)) + 1ull : (unsigned long long)n;
    char raw[32];
    int len = std::snprintf(raw, sizeof(raw), "%llu", mag);
    std::string out;
    out.reserve((size_t)len + len / 3 + (neg ? 1 : 0));
    int first_group = len % 3;
    if (first_group == 0) first_group = 3;
    int i = 0;
    out.append(raw + i, first_group); i += first_group;
    while (i < len)
    {
        out.push_back(',');
        out.append(raw + i, 3);
        i += 3;
    }
    if (neg) out.insert(out.begin(), '-');
    return out;
}

util::SubKey util::sub_order(const std::optional<std::string>& sub)
{
    SubKey k;
    if (!sub) { k.group = 0; return k; }
    const std::string& s = *sub;
    // Pure-digit -> numeric group.
    if (!s.empty() && std::all_of(s.begin(), s.end(),
                                  [](unsigned char c){ return c >= '0' && c <= '9'; }))
    {
        k.group = 1;
        k.num   = std::strtoull(s.c_str(), nullptr, 10);
    }
    else
    {
        k.group = 2;
        k.text.resize(s.size());
        std::transform(s.begin(), s.end(), k.text.begin(),
                       [](unsigned char c){ return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c; });
    }
    return k;
}
