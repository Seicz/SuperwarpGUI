-- unlocks.lua -- decode per-destination unlock state from an open warp menu.
--
-- When a warp/scan/missing menu (incoming 0x034/0x032) is caught, its "Menu
-- Parameters" buffer encodes which destinations are unlocked. Each map module
-- already decodes that buffer in its missing() function -- and the decode varies
-- per system (has_bit on offset, on index, inverted invoffset, or voidwatch's
-- 4-byte bitmask compare). Rather than re-implement four decoders and risk drift,
-- this module DELEGATES all bit decoding to map.missing() (authoritative,
-- retail-verified) and only adds the one thing missing() doesn't expose: scope --
-- what a single menu reveals.
--
--   'global' -- the whole system's unlock state (a survival guide shows all)
--   'zone'   -- only the current zone's group (abyssea/escha confluxes); these
--               accumulate region-by-region, flagged partial=true downstream.
--
-- Systems with no scheme expose no capturable unlock state (spd has none; sortie/
-- odyssey/limbus have no missing()).
--
-- decode() returns a table consumed by unlockcache.merge():
--   { system, out_of_range, partial, entries = { {zone, sub?, unlocked}, ... } }
-- Identifiers fed to missing()'s locked-set match its own string convention:
-- flat "<zone>", nested "<zone>-<sub>".
--
-- by Eric Strawser (Seicz@Bahamut) . ITIWH.com

local M = {}

local SCHEME = {
    survivalguides = 'global',
    homepoints     = 'global',
    waypoints      = 'global',
    unity          = 'global',
    voidwatch      = 'global',
    protowaypoints = 'global',
    portals        = 'global',   -- only readable at Whitegate; missing() guards that
    abyssea        = 'zone',
    escha          = 'zone',
}

-- Currency the menu reveals, surfaced so the GUI can show affordability. Format
-- and position are taken verbatim from each module's build_warp_packets, which
-- reads the same menu to gate its own warps -- so these are the addon's own
-- retail-verified offsets, not new guesses. Only systems that surface a balance
-- appear here; the gil systems (homepoints/survivalguides) are included for
-- completeness, but the non-gil currencies (cruor/silt/accolades) are where this
-- actually matters since those can run dry. Currency is only read when decode()
-- succeeds, i.e. at the very menu these offsets describe -- abyssea's conflux
-- menu (cruor at I,29), never its entry NPC (i,5), since missing() only resolves
-- inside an abyssea zone.
local CURRENCY = {
    homepoints     = { name = 'gil',       fmt = 'i', pos = 21 },
    survivalguides = { name = 'gil',       fmt = 'I', pos = 9  },
    escha          = { name = 'silt',      fmt = 'i', pos = 21 },
    abyssea        = { name = 'cruor',     fmt = 'I', pos = 29 },
    voidwatch      = { name = 'cruor',     fmt = 'i', pos = 17 },
    unity          = { name = 'accolades', fmt = 'i', pos = 9  },
}

function M.has_scheme(map_name) return SCHEME[map_name] ~= nil end

-- Read the system's currency balance from a caught menu, or nil if the system
-- surfaces none / the value can't be read. Guarded: a negative or absurd result
-- means we misread the buffer, so we discard it rather than cache garbage.
local function read_currency(map_name, p)
    local cur = CURRENCY[map_name]
    if not cur or not p or not p['Menu Parameters'] then return nil end
    local ok, val = pcall(function() return p['Menu Parameters']:unpack(cur.fmt, cur.pos) end)
    if not ok or type(val) ~= 'number' or val < 0 or val >= 2147483647 then return nil end
    return { name = cur.name, value = math.floor(val) }
end

function M.decode(maps, map_name, zone_id, p)
    local scope = SCHEME[map_name]
    local map = maps[map_name]
    if not scope or not map or not map.missing then return nil end

    local partial = (scope == 'zone')
    local locked_list, err = map.missing(map.warpdata, zone_id, p)
    if err or locked_list == nil then
        -- e.g. portals away from Whitegate, or a zone-gated system in a zone it
        -- can't read: nothing to merge, but flag so the cache leaves it alone.
        return { system = map_name, out_of_range = true, partial = partial, entries = {} }
    end

    local locked = {}
    for _, name in ipairs(locked_list) do locked[name] = true end

    local entries = {}
    for zone_name, zd in pairs(map.warpdata) do
        if type(zd) == 'table' and zd.index ~= nil then
            -- FLAT: the zone itself is the destination.
            if not zd.shortcut and ((scope == 'global') or (zd.zone == zone_id)) then
                entries[#entries + 1] = { zone = zone_name, unlocked = not locked[zone_name] }
            end

        elseif type(zd) == 'table' then
            -- NESTED group. For zone scope, find the group's numeric zone id from
            -- a sub-entry that carries one, then take every real entry in the
            -- group (incl. ones without their own zone, e.g. "Cavernous Maw").
            local group_zone = nil
            for _, sv in pairs(zd) do
                if type(sv) == 'table' and sv.zone ~= nil then group_zone = sv.zone; break end
            end
            if (scope == 'global') or (group_zone == zone_id) then
                for sk, sv in pairs(zd) do
                    if type(sv) == 'table' and not sv.shortcut
                       and (sv.index ~= nil or sv.menu_id ~= nil) then
                        local id = zone_name .. '-' .. sk
                        entries[#entries + 1] = { zone = zone_name, sub = sk, unlocked = not locked[id] }
                    end
                end
            end
        end
    end

    return { system = map_name, out_of_range = false, partial = partial, entries = entries,
             currency = read_currency(map_name, p) }
end

return M
