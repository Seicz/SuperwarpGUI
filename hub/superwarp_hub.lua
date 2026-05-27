_addon.name     = 'superwarp_hub'
_addon.author   = 'Eric Strawser (Seicz@Bahamut) - ITIWH.com'
_addon.version  = '0.1.0'
_addon.commands = {'swhub'}

-- superwarp_hub
-- A minimal loopback TCP relay. Listens on 127.0.0.1 and runs each received
-- line as a Windower console command via windower.send_command. It is
-- deliberately generic: it relays ANY command, so the same hub can front
-- superwarp, fisher, and crucible once the companion grows to cover all three.
--
-- Protocol: newline-delimited UTF-8/ASCII command lines, e.g.
--     sw hp party Norg 2\n
--     sw cancel\n
-- The companion app builds these strings; the hub just dispatches them.
--
-- NOTE: ASCII only in chat output - Unicode does not render in the FFXI log.
-- NOTE: binds loopback only. A localhost listener that runs console commands
--       is a minor local-trust surface; a shared token could be added later.

require('logger')
local socket = require('socket')

local PORT = 51337

local server  = nil
local client  = nil
local inbuf   = ''
local enabled = true
local count   = 0

local function start_server()
    server = socket.tcp()
    server:setoption('reuseaddr', true)
    local ok, err = server:bind('127.0.0.1', PORT)
    if not ok then
        error('superwarp_hub: bind failed on 127.0.0.1:' .. PORT .. ' - ' .. tostring(err))
        server = nil
        return
    end
    server:listen(1)
    server:settimeout(0)
    notice('superwarp_hub: listening on 127.0.0.1:' .. PORT)
end

local function drop_client(reason)
    if client then
        client:close()
        client = nil
        inbuf = ''
        notice('superwarp_hub: companion disconnected (' .. tostring(reason) .. ')')
    end
end

local function dispatch(line)
    line = line:gsub('[\r\n]', ''):gsub('^%s+', ''):gsub('%s+$', '')
    if line == '' then return end
    count = count + 1
    if enabled then
        windower.send_command(line)
    end
    -- ASCII-only arrow
    log('relay -> ' .. line)
end

local function pump()
    if not server then return end

    -- Accept a single companion connection (replace any prior one).
    if not client then
        local c = server:accept()
        if c then
            if client then drop_client('replaced') end
            client = c
            client:settimeout(0)
            inbuf = ''
            notice('superwarp_hub: companion connected')
        end
    end

    if not client then return end

    -- Pull whatever bytes are available this frame.
    for _ = 1, 8 do
        local chunk, err, partial = client:receive(2048)
        local data = chunk or partial
        if data and #data > 0 then
            inbuf = inbuf .. data
        end
        if err == 'closed' then
            drop_client('closed')
            return
        end
        if not chunk then break end -- timeout: nothing more right now
    end

    -- Split complete lines out of the buffer.
    while true do
        local nl = inbuf:find('\n', 1, true)
        if not nl then break end
        local line = inbuf:sub(1, nl - 1)
        inbuf = inbuf:sub(nl + 1)
        dispatch(line)
    end
end

windower.register_event('prerender', pump)

windower.register_event('unload', function()
    drop_client('unload')
    if server then server:close() server = nil end
end)

windower.register_event('addon command', function(cmd, ...)
    cmd = (cmd or 'status'):lower()
    if cmd == 'status' then
        local cs = client and 'connected' or 'no companion'
        log(('status: %s on 127.0.0.1:%d, relay %s, %d command(s) relayed'):format(
            cs, PORT, enabled and 'ON' or 'OFF', count))
    elseif cmd == 'on' then
        enabled = true
        log('relay ENABLED')
    elseif cmd == 'off' then
        enabled = false
        log('relay DISABLED (commands logged but not executed)')
    else
        log('commands: //swhub [status|on|off]')
    end
end)

start_server()
