dofile("env.lua")
local am = import("design.characters.assetmap")
local I = am.AssetMapItems
local src = {}
for l in io.lines(((arg and arg[1]) or "script-game/") .. "design/characters/assetmap.lua") do src[#src+1]=l end
local function body(f) local i=debug.getinfo(f,"S") return table.concat(src,"\n",i.linedefined+1,i.lastlinedefined-1) end
local mk = function(sex) return {getOption=function(_,k) if k=="sex" then return sex end return 0 end} end
local groups, maps = {}, {}
for _,g in ipairs({"headColourLayers","tier1_headColourLayers","eyesColourLayers","bodyColourLayers","beardCapColourLayers",
                   "headMaterialLayers","tier1_headMaterialLayers","bodyMaterialLayers","headNormalLayers","tier1_headNormalLayers","bodyNormalLayers"}) do
  local out = {}
  for _,L in ipairs(am[g]) do
    local e = {guid=L.guid, option=L.option, intensity=L.intensity}
    if type(L.map)=="function" then e.mapM = L.map(mk(0)); e.mapF = L.map(mk(1)) else e.map = L.map end
    for _,m in ipairs({e.map, e.mapM, e.mapF}) do if m then maps[m]=true end end
    if type(L.guidName)=="string" then e.guidName = L.guidName
    elseif type(L.guidName)=="function" then
      local b = body(L.guidName)
      local fn = b:match("var_0_(%d+)%(")
      local slot = ({["24"]="moustache",["25"]="chinhair",["26"]="sideburns"})[fn]
      local field = b:match("%.([%w_]+)$") or b:match("and var_[%d_]+%.([%w_]+)")
      e.guidNameFrom = slot; e.guidNameField = field
      if not slot then
        local n = b:match('or "([%w_]+)"')
        local o, v = b:match('"([%w_]+)"%s*return arg_[%d_]+:getOption%(var_[%d_]+%) == (%d+)')
        if n then e.guidName = n end
        if o then e.skipIfOption = {o, tonumber(v)} end
        if not n then e.guidNameSource = b end
      end
    end
    if type(L.tints)=="function" then
      local opt = body(L.tints):match('getOption%("([%w_]+)"%)')
      e.tintHairColour = opt
    elseif type(L.tints)=="table" then e.tintOptions = L.tints end
    out[#out+1] = e
  end
  groups[g] = out
end
-- direct texture item maps
for _,m in ipairs({"hairColMap_textures","beardColMap_textures","hairCap_col","hairTie_textures_col","headBand_textures_col","eyelashStrand_col",
                "shoes_col","shoes_mat","shoes_nrm","helmet_col","helmet_mat","helmet_nrm"}) do maps[m]=true end
local mo = {}
for m,_ in pairs(maps) do
  local M = I[m]
  if M then
    local items = {}
    for _,it in ipairs(M.item_setup or {}) do items[#items+1] = {guid=it.guid, name=it.name, blend=it.blend} end
    mo[m] = {basePath=M.basePath, namePrefix=M.namePrefix, items=items}
  end
end
local hc = {}
for _,c in ipairs(I.hairColours) do hc[#hc+1] = {guid=c.guid, colour=c.colour} end
local function esc(s) return (s:gsub('[%c"\\]', function(c) return string.format("\\u%04x", c:byte()) end)) end
local function enc(v, depth)
  depth = depth or 0
  if depth > 12 then return "null" end
  local t = type(v)
  if t == "nil" then return "null"
  elseif t == "boolean" then return tostring(v)
  elseif t == "number" then
    if v ~= v or v == math.huge or v == -math.huge then return "null" end
    if math.type(v) == "integer" then return tostring(v) end
    return string.format("%.9g", v)
  elseif t == "string" then return '"'..esc(v)..'"'
  elseif t == "table" then
    local n = #v
    local isArr = n > 0
    if isArr then for k,_ in pairs(v) do if type(k)~="number" then isArr=false break end end end
    local out = {}
    if isArr then
      for i=1,n do out[#out+1]=enc(v[i],depth+1) end
      return "["..table.concat(out,",").."]"
    end
    local keys = {}
    for k,_ in pairs(v) do if type(k)=="string" or type(k)=="number" then keys[#keys+1]=k end end
    table.sort(keys, function(a,b) return tostring(a)<tostring(b) end)
    for _,k in ipairs(keys) do
      local vv = v[k]
      if type(vv) ~= "function" and type(vv) ~= "userdata" then
        out[#out+1] = '"'..esc(tostring(k))..'":'..enc(vv,depth+1)
      end
    end
    return "{"..table.concat(out,",").."}"
  end
  return "null"
end


local f=io.open("rl26_textures.json","w") f:write(enc({groups=groups, maps=mo, hairColours=hc})) f:close() print("ok")
