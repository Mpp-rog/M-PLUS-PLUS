-- ============================================================
-- mpp.lua — Lua bridge module for M++
--
-- Usage in a Lua file run via plum or lua call:
--
--   local mpp = require("mpp")
--
--   local score = mpp.get("score")   -- read M++ variable
--   mpp.set("score", score + 10)     -- write M++ variable
--
-- The bridge file path is passed as the first argument
-- when M++ launches Lua, or set via mpp._init(path).
-- ============================================================

local mpp = {}

local bridge_path = nil
local vars = {}


-- ============================================================
-- Internal: escape / unescape (matches runtime.cpp)
-- ============================================================

local function escape(s)
    s = tostring(s)
    s = s:gsub("\\", "\\\\")
    s = s:gsub("\n", "\\n")
    return s
end

local function unescape(s)
    local out = ""
    local i = 1
    while i <= #s do
        if s:sub(i,i) == "\\" and i < #s then
            local nc = s:sub(i+1,i+1)
            if nc == "n" then
                out = out .. "\n"
                i = i + 2
            elseif nc == "\\" then
                out = out .. "\\"
                i = i + 2
            else
                out = out .. s:sub(i,i)
                i = i + 1
            end
        else
            out = out .. s:sub(i,i)
            i = i + 1
        end
    end
    return out
end


-- ============================================================
-- Internal: read bridge file into vars table
-- ============================================================

local function readBridge()
    if not bridge_path then return end
    vars = {}
    local f = io.open(bridge_path, "r")
    if not f then return end
    for line in f:lines() do
        local eq = line:find("=")
        if eq then
            local k = unescape(line:sub(1, eq-1))
            local v = unescape(line:sub(eq+1))
            vars[k] = v
        end
    end
    f:close()
end


-- ============================================================
-- Internal: write vars table back to bridge file
-- ============================================================

local function writeBridge()
    if not bridge_path then return end
    local f = io.open(bridge_path, "w")
    if not f then return end
    for k, v in pairs(vars) do
        f:write(escape(k) .. "=" .. escape(v) .. "\n")
    end
    f:close()
end


-- ============================================================
-- mpp._init(path) — called automatically by M++ runtime
-- ============================================================

function mpp._init(path)
    bridge_path = path
    readBridge()
end


-- ============================================================
-- mpp.get(name) — read an M++ variable
-- ============================================================

function mpp.get(name)
    return vars[name] or ""
end


-- ============================================================
-- mpp.set(name, value) — write an M++ variable
-- (writes through to the bridge file immediately)
-- ============================================================

function mpp.set(name, value)
    vars[name] = tostring(value)
    writeBridge()
end


-- ============================================================
-- Auto-init from command line arg if present
-- M++ passes the bridge path as the first argument
-- ============================================================

if arg and arg[1] then
    mpp._init(arg[1])
end


return mpp
