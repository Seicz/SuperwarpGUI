// fuzzy.cpp - direct C++ port of the Rust fuzzyfind implementation.
// One-for-one with the Rust source (which is one-for-one with the Lua scorer).
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com
//   (scorer lineage: research+assembly by Lili; gap-aware + lean build by ES)

#include "fuzzy.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <vector>

namespace
{
    enum class Cls { White, NonWord, Lower, Upper, Digit };

    Cls fclass(uint8_t b)
    {
        if (b == ' ' || b == '\t')                       return Cls::White;
        if (b >= '0' && b <= '9')                        return Cls::Digit;
        if (b >= 'A' && b <= 'Z')                        return Cls::Upper;
        if (b >= 'a' && b <= 'z')                        return Cls::Lower;
        return Cls::NonWord;
    }
    bool fis_word(Cls c) { return c == Cls::Lower || c == Cls::Upper || c == Cls::Digit; }

    int64_t fbonus(Cls prev, Cls cur)
    {
        if (!fis_word(cur)) return 0;
        if (prev == Cls::White || prev == Cls::NonWord) return fuzzy::F_BON_BOUNDARY;
        if (prev == Cls::Lower && cur == Cls::Upper)    return fuzzy::F_BON_CAMEL;
        if (prev != Cls::Digit && cur == Cls::Digit)    return fuzzy::F_BON_CAMEL;
        return 0;
    }

    inline uint8_t to_lower(uint8_t b)
    {
        return (b >= 'A' && b <= 'Z') ? static_cast<uint8_t>(b + ('a' - 'A')) : b;
    }
}

std::string fuzzy::clean(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char ch : s)
    {
        unsigned char c = static_cast<unsigned char>(ch);
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            out.push_back(static_cast<char>(to_lower(c)));
    }
    return out;
}

std::optional<int64_t> fuzzy::score(const std::string& needle, const std::string& text)
{
    const auto* nb = reinterpret_cast<const uint8_t*>(needle.data());
    const auto* tb = reinterpret_cast<const uint8_t*>(text.data());
    const size_t m = needle.size();
    const size_t n = text.size();
    if (m == 0 || n == 0) return std::nullopt;

    // 1-indexed scratch arrays to mirror the Lua exactly.
    std::vector<int64_t> b(n + 1, 0);
    std::vector<uint8_t> tl(n + 1, 0);
    {
        Cls prev = Cls::White;
        for (size_t j = 1; j <= n; ++j)
        {
            uint8_t c = tb[j - 1];
            Cls cl = fclass(c);
            b[j]  = fbonus(prev, cl);
            tl[j] = to_lower(c);
            prev  = cl;
        }
    }

    std::vector<int64_t> hprev(n + 1, F_NEG);
    std::vector<int64_t> hcur(n + 1, F_NEG);

    const uint8_t p1 = to_lower(nb[0]);
    for (size_t j = 1; j <= n; ++j)
    {
        if (tl[j] == p1)
        {
            int64_t lead = (j == 1) ? 0 : (F_GAP_START + F_GAP_EXT * (static_cast<int64_t>(j) - 2));
            hprev[j] = F_MATCH + b[j] * F_FIRST_MULT + lead;
        }
        else
        {
            hprev[j] = F_NEG;
        }
    }

    for (size_t i = 2; i <= m; ++i)
    {
        const uint8_t pi = to_lower(nb[i - 1]);
        int64_t gbest = F_NEG; // max over k<=j-2 of Hprev[k] - GAP_EXT*k
        for (size_t j = 1; j <= n; ++j)
        {
            int64_t k = static_cast<int64_t>(j) - 2;
            if (k >= 1)
            {
                int64_t v = hprev[static_cast<size_t>(k)];
                if (v > F_NEG)
                {
                    int64_t c = v - F_GAP_EXT * k;
                    if (c > gbest) gbest = c;
                }
            }
            if (tl[j] == pi)
            {
                int64_t best = F_NEG;
                if (j >= 2 && hprev[j - 1] > F_NEG)
                {
                    int64_t bb = (b[j] > F_BON_CONSEC) ? b[j] : F_BON_CONSEC;
                    int64_t a  = hprev[j - 1] + bb;
                    if (a > best) best = a;
                }
                if (gbest > F_NEG)
                {
                    int64_t bg = b[j] + F_GAP_START + F_GAP_EXT * (static_cast<int64_t>(j) - 2) + gbest;
                    if (bg > best) best = bg;
                }
                hcur[j] = (best > F_NEG / 2) ? (F_MATCH + best) : F_NEG;
            }
            else
            {
                hcur[j] = F_NEG;
            }
        }
        std::copy(hcur.begin(), hcur.begin() + (n + 1), hprev.begin());
    }

    int64_t best = F_NEG;
    for (size_t j = m; j <= n; ++j)
        if (hprev[j] > best) best = hprev[j];

    if (best <= F_NEG / 2) return std::nullopt;
    return best;
}

size_t fuzzy::levenshtein(const std::string& a, const std::string& b)
{
    const size_t col = b.size() + 1;
    std::vector<size_t> prev(col), cur(col);
    for (size_t j = 0; j < col; ++j) prev[j] = j;
    for (size_t i = 1; i <= a.size(); ++i)
    {
        cur[0] = i;
        for (size_t j = 1; j < col; ++j)
        {
            size_t cost = (a[i - 1] == b[j - 1]) ? 0u : 1u;
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
        }
        std::swap(prev, cur);
    }
    return prev[col - 1];
}

namespace
{
    // The raw searchable fields of a destination: label, zone, and aliases.
    std::vector<const std::string*> dest_fields(const proto::Dest& d)
    {
        std::vector<const std::string*> v;
        v.reserve(2 + d.aliases.size());
        v.push_back(&d.label);
        v.push_back(&d.zone);
        for (const auto& a : d.aliases) v.push_back(&a);
        return v;
    }
}

std::optional<int64_t> fuzzy::dest_fuzzy(const proto::Dest& d, const std::string& needle_clean)
{
    std::optional<int64_t> best;
    for (const auto* raw : dest_fields(d))
    {
        std::optional<int64_t> v;
        if (clean(*raw) == needle_clean)
            v = FUZZY_EXACT;
        else
            v = score(needle_clean, *raw);
        if (v)
        {
            if (!best || *v > *best) best = v;
        }
    }
    return best;
}

int64_t fuzzy::dest_approx(const proto::Dest& d, const std::string& needle_clean)
{
    int64_t best = 0;
    const size_t nlen = needle_clean.size();
    for (const auto* raw : dest_fields(d))
    {
        std::string cl = clean(*raw);
        size_t denom = std::max<size_t>({ nlen, cl.size(), 1 });
        size_t lev = levenshtein(needle_clean, cl);
        int64_t sim = 10000 - static_cast<int64_t>((lev * 10000) / denom);
        if (sim > best) best = sim;
    }
    return best;
}
