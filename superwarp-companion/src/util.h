// util.h - small shared helpers.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace util
{
    // Destination key shared by the addon, the catalogue, and the unlock cache.
    // Mirrors the addon: `zone` for flat entries, `zone\x1e{sub}` for nested.
    std::string destkey(const std::string& zone, const std::optional<std::string>& sub);

    int64_t     now_unix();
    std::string time_ago(int64_t updated);
    std::string fmt_thousands(int64_t n);

    // numeric/text sort key for a destination's sub. Numeric subs sort before
    // named ones; numeric values sort by value, named by text.
    // Returns (group, value, text). Group: 0 = none, 1 = numeric, 2 = named.
    struct SubKey
    {
        uint8_t     group = 0;
        uint64_t    num   = 0;
        std::string text;
    };
    SubKey sub_order(const std::optional<std::string>& sub);
}
