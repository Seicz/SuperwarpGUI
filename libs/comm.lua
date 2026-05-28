-- comm.lua -- TCP/JSON transport bridge for the superwarp external GUI.
--
-- Self-contained: knows nothing about warps. The host addon supplies an
-- on_command callback (invoked with each decoded inbound table) and an
-- on_connect callback (invoked with the freshly accepted client so the host
-- can push an initial snapshot). Newline-delimited JSON, both directions.
--
-- Mirrors the proven Fisher bridge: non-blocking LuaSocket server polled once
-- per frame from 'prerender', accept cap to prevent flooding, oversize-line
-- guards, and an early-out broadcast when no client is connected.
--
-- by Eric Strawser (Seicz@Bahamut) . ITIWH.com
-- JSON encoder/decoder ported from Fisher v7.x (same author).

local socket = require('socket')

local comm = {}

-- =========================================================================
-- Minimal JSON (no external dependencies)
--   encode: full value support, emits [] for empty tables (the Rust side
--           silently fails to parse {} where it expects an array).
--   decode: flat top-level object only -- string / bool / null / number
--           values. Inbound commands are intentionally kept flat (the warp
--           command ships its tokens as a single space-joined "line" string),
--           so nested decoding is never required.
-- =========================================================================

local json = {}

local json_escape_map = {
    ['\\'] = '\\\\', ['"'] = '\\"',
    ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t',
}

local function json_escape(s)
    return s:gsub('[\\"\n\r\t]', json_escape_map)
end

function json.encode(val)
    local t = type(val)
    if val == nil or t == 'nil' then return 'null' end
    if t == 'boolean' then return val and 'true' or 'false' end
    if t == 'number' then
        if val ~= val or val == math.huge or val == -math.huge then return 'null' end
        return tostring(val)
    end
    if t == 'string' then
        return '"' .. json_escape(val) .. '"'
    end
    if t == 'table' then
        if next(val) == nil then return '[]' end
        local n = #val
        if n > 0 and next(val, n) == nil then
            local parts = {}
            for i = 1, n do parts[i] = json.encode(val[i]) end
            return '[' .. table.concat(parts, ',') .. ']'
        end
        local parts = {}
        local count = 0
        for k, v in pairs(val) do
            count = count + 1
            parts[count] = '"' .. json_escape(tostring(k)) .. '":' .. json.encode(v)
        end
        return '{' .. table.concat(parts, ',') .. '}'
    end
    return 'null'
end

function json.decode(str)
    if not str or str == '' then return nil end
    str = str:match('^%s*(.-)%s*$')
    if str:sub(1, 1) ~= '{' then return nil end

    local obj = {}
    local pos = 2
    while pos <= #str do
        local ws = str:match('^[%s,]+', pos)
        if ws then pos = pos + #ws end
        if str:sub(pos, pos) == '}' then break end

        local _, ke, key = str:find('^"([^"]*)"', pos)
        if not key then break end
        pos = ke + 1

        local colon = str:match('^%s*:%s*', pos)
        if colon then pos = pos + #colon end

        local ch = str:sub(pos, pos)
        if ch == '"' then
            local _, ve, val = str:find('^"([^"]*)"', pos)
            if not val then break end
            obj[key] = val:gsub('\\n', '\n'):gsub('\\t', '\t'):gsub('\\"', '"'):gsub('\\\\', '\\')
            pos = ve + 1
        elseif ch == 't' then
            obj[key] = true; pos = pos + 4
        elseif ch == 'f' then
            obj[key] = false; pos = pos + 5
        elseif ch == 'n' then
            obj[key] = nil; pos = pos + 4
        else
            local num_str = str:match('^%-?%d+%.?%d*', pos)
            if num_str then
                obj[key] = tonumber(num_str)
                pos = pos + #num_str
            else
                break
            end
        end
    end
    return obj
end

comm.json = json

-- =========================================================================
-- Server state
-- =========================================================================

local DEFAULT_PORT = 19519
local MAX_CLIENTS = 4
local MAX_LINE = 4096

local server = nil
local clients = {}
local on_command = nil      -- function(decoded_table)
local on_connect = nil      -- function(client)  -- host pushes initial snapshot

function comm.client_count()
    return #clients
end

-- pcall-encode and send a single table to one client. Returns false on failure.
function comm.send(client, msg)
    local ok, encoded = pcall(json.encode, msg)
    if not ok then return false end
    local _, err = client:send(encoded .. '\n')
    return err == nil
end

-- Encode once, push to every client. Early-out when nobody is listening so
-- the host pays zero encoding cost for CLI-only sessions.
function comm.broadcast(msg)
    if #clients == 0 then return end
    local ok, encoded = pcall(json.encode, msg)
    if not ok then return end
    local payload = encoded .. '\n'
    for i = #clients, 1, -1 do
        local _, err = clients[i]:send(payload)
        if err == 'closed' then
            clients[i]:close()
            table.remove(clients, i)
        end
    end
end

-- opts = { port=, on_command=function(tbl), on_connect=function(client), banner=bool }
function comm.init(opts)
    opts = opts or {}
    on_command = opts.on_command
    on_connect = opts.on_connect
    local port = opts.port or DEFAULT_PORT

    local srv, err = socket.bind('127.0.0.1', port)
    if not srv then
        windower.add_to_chat(167, string.format('[superwarp] TCP bind failed on %d: %s', port, tostring(err)))
        return false
    end
    srv:settimeout(0)
    server = srv
    if opts.banner ~= false then
        windower.add_to_chat(207, string.format('[superwarp] GUI server listening on 127.0.0.1:%d', port))
    end
    return true
end

-- Per-frame, non-blocking. Safe to call before init() (no-ops until bound).
function comm.poll()
    if not server then return end

    -- Accept new connections (capped to prevent flooding).
    if #clients < MAX_CLIENTS then
        local client = server:accept()
        if client then
            client:settimeout(0)
            clients[#clients + 1] = client
            if on_connect then
                local ok = pcall(on_connect, client)
                if not ok then
                    -- a failed snapshot push should not kill the connection
                end
            end
        end
    end

    -- Drain one line per client per frame.
    for i = #clients, 1, -1 do
        local line, err, partial = clients[i]:receive('*l')
        if partial and #partial > MAX_LINE then
            clients[i]:close()
            table.remove(clients, i)
        elseif line then
            if #line <= MAX_LINE then
                local ok, cmd = pcall(json.decode, line)
                if ok and cmd and on_command then
                    pcall(on_command, cmd)
                end
            end
        elseif err == 'closed' then
            clients[i]:close()
            table.remove(clients, i)
        end
    end
end

function comm.shutdown()
    for i = #clients, 1, -1 do
        pcall(function() clients[i]:close() end)
        clients[i] = nil
    end
    if server then
        pcall(function() server:close() end)
        server = nil
    end
end

return comm
