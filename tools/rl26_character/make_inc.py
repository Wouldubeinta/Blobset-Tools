"""Builds src/core/rl26_character_data.inc from rl26_character.json (written by export.lua).

    lua export.lua "<RL26 decompiled scripts>/script-game/"     (Lua 5.4; writes rl26_character.json)
    lua export_tex.lua "<RL26 decompiled scripts>/script-game/"  (writes rl26_textures.json)
    python make_inc.py rl26_character.json rl26_textures.json ../../src/core/rl26_character_data.inc
"""
import json, sys, zlib, struct
d = json.load(open(sys.argv[1]))
M = d['maps']; O = d['options']
slotmaps = ['tier1head', 'head', 'body', 'uniformTop', 'uniformBottom', 'uniformSocks', 'shoes', 'hairTop', 'hairSide',
            'hairSide2', 'hairTop_gear', 'hairSide_gear', 'hairCap', 'hairCap_col', 'moustache', 'chinhair', 'sideburns',
            'helmet', 'sex', 'hairGear', 'topType', 'sockType', 'sockHeight']
lists = set(slotmaps)
for k, v in O.items():
    if v.get('kind') == 'match': lists.add(v['list'])
    if v.get('type') == 'guid' and k in M: lists.add(k)
keep = ['guid', 'name', 'modelName', 'sex', 'permutations', 'noModel', 'noModelMissingPermutations', 'backupPermutations',
        'noSocks', 'gear', 'gear_top', 'gear_side', 'headwearBlendshape', 'ears_pinned', 'ears_headstrapping', 'noLayer',
        'hidden', 'uiName', 'colTexture', 'colTexture2', 'colTexture3', 'hairBand', 'matTexture', 'nrmTexture', 'dirTexture', 'colTexture_cap', 'tier2', 'headStrapping']
# The beard / hair presets' parts (item names in the moustache, chinhair, ... maps), kept for those two lists only.
presetKeep = ['moustache', 'chinhair', 'sideburns', 'hairTop', 'hairSide', 'hairSide2', 'hairTexture_cap']
maps = {}
for l in sorted(x for x in lists if x in M):
    m = M[l]
    o = {k: m[k] for k in ('basePath', 'namePrefix', 'permutationOrder', 'permutationMap', 'backupPermutations') if m.get(k) is not None}
    k2 = keep + presetKeep if l in ('beard', 'hair') else keep
    o['items'] = [{k: it[k] for k in k2 if k in it} for it in m['items']]
    maps[l] = o
opts = {}
for k, v in O.items():
    if v.get('derived'):
        opts[k] = {kk: v[kk] for kk in ('kind', 'option', 'list', 'name', 'in', 'out') if kk in v}
    else:
        opts[k] = {kk: v[kk] for kk in ('type', 'default', 'range', 'byteSize', 'noSave') if v.get(kk) is not None}
out = {'maps': maps, 'options': opts, 'blendShapes': [{'option': b['option'], 'name': b['name']} for b in d['blendShapes']],
       'textures': json.load(open(sys.argv[2]))}   # Skin layer groups, texture item maps, hair colour swatches.
js = json.dumps(out, separators=(',', ':')).encode()
comp = struct.pack('>I', len(js)) + zlib.compress(js, 9)   # qCompress format
lines = ['    ' + ', '.join('0x%02X' % b for b in comp[i:i + 24]) + ',' for i in range(0, len(comp), 24)]
inc = ('// Generated from Rugby League 26\'s character scripts (design/characters/assetmapitems.lua, assetmap.lua and\n'
       '// blendshapes.lua): the model item maps, the player appearance options and the blend shapes, as qCompress\'d JSON.\n'
       '// Read by rl26_character.cpp. Regenerate with tools/rl26_character (export.lua, make_inc.py); don\'t edit by hand.\n'
       '// clang-format off\nstatic const unsigned char kRl26CharacterData[] = {\n' + '\n'.join(lines) + '\n};\n// clang-format on\n')
open(sys.argv[3], 'w', newline='\r\n').write(inc)
print(len(maps), 'maps,', len(js), 'bytes of JSON,', len(comp), 'compressed')
