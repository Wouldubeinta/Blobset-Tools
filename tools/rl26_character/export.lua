dofile("env.lua")
local am = import("design.characters.assetmap")
local items = am.AssetMapItems
local src = {}
for l in io.lines(((arg and arg[1]) or "script-game/") .. "design/characters/assetmap.lua") do src[#src+1]=l end

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

-- Item maps: model maps and the lists options point into. Only scalar item fields (plus permutations /
-- subOptions tables), no item_setup_lookup duplicates.
local wantMaps = {}
for k,v in pairs(items) do if type(v)=="table" and v.item_setup then wantMaps[k]=true end end
local maps = {}
for name,_ in pairs(wantMaps) do
  local m = items[name]
  local mo = {basePath=m.basePath, namePrefix=m.namePrefix, permutationOrder=m.permutationOrder,
              permutationMap=m.permutationMap, backupPermutations=m.backupPermutations, modelSlotName=m.modelSlotName, items={}}
  for _,it in ipairs(m.item_setup) do
    local o = {}
    for k,v in pairs(it) do
      local tv = type(v)
      if tv=="string" or tv=="number" or tv=="boolean" then o[k]=v
      elseif tv=="table" and (k=="permutations" or k=="backupPermutations" or k=="colour" or k=="subOptions") then o[k]=v end
    end
    mo.items[#mo.items+1]=o
  end
  maps[name]=mo
end

-- Player options: stored ones (type / default / range / byteSize) and the derived ones the blendshapes use.
local opts = {}
for k,v in pairs(am.options.player) do
  if type(v)=="table" then
    opts[k] = {type=v.type, default=v.default, range=v.range, byteSize=v.byteSize, noSave=v.noSave}
  elseif type(v)=="function" then
    local i = debug.getinfo(v,"S")
    local body = table.concat(src, "\n", i.linedefined+1, i.lastlinedefined-1)
    local d = {derived=true}
    local a,b,c = body:match('optionMatchesName%(arg_[%d_]+, "([^"]+)", "([^"]+)", "([^"]+)"%)')
    if a then d.kind="match"; d.option=a; d.list=b; d.name=c
    else
      local s, i1, i2, o1, o2 = body:match('clampScale%(arg_[%d_]+, "([^"]+)", {%s*([%-%d%.]+),%s*([%-%d%.]+)%s*}, {%s*([%-%d%.]+),%s*([%-%d%.]+)%s*}%)')
      if s then d.kind="clamp"; d.option=s; d["in"]={tonumber(i1),tonumber(i2)}; d.out={tonumber(o1),tonumber(o2)}
      else d.kind="other"; d.source=body end
    end
    opts[k]=d
  end
end

local f = io.open("rl26_character.json","w")
f:write(enc({maps=maps, options=opts, blendShapes=am.blendShapes}))
f:close()
print("ok")
