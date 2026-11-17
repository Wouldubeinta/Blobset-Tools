math.pow = function(a,b) return a^b end
unpack = table.unpack
loadstring = load
setfenv = function() end
getfenv = function() return CURRENT_MOD end
math.mod = math.fmod
local ROOT = (arg and arg[1]) or "script-game/"   -- the decompiled RL26 scripts folder (script-game)
native = setmetatable({}, {__index=function(t,k) return nil end})
native.breakpoint = function() end
ASSERT = function(c, ...) return c end
VERIFY = function(c) return c end
local cache = {}
function import(name)
  if cache[name] then return cache[name] end
  local path = ROOT .. name:gsub("%.", "/") .. ".lua"
  local f = io.open(path, "r")
  if not f then cache[name] = setmetatable({}, {__index=function() return {} end}); return cache[name] end
  f:close()
  -- module environment: globals go into a module table that falls back to _G
  local mod = setmetatable({}, {__index=_G})
  local chunk = assert(loadfile(path, "t", mod))
  local prev = CURRENT_MOD; CURRENT_MOD = mod
  local ret = chunk()
  CURRENT_MOD = prev
  cache[name] = ret or mod
  return cache[name]
end
