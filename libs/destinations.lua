-- destinations.lua -- static destination export for the superwarp GUI bridge.
--
-- Walks every map module's warpdata into one normalized, browsable schema.
-- Pure data transformation: depends only on the maps table, never on
-- windower/packets, so it is unit-testable outside the game.
--
-- Two warpdata shapes exist in the addon (the same split maps.lua keys on):
--   * FLAT   warpdata[zone] has .index            -> the zone IS the destination
--   * NESTED warpdata[zone] is a group of subkeys -> each sub is a destination
--                                                    (entries with .shortcut are
--                                                     human-readable ALIASES)
--
-- Every real destination (an entry with .index or .menu_id) is addressed
-- structurally by (system key, zone, sub) and executed via the addon's
-- `warp_to` command, which calls do_warp directly. That sidesteps the CLI's
-- single-token sub-zone limitation, so multi-word destinations (e.g. waypoints'
-- "Frontier Station") are reachable too -- resolve_warp fuzzy-matches the exact
-- sub key. Pure .shortcut entries are folded in as display aliases, never
-- emitted as separate destinations.
--
-- by Eric Strawser (Seicz@Bahamut) . ITIWH.com

local M = {}

local function as_list(short_name)
    if type(short_name) == 'table' then
        local out = {}
        for i = 1, #short_name do out[i] = short_name[i] end
        return out
    end
    return { short_name }
end

local function sorted_keys(t)
    local ks = {}
    for k in pairs(t) do ks[#ks + 1] = k end
    table.sort(ks, function(a, b) return tostring(a) < tostring(b) end)
    return ks
end

-- Build one destination record, including only the fields that exist.
-- `raw` is the warpdata leaf. Its `zone` field is the numeric zone id (distinct
-- from `zone`, the human zone-name key); the rest are unlock-relevant fields
-- carried forward for Stage 3 (offset/unlocked/expac/cost/index).
local function make_dest(zone, sub, label, raw, aliases)
    local d = { zone = zone, label = label }
    if sub ~= nil then d.sub = sub end
    if aliases and #aliases > 0 then d.aliases = aliases end
    if raw.zone ~= nil then d.zone_id = raw.zone end
    if raw.index ~= nil then d.index = raw.index end
    if raw.offset ~= nil then d.offset = raw.offset end
    if raw.expac ~= nil then d.expac = raw.expac end
    if raw.cost ~= nil then d.cost = raw.cost end
    if raw.unlocked ~= nil then d.unlocked = raw.unlocked end
    return d
end

local function build_system(key, map)
    local aliases = as_list(map.short_name)

    local subcmds = nil
    if map.sub_commands then
        subcmds = sorted_keys(map.sub_commands)
        if #subcmds == 0 then subcmds = nil end
    end

    local dests = {}
    local wd = map.warpdata or {}

    for _, zk in ipairs(sorted_keys(wd)) do
        local zd = wd[zk]
        if type(zd) == 'table' and zd.index ~= nil then
            -- FLAT: the zone itself is the destination.
            dests[#dests + 1] = make_dest(zk, nil, zk, zd)

        elseif type(zd) == 'table' then
            -- NESTED: map shortcut aliases (real subkey -> friendly names).
            local alias_of = {}
            for sk, sv in pairs(zd) do
                if type(sv) == 'table' and sv.shortcut ~= nil then
                    local tgt = tostring(sv.shortcut)
                    alias_of[tgt] = alias_of[tgt] or {}
                    alias_of[tgt][#alias_of[tgt] + 1] = sk
                end
            end
            -- Emit real entries (index or menu_id); skip pure shortcut aliases.
            for _, sk in ipairs(sorted_keys(zd)) do
                local sv = zd[sk]
                if type(sv) == 'table' and sv.shortcut == nil
                   and (sv.index ~= nil or sv.menu_id ~= nil) then
                    local al = alias_of[sk]
                    if al then table.sort(al) end
                    local label = sv.display_name
                        or (al and table.concat(al, ' / '))
                        or sk
                    dests[#dests + 1] = make_dest(zk, sk, label, sv, al)
                end
            end
        end
    end

    return {
        key = key,                 -- maps-table key; the system handle for warp_to
        short = aliases[1],         -- canonical short name (display/CLI)
        aliases = aliases,
        long = map.long_name,
        plural = map.npc_plural,
        subcommands = subcmds,
        count = #dests,
        destinations = dests,
    }
end

-- M.build(maps) -> { t='lists', systems={ ... } }, sorted by system key.
function M.build(maps)
    local systems = {}
    for _, key in ipairs(sorted_keys(maps)) do
        systems[#systems + 1] = build_system(key, maps[key])
    end
    return { t = 'lists', systems = systems }
end

return M
