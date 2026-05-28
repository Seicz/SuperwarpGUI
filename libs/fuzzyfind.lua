-- fuzzyfind (lean) -- fuzzy string matcher.
--
-- Two paths:
--   needle IS a subsequence of some candidate -> gap-aware score (fzf-inspired:
--     boundary / camelCase / consecutive bonuses, gap penalties), scored against
--     the RAW candidate so case + separators carry signal. Covers exact, prefix,
--     and abbreviation queries.
--   needle is a subsequence of nothing (typo / extra chars) -> closest candidate
--     by normalized Levenshtein distance. Graceful degradation; never nil.
--
-- fmatch returns: winner (raw string), score (number, higher = better, only
-- comparable within a call), tier ("exact" | "fuzzy" | "approx").
--
-- Trimmed from the hybrid for superwarp: LCS is gone (it had become a rarely
-- used fallback, and Levenshtein is a better typo metric), the confidence floor
-- is gone (rarely fired on real input), cleaning is unified, and the scorer's
-- scratch buffers are dropped since the haystack is tiny. Two algorithms total.
--
-- Lineage: research+assembly by Lili; gap-aware scorer + lean build by Eric
-- Strawser (Seicz@Bahamut) - ITIWH.com, with AI assistance.

local M = {}

local MATCH, GAP_START, GAP_EXT = 16, -3, -1
local BONUS_BOUNDARY, BONUS_CAMEL, BONUS_CONSEC, FIRST_MULT = 8, 7, 8, 2
local WHITE, NONWORD, LOWER, UPPER, DIGIT = 1, 2, 3, 4, 5
local NEG = -1e9

local function class(c)
    if c == ' ' or c == '\t' then return WHITE end
    local b = c:byte()
    if b >= 48 and b <= 57 then return DIGIT end
    if b >= 65 and b <= 90 then return UPPER end
    if b >= 97 and b <= 122 then return LOWER end
    return NONWORD
end
local function is_word(cl) return cl == LOWER or cl == UPPER or cl == DIGIT end
local function bonus(prev, cur)
    if not is_word(cur) then return 0 end
    if prev == WHITE or prev == NONWORD then return BONUS_BOUNDARY end
    if prev == LOWER and cur == UPPER then return BONUS_CAMEL end
    if prev ~= DIGIT and cur == DIGIT then return BONUS_CAMEL end
    return 0
end

-- score(needle, text): needle lowercased alnum-only; text RAW.
-- number, or nil if needle is not a subsequence of text's alnum. O(m*n).
local function score(needle, text)
    local m, n = #needle, #text
    if m == 0 or n == 0 then return nil end

    local b, tl = {}, {}
    local prev = WHITE
    for j = 1, n do
        local c = text:sub(j, j)
        local cl = class(c)
        b[j] = bonus(prev, cl); tl[j] = c:lower(); prev = cl
    end

    local Hprev, Hcur = {}, {}
    local p1 = needle:sub(1, 1)
    for j = 1, n do
        if tl[j] == p1 then
            local lead = (j == 1) and 0 or (GAP_START + GAP_EXT * (j - 2))
            Hprev[j] = MATCH + b[j] * FIRST_MULT + lead
        else
            Hprev[j] = NEG
        end
    end

    for i = 2, m do
        local pi = needle:sub(i, i)
        local gbest = NEG                    -- max over k<=j-2 of Hprev[k]-GAP_EXT*k
        for j = 1, n do
            local k = j - 2
            if k >= 1 then
                local v = Hprev[k]
                if v > NEG then
                    local c = v - GAP_EXT * k
                    if c > gbest then gbest = c end
                end
            end
            if tl[j] == pi then
                local best = NEG
                if j - 1 >= 1 and Hprev[j - 1] > NEG then          -- consecutive
                    local bb = b[j] > BONUS_CONSEC and b[j] or BONUS_CONSEC
                    local a = Hprev[j - 1] + bb
                    if a > best then best = a end
                end
                if gbest > NEG then                                 -- gapped
                    local bg = b[j] + GAP_START + GAP_EXT * (j - 2) + gbest
                    if bg > best then best = bg end
                end
                Hcur[j] = (best > NEG / 2) and (MATCH + best) or NEG
            else
                Hcur[j] = NEG
            end
        end
        for j = 1, n do Hprev[j] = Hcur[j] end
    end

    local best = NEG
    for j = m, n do if Hprev[j] > best then best = Hprev[j] end end
    if best <= NEG / 2 then return nil end
    return best
end

-- Wagner-Fischer Levenshtein (fallback path only)
local function LEV(a, b)
    local Mx = {}
    local row, col = #a + 1, #b + 1
    for i = 1, row do Mx[i] = {}; for j = 1, col do Mx[i][j] = 0 end end
    for i = 1, row do Mx[i][1] = i - 1 end
    for j = 1, col do Mx[1][j] = j - 1 end
    for i = 2, row do
        for j = 2, col do
            local cost = (a:sub(i - 1, i - 1) == b:sub(j - 1, j - 1)) and 0 or 1
            Mx[i][j] = math.min(math.min(Mx[i - 1][j] + 1, Mx[i][j - 1] + 1), Mx[i - 1][j - 1] + cost)
        end
    end
    return Mx[row][col]
end

local function clean(s) return (s:gsub('[^%w]', ''):lower()) end

function fmatch(needle, haystack)
    local nq = clean(needle)
    if nq == '' then return nil, 0, 'none' end

    local best_v, best_s
    for _, v in ipairs(haystack) do
        if clean(v) == nq then return v, 10000, 'exact' end
        local s = score(nq, v)
        if s ~= nil and (best_s == nil or s > best_s or (s == best_s and #v < #best_v)) then
            best_v, best_s = v, s
        end
    end
    if best_v ~= nil then
        return best_v, best_s, 'fuzzy'
    end

    -- subsequence of nothing -> closest by normalized edit distance
    local bw, bsim
    for _, v in ipairs(haystack) do
        local cl = clean(v)
        local sim = 1 - LEV(nq, cl) / math.max(#nq, #cl, 1)
        if bsim == nil or sim > bsim or (sim == bsim and #v < #bw) then
            bw, bsim = v, sim
        end
    end
    return bw, math.floor((bsim or 0) * 100), 'approx'
end

M.match = fmatch
M.score = score
M.lev = LEV
_G.fmatch = fmatch
_G.LEV = LEV
return M
