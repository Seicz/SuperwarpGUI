-- unlockcache.lua -- per-system cache of last-seen destination unlock state.
--
-- Fed by unlocks.decode() results (passive captures during warps, or active
-- scans). Merges by destination so zone-gated systems (escha/abyssea/portals),
-- which only ever reveal one region per menu, accumulate full coverage across
-- visits, while non-gated systems are refreshed wholesale each time.
--
-- Pure and testable: no windower/file dependencies. The host injects I/O --
-- serialize() returns a Lua-source string and restore() takes the table that
-- loading it produces, so persistence round-trips through a plain loadfile.
--
-- by Eric Strawser (Seicz@Bahamut) . ITIWH.com

local M = {}

local SEP = '\30'  -- record separator; never appears in zone/sub names

local store = {}   -- store[system] = { entries = { [destkey] = bool }, updated = ts, partial = bool }

local function destkey(zone, sub)
    if sub == nil then return zone end
    return zone .. SEP .. sub
end

local function split_key(key)
    local i = key:find(SEP, 1, true)
    if not i then return key, nil end
    return key:sub(1, i - 1), key:sub(i + 1)
end

-- Merge a decode() result into the cache. Overwrites the covered destinations,
-- leaving other regions of a gated system intact.
function M.merge(decoded, now)
    if not decoded or not decoded.system or decoded.out_of_range then return false end
    local sys = store[decoded.system]
    if not sys then
        sys = { entries = {}, updated = 0, partial = false }
        store[decoded.system] = sys
    end
    for _, e in ipairs(decoded.entries) do
        sys.entries[destkey(e.zone, e.sub)] = e.unlocked and true or false
    end
    sys.updated = now or sys.updated
    -- partial reflects whether this system's data comes from gated (region-by-
    -- region) reads; non-gated menus reveal the whole network in one shot.
    sys.partial = decoded.partial and true or false
    -- currency is last-seen balance; keep the prior value if this capture didn't
    -- surface one (some menus of a system may not carry it).
    if decoded.currency then sys.currency = decoded.currency end
    return true
end

-- Snapshot one system as a broadcast-ready table, or nil if unknown.
function M.snapshot_system(system)
    local sys = store[system]
    if not sys then return nil end
    local dests = {}
    for key, unlocked in pairs(sys.entries) do
        local zone, sub = split_key(key)
        local d = { zone = zone, unlocked = unlocked }
        if sub ~= nil then d.sub = sub end
        dests[#dests + 1] = d
    end
    return { system = system, updated = sys.updated, partial = sys.partial, currency = sys.currency, destinations = dests }
end

-- Snapshot every cached system, sorted by name.
function M.snapshot_all()
    local keys = {}
    for k in pairs(store) do keys[#keys + 1] = k end
    table.sort(keys)
    local out = {}
    for _, k in ipairs(keys) do out[#out + 1] = M.snapshot_system(k) end
    return out
end

-- Number of cached unlock records for a system (for logging).
function M.count(system)
    local sys = store[system]
    if not sys then return 0 end
    local n = 0
    for _ in pairs(sys.entries) do n = n + 1 end
    return n
end

-- Serialize the whole store to a Lua-source string: `return { ... }`.
local function quote(s) return string.format('%q', s) end
function M.serialize()
    local lines = { 'return {' }
    local syskeys = {}
    for k in pairs(store) do syskeys[#syskeys + 1] = k end
    table.sort(syskeys)
    for _, sk in ipairs(syskeys) do
        local sys = store[sk]
        lines[#lines + 1] = '  [' .. quote(sk) .. '] = {'
        lines[#lines + 1] = '    updated = ' .. tostring(math.floor(sys.updated or 0)) .. ','
        lines[#lines + 1] = '    partial = ' .. tostring(sys.partial and true or false) .. ','
        if sys.currency then
            lines[#lines + 1] = '    currency = { name = ' .. quote(sys.currency.name)
                .. ', value = ' .. tostring(math.floor(sys.currency.value or 0)) .. ' },'
        end
        lines[#lines + 1] = '    entries = {'
        local ekeys = {}
        for k in pairs(sys.entries) do ekeys[#ekeys + 1] = k end
        table.sort(ekeys)
        for _, ek in ipairs(ekeys) do
            lines[#lines + 1] = '      [' .. quote(ek) .. '] = ' .. tostring(sys.entries[ek]) .. ','
        end
        lines[#lines + 1] = '    },'
        lines[#lines + 1] = '  },'
    end
    lines[#lines + 1] = '}'
    return table.concat(lines, '\n')
end

-- Restore the store from a table produced by loading serialize()'s output.
function M.restore(tbl)
    store = {}
    if type(tbl) ~= 'table' then return end
    for sk, sys in pairs(tbl) do
        if type(sys) == 'table' and type(sys.entries) == 'table' then
            local clean = {}
            for ek, v in pairs(sys.entries) do
                if type(ek) == 'string' then clean[ek] = v and true or false end
            end
            store[sk] = { entries = clean, updated = tonumber(sys.updated) or 0, partial = sys.partial and true or false }
            if type(sys.currency) == 'table' and type(sys.currency.name) == 'string'
               and tonumber(sys.currency.value) then
                store[sk].currency = { name = sys.currency.name, value = math.floor(tonumber(sys.currency.value)) }
            end
        end
    end
end

-- Test/maintenance helper.
function M.reset() store = {} end

return M
